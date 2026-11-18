#!/bin/sh
# Corre el port dentro de gdb y muestra la pila cuando hay un error fatal o un fallo.
#   sh port/run_gdb.sh [opciones de escuadron2...]
make -C port -j2 >/dev/null 2>build/make.err || { tail -20 build/make.err; exit 1; }
gdb -batch -ex "break rt_fatal" -ex "run" -ex "bt 40" -ex "info registers rip" --args ./build/port/escuadron2 "$@" 2>&1 \
  | grep -vE "^\[(New|Thread)|libthread_db|Using host" | sed 's/ (ctx=[^)]*)//; s/ at \/home\/claude\/escuadron2\// at /' | tail -${LINES:-60}
