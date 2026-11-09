#!/bin/zsh
# tap.sh X Y [env]: reinicia o ambiente a frio, espera ficar pronto, toca em (X,Y) e compara a tela antes e depois (depuração do toque)
E=${3:-f30-bmw}; B=http://127.0.0.1:8765/api
curl -s -XPOST $B/env/$E/stop >/dev/null; sleep 3
curl -s -XPOST -d '{"cold":true}' $B/env/$E/start >/dev/null
for i in $(seq 1 150); do s=$(curl -s $B/run/status); echo "$s" | grep -q '"state": "running"' && break; sleep 2; done
sleep 12; curl -s $B/run/screen.png -o /tmp/tap_a.png
curl -s -XPOST -d "{\"x\":$1,\"y\":$2,\"down\":true}" $B/input/click >/dev/null; sleep 0.12
curl -s -XPOST -d "{\"x\":$1,\"y\":$2,\"down\":false}" $B/input/click >/dev/null; sleep 4
curl -s $B/run/screen.png -o /tmp/tap_b.png
md5 -q /tmp/tap_a.png /tmp/tap_b.png | cut -c1-8 | tr '\n' ' '; echo
