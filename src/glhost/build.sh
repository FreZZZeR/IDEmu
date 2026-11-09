#!/bin/zsh
# Compila o renderizador do host (bin/glhost).
set -e
cd "$(dirname "$0")"
cp ../shim/glops.h ../shim/gen_host.inc ../shim/opnames.inc ../shim/gl_hand_names.inc . 2>/dev/null || true
clang -O2 -Wno-everything host.c pngload.c -framework GLUT -framework OpenGL -framework ApplicationServices -framework CoreFoundation -o ../../bin/glhost
ls -l ../../bin/glhost
