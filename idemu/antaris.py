"""antaris – receptor GPS Atmel ATR0620 (ANTARIS 3, u-blox) virtual do CIC.

O `vdev-flexgps` do CIC não lê NMEA puro: ele identifica o módulo pelos `$GPTXT` do banner, pergunta a versão (UBX MON-VER), configura (UBX CFG-*) e espera
telegramas UBX cíclicos (NAV-POSLLH, NAV-DOP, NAV-SOL, NAV-TIMEUTC, NAV-VELNED, NAV-SVINFO, MON-HW) além do NMEA. Esta classe fala esse protocolo pela UART
(SCIF0 = /dev/ser2) usando o estado do `GpsSim` (posição, velocidade, rumo, satélites). Detalhes em docs/CIC.md.

Uso: a = Antaris(sim, send); a.start(); a.feed(bytes_do_guest); a.stop(). `send(bytes)` escreve na UART.
"""
import struct, threading, time, datetime

BANNER = ('u-blox ag - www.u-blox.com', 'ANTARIS ATR0620', 'ROM CORE 3.00 (27004) Oct 18 2004')
SW_VERSION = b'ROM CORE 3.00 (27004) Oct 18 2004'
HW_VERSION = b'00030000'
LEAP = 14            # GPS-UTC em 2010 (o firmware do HMI é de 2010)


def nmea(body):
    cs = 0
    for ch in body.encode(): cs ^= ch
    return ('$%s*%02X\r\n' % (body, cs)).encode()


def ubx(cls, mid, payload=b''):
    a = b = 0
    for ch in bytes([cls, mid]) + struct.pack('<H', len(payload)) + payload:
        a = (a + ch) & 0xff; b = (b + a) & 0xff
    return b'\xb5\x62' + bytes([cls, mid]) + struct.pack('<H', len(payload)) + payload + bytes([a, b])


class Antaris(object):
    UNKNOWN_SKIP = ()
    UNKNOWN_LEN = {}

    def __init__(self, sim, send, log=lambda s: None):
        self.sim, self.send, self.log = sim, send, log
        self.buf = b''; self.lock = threading.Lock(); self.stop_ev = threading.Event()
        self.t0 = time.time(); self.polled = False; self.frames = 0; self.t_last_banner = 0.0; self.rates = {}

    # ---------------------------------------------------------------- guest -> módulo
    def feed(self, data):
        self.buf += data
        while True:
            i = self.buf.find(b'\xb5\x62')
            j = self.buf.find(b'$')
            if i < 0 and j < 0:
                self.buf = b'' if len(self.buf) < 4096 else self.buf[-4:]; return
            if j >= 0 and (i < 0 or j < i):                     # texto NMEA do guest ($GPTXT,...,EXT_GPSRESET, $PUBX, ...)
                k = self.buf.find(b'\n', j)
                if k < 0: self.buf = self.buf[j:]; return
                self._nmea(self.buf[j:k + 1].decode('latin1').strip()); self.buf = self.buf[k + 1:]; continue
            if len(self.buf) < i + 8: self.buf = self.buf[i:]; return
            n = struct.unpack_from('<H', self.buf, i + 4)[0]
            if len(self.buf) < i + 8 + n: self.buf = self.buf[i:]; return
            self._frame(self.buf[i + 2], self.buf[i + 3], self.buf[i + 6:i + 6 + n]); self.buf = self.buf[i + 8 + n:]

    def _nmea(self, line):
        self.log('guest NMEA: %s' % line)
        if 'EXT_GPSRESET' in line or 'PBE_RESET' in line or line.startswith('$PBOOT'):
            threading.Timer(0.3, self.banner).start()           # "reset" do módulo: ele volta a se apresentar

    def _frame(self, cls, mid, pl):
        self.frames += 1
        if self.frames <= 400: self.log('guest UBX %02x %02x len=%d %s' % (cls, mid, len(pl), pl[:24].hex()))
        if (cls, mid) == (0x0a, 0x04) and not pl:               # MON-VER (consulta)
            self.polled = True
            self.send(ubx(0x0a, 0x04, SW_VERSION[:30].ljust(30, b'\0') + HW_VERSION[:10].ljust(10, b'\0')))
        elif cls == 0x06 and pl:                                 # CFG-xxx (gravação): reconhece
            if mid == 0x01 and len(pl) == 6:                     # CFG-MSG: taxa de cada telegrama por porta
                self.rates[(pl[0], pl[1])] = max(pl[2:6])
                self.log('CFG-MSG ativos: %s' % sorted('%02x%02x' % k for k, v in self.rates.items() if v))
            self.send(ubx(0x05, 0x01, bytes([cls, mid])))
        elif cls == 0x06 and not pl:                             # CFG-xxx (consulta): sem dados -> NAK
            self.send(ubx(0x05, 0x00, bytes([cls, mid])))

    # ---------------------------------------------------------------- módulo -> guest
    def banner(self):
        self.t_last_banner = time.time()
        for t in BANNER:
            self.send(nmea('GPTXT,01,01,02,' + t)); time.sleep(0.1)

    def start(self):
        threading.Thread(target=self._run, daemon=True).start()

    def stop(self):
        self.stop_ev.set()

    def _run(self):
        time.sleep(0.5); self.banner()
        while not self.stop_ev.wait(1.0):
            try: self.cycle()
            except Exception as e: self.log('erro no ciclo: %r' % (e,))

    def cycle(self):
        """Um segundo de telegramas: NMEA + UBX de navegação."""
        st = self.sim.state(); t = time.time()
        now = datetime.datetime.utcfromtimestamp(t); gps = now + datetime.timedelta(seconds=LEAP)
        days = (gps - datetime.datetime(1980, 1, 6)).days
        tow = int(((days % 7) * 86400 + gps.hour * 3600 + gps.minute * 60 + gps.second) * 1000)
        fix = bool(st.get('fix')); nsv = int(st.get('sats', 0)) if fix else 0
        lat, lon, alt = st.get('lat', 0.0), st.get('lon', 0.0), st.get('alt', 0.0)
        spd = st.get('speed_kmh', 0.0) / 3.6; hdg = st.get('heading', 0.0) % 360.0
        hdop = st.get('hdop', 1.0) or 1.0
        import math
        e7 = lambda v: int(round(v * 1e7))
        out = []
        out.append(ubx(0x01, 0x02, struct.pack('<IiiiiII', tow, e7(lon), e7(lat), int(alt * 1000), int(alt * 1000), int(hdop * 2500), int(hdop * 4000))))
        dop = lambda v: int(round(v * 100))
        out.append(ubx(0x01, 0x03, struct.pack('<IBBBBII', tow, 3 if fix else 0, 0x01 if fix else 0, 0, 0, 30000, int(time.time() - self.t0) * 1000)))
        out.append(ubx(0x01, 0x22, struct.pack('<IiiII', tow, 0, 0, 50, 100)))
        out.append(ubx(0x01, 0x04, struct.pack('<IHHHHHHH', tow, dop(hdop * 1.6), dop(hdop * 1.4), dop(hdop * 0.8), dop(hdop * 1.1), dop(hdop), dop(hdop * 0.7), dop(hdop * 0.6))))
        out.append(ubx(0x01, 0x06, struct.pack('<IihBBiiiIiiiIHBBI', tow, 0, days // 7, 3 if fix else 0, 0x0d if fix else 0, *self._ecef(lat, lon, alt), 250, 0, 0, 0, 10, dop(hdop * 1.4), 0, nsv, 0)))
        out.append(ubx(0x01, 0x21, struct.pack('<IIiHBBBBBB', tow, 50, 0, now.year, now.month, now.day, now.hour, now.minute, now.second, 0x07 if fix else 0)))
        vn, ve = int(spd * math.cos(math.radians(hdg)) * 100), int(spd * math.sin(math.radians(hdg)) * 100)
        out.append(ubx(0x01, 0x12, struct.pack('<IiiiIIiII', tow, vn, ve, 0, int(spd * 100), int(spd * 100), int(hdg * 1e5), 50, 100000)))
        sv = struct.pack('<IBBH', tow, nsv, 0, 0)
        for i in range(nsv):
            sv += struct.pack('<BBBBBbhi', i, 2 + i * 3, 0x0d if fix else 0, 7 if fix else 0, 38 + (i * 5) % 12, 15 + (i * 17) % 70, (i * 47) % 360, 0)
        out.append(ubx(0x01, 0x30, sv))
        out.append(ubx(0x0a, 0x09, struct.pack('<IIIIHHBBBBI25sBHI', 0, 0, 0, 0, 100, 600, 2, 1, 0, 0, 0, bytes(25), 0, 0, 0)))
        if self.polled:
            for u in out: self.send(u)
            for (c, m), r in sorted(self.rates.items()):          # telegramas que o driver ligou por CFG-MSG (as classes do Atmel não são as do u-blox)
                if r and c not in (0x01, 0x0a, 0xf0, 0xf1) and (c, m) not in self.UNKNOWN_SKIP:
                    for _ in range(r): self.send(ubx(c, m, bytes(self.UNKNOWN_LEN.get((c, m), 0))))

    @staticmethod
    def _ecef(lat, lon, h):
        import math
        a, f = 6378137.0, 1 / 298.257223563; e2 = f * (2 - f)
        la, lo = math.radians(lat), math.radians(lon); n = a / math.sqrt(1 - e2 * math.sin(la) ** 2)
        return (int(round((n + h) * math.cos(la) * math.cos(lo) * 100)), int(round((n + h) * math.cos(la) * math.sin(lo) * 100)),
                int(round((n * (1 - e2) + h) * math.sin(la) * 100)))
