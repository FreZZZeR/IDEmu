#!/bin/zsh
cd /private/tmp/claude-501/scratch/gl && clang -O1 -o glhost host.c -framework GLUT -framework OpenGL -Wno-deprecated-declarations -Wno-int-conversion -Wno-pointer-sign -Wno-implicit-function-declaration 2>&1 | grep -E "error" | head
