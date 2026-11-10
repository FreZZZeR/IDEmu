"""Receptor GPS ANTARIS virtual do CIC (idemu/antaris.py): telegramas UBX com o tamanho que o vdev-flexgps confere, respostas ao driver e o banner."""
import os, struct, sys, time, unittest
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from idemu import antaris, gps


class Sim:
    def __init__(self, **kw): self.s = dict(fix=True, lat=-23.55, lon=-46.63, alt=760.0, speed_kmh=36.0, heading=90.0, sats=8, hdop=0.9); self.s.update(kw)
    def state(self): return self.s


def frames(blob):
    out = []; i = 0
    while i < len(blob):
        assert blob[i:i + 2] == b'\xb5\x62', blob[i:i + 4]
        n = struct.unpack_from('<H', blob, i + 4)[0]
        a = b = 0
        for ch in blob[i + 2:i + 6 + n]: a = (a + ch) & 0xff; b = (b + a) & 0xff
        assert (blob[i + 6 + n], blob[i + 7 + n]) == (a, b), 'checksum UBX'
        out.append((blob[i + 2], blob[i + 3], blob[i + 6:i + 6 + n])); i += 8 + n
    return out


class AntarisTest(unittest.TestCase):
    def setUp(self):
        self.out = []; self.a = antaris.Antaris(Sim(), self.out.append)

    def test_telegramas_ciclicos_com_os_tamanhos_do_firmware(self):
        self.a.polled = True; self.a.cycle()
        got = {(c, m): len(p) for c, m, p in frames(b''.join(self.out))}
        self.assertEqual(got, {(1, 2): 28, (1, 3): 16, (1, 0x22): 20, (1, 4): 18, (1, 6): 52, (1, 0x21): 20, (1, 0x12): 36, (1, 0x30): 8 + 12 * 8, (0x0a, 9): 60})

    def test_nada_ate_o_driver_perguntar_a_versao(self):
        self.a.cycle(); self.assertEqual(self.out, [])

    def test_mon_ver_e_ack_de_configuracao(self):
        self.a.feed(antaris.ubx(0x0a, 0x04))
        (c, m, p), = frames(self.out[-1]); self.assertEqual((c, m, len(p)), (0x0a, 4, 40)); self.assertTrue(p.startswith(b'ROM CORE 3.00'))
        self.assertTrue(self.a.polled)
        self.a.feed(b'junk' + antaris.ubx(0x06, 0x01, bytes([0x02, 0x22, 0, 0, 1, 0])))       # lixo antes do quadro; entrega em pedaços
        (c, m, p), = frames(self.out[-1]); self.assertEqual((c, m, p), (5, 1, bytes([6, 1])))

    def test_quadro_em_pedacos(self):
        f = antaris.ubx(0x0a, 0x04)
        self.a.feed(f[:3]); self.assertEqual(self.out, []); self.a.feed(f[3:]); self.assertEqual(len(self.out), 1)

    def test_banner_apos_reset_do_driver(self):
        self.a.feed(b'$GPTXT,01,01,02,EXT_GPSRESET*5F\r\n')
        for _ in range(60):
            if len(self.out) >= 3: break
            time.sleep(0.05)
        txt = b''.join(self.out).decode()
        self.assertIn('ANTARIS ATR0620', txt); self.assertIn('u-blox ag', txt)
        for ln in txt.strip().split('\r\n'): self.assertTrue(gps.verify_nmea(ln + '\r\n') if ln.startswith('$') else False, ln)

    def test_sem_fix_nao_anuncia_satelites(self):
        a = antaris.Antaris(Sim(fix=False), self.out.append); a.polled = True; a.cycle()
        d = {(c, m): p for c, m, p in frames(b''.join(self.out))}
        self.assertEqual(d[(1, 0x30)][4], 0)                        # numCh
        self.assertEqual(d[(1, 6)][10], 0)                          # gpsFix


if __name__ == '__main__':
    unittest.main()
