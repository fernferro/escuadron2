#!/bin/sh
# Compila el port, lo corre N segundos y, si sigue vivo, muestra la pila de cada hilo.
#   sh port/run_debug.sh [segundos] [opciones de escuadron2...]
SECS=${1:-10}; shift
make -C port -j2 >/dev/null 2>build/make.err || { tail -20 build/make.err; exit 1; }
./build/port/escuadron2 "$@" > build/run.out 2> build/run.err &
PID=$!
sleep "$SECS"
if kill -0 $PID 2>/dev/null; then
  echo "== sigue corriendo despues de ${SECS}s; pilas:"
  gdb -p $PID -batch -ex "thread apply all bt 30" 2>/dev/null | grep -E "^#|^Thread" | sed 's/ (ctx=[^)]*)//; s/ at \/home\/claude\/escuadron2\// at /' | head -${STACKLINES:-60}
  kill $PID 2>/dev/null
else
  wait $PID; echo "== termino con codigo $?"
fi
echo "== stderr (ultimas lineas):"; tail -${ERRLINES:-25} build/run.err
echo "== stdout (ultimas lineas):"; tail -${OUTLINES:-15} build/run.out
