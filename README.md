# Escuadron2

**Port nativo a PC (Windows y Linux), hecho por fans, de un juego de naves de GameCube del año 2001**
(disco de EE.UU., código **`GSWE64`**), más la decompilación que lo hizo posible.

> **Este repositorio no contiene ningún archivo del juego.** Para jugar necesitás tu propia copia
> del disco `GSWE64`, sacada de tu consola (`.iso` / `.gcm`, o los archivos extraídos con Dolphin).
> Este proyecto no está relacionado con los dueños del juego, de sus marcas ni de la consola, ni
> tiene su respaldo. Los nombres y marcas del juego original pertenecen a sus dueños.

## Jugar (Windows)

1. Bajá el zip del último [release](../../releases) y descomprimilo en una carpeta tuya
   (por ejemplo `C:\Escuadron2`; no en "Archivos de programa").
2. Abrí `Escuadron2.exe`, elegí la imagen de **tu** disco (`.iso` / `.gcm` sin comprimir) y tocá
   **Prepare game**. La primera vez el launcher traduce y compila el juego **en tu PC**, a partir
   de tu disco, con el compilador y el Python portables que vienen en `herramientas\`
   (unos minutos, una sola vez). Nada del juego viene en el zip.
3. Tocá **Play**.

## Cómo funciona

- **Recompilación estática:** `port/recomp/recomp.py` traduce el código PowerPC de **tu** `main.dol`
  a C al compilar. Ese código generado queda en `build/` y nunca se sube al repositorio.
- **Runtime:** `port/runtime/` hace de "GameCube": memoria, hilos e interrupciones del sistema
  operativo, lector de disco, tarjetas de memoria, controles (SDL2), sonido (DSP) y el chip
  gráfico traducido a OpenGL 3.3.
- **Extras del port:** resolución interna alta, pantalla panorámica 16:9, antialiasing, filtro
  anisotrópico, texturas HD (packs de Dolphin), menú en el juego (F1), launcher, instalador y
  shaders precompilados para que no haya tirones.

Los detalles técnicos están en [`docs/port.md`](docs/port.md) y la arquitectura del motor en
[`docs/motor.md`](docs/motor.md).

## Compilar

Requisitos: Python 3.10+, `make`, un compilador de C/C++ (gcc o clang) y SDL2. Para Windows se
compila desde Linux con [llvm-mingw](https://github.com/mstorsjo/llvm-mingw).

```sh
python tools/setup_rl.py RUTA/A/TU/sys/main.dol   # verifica el SHA-1 y prepara orig/GSWE64/
make -C port -j4                                  # traduce tu main.dol y compila build/port/escuadron2
make -C port launcher                             # el launcher (Escuadron2)
```

`main.dol` tiene que ser el del disco `GSWE64` (SHA-1 `bc595e8b8c89ec8307ef61658b6cd5c854670d60`).
Windows: ver `docs/port.md` (variables `TARGET=win`, `MINGW` y `SDL2_WIN`).

## Jugar

Abrí el launcher (`Escuadron2` / `Escuadron2.exe`), elegí tu imagen del disco y apretá Jugar.
O directamente: `escuadron2 --iso TU_DISCO.iso`.

- **F1:** menú de opciones dentro del juego.
- **F12:** captura de diagnóstico (`capturas/cuadro_N.png` + `.gxc`). Si algo se ve mal, abrí un
  issue con esa captura y el archivo `escuadron2_log.txt`.
- **Mayús + F12:** foto común de la pantalla.

## La decompilación

El repo también es un proyecto de decompilación basado en
[dtk-template](https://github.com/encounter/dtk-template) y
[decomp-toolkit](https://github.com/encounter/decomp-toolkit): `config/GSWE64/` tiene el mapa de
funciones y `src/` las funciones ya pasadas a C, que se compilan con el compilador original
(Metrowerks CodeWarrior GC/1.2.5) y tienen que dar exactamente el mismo binario.

```sh
python configure.py
ninja   # tiene que terminar en "COINCIDE"
```

## Licencias

- El código propio de este proyecto: CC0 1.0 (ver `LICENSE`).
- `port/dsp/` y `port/dsp_nativo/` vienen de [Dolphin](https://github.com/dolphin-emu/dolphin)
  (GPL-2.0 o posterior). Por eso **el ejecutable del juego, compilado, se distribuye bajo GPL-2.0
  o posterior.**
- `port/third_party/`: Dear ImGui (MIT), stb (dominio público / MIT), dr_libs (dominio público /
  MIT-0), zlib (zlib), xxHash (BSD-2). Las fuentes Poppins están bajo la SIL Open Font License.
