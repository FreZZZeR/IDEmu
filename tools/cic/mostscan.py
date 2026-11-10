#!/usr/bin/env python3
"""Descobre que FBlocks MOST (e quais funções) o HMI do CIC consome: anuncia cada FBlock no registro MOST ("registry changed") e lê o que o HMI
assina em troca (Notification.Set, FktID 0x001). Precisa do IDEmu (python3 -m idemu gui) com um CIC em execução.

uso: mostscan.py [--url http://127.0.0.1:8765] [--inst 1,2,3] [--de 00 --ate ff]
Saída: uma linha por FBlock.instância com os FktIDs assinados (os mesmos que o HMI assinaria numa head unit real com aquele FBlock na rede).
Os resultados medidos na 008_090_002 estão em docs/CIC.md ("Dados do veículo")."""
import argparse, json, sys, time, urllib.request


def call(url, path, body=None):
    req = urllib.request.Request(url + '/api/' + path, data=None if body is None else json.dumps(body).encode(),
                                 headers={'Content-Type': 'application/json'}, method='GET' if body is None else 'POST')
    with urllib.request.urlopen(req, timeout=30) as r: return json.loads(r.read().decode())


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    ap.add_argument('--url', default='http://127.0.0.1:8765'); ap.add_argument('--inst', default='1,2,3')
    ap.add_argument('--de', default='00'); ap.add_argument('--ate', default='ff'); ap.add_argument('--espera', type=float, default=15.0)
    a = ap.parse_args(argv)
    lo, hi = int(a.de, 16), int(a.ate, 16)
    for inst in [int(x) for x in a.inst.split(',')]:
        for fb in range(lo, hi + 1):
            if fb == 0xe5: continue                                  # o controlador virtual já o anuncia
            call(a.url, 'most/send', {'hex': '000c07010001000001' + '02%02x%02x' % (fb, inst)}); time.sleep(0.03)
        time.sleep(a.espera / 3)
    time.sleep(a.espera)
    for e in call(a.url, 'most/seen')['fblocks']:
        if e['subscribes']: print('%02x.%d  %s' % (e['fblock'], e['inst'], ' '.join('%03x' % f for f in e['subscribes'])))
        for t in e['telegrams']:
            if t['fkt'] != 1: print('%02x.%d  fkt %03x %s n=%d %s' % (e['fblock'], e['inst'], t['fkt'], t['op'], t['n'], t['last'][:60]))


if __name__ == '__main__':
    sys.exit(main())
