#!/bin/bash
cd ~/Desktop/IDEmu/tools/mgu
S=~/Downloads/psdzdata/swe/swfl
rm -rf /tmp/mgu/rse && mkdir -p /tmp/mgu/rse/app /tmp/mgu/rse/data /tmp/mgu/rse/rsu
for id in 566c 566d 566e 566f 5670 5671 5672 5673 5675 5678 567a; do
  f=$(ls $S/swfl_0000$id.bin.* | tail -1); echo "app $id"; python3 har.py $f /tmp/mgu/rse/app | tail -1
done
for id in 566b 567b 567c 567d 567e 5682 5683; do
  f=$(ls $S/swfl_0000$id.bin.* | tail -1); echo "data $id"; python3 har.py $f /tmp/mgu/rse/data | tail -1
done
f=$(ls $S/swfl_0000632d.bin.* | tail -1); echo rsu; python3 har.py $f /tmp/mgu/rse/rsu | tail -1
echo DONE
mkdir -p /tmp/mgu/hu/app /tmp/mgu/hu/rsu /tmp/mgu/hu/data
for id in 56e5 56e6 56e7 56e8 56e9 56ea 56eb 56ec 56ee; do f=$(ls $S/swfl_0000$id.bin.* | tail -1); echo "hu app $id"; python3 har.py $f /tmp/mgu/hu/app | tail -1; done
f=$(ls $S/swfl_00005685.bin.* | tail -1); python3 har.py $f /tmp/mgu/hu/rsu | tail -1
f=$(ls $S/swfl_00005687.bin.* | tail -1); python3 har.py $f /tmp/mgu/hu/data | tail -1
echo DONE2
