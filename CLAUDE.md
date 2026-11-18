# Escuadron2: instrucciones para Claude

Proyecto del usuario de GitHub `fernferro`: **Escuadron2**, decompilación y **port nativo a PC**
de un juego de GameCube (disco de EE.UU., código `GSWE64`). En el repo no van nombres ni marcas
del juego original (ni en textos, ni en iconos, ni en nombres de archivos): se lo nombra por su
código `GSWE64` o como "el juego". Hablar en español rioplatense, explicar en términos simples
y no asumir experiencia en ensamblador.

## Lo que NO está en el repo (hay que pedirlo)

- **Archivos del juego**: `orig/GSWE64/sys/main.dol` (+ `boot.bin`, `bi2.bin`, `apploader.img`, `fst.bin`).
  SHA-1 de `main.dol`: `bc595e8b8c89ec8307ef61658b6cd5c854670d60`. Pedirle al usuario que los adjunte
  (los saca con Dolphin → Propiedades → Sistema de archivos → Extraer datos del sistema).
- **Compiladores CodeWarrior**: `files.decomp.dev` está bloqueado en el entorno de Claude. Pedirle al
  usuario el zip de su carpeta `build\compilers` y descomprimirlo en `build/mwcc/`.
  Configurar con `python configure.py --compilers build/mwcc`.
- Nunca subir al repo archivos del juego, compiladores ni nada de `build/` (`.gitignore` ya lo excluye).

## Puesta en marcha en una sesión nueva (decomp)

```sh
python tools/setup_rl.py orig/GSWE64/sys/main.dol   # genera orig/GSWE64/combined.dol
python configure.py --compilers build/mwcc
ninja                                               # debe terminar en "COINCIDE"
```

## Port a PC (lo principal ahora)

- Leer **`docs/port.md`**: recompilación estática PPC→C (`port/recomp/recomp.py`) + runtime que
  emula la GameCube (`port/runtime/`). Compilar con `make -C port -j4`; correr
  `./build/port/escuadron2 --frames 1500 --dump build/frames --every 100 --pad "650:A,1000:D,1060:D,1120:A"`.
- Necesita además los **datos del disco** en `build/disc/Rogue2/` (data.dat, data.dir, resource.inf,
  dg00020.vsp, lvlPack00..06.dir/.dat). Pedírselos al usuario si faltan (Dolphin → Extraer archivos).
- `docs/port.md` tiene la lista de lo que falta, en orden. Actualizarla al terminar cada parte.
- Las corridas largas pasan los 10 minutos de la herramienta: lanzarlas con `nohup ... &` y revisar.

## Cosas clave del proyecto (leer docs/ antes de tocar nada)

- `docs/motor.md`: arquitectura. `main.dol` es un cargador con `rogue2vm.dol` embebido; el juego
  corre en memoria virtual en `0x7FC00000` (secciones `.vmtext/.vmtext2/.vmdata/.vmbss`).
  Se analiza una imagen combinada (`tools/combine_vm.py`) y el build se reempaqueta y verifica
  por SHA-1 contra el original (`tools/pack_main.py`, `tools/check_build.py`).
- `docs/decompilar.md`: flujo para pasar funciones a C, con `tools/fcmp.py` para comparar,
  y trucos de CodeWarrior ya descubiertos. Compilador del juego: **GC/1.2.5**.
- `docs/clases.md`: 396 clases C++ reconstruidas desde las RTTI. `tools/analysis/` regenera nombres.
- Regla de oro: **el `CHECK` de `ninja` tiene que seguir dando `COINCIDE`** después de cada cambio.
  Los archivos en progreso van como `Object(NonMatching, ...)` y no se enlazan hasta coincidir al 100%.

## Flujo con el usuario

- Él trabaja en Windows (PowerShell) y actualiza con `git pull`.
- Hacer commits chicos con mensajes en español y subirlos a `main`.
- Prioridad hacia el port: sistemas que el juego necesita para arrancar (recursos `gsys*`,
  bucle principal, carga de niveles), antes que el matching perfecto.
