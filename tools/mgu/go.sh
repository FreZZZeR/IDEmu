#!/bin/bash
# go.sh: reconstrói a imagem, inicia a VM e o glhost7 (sem esperar)
kill $(cat /tmp/mgu/t1/qpid) 2>/dev/null; kill $(cat /tmp/mgu/t1/ghpid) 2>/dev/null; kill $(cat /tmp/mgu/t1/cpid) 2>/dev/null
cd ~/Desktop/IDEmu/tools/mgu && python3 mkext2.py /tmp/mgu/rse/root /tmp/mgu/rse/root.img 4096 | tail -1
rm -f /tmp/idemu-id7/frame.rgba; mkdir -p /tmp/idemu-id7
/tmp/mgu/t1/run4.sh 0 "init=/idemu-init"
(python3 /tmp/mgu/t1/conctl.py > /dev/null 2>&1 & echo $! > /tmp/mgu/t1/cpid)
(GLHOST7_VERBOSE=${V:-} ~/Desktop/IDEmu/src/mgu/out/glhost7 /tmp/idemu-gl.sock /tmp/idemu-id7 > /tmp/mgu/t1/glhost.log 2>&1 & echo $! > /tmp/mgu/t1/ghpid)
