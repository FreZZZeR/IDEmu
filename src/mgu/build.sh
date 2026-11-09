#!/bin/zsh
# Compila as peças do guest x86-64 do iDrive 7 (clang + ld.lld do Rust). Uso: ./build.sh <libc-2.27.so do MGU>
set -e; cd "$(dirname "$0")"; mkdir -p out/lib
LIBC=${1:-/tmp/mgu/rse/root/lib/libc-2.27.so}; cp "$LIBC" out/lib/libc.so.6
LLD=$(ls ~/.rustup/toolchains/*/lib/rustlib/aarch64-apple-darwin/bin/gcc-ld/ld.lld | head -1)
CF=(-target x86_64-linux-gnu -ffreestanding -fPIC -O1 -fno-stack-protector -Wno-incompatible-library-redeclaration -Wno-builtin-requires-header -Wno-implicit-function-declaration -Wno-empty-body)
for f in "$@"; do :; done
clang $CF -c wlstub.c -o out/wlstub.o
$LLD -pie -o out/idemu-wl --dynamic-linker /lib/ld-linux-x86-64.so.2 -e _start out/wlstub.o -Lout/lib -l:libc.so.6
clang $CF -c idemu_run.c -o out/idemu_run.o
$LLD -pie -o out/idemu-run --dynamic-linker /lib/ld-linux-x86-64.so.2 -e _start out/idemu_run.o -Lout/lib -l:libc.so.6
clang $CF -c lifecycle.c -o out/lifecycle.o
$LLD -pie -o out/idemu-lifecycle --dynamic-linker /lib/ld-linux-x86-64.so.2 -e _start out/lifecycle.o -Lout/lib -l:libc.so.6
ls -l out/idemu-wl
cp /tmp/mgu/rse/root/lib/libdl-2.27.so out/lib/libdl.so.2
python3 gen_gl.py >/dev/null
clang $CF -c gles_guest.c -o out/gles_guest.o
$LLD -shared -soname libGLESv2.so.2 -o out/libGLESv2.so.2 out/gles_guest.o -Lout/lib -l:libc.so.6 -l:libdl.so.2
echo 'int idemu_egl_stub;' > out/egl_stub.c; clang $CF -c out/egl_stub.c -o out/egl_stub.o
$LLD -shared -soname libEGL.so.1 -o out/libEGL.so.1 out/egl_stub.o --no-as-needed -Lout -l:libGLESv2.so.2
ls -l out/libGLESv2.so.2 out/libEGL.so.1
clang $CF -c fakeipc.c -o out/fakeipc.o
$LLD -shared -soname libipc.so.1 -o out/libipc.so.1 out/fakeipc.o -Lout/lib -l:libc.so.6
ls -l out/libipc.so.1
clang $CF -c fakesvc.c -o out/fakesvc.o
$LLD -pie -o out/idemu-fakesvc --dynamic-linker /lib/ld-linux-x86-64.so.2 -e _start out/fakesvc.o -Lout/lib -l:libc.so.6
ls -l out/idemu-fakesvc
clang $CF -c fastclock.c -o out/fastclock.o
$LLD -shared -soname libfastclock.so -o out/libfastclock.so out/fastclock.o
ls -l out/libfastclock.so
