#!/usr/bin/env python3
"""
Prepara el proyecto a partir de TU copia del juego (GSWE64).

Uso:
    python tools/setup_rl.py ruta/al/juego.iso      (también .rvz, .gcm, .ciso)
    python tools/setup_rl.py ruta/a/sys/main.dol    (si ya lo extrajiste con Dolphin)

Qué hace:
  1. Extrae el disco (o copia la carpeta sys) en orig/GSWE64/.
  2. Verifica el SHA-1 de main.dol contra el de referencia.
  3. Genera orig/GSWE64/combined.dol (cargador + código VM) y pone su hash en config.yml.
"""
import hashlib
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
VERSION = "GSWE64"
ORIG = ROOT / "orig" / VERSION
CFG = ROOT / "config" / VERSION


def sha1(path: Path) -> str:
    h = hashlib.sha1()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def find_dtk() -> Path:
    for cand in (ROOT / "build" / "tools" / "dtk", ROOT / "build" / "tools" / "dtk.exe"):
        if cand.exists():
            return cand
    found = shutil.which("dtk")
    if found:
        return Path(found)
    print("Descargando decomp-toolkit...")
    subprocess.run([sys.executable, "configure.py"], cwd=ROOT, check=True)
    subprocess.run(["ninja", "build/tools/dtk" + (".exe" if sys.platform == "win32" else "")],
                   cwd=ROOT, check=True)
    return find_dtk()


def main() -> None:
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    disc = Path(sys.argv[1]).resolve()
    if not disc.exists():
        sys.exit(f"No existe: {disc}")

    ORIG.mkdir(parents=True, exist_ok=True)
    sysdir = ORIG / "sys"
    if disc.is_dir() or disc.suffix.lower() == ".dol":
        # Ya extraído con Dolphin ("Extract System Data"): copiar la carpeta sys
        src = disc if disc.is_dir() else disc.parent
        sysdir.mkdir(exist_ok=True)
        for f in ("main.dol", "boot.bin", "bi2.bin", "apploader.img", "fst.bin"):
            if (src / f).exists() and (src / f).resolve() != (sysdir / f).resolve():
                shutil.copy2(src / f, sysdir / f)
        print(f"Copiados los archivos de sistema desde {src}")
    else:
        dtk = find_dtk()
        print(f"Extrayendo {disc.name} en {ORIG} ...")
        subprocess.run([str(dtk), "disc", "extract", str(disc), str(ORIG)], check=True)

    dol = sysdir / "main.dol"
    if not dol.exists():
        sys.exit("No se encontró sys/main.dol tras la extracción.")

    gameid = (ORIG / "sys" / "boot.bin").read_bytes()[:6].decode("ascii", "replace") \
        if (ORIG / "sys" / "boot.bin").exists() else "?"
    print(f"ID del disco: {gameid}")
    if gameid != VERSION and gameid != "?":
        print(f"AVISO: el proyecto está configurado para {VERSION}. "
              f"Renombrá config/{VERSION} y orig/{VERSION} a {gameid} y actualizá configure.py.")

    digest = sha1(dol)
    print(f"main.dol SHA-1: {digest}")
    if digest != "bc595e8b8c89ec8307ef61658b6cd5c854670d60":
        print("AVISO: este main.dol no coincide con el de referencia (GSWE64 USA).")

    # main.dol lleva embebido rogue2vm.dol; se analiza una imagen combinada.
    combined = ORIG / "combined.dol"
    subprocess.run([sys.executable, str(ROOT / "tools" / "combine_vm.py"), str(dol), str(combined)],
                   check=True)
    cdigest = sha1(combined)
    cfg = CFG / "config.yml"
    text = re.sub(r"^hash: .*$", f"hash: {cdigest}", cfg.read_text(), count=1, flags=re.M)
    cfg.write_text(text)
    # La verificación del build compara contenido: ver tools/verify_build.py
    print(f"combined.dol SHA-1: {cdigest}  -> config.yml actualizado")
    print("\nListo. Siguiente paso:  python configure.py  &&  ninja")


if __name__ == "__main__":
    main()
