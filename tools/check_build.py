#!/usr/bin/env python3
"""
Paso CHECK del build:
  1. Reempaqueta build/GSWE64/combined.elf -> build/GSWE64/main.dol (tools/pack_main.py)
  2. Compara su SHA-1 con el main.dol original.
  3. Si no coincide, muestra el detalle por sección (tools/verify_build.py).

    python tools/check_build.py orig/GSWE64/sys/main.dol build/GSWE64 -o build/GSWE64/ok
"""
import hashlib
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import pack_main  # noqa: E402
import verify_build  # noqa: E402


def main() -> int:
    args = sys.argv[1:]
    ok_path = None
    if "-o" in args:
        i = args.index("-o")
        ok_path = Path(args[i + 1])
        del args[i:i + 2]
    if len(args) != 2:
        print(__doc__)
        return 2
    orig = Path(args[0])
    build_dir = Path(args[1])

    secs, entry = pack_main.read_elf(build_dir / "combined.elf")
    main_dol, vm_dol, removed = pack_main.pack(secs, entry)
    out = build_dir / "main.dol"
    out.write_bytes(main_dol)

    want = hashlib.sha1(orig.read_bytes()).hexdigest()
    got = hashlib.sha1(main_dol).hexdigest()
    print(f"original : {want}")
    print(f"build    : {got}  ({out})")
    if want == got:
        print("\nCOINCIDE: main.dol es idéntico byte a byte al original.")
        if ok_path:
            ok_path.write_text("ok\n")
        return 0

    print("\nNO COINCIDE. Detalle por sección:\n")
    sys.argv = [sys.argv[0], str(orig), str(out)]
    verify_build.main()
    return 1


if __name__ == "__main__":
    sys.exit(main())
