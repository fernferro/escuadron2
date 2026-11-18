#!/bin/sh
# Arma el decompilador de Ghidra (solo la parte de C++) y el modulo del procesador de la
# GameCube en build/ghidra (fuera del repo). Necesita git, g++, make, bison y m4. Si no hay
# flex (o no se puede instalar), lo compila desde winflexbison, que trae los .c ya generados.
set -e
RAIZ=$(cd "$(dirname "$0")/../.." && pwd)
GH=${RL_GHIDRA:-$RAIZ/build/ghidra}
mkdir -p "$GH" && cd "$GH"
[ -d ghidra ] || { git clone --depth 1 --filter=blob:none --sparse https://github.com/NationalSecurityAgency/ghidra.git ghidra
  (cd ghidra && git sparse-checkout set Ghidra/Features/Decompiler/src/decompile Ghidra/Processors/PowerPC/data); }
[ -d gekko ] || git clone --depth 1 https://github.com/aldelaro5/ghidra-gekko-broadway-lang.git gekko
if ! command -v flex >/dev/null; then
  [ -x flex/flex ] || {
    [ -d winflexbison ] || git clone --depth 1 https://github.com/lexxmark/winflexbison.git winflexbison
    mkdir -p flex && cd flex
    printf '#pragma once\n#define _O_BINARY 0\n#define _setmode(a,b) 0\n#include <unistd.h>\n' > io.h
    printf '#pragma once\n#include <unistd.h>\n#include <stdlib.h>\n#include <string.h>\n#include <stdio.h>\n#define _getpid getpid\n#define _unlink unlink\n#define _strdup strdup\n#define _fdopen fdopen\n#define _dup dup\n#define _dup2 dup2\n#define _close close\n#define _fileno fileno\nstatic inline char* _tempnam(const char* d, const char* p) { static char b[256]; static int n; snprintf(b, sizeof b, "/tmp/%%s%%d_%%d", p, (int)getpid(), n++); return b; }\n' > process.h
    cat > extra.c <<'EOC'
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
char* xstrdup(const char* s) { return strdup(s); }
int _isatty(int f) { return isatty(f); }
int _stricmp(const char* a, const char* b) { return strcasecmp(a, b); }
/* winflexbison trae m4 adentro; aca se usa el del sistema */
int main_m4(int argc, char* const* argv, FILE* in, FILE* out) {
    char a[64], b[64], cmd[256], buf[65536]; FILE* t; size_t n;
    (void)argc; (void)argv;
    snprintf(a, sizeof a, "/tmp/m4in_%d", (int)getpid()); snprintf(b, sizeof b, "/tmp/m4out_%d", (int)getpid());
    t = fopen(a, "wb"); rewind(in);
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, t);
    fclose(t);
    snprintf(cmd, sizeof cmd, "m4 -P %s > %s", a, b);
    if (system(cmd) != 0) return 1;
    t = fopen(b, "rb");
    while ((n = fread(buf, 1, sizeof buf, t)) > 0) fwrite(buf, 1, n, out);
    fclose(t); remove(a); remove(b);
    return 0;
}
EOC
    S=../winflexbison/flex/src
    for f in $S/*.c ../winflexbison/common/misc/pid_tempname.c extra.c; do
      case $(basename $f) in libmain.c|libyywrap.c) continue;; esac
      gcc -w -O1 -include limits.h -include ./process.h -I. -I$S -I../winflexbison/common/misc -c $f -o $(basename $f .c).o
    done
    gcc -o flex *.o -lm
    cd ..
  }
  PATH=$GH/flex:$PATH; export PATH
fi
CPP=ghidra/Ghidra/Features/Decompiler/src/decompile/cpp
make -C $CPP -j8 sleigh_opt
# decomp_opt sin la carga por BFD (libbfd no hace falta: se carga la imagen plana)
EXTRA="callgraph ifacedecomp ifaceterm inject_sleigh interface libdecomp loadimage_xml raw_arch rulecompile sleigh_arch testfunction unify xml_arch"
make -C $CPP -j8 decomp_opt EXTRA="$EXTRA" LNK= || true
( cd $CPP && g++ -O2 -o decomp_opt $(ls com_opt/*.o | grep -v 'bfd\|analyzesigs\|codedata\|/test\.o\|sleighexample\|slgh_compile\|slghparse\|slghscan') -lz )
L=ghidra/Ghidra/Processors/PowerPC/data/languages
cp gekko/data/languages/* $L/
( cd $L && "$GH/$CPP/sleigh_opt" ppc_gekko_broadway.slaspec ppc_gekko_broadway.sla )
echo "listo: $GH/$CPP/decomp_opt"
