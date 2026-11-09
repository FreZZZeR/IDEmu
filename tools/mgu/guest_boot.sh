#!/bin/sh
# init do guest do iDrive 8: lista mínima de serviços e o Hmi8 do firmware.
export PATH=/usr/bin:/bin:/usr/sbin:/sbin
mount -t proc proc /proc; mount -t sysfs sys /sys; mount -t devtmpfs dev /dev; mount -t tmpfs tmp /tmp
mkdir -p /run/wayland /var/run/wayland /var/run/someip /var/data /var/opt/public/hmi /var/begu/IBA /etc/idemu
ip link set lo up 2>/dev/null
echo 0 > /proc/sys/vm/compact_unevictable_allowed  # o host lê as páginas dos canais pelo endereço físico
H=/opt/ivi/rootfs/usr
cp /idemu/coding.tab /etc/idemu/coding.tab
echo "== coding applied $(wc -l < /etc/idemu/coding.tab)"
export VSOMEIP_CONFIGURATION=/etc/vsomeip VSOMEIP_APPLICATION_NAME=vsomeipd
(/usr/bin/vsomeipd > /tmp/vsomeipd.log 2>&1 &) ; sleep 2
unset VSOMEIP_APPLICATION_NAME
export XDG_RUNTIME_DIR=/var/run/wayland LC_ALL=C DISABLE_UPDATER_THREAD=1
t=$(sed -n 's/.*idemu.time=\([0-9]*\).*/\1/p' /proc/cmdline); [ -n "$t" ] && date -s "@$t" >/dev/null 2>&1
# no carro o IVI roda em contêiner: junta a /etc dele e sobe os daemons do firmware
for f in /opt/ivi/rootfs/etc/*; do [ -e /etc/$(basename $f) ] || ln -s $f /etc/; done
mkdir -p /run/etc /var/run/dbus /run/dbus /var/data/pim; printf 'ECU=MGU_02_L\nHWVAR=highf\n' > /run/etc/variant.env
(LD_LIBRARY_PATH=/usr/lib dbus-daemon --system --fork > /tmp/dbus.log 2>&1; LD_LIBRARY_PATH=/usr/lib dbus-daemon --session --address=unix:path=/run/dbus/session --fork >> /tmp/dbus.log 2>&1)
daemon() { n=$1; shift; (cd /; VSOMEIP_APPLICATION_NAME=$n ECU=MGU_02_L HWVAR=highf DBUS_SESSION_BUS_ADDRESS=unix:path=/run/dbus/session LD_LIBRARY_PATH=${XLD:+$XLD:}$H/lib/wicome:$H/lib/cinemo:$H/lib:/opt/ext/rootfs/usr/lib:/usr/lib CINEMO_PLUGIN_INIT=$H/lib/cinemo COMMONAPI_CONFIG=/etc/conn_commonapi.ini COMMONAPI_DBUS_CONFIG=/etc/conn_commonapi-dbus.ini exec "$@" > /tmp/d_$n.log 2>&1) & }
insmod /lib/modules/5.15.123/kernel/drivers/input/evdev.ko; insmod /lib/modules/5.15.123/kernel/drivers/input/misc/uinput.ko
LD_LIBRARY_PATH=/usr/lib /idemu/idemuinput > /tmp/input.log 2>&1 &
sleep 1
for e in /sys/class/input/event*; do [ "$(cat $e/device/name)" = cid-touchscreen ] && export HMI_TOUCH_INPUT_DEVICE=/dev/input/$(basename $e); done
echo "toque em $HMI_TOUCH_INPUT_DEVICE"
HMILOG=""; grep -q idemu.debug /proc/cmdline && HMILOG="-Ld -Ldl=info -Lo console"
export LD_LIBRARY_PATH=/idemu:/usr/lib
(/idemu/idemuwl > /tmp/wl.log 2>&1 &); sleep 1
export LD_LIBRARY_PATH=/idemu:$H/lib/hmi:$H/lib:/opt/ext/rootfs/usr/lib:/usr/lib
# pen drive virtual: o usbDaemon e o NodeStateManager do firmware, com um udisks falso (idemuusb) e os arquivos vindos do host
USB=0; grep -q idemu.usb /proc/cmdline && USB=1  # pilha de USB experimental (IDEMU_MGU_USB=1), ver docs/PENDRIVE.md
mkdir -p /run/idemu-usb /var/data/node0/node-state /run/lifecycle /var/run/nsm /run/dlt-files /var/data/node0/dlt-daemon /media
if grep -q idemu.dlt /proc/cmdline; then  # registro DLT dos daemons do firmware, só para depuração
  grep -v '^#' /etc/dlt_MGU22.conf | grep -v '^ *$' | sed 's/^LoggingMode.*/LoggingMode = 0/;s/^TransportProtocol.*//;s/^DaemonFifoGroup.*//;s/^UdpDest.*//' > /tmp/dlt.conf
  (LD_LIBRARY_PATH=/usr/lib dlt-daemon -c /tmp/dlt.conf > /tmp/dltd.log 2>&1 &); sleep 1
  (LD_LIBRARY_PATH=/usr/lib dlt-receive -a 127.0.0.1 > /tmp/dlt.txt 2>&1 &)
fi
rdaemon() { n=$1; shift; (cd /; DBUS_SESSION_BUS_ADDRESS=unix:path=/run/dbus/session VSOMEIP_APPLICATION_NAME=$n ECU=MGU_02_L HWVAR=highf APP_BOOT_MAX_COUNTER=10000 RSU_BOOT_MAX_COUNTER=5 LD_LIBRARY_PATH=/usr/lib COMMONAPI_CONFIG=/etc/conn_commonapi.ini COMMONAPI_DBUS_CONFIG=/etc/conn_commonapi-dbus.ini exec "$@" > /tmp/d_$n.log 2>&1) & }
if [ $USB = 1 ]; then
  rdaemon idemuusb /idemu/idemuusb
  rdaemon nsm /usr/bin/NodeStateManagerFRONT
  rdaemon usbDaemon /usr/bin/usbDaemon
fi
daemon persod $H/bin/persod --ecu=MGU_02_L
daemon dataprivacyd $H/bin/dataprivacyd
if [ $USB = 1 ]; then XLD=/idemu/usbfake daemon devicemanager $H/bin/devicemanager; else daemon devicemanager $H/bin/devicemanager; fi
daemon pim $H/bin/pim
daemon telephonymanager $H/bin/telephonymanager
daemon mediaapp $H/bin/mediaapp -v /etc/mediaconfig/mediaapp_variant_select.xml -i MAPP
daemon NavController $H/bin/NavController
echo IDEMU-HMI-START
# como o systemd no carro: se o HMI cair, sobe de novo
(n=0; while :; do
  LD_PRELOAD=/idemu/libidemuhmi.so $H/bin/Hmi8 -bsc -ap=$H/lib/hmi/applet -ro=$H/share/hmi/data/ro/ -lm=ramses_thread $HMILOG >> /tmp/hmi.log 2>&1
  r=$?; n=$((n+1)); echo "IDEMU-HMI-EXIT status=$r vez=$n up=$(cut -d' ' -f1 /proc/uptime)"; sleep 1
done) &
(while :; do
  sleep 30; P=$(pidof Hmi8)
  echo "IDEMU-HB $(grep -E 'VmRSS' /proc/$P/status | tr -s ' \t' ' ') threads=$(ls /proc/$P/task | wc -l) up=$(cut -d' ' -f1 /proc/uptime)"
done) &
exec /bin/sh
