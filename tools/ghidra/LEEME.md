# Ghidra (solo el decompilador)

C legible y aproximado de cualquier funcion del juego, con los nombres de `symbols.txt`.
No es C para compilar (para eso esta `docs/decompilar.md`): sirve para entender que hace
una funcion antes de reescribirla o para buscar un error.

    sh tools/ghidra/armar.sh                       # una vez: queda en build/ghidra (fuera del repo)
    python3 tools/ghidra/decompilar.py PLAYERcockpit_vfunc24 0x7FCC3D88
    python3 tools/ghidra/decompilar.py --desensamblar fn_7FC2E760

`armar.sh` baja con git solo el codigo C++ del decompilador de Ghidra y los archivos del
PowerPC, mas el modulo Gekko/Broadway (paired singles) de aldelaro5, y compila `sleigh_opt`
y `decomp_opt` (sin libbfd: se le pasa la memoria del juego como imagen plana). Si no hay
`flex`, lo arma desde winflexbison, que trae el analizador ya generado.
