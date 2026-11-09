#!/bin/zsh
# Compila as peças do iDrive 8: aarch64 (glibc do firmware) para o guest e arm64 macOS para o host. Uso: build.sh <sysroot> <saida>
set -e
cd "$(dirname "$0")"
SYS=$1; OUT=$2; mkdir -p $OUT
LLD=${LLD:-$HOME/.rustup/toolchains/stable-aarch64-apple-darwin/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld}
CF=(-target aarch64-linux-gnu -ffreestanding -fPIC -O2 -fno-stack-protector -fno-builtin-malloc -Wno-everything)
LD=($LLD -shared -z noexecstack --hash-style=gnu)
EX=($LLD --dynamic-linker=/lib/ld-linux-aarch64.so.1 -z noexecstack --hash-style=gnu --unresolved-symbols=ignore-in-shared-libs)
L=(-L$SYS/lib -L$SYS/usr/lib)
python3 gen.py
clang $CF -c libcod.c -o $OUT/libcod.o
$LD -soname libcod.so.0 -o $OUT/libcod.so.0 $OUT/libcod.o $L -l:libc.so.6
clang $CF -c crt.c -o $OUT/crt.o
clang $CF -c ustk.c -o $OUT/ustk.o
$EX -o $OUT/ustk $OUT/crt.o $OUT/ustk.o $L -l:libc.so.6
clang $CF -c guest_egl.c -o $OUT/guest_egl.o
$LD -soname libEGL.so.1 -o $OUT/libEGL.so.1 $OUT/guest_egl.o $L -l:libc.so.6 -l:libwayland-client.so.0
echo 'int idemu_gles_stub;' > $OUT/empty.c; clang $CF -c $OUT/empty.c -o $OUT/empty.o
$LD -soname libGLESv2.so.2 -o $OUT/libGLESv2.so.2 $OUT/empty.o -L$OUT -l:libEGL.so.1
clang $CF -c idemuinput.c -o $OUT/idemuinput.o
$EX -o $OUT/idemuinput $OUT/crt.o $OUT/idemuinput.o $L -l:libc.so.6
clang $CF -c idemuusb.c -o $OUT/idemuusb.o
$EX -o $OUT/idemuusb $OUT/crt.o $OUT/idemuusb.o $L -l:libc.so.6 -l:libdbus-1.so.3
clang $CF -c usbfake/libusb_fake.c -o $OUT/libusb_fake.o
$LD -soname libusb-1.0.so.0 -o $OUT/libusb-1.0.so.0 $OUT/libusb_fake.o $L -l:libc.so.6
clang $CF -c usbfake/libudev_fake.c -o $OUT/libudev_fake.o
$LD -soname libudev.so.1 --version-script=usbfake/libudev.map -o $OUT/libudev.so.1 $OUT/libudev_fake.o $L -l:libc.so.6
clang $CF -c hmihook.c -o $OUT/hmihook.o
$LD -soname libidemuhmi.so -o $OUT/libidemuhmi.so $OUT/hmihook.o $L -l:libc.so.6
clang $CF -c idemuwl.c -o $OUT/idemuwl.o
$EX -o $OUT/idemuwl $OUT/crt.o $OUT/idemuwl.o $L -l:libc.so.6 -l:libwayland-server.so.0 -l:libivi-application.so.1 -l:libivi-controller.so.1
clang -O2 -Wno-everything mguhost.c -o $OUT/mguhost
clang -O2 -Wno-everything -dynamiclib qemushm.c -o $OUT/qemushm.dylib
