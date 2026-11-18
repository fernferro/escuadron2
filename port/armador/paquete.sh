#!/bin/sh
# Arma el paquete publicable de Escuadron2 para Windows (sin nada del juego):
#   Escuadron2/Escuadron2.exe            el launcher
#   Escuadron2/herramientas/armar/        armar.py, los traductores, el runtime ya compilado (obj/), SDL2
#   Escuadron2/herramientas/clang/        llvm-mingw recortado (compila el juego traducido en la PC del usuario)
#   Escuadron2/herramientas/python/       Python portable (python-build-standalone)
# Uso: port/armador/paquete.sh SALIDA.zip   (antes: make TARGET=win ... escuadron2.exe y launcher)
# Variables: WINHOST (llvm-mingw para Windows descomprimido), PYWIN (python-build-standalone),
#            MINGW y SDL2_WIN (los mismos del Makefile).
set -e
RAIZ=$(cd "$(dirname "$0")/../.." && pwd)
SAL=${1:-$RAIZ/build/paquete/Escuadron2.zip}
WINHOST=${WINHOST:-/home/claude/thirdparty/winhost/llvm-mingw-20250114-ucrt-x86_64}
PYWIN=${PYWIN:-/home/claude/thirdparty/pywin/python}
MINGW=${MINGW:-/home/claude/thirdparty/llvm-mingw-20250114-ucrt-ubuntu-20.04-x86_64}
SDL2_WIN=${SDL2_WIN:-/home/claude/thirdparty/SDL2-2.30.12/x86_64-w64-mingw32}
OBJ=$RAIZ/build/port-win/obj
P=$RAIZ/build/paquete/Escuadron2
H=$P/herramientas
rm -rf "$P"; mkdir -p "$H/armar/scripts" "$H/armar/datos" "$H/armar/include" "$H/armar/obj" "$H/armar/lib" "$P/licencias"

# launcher y textos
cp "$RAIZ/build/port-win/Escuadron2.exe" "$P/"
cp "$RAIZ/port/launcher/LEEME.txt" "$P/"
cp "$RAIZ/port/third_party/imgui/LICENSE.txt" "$P/licencias/imgui.txt"
cp "$RAIZ/port/launcher/fuentes/OFL.txt" "$P/licencias/poppins_OFL.txt"
cp "$RAIZ/port/third_party/zlib/LICENSE" "$P/licencias/zlib.txt"
cp "$RAIZ/port/dsp/COPYING" "$P/licencias/dolphin_dsp_GPL2.txt"
cp "$RAIZ/port/launcher/licencia_SDL2.txt" "$P/licencias/SDL2.txt"
cp "$WINHOST/LICENSE.TXT" "$P/licencias/llvm-mingw.txt"
cp "$PYWIN/LICENSE.txt" "$P/licencias/python.txt"

# armador: scripts, datos (mapa de funciones, ROM libre del DSP), cabeceras para el codigo traducido
cp "$RAIZ/port/armador/armar.py" "$H/armar/"
cp "$RAIZ/port/recomp/recomp.py" "$RAIZ/port/recomp/dsprecomp.py" "$RAIZ/port/recomp/dsp_tablas.py" \
   "$RAIZ/port/tools/rapidas.py" "$RAIZ/tools/combine_vm.py" "$H/armar/scripts/"
cp "$RAIZ/config/GSWE64/symbols.txt" "$RAIZ/port/hle.txt" "$H/armar/datos/"
cp "$RAIZ/port/dsp/rom/dsp_rom.bin" "$H/armar/datos/"
cp "$RAIZ"/port/runtime/*.h "$RAIZ"/port/dsp_nativo/*.h "$H/armar/include/"
# runtime ya compilado (nuestro codigo, sin nada traducido del juego), sin informacion de depuracion
for o in "$OBJ"/main.o "$OBJ"/juego_rc.o "$OBJ"/rt_*.o "$OBJ"/dsp/*.o "$OBJ"/nat_dsp_nativo.o "$OBJ"/menu.o \
         "$OBJ"/launcher/imgui.o "$OBJ"/launcher/imgui_draw.o "$OBJ"/launcher/imgui_tables.o \
         "$OBJ"/launcher/imgui_widgets.o "$OBJ"/launcher/imgui_impl_sdl2.o "$OBJ"/launcher/imgui_impl_opengl3.o; do
    n=$(basename "$o"); case "$o" in */dsp/*) n="dsp_$n";; */launcher/*) n="lnc_$n";; esac
    "$MINGW/bin/llvm-strip" --strip-debug -o "$H/armar/obj/$n" "$o"
done
cp "$SDL2_WIN/lib/libSDL2.a" "$H/armar/lib/"

# clang recortado: solo lo necesario para compilar C para x86_64 y enlazar
C=$H/clang
mkdir -p "$C/bin" "$C/lib/clang/19/lib/windows" "$C/x86_64-w64-mingw32"
for f in clang.exe clang++.exe clang-19.exe x86_64-w64-mingw32-clang.exe x86_64-w64-mingw32-clang++.exe \
         x86_64-w64-windows-gnu.cfg mingw32-common.cfg ld.lld.exe llvm-strip.exe llvm-objcopy.exe \
         libLLVM-19.dll libclang-cpp.dll libc++.dll libunwind.dll; do
    cp "$WINHOST/bin/$f" "$C/bin/"
done
cp -r "$WINHOST/include" "$C/"
cp -r "$WINHOST/lib/clang/19/include" "$C/lib/clang/19/"
cp "$WINHOST/lib/clang/19/lib/windows/libclang_rt.builtins-x86_64.a" "$C/lib/clang/19/lib/windows/"
cp -r "$WINHOST/x86_64-w64-mingw32/lib" "$C/x86_64-w64-mingw32/"

# Python portable, sin lo que no se usa
cp -r "$PYWIN" "$H/python"
rm -rf "$H/python/Lib/test" "$H/python/Lib/idlelib" "$H/python/Lib/tkinter" "$H/python/Lib/turtledemo" \
       "$H/python/Lib/ensurepip" "$H/python/Lib/site-packages"/* "$H/python/include" "$H/python/libs" \
       "$H/python/Scripts" "$H/python/tcl" "$H/python/DLLs"/tcl* "$H/python/DLLs"/tk* "$H/python/DLLs/_tkinter.pyd" "$H/python/DLLs"/_test* 2>/dev/null || true
find "$H/python" -name "__pycache__" -type d -prune -exec rm -rf {} +

du -sh "$P" "$H/clang" "$H/python" "$H/armar"
cd "$(dirname "$P")" && rm -f "$SAL" && zip -q -r -9 "$SAL" Escuadron2 && ls -la "$SAL"
