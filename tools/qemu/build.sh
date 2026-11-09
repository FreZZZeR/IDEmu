#!/bin/zsh
# Compila o QEMU x86-64 do IDEmu: o QEMU 11.1.2 oficial mais o dispositivo `idemu-shm` (memória compartilhada guest <-> glhost7) e a correção de migração do `sdhci-pci`
# (sem ela o snapshot do iDrive 7 perde o controlador do cartão SD). Resultado: bin/qemu-system-x86_64 + bin/qemu-share/ (firmware).
#
# Uso: tools/qemu/build.sh <qemu-11.1.2.tar.xz> [python3.10+]
#   * O tarball oficial vem de https://download.qemu.org (o script não baixa nada).
#   * Precisa de ninja, pkg-config, glib e pixman (brew install ninja pkg-config glib pixman) e de um Python >= 3.10 (o 3.9 da Apple não serve ao configure).
set -e
TAR=${1:?uso: build.sh <qemu-11.1.2.tar.xz> [python]}; PY=${2:-$(command -v python3.12 || command -v python3.11 || command -v python3.10 || true)}
[ -n "$PY" ] || { echo "preciso de um Python >= 3.10 (segundo argumento)"; exit 1; }
HERE=${0:A:h}; ROOT=${HERE:h:h}; W=${QEMU_BUILD_DIR:-$HOME/idemu-qemu}
mkdir -p $W && cd $W && [ -d qemu-11.1.2 ] || tar xf $TAR
SRC=$W/qemu-11.1.2
cp $ROOT/src/qemu/idemu-shm.c $SRC/hw/misc/idemu-shm.c
cp $ROOT/src/qemu/sdhci-pci.c $SRC/hw/sd/sdhci-pci.c
grep -q IDEMU_SHM $SRC/hw/misc/Kconfig || python3 - "$SRC" <<'PYEOF'
import sys
s = sys.argv[1]
p = s + '/hw/misc/Kconfig'; t = open(p).read(); t = t.replace('config PCI_TESTDEV\n', 'config IDEMU_SHM\n    bool\n    default y if PCI_DEVICES\n    depends on PCI\n\nconfig PCI_TESTDEV\n', 1); open(p, 'w').write(t)
p = s + '/hw/misc/meson.build'; t = open(p).read(); t = t.replace("system_ss.add(when: 'CONFIG_PCI_TESTDEV'", "system_ss.add(when: 'CONFIG_IDEMU_SHM', if_true: files('idemu-shm.c'))\nsystem_ss.add(when: 'CONFIG_PCI_TESTDEV'", 1); open(p, 'w').write(t)
PYEOF
mkdir -p $W/build && cd $W/build
[ -f build.ninja ] || $SRC/configure --python=$PY --target-list=x86_64-softmmu --disable-docs --disable-guest-agent --disable-tools --disable-gtk --disable-sdl --disable-vnc --disable-curses --disable-rust --disable-user --disable-linux-user --disable-bsd-user --without-default-features --enable-tcg
ninja qemu-system-x86_64
# o macOS mata (SIGKILL) um executável sobrescrito no mesmo arquivo: copia para um nome novo e troca
cp qemu-system-x86_64 $ROOT/bin/qemu-system-x86_64.new && mv $ROOT/bin/qemu-system-x86_64.new $ROOT/bin/qemu-system-x86_64
mkdir -p $ROOT/bin/qemu-share && for f in bios-256k.bin vgabios-stdvga.bin linuxboot_dma.bin kvmvapic.bin; do cp $SRC/pc-bios/$f $ROOT/bin/qemu-share/; done
$ROOT/bin/qemu-system-x86_64 -device help 2>&1 | grep -q idemu-shm && echo "ok: bin/qemu-system-x86_64 com o dispositivo idemu-shm"
