#!/bin/zsh
# Compila o transporte io-ipc virtual (ARMv7, freestanding) -> ../../idemu/shim/lib/ioipc-idemu.so
set -e
cd "$(dirname "$0")"
PY=${1:-/private/tmp/claude-501/scratch/venv/bin/python}
LIBC=${2:-../shim/lib/libc.so.3}
OUT=../../idemu/shim/lib; mkdir -p out $OUT
CF=(-target armv7-none-eabi -march=armv7-a -mfloat-abi=soft -ffreestanding -mno-unaligned-access -fPIC -Os -Wno-incompatible-library-redeclaration -Wno-builtin-requires-header)
LD=($PY -m ziglang ld.lld -shared -z max-page-size=4096 -z common-page-size=4096 -z norelro -z noseparate-code --no-rosegment --hash-style=sysv --no-eh-frame-hdr -z nognustack --allow-shlib-undefined)
cp ${3:-/private/tmp/claude-501/scratch/fatsrc2/lib/libsocket.so.3} out/libsocket.so.3
clang "${CF[@]}" -c ioipc_idemu.c -o out/ioipc_idemu.o
$LD -soname ioipc-idemu.so -o out/ioipc-idemu.so out/ioipc_idemu.o -L$(dirname $LIBC) -l:libc.so.3
cp out/ioipc-idemu.so $OUT/
ls -l $OUT/ioipc-idemu.so
