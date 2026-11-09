#!/bin/sh
export PATH=/bin:/sbin:/usr/bin:/usr/sbin
mount -t proc proc /proc; mount -t sysfs sysfs /sys; mount -t devtmpfs dev /dev 2>/dev/null
chmod 666 /dev/ttyS1; mkdir -p /dev/pts /dev/shm; mount -t devpts devpts /dev/pts; mount -t tmpfs tmpfs /dev/shm
mount -t tmpfs tmpfs /run; mount -t tmpfs tmpfs /tmp; mount -t tmpfs tmpfs /var/volatile 2>/dev/null
mkdir -p /run/wayland /var/run /run/nsc /var/volatile/tmp
chmod 1777 /tmp; chmod -R a+rwX /var /mnt/share /run 2>/dev/null; mkdir -p /mnt/share/hmi; chmod 777 /run/wayland
export XDG_RUNTIME_DIR=/run/wayland LC_ALL=C
# anel de comandos GL em memória compartilhada (dispositivo PCI idemu-shm): o HMI, que não é root, precisa abrir o BAR
chmod 666 /sys/bus/pci/devices/*/resource0 2>/dev/null
for d in /sys/bus/pci/devices/*; do echo "== pci $(basename $d) $(cat $d/vendor) $(cat $d/device)"; done
echo 1 > /proc/sys/kernel/printk                  # o console (serial) não precisa dos avisos do kernel
echo 0 > /proc/sys/vm/compact_unevictable_allowed 2>/dev/null
echo "== idemu-init $(cut -d' ' -f1 /proc/uptime)"
st() { echo "== start $1 $(cut -d' ' -f1 /proc/uptime)"; }
# espera um arquivo existir (no máximo $2 segundos); no lugar dos `sleep` fixos de antes
waitf() { i=0; while [ ! -e "$1" ] && [ $i -lt $(( $2 * 20 )) ]; do usleep 50000; i=$((i+1)); done; }
# espera ter pelo menos $1 sockets de clientes vsomeip (cada aplicativo registrado cria /tmp/vsomeip-26xx)
waitclients() { i=0; while [ $(ls /tmp/vsomeip-260? 2>/dev/null | wc -l) -lt $1 ] && [ $i -lt $(( $2 * 20 )) ]; do usleep 50000; i=$((i+1)); done; }
FC=/usr/lib/idemu/libfastclock.so                 # relógio pelo rdtsc: o guest usa o HPET, e cada leitura do relógio custa 15 µs
run() { u=$1; shift; idemu-run $u /bin/busybox env LD_PRELOAD=$FC "$@"; }
ifconfig lo 127.0.0.1 up
mkdir -p /run/dlt-files/dltpipes; chmod 1777 /run/dlt-files/dltpipes; chmod 755 /run/dlt-files; mkfifo /run/dlt-files/dlt; chmod 622 /run/dlt-files/dlt
(while true; do cat /run/dlt-files/dlt >> /tmp/dltraw; done) &
export LD_LIBRARY_PATH=/usr/lib/hmi:/usr/lib
ROLE=rse; grep -q 'idemu.role=hu' /proc/cmdline && ROLE=hu
st vsomeipd;  run sin_vsp /usr/bin/vsomeipd > /tmp/vsomeipd.log 2>&1 &
waitf /tmp/vsomeip-0 40
# os serviços não dependem uns dos outros para subir: começam juntos (o vsomeip espera quem ainda não ofereceu)
st codingshm; run sf_codingshmservice /usr/bin/CodingShmApp > /tmp/codingshm.log 2>&1 &
st persod; run sps_8000 /usr/bin/persod --ecu=rse > /tmp/persod.log 2>&1 &
st ioclc; mkdir -p /run/lifecycle-ioc; chmod 777 /run/lifecycle-ioc; idemu-run sin_ioclc /bin/busybox env LD_LIBRARY_PATH=/usr/lib/idemu:/usr/lib VSOMEIP_CLIENTSIDELOGGING=enabled LD_PRELOAD=/usr/lib/idemu/libipc.so.1:$FC /usr/bin/ioclcproxy > /tmp/ioclc.log 2>&1 &
st udpnm; run sin_udpnm /usr/bin/udpnm > /tmp/udpnm.log 2>&1 &
st nsm; idemu-run sin_nsm /bin/busybox env LD_PRELOAD=/usr/lib/libfakecoding_rse.so.0:$FC /usr/bin/NodeStateManager_rse > /tmp/nsm.log 2>&1 &
st lpm; run sin_lpmctrl /usr/bin/rselowpowermodecontroller > /tmp/lpm.log 2>&1 &
if [ $ROLE = hu ]; then
  # tela central (HU_HMI, 1920x720): sem `-r=` o HmiMain3D sobe como HMI do head unit; os serviços SOME/IP do head unit que ele espera são falsos
  export IDEMU_WL_SIZE=1920x720
  st fakesvc; run sin_nsm /usr/bin/idemu-fakesvc > /tmp/fakesvc.log 2>&1 &
  # relógio do cluster (KombiIF.statusShowTimeDate: hora, min, s, dia, dia da semana, mês, ano (2 bytes), status); a hora do guest é a do Mac (-rtc base=localtime)
  ( waitf /run/fakesvc.ctl 90
    while true; do
      set -- $(date '+%H %M %S %d %u %m %Y'); n() { v=${1#0}; echo ${v:-0}; }
      [ -p /run/fakesvc.ctl ] && printf 'set statusShowTimeDate %02x%02x%02x%02x%02x%02x%04x01\n' $(n $1) $(n $2) $(n $3) $(n $4) $5 $(n $6) $7 > /run/fakesvc.ctl
      sleep 5
    done ) &
fi
st wl; /usr/bin/idemu-wl > /tmp/wl.log 2>&1 &
waitf /run/wl.ready 30; echo "== wl ready"
if grep -q 'idemu.coding=1' /proc/cmdline; then
  # codificação alterada no editor: o Mac a manda pela serial de entrada (idemu-wl grava em /run/coding.bin e /run/coding.ok); vai para a memória compartilhada antes do HMI ler
  waitf /dev/shm/rawcodingdata 40; waitf /run/coding.ok 20
  if [ -s /run/coding.bin ]; then
    dd if=/run/coding.bin of=/dev/shm/rawcodingdata bs=1 seek=44 conv=notrunc 2>/dev/null
    printf '\011' | dd of=/dev/shm/rawcodingdata bs=1 seek=40 conv=notrunc 2>/dev/null       # estado 0x09: bloco presente e dados reais
    echo "== coding applied $(wc -c < /run/coding.bin | tr -d ' ')"
  fi
fi
# o HMI leva ≈ 1 min só carregando os dados: começa já, em paralelo com os serviços (ele espera o que ainda não está disponível)
st hmi
if [ $ROLE = hu ]; then
idemu-run hmi_5000 /bin/busybox env LD_PRELOAD=$FC /usr/bin/HmiMain3D -ro=/mnt/dataro/data/hmi -ibaroot=/mnt/iba/ibaroot -nfsrw=/mnt/share/hmi -bsc -lm=ramses_thread > /tmp/hmi.log 2>&1 &
else
idemu-run hmi_5000 /bin/busybox env LD_PRELOAD=$FC /usr/bin/HmiMain3D -ro=/mnt/dataro/data/hmi -ibaroot=/mnt/iba/ibaroot -nfsrw=/mnt/share/hmi -r=RSE_HIGH_LEFT -bsc -lm=ramses_thread > /tmp/hmi.log 2>&1 &
fi
P=$!
echo "== HMI pid $P"
# o controlador de partida só depois que todos os aplicativos se registraram no vsomeip (antes: 10 s fixos)
waitclients 12 60
st nsc; /bin/busybox env LD_PRELOAD=$FC /usr/bin/nodestartupcontroller --uid=0 --gid=0 --shutdown=x --restart=x --suspend=x --poweroff=x --no_blame=/run/nsc/no_blame > /tmp/nsc.log 2>&1 &
echo "== procs"; ps | grep -E "vsomeip|Coding|persod|NodeState|nodestart" | grep -v grep
exec /bin/sh
