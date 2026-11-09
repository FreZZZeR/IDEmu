#!/bin/bash
# uso: run4.sh SEGUNDOS "cmdline extra" [extras]  — ttyS1 vira socket /tmp/idemu-gl.sock (host conecta)
cd /tmp/mgu/t1; rm -f s2.log tf.log /tmp/idemu-gl.sock /tmp/idemu-con.sock; PORT=$((20000 + RANDOM % 20000))
qemu-system-x86_64 -M q35 -cpu Skylake-Client -accel tcg,thread=multi -smp 4 -m ${MEM:-3G} -kernel ${KERNEL:-/tmp/ias/bz} \
  -append "console=ttyS2,115200 earlycon=uart8250,io,0x3e8,115200n8 root=/dev/mmcblk0 rootfstype=ext4 rw rootwait ignore_loglevel nokaslr sdhci_pci.power_default_on=1 $2" \
  -device sdhci-pci -drive if=none,id=sd0,file=${IMG:-/tmp/mgu/rse/root.img},format=raw -device sd-card,drive=sd0 \
  -display none -serial null -chardev socket,id=gl,path=/tmp/idemu-gl.sock,server=on,wait=off -serial chardev:gl -chardev socket,id=con,path=/tmp/idemu-con.sock,server=on,wait=off -serial chardev:con -monitor none -gdb tcp:127.0.0.1:$PORT -S ${@:4} > q.err 2>&1 &
echo $! > qpid; python3 ~/Desktop/IDEmu/tools/mgu/tscfix.py $PORT > tf.log 2>&1 &
