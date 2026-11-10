"""CIC: arquivos do usuário no EFS_RO, veículo virtual (tensão da bateria na IOC), telegramas MOST crus e o que o HMI pede a outros FBlocks."""
import importlib.util, os, struct, sys, tempfile, unittest
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT); sys.path.insert(0, os.path.join(ROOT, 'tools', 'cic'))
from idemu import cic
import efs, mkimagefs

PKG = cic.find_pkg(None)


def hmiinput():
    spec = importlib.util.spec_from_file_location('hmiinput', os.path.join(ROOT, 'tools', 'cic', 'hmiinput.py'))
    h = importlib.util.module_from_spec(spec); spec.loader.exec_module(h); return h


class DirEntriesTest(unittest.TestCase):
    def test_arvore_modos_e_arquivos_ocultos(self):
        with tempfile.TemporaryDirectory() as d:
            os.makedirs(os.path.join(d, 'app', 'sub')); os.makedirs(os.path.join(d, '.git'))
            for rel, mode in (('app/run', 0o755), ('app/sub/dados.bin', 0o644), ('.DS_Store', 0o644), ('app/.oculto', 0o644)):
                with open(os.path.join(d, rel), 'wb') as f: f.write(b'x' + rel.encode())
                os.chmod(os.path.join(d, rel), mode)
            os.symlink('run', os.path.join(d, 'app', 'atalho'))
            got = {e[0]: e for e in efs.dir_entries(d)}
            self.assertEqual(sorted(got), ['app', 'app/atalho', 'app/run', 'app/sub', 'app/sub/dados.bin'])
            self.assertEqual((got['app/run'][1], got['app/run'][3]), ('F', 0o755))
            self.assertEqual(got['app/sub/dados.bin'][3], 0o644)
            self.assertEqual((got['app/atalho'][1], got['app/atalho'][2]), ('L', 'run'))
            self.assertEqual(got['app'][1], 'D')

    def test_imagefs_contem_os_arquivos(self):
        img = mkimagefs.build([('hello', 'D', None, 0o755), ('hello/a.txt', 'F', b'conteudo-unico-123', 0o644)])
        self.assertTrue(img.startswith(b'imagefs')); self.assertIn(b'conteudo-unico-123', img); self.assertIn(b'hello/a.txt', img)


@unittest.skipUnless(PKG, 'pacote do CIC não está nesta máquina')
class EfsExtraTest(unittest.TestCase):
    def test_arquivo_do_usuario_entra_e_substitui(self):
        log = []
        base = {e[0]: e for e in efs.entries(PKG)}
        victim = next(p for p, e in base.items() if e[1] == 'F' and not p.startswith('res/'))
        extra = [('meuapp', 'D', None, 0o755), ('meuapp/x', 'F', b'abc', 0o755), (victim, 'F', b'novo', 0o644)]
        got = {e[0]: e for e in efs.entries(PKG, extra=extra, log=log.append)}
        self.assertEqual(got['meuapp/x'][2], b'abc'); self.assertEqual(got[victim][2], b'novo')
        self.assertTrue(any(victim in l for l in log))

    def test_imagens_de_veiculo_cedem_lugar(self):
        base = efs.entries(PKG); nveh = sum(1 for e in base if e[0].startswith('res/ResDB.vehicle-'))
        big = [('grande', 'F', bytes(4 << 20), 0o644)]
        got = efs.entries(PKG, extra=big)
        self.assertLess(sum(1 for e in got if e[0].startswith('res/ResDB.vehicle-')), nveh)       # ~3 imagens de 1,3 MB saem
        self.assertTrue(sum(len(e[2]) for e in got if e[1] == 'F') < efs.LIMIT)
        self.assertTrue(len(mkimagefs.build(got)) <= efs.LIMIT)

    def test_nao_cabe_nem_sem_as_imagens(self):
        with self.assertRaises(ValueError): efs.entries(PKG, extra=[('enorme', 'F', bytes(60 << 20), 0o644)])

    def test_tipo_diferente_colide(self):
        base = efs.entries(PKG); d = next(e[0] for e in base if e[1] == 'D' and e[0] != '.')
        with self.assertRaises(ValueError): efs.entries(PKG, extra=[(d, 'F', b'x', 0o644)])


class PatchUpdateTest(unittest.TestCase):
    """patch_flash_file numa flash sintética: aplica, é idempotente e troca um stub antigo."""
    def make(self, h, d):
        p = os.path.join(d, 'flash.img'); o = 0x1000
        with open(p, 'wb') as f:
            f.truncate(h.IFS2_OFF + 0x90000)
            region = bytearray(0x90000)
            region[o:o + len(h.ANCHOR)] = h.ANCHOR
            off = lambda va: o + (va - h.ANCHOR_VA)
            region[off(h.CAVE_VA):off(h.CAVE_VA) + len(h.CAVE)] = h.CAVE
            struct.pack_into('<I', region, off(h.LIT_VA), (h.RECV_VA - h.LIT_BASE) & 0xffffffff)
            struct.pack_into('<I', region, off(h.TXLIT_VA), (h.SEND_VA - h.TXLIT_BASE) & 0xffffffff)
            f.seek(h.IFS2_OFF); f.write(region)
        return p, off

    def test_aplica_atualiza_e_e_idempotente(self):
        h = hmiinput()
        with tempfile.TemporaryDirectory() as d:
            p, off = self.make(h, d)
            self.assertEqual(h.patch_flash_file(p), 'patched'); self.assertEqual(h.patch_flash_file(p), 'ok')
            code, tx = h.stub()
            with open(p, 'r+b') as f:                                    # stub de uma versão antiga: outro código e outro destino do envio
                f.seek(h.IFS2_OFF + off(h.CAVE_VA)); f.write(b'\x09\x00' * 40)
                f.seek(h.IFS2_OFF + off(h.TXLIT_VA)); f.write(struct.pack('<I', 0x1234))
            self.assertEqual(h.patch_flash_file(p), 'patched'); self.assertEqual(h.patch_flash_file(p), 'ok')
            with open(p, 'rb') as f:
                f.seek(h.IFS2_OFF + off(h.CAVE_VA)); self.assertEqual(f.read(len(code)), code)
                f.seek(h.IFS2_OFF + off(h.TXLIT_VA)); self.assertEqual(struct.unpack('<I', f.read(4))[0], (tx - h.TXLIT_BASE) & 0xffffffff)

    def test_telegramas_de_outros_fblocks_sao_registrados(self):
        sent = []
        from idemu import cic as c
        z = c.VirtualZBE(tempfile.mkdtemp(), sent.append, log=sent.append)
        try:
            z._other(bytes([0, 17, 2, 0, 1, 2, 0xdc, 1, 0x00, 0x01, 0, 1, 1, 0, 0, 0x0c, 0x58]))
            z._other(bytes([0, 17, 2, 0, 1, 2, 0xdc, 1, 0x00, 0x01, 0, 1, 1, 0, 0, 0x0c, 0x5a]))
            self.assertEqual(z.subs[(0xdc, 1)], {0xc58, 0xc5a})
            self.assertEqual(z.seen[(0xdc, 1, 1, 0)][0], 2)
            self.assertEqual(sum(1 for m in sent if 'MOST HMI->dc.1' in m), 1)       # só o 1º telegrama de cada tipo vai para o log
        finally: z.close()


class VehicleTest(unittest.TestCase):
    def rt(self):
        d = tempfile.mkdtemp(); return cic.CicRuntime(d, {'id': 'teste', 'name': 't', 'kind': 'cic', 'net': {}})

    def test_tensao_vira_decimos_de_volt(self):
        r = self.rt(); sent = []; r.send_cmd = sent.append
        r.set_vehicle('battery_voltage', 12.6); self.assertEqual(sent, ['v 126']); self.assertEqual(r.vehicle_state()['signals'], {'battery_voltage': 12.6})
        r.set_vehicle('battery_voltage', 14.45); self.assertEqual(sent[-1], 'v 144')

    def test_recusa_valor_e_sinal_invalidos(self):
        r = self.rt(); r.send_cmd = lambda l: self.fail('não deveria enviar')
        for name, v in (('battery_voltage', 5.9), ('battery_voltage', 16.1), ('velocidade', 10)):
            with self.assertRaises(ValueError): r.set_vehicle(name, v)
        with self.assertRaises(ValueError): r.set_vehicle('battery_voltage', 'abc')

    def test_most_send_completa_o_tamanho(self):
        r = self.rt(); sent = []; r.send_cmd = sent.append
        r.most_send('0000 07 01 0001 0000 0102 d8 01'); self.assertEqual(sent, ['t 000c07010001000001' + '02d801'])
        with self.assertRaises(ValueError): r.most_send('0102')
        with self.assertRaises(ValueError): r.most_send('zz' * 12)


class UserFilesListTest(unittest.TestCase):
    def test_lista_so_arquivos_visiveis(self):
        with tempfile.TemporaryDirectory() as env:
            d = cic.userfiles_dir(env); os.makedirs(os.path.join(d, 'a', 'b'))
            for rel in ('a/b/c.bin', 'a/d.txt', '.DS_Store'):
                with open(os.path.join(d, rel), 'wb') as f: f.write(b'12345')
            self.assertEqual(sorted((x['path'], x['size']) for x in cic.userfiles_list(env)), [('a/b/c.bin', 5), ('a/d.txt', 5)])
            self.assertEqual(cic.apply_userfiles('/nao/existe', tempfile.mkdtemp()), 0)         # sem arquivos: não mexe na flash


if __name__ == '__main__':
    unittest.main()
