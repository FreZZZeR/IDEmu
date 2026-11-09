"""iDrive 5/6: o servidor refaz o shim.img de um ambiente quando as bibliotecas do guest (idemu/shim/lib) ou os scripts de boot são mais novos que ele."""
import os, sys, tempfile, time, unittest
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from idemu import server


class ShimStaleTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(); self.old = server.ENVS
        server.ENVS = os.path.join(self.tmp.name, 'environments')
        self.img = os.path.join(server.ENVS, 'nbt', 'images'); os.makedirs(self.img)
        self.app = server.App()
        here = os.path.dirname(os.path.abspath(server.__file__))
        self.newest = max(os.path.getmtime(os.path.join(here, 'shim', 'lib', n)) for n in os.listdir(os.path.join(here, 'shim', 'lib')))

    def tearDown(self):
        server.ENVS = self.old; self.tmp.cleanup()

    def test_sem_shim_nao_refaz(self):
        self.assertFalse(self.app._shim_stale('nbt'))          # ambiente ainda não compilado: quem gera o shim é a compilação

    def test_shim_mais_velho_que_as_bibliotecas(self):
        p = os.path.join(self.img, 'shim.img'); open(p, 'wb').write(b'x')
        os.utime(p, (self.newest - 60, self.newest - 60))
        self.assertTrue(self.app._shim_stale('nbt'))

    def test_shim_em_dia(self):
        p = os.path.join(self.img, 'shim.img'); open(p, 'wb').write(b'x')
        t = time.time() + 60; os.utime(p, (t, t))
        self.assertFalse(self.app._shim_stale('nbt'))


if __name__ == '__main__':
    unittest.main()
