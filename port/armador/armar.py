#!/usr/bin/env python3
"""Armador de Escuadron2: arma escuadron2_game.exe en la PC del usuario a partir de SU disco.

El paquete que se publica no lleva nada del juego. La primera vez, el launcher corre esto:

    python armar.py --iso DISCO.iso --salida CARPETA [--trabajo CARPETA] [--hilos N]

1. Lee el disco, comprueba que sea GSWE64 y que main.dol sea el esperado (SHA-1).
2. Arma la imagen combinada (cargador + codigo del juego en sus direcciones).
3. Traduce el codigo del juego a C (recomp.py), el sonido del DSP (dsprecomp.py) y las
   copias rapidas del decodificador de video (rapidas.py).
4. Compila eso con el clang que viene en el paquete y lo enlaza con el runtime ya compilado.

Escribe lineas "PASO texto" y "AVANCE n" (0-100) para la barra del launcher.
Carpetas, relativas a este archivo: scripts/, datos/, include/, obj/, lib/ y ../clang/.
"""
import argparse
import concurrent.futures as cf
import hashlib
import os
import shutil
import struct
import subprocess
import sys
import time

AQUI = os.path.dirname(os.path.abspath(__file__))
SHA1_MAIN = "bc595e8b8c89ec8307ef61658b6cd5c854670d60"
RAPIDAS = ["80055F60", "8005AD80"]
DSP_MUSYX = "dol:2091392:6624:badc6015333328edb10e72f25b5afbf3ef903090"
DSP_INIT = "dol:2099416:128:f5a04aac3392caf001fca12bf61563ac21d86a1f"
VERSION_ARMADOR = "1"   # si cambia, el launcher vuelve a armar el juego


def paso(t):
    print("PASO " + t, flush=True)


def avance(n):
    print("AVANCE %d" % max(0, min(100, int(n))), flush=True)


def fallar(t):
    print("ERROR " + t, flush=True)
    sys.exit(1)


def leer_main_dol(iso):
    """main.dol del disco: su posicion esta en boot.bin (0x420); el largo sale de la cabecera."""
    with open(iso, "rb") as f:
        cab = f.read(0x440)
        if len(cab) < 0x440:
            fallar("The file is too small to be a GameCube disc.")
        if struct.unpack(">I", cab[0x1C:0x20])[0] != 0xC2339F3D:
            fallar("This is not an uncompressed GameCube disc image (.iso / .gcm).")
        ident = cab[:6].decode("ascii", "replace")
        if ident != "GSWE64":
            fallar("This disc is %s; the port needs the US disc (GSWE64)." % ident)
        off = struct.unpack(">I", cab[0x420:0x424])[0]
        f.seek(off)
        h = f.read(0x100)
        offs = struct.unpack(">18I", h[0:72])
        tams = struct.unpack(">18I", h[144:216])
        largo = max(o + t for o, t in zip(offs, tams) if t)
        f.seek(off)
        dol = f.read(largo)
    if hashlib.sha1(dol).hexdigest() != SHA1_MAIN:
        fallar("The program on this disc (main.dol) is not the expected one: it may be another revision "
               "of the game or a modified image.")
    return dol


def correr(cmd, cwd=None):
    flags = 0x08000000 if os.name == "nt" else 0   # CREATE_NO_WINDOW: sin ventanas negras
    r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, creationflags=flags)
    if r.returncode != 0:
        print((r.stdout or "") + (r.stderr or ""), flush=True)
        fallar("Failed: " + " ".join(os.path.basename(c) if i == 0 else c for i, c in enumerate(cmd[:3])) + " ...")
    return r


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", help="imagen del disco (.iso / .gcm)")
    ap.add_argument("--dol", help="o directamente sys/main.dol (extraido con Dolphin)")
    ap.add_argument("--salida", required=True, help="carpeta donde queda escuadron2_game.exe")
    ap.add_argument("--trabajo", help="carpeta temporal (por defecto SALIDA/armado)")
    ap.add_argument("--hilos", type=int, default=0)
    ap.add_argument("--clang", help="carpeta de llvm-mingw (por defecto ../clang)")
    a = ap.parse_args()

    t0 = time.time()
    py = sys.executable
    sc = os.path.join(AQUI, "scripts")
    dat = os.path.join(AQUI, "datos")
    inc = os.path.join(AQUI, "include")
    clang_dir = a.clang or os.path.join(os.path.dirname(AQUI), "clang")
    exe = ".exe" if os.name == "nt" else ""
    cc = os.path.join(clang_dir, "bin", "x86_64-w64-mingw32-clang" + exe)
    cxx = os.path.join(clang_dir, "bin", "x86_64-w64-mingw32-clang++" + exe)
    strip = os.path.join(clang_dir, "bin", "llvm-strip" + exe)
    if not os.path.exists(cc):
        fallar("The bundled compiler is missing (%s). Extract the whole zip again." % cc)
    trab = a.trabajo or os.path.join(a.salida, "armado")
    gen = os.path.join(trab, "gen")
    obj = os.path.join(trab, "obj")
    os.makedirs(gen, exist_ok=True)
    os.makedirs(obj, exist_ok=True)

    paso("Reading your disc")
    avance(1)
    if a.iso:
        dol = leer_main_dol(a.iso)
    elif a.dol:
        dol = open(a.dol, "rb").read()
        if hashlib.sha1(dol).hexdigest() != SHA1_MAIN:
            fallar("That main.dol is not the expected one (GSWE64).")
    else:
        fallar("Missing --iso (or --dol).")
    main_dol = os.path.join(trab, "main.dol")
    combinado = os.path.join(trab, "combined.dol")
    open(main_dol, "wb").write(dol)
    correr([py, "-I", os.path.join(sc, "combine_vm.py"), main_dol, combinado])
    avance(4)

    paso("Translating the game program")
    for f in os.listdir(gen):
        os.remove(os.path.join(gen, f))
    correr([py, "-I", os.path.join(sc, "recomp.py"), combinado, os.path.join(dat, "symbols.txt"), gen,
            "--hle", os.path.join(dat, "hle.txt")])
    avance(12)
    paso("Translating the sound program")
    correr([py, "-I", os.path.join(sc, "dsprecomp.py"), os.path.join(gen, "dsp_programas.c"), "--dol=" + combinado,
            "musyx=%s@0" % DSP_MUSYX, "init=%s@0" % DSP_INIT, "rom=%s@8000" % os.path.join(dat, "dsp_rom.bin")])
    correr([py, "-I", os.path.join(sc, "rapidas.py"), gen, os.path.join(gen, "rapidas.c")] + RAPIDAS)
    avance(16)

    # que compilar y con que opciones (las mismas que port/Makefile, sin -g)
    base = ["-w", "-I" + inc, "-I" + gen, "-DRL_GL"]
    nat = ["-O2", "-std=gnu11", "-fno-strict-aliasing", "-w", "-Werror=implicit-function-declaration", "-I" + inc]
    trabajos = []
    for f in sorted(os.listdir(gen)):
        if f.startswith("code_") and f.endswith(".c") or f == "table.c":
            trabajos.append((f, ["-O2"] + base))
    trabajos.append(("rapidas.c", ["-O3", "-fno-math-errno", "-ffp-contract=fast"] + base))
    trabajos.append(("dsp_programas.c", nat))
    trabajos.append(("dsp_programas_paso.c", nat + ["-O1"]))
    # los mas grandes primero, para que los hilos terminen parejo
    trabajos.sort(key=lambda t: -os.path.getsize(os.path.join(gen, t[0])))

    paso("Compiling (this is the longest part)")
    hilos = a.hilos or max(1, (os.cpu_count() or 2))
    hechos = [0]

    def compilar(t):
        f, fl = t
        o = os.path.join(obj, f[:-2] + ".o")
        correr([cc] + fl + ["-c", os.path.join(gen, f), "-o", o])
        return o

    objs = []
    with cf.ThreadPoolExecutor(hilos) as ex:
        for o in ex.map(compilar, trabajos):
            objs.append(o)
            hechos[0] += 1
            avance(16 + 76 * hechos[0] / len(trabajos))

    paso("Linking")
    pre = sorted(os.path.join(AQUI, "obj", f) for f in os.listdir(os.path.join(AQUI, "obj")) if f.endswith(".o"))
    libs = [os.path.join(AQUI, "lib", "libSDL2.a"), "-lsetupapi", "-lwinmm", "-limm32", "-lversion", "-lole32",
            "-loleaut32", "-lgdi32", "-luser32", "-lshell32", "-lcfgmgr32", "-luuid", "-ladvapi32", "-lopengl32",
            "-lm", "-lpthread"]
    os.makedirs(a.salida, exist_ok=True)
    tmp = os.path.join(trab, "escuadron2_game.exe")
    correr([cxx, "-static", "-Wl,--stack,67108864", "-o", tmp, "-Wl,--start-group"] + pre + objs + libs + ["-Wl,--end-group"])
    correr([strip, tmp])
    final = os.path.join(a.salida, "escuadron2_game.exe")
    shutil.move(tmp, final)
    with open(os.path.join(a.salida, "escuadron2.armado"), "w") as f:
        f.write("armador %s\nsha1 %s\n" % (VERSION_ARMADOR, SHA1_MAIN))
    avance(100)
    paso("Done in %.0f s" % (time.time() - t0))
    shutil.rmtree(trab, ignore_errors=True)


if __name__ == "__main__":
    main()
