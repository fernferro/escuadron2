#!/bin/sh
# Prepara una copia de trabajo (git worktree) para compilar sin volver a
# extraer el juego: enlaza los archivos del juego y las herramientas del
# repo principal y arma todo.
#
#   sh tools/worktree_setup.sh /ruta/al/repo/principal
set -e
MAIN="$1"
[ -n "$MAIN" ] || { echo "uso: sh tools/worktree_setup.sh /ruta/al/repo/principal"; exit 1; }
mkdir -p orig/GSWE64 build
for f in "$MAIN"/orig/GSWE64/*; do ln -sfn "$f" orig/GSWE64/; done
ln -sfn "$MAIN/build/mwcc" build/mwcc
# las herramientas se copian: ninja puede reescribirlas
for d in binutils tools; do [ -e "build/$d" ] || cp -a "$MAIN/build/$d" "build/$d"; done
python3 configure.py --compilers build/mwcc >/dev/null
ninja >/dev/null 2>&1 || ninja
python3 tools/check_build.py orig/GSWE64/sys/main.dol build/GSWE64 | tail -1
