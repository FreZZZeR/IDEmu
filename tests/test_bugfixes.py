"""Regressões da revisão de 2026-11-09: NMEA, MOST com posição no limite, V850 parando os threads, corpo negativo no servidor do celular."""
import os, sys, tempfile, threading, time, unittest
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
from idemu import gps, most, v850


class NmeaTest(unittest.TestCase):
    def test_minutos_nao_chegam_a_60(self):
        self.assertEqual(gps._lon_nmea(-46.99999999), ('04700.00000', 'W'))
        self.assertEqual(gps._lat_nmea(23.5505), ('2333.03000', 'N'))
        for v in (0.999999999, 10.9999999, 179.9999999):
            self.assertLess(float(gps._lon_nmea(v)[0][3:]), 60.0)


class FakeGps:
    def __init__(self, **kw): self.st = dict(fix=True, lat=0.0, lon=0.0, alt=0.0, heading=0.0, speed_kmh=0.0, sats=8, hdop=1.0); self.st.update(kw)
    def state(self): return self.st


class MostTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.link = v850.V850Link(os.path.join(self.tmp.name, 'nao-existe.sock'), os.path.join(self.tmp.name, 'v850.log'))

    def tearDown(self):
        self.link.stop(); self.tmp.cleanup()

    def test_posicao_no_limite_nao_quebra_o_pack(self):
        car = self.link.bridge.car
        car.gps = FakeGps(lon=180.0, lat=-90.0, alt=50000.0)
        for fb, fkt in ((0x52, 0x243), (0x52, 0x440), (0xCD, 0x20b)):
            self.assertIsNotNone(car._nav_status(fb, fkt))

    def test_stop_encerra_threads_e_fecha_o_log(self):
        class Can:
            def signals(self): return {}
        self.link.bridge.car.attach_can(Can())
        self.link.bridge._enqueue(b'\x00')                  # cria o thread most-tx
        time.sleep(0.3)
        names = lambda: {t.name for t in threading.enumerate()}
        self.assertTrue({'most-car', 'most-tx'} <= names())
        self.link.stop(); time.sleep(0.6)
        self.assertFalse({'most-car', 'most-tx'} & names())
        self.assertIsNone(self.link._logf)
        self.link.log('depois de parar')                     # não pode levantar exceção


class ServerTest(unittest.TestCase):
    """Servidor HTTP num diretório de ambientes temporário: entrada mal formada vira 400, VIN validado no update, stop só do ambiente pedido."""
    @classmethod
    def setUpClass(cls):
        from http.server import ThreadingHTTPServer
        from idemu import server
        cls.server = server; cls.tmp = tempfile.TemporaryDirectory(); cls.old = (server.ENVS, server.APP)
        server.ENVS = os.path.join(cls.tmp.name, 'environments'); os.makedirs(os.path.join(server.ENVS, 'nbt'))
        server.APP = server.App()
        server.APP.save_env({'id': 'nbt', 'name': 'NBT', 'chassis': 'F30', 'coding': {'vin': ''}, 'net': {}, 'swfl': {}})
        cls.srv = ThreadingHTTPServer(('127.0.0.1', 0), server.H); cls.port = cls.srv.server_address[1]
        threading.Thread(target=cls.srv.serve_forever, daemon=True).start()

    @classmethod
    def tearDownClass(cls):
        cls.srv.shutdown(); cls.srv.server_close(); cls.server.ENVS, cls.server.APP = cls.old; cls.tmp.cleanup()

    def req(self, method, path, body=None, headers=None):
        import http.client, json
        c = http.client.HTTPConnection('127.0.0.1', self.port, timeout=10)
        h = {'Host': '127.0.0.1:%d' % self.port}; h.update(headers or {})
        c.request(method, path, body=body if isinstance(body, (bytes, str)) or body is None else json.dumps(body), headers=h)
        r = c.getresponse(); d = r.read(); c.close()
        return r.status, d

    def test_entrada_mal_formada_e_400(self):
        self.assertEqual(self.req('POST', '/api/env/nbt/update', b'[1,2]', {'Content-Type': 'application/json'})[0], 400)
        self.assertEqual(self.req('POST', '/api/env/nbt/update', b'{}', {'Content-Length': 'abc'})[0], 400)

    def test_vin_invalido_no_update(self):
        st, d = self.req('POST', '/api/env/nbt/update', {'coding': {'vin': 'WBA123'}})
        self.assertEqual(st, 400, d)
        st, d = self.req('POST', '/api/env/nbt/update', {'coding': {'vin': 'wba8e9g50gnu12345'}})
        self.assertEqual(st, 200, d)
        self.assertEqual(self.server.APP.load_env('nbt')['coding']['vin'], 'WBA8E9G50GNU12345')

    def test_stop_de_outro_ambiente_nao_para_o_atual(self):
        class Rt:
            id = 'outro'; state = 'running'; stopped = False
            def stop(self): Rt.stopped = True
        self.server.APP.rt = Rt()
        try:
            self.assertEqual(self.req('POST', '/api/env/nbt/stop')[0], 200)
            self.assertFalse(Rt.stopped)
        finally: self.server.APP.rt = None

    def test_arquivo_estatico_fora_da_pasta_web(self):
        self.assertEqual(self.req('GET', '/../web2/x.js')[0], 404)


if __name__ == '__main__':
    unittest.main()
