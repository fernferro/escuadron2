# Cómo se decompila una función

## Configuración

- Compilador del juego: **CodeWarrior GC 1.2.5** (1.0 y 1.1 dan el mismo código), con los flags
  base de `configure.py` (`-O4,p -inline auto ...`).
- Todo módulo del juego empieza con `#include "vm_sections.h"`, que pone el código en `.vmtext`
  (memoria virtual). Los datos chicos (`.sdata`/`.sbss`) quedan en el cargador, igual que en el original.
- Los textos que todavía están en archivos autogenerados se referencian como `extern char lbl_XXXXXXXX[]`.
  No se escriben como literales, porque si no aparecerían duplicados.

## Ciclo de trabajo

1. Elegir un rango de funciones contiguas y agregarlo como unidad en `config/GSWE64/splits.txt`:
   ```
   gsys/gsys_resource.c:
   	.vmtext     start:0x7FC25588 end:0x7FC25CB0
   ```
2. Agregar el objeto en `configure.py` (lib `game`) como `Object(NonMatching, "gsys/gsys_resource.c")`.
3. Escribir el C en `src/...` y comparar función por función:
   ```sh
   python tools/fcmp.py src/gsys/gsys_resource.c gsysGetResourceLength gsysFindNext
   ```
   Muestra el original y lo compilado lado a lado, marcando con `*` las instrucciones distintas.
4. Cuando **todas** las funciones del archivo dan `OK`, se cambia a `Object(Matching, ...)`.
   `ninja` lo enlaza y el `CHECK` confirma que `main.dol` sigue idéntico.
5. `ninja` también regenera el reporte de progreso, que incluye el porcentaje por función de los
   archivos en progreso (`build/GSWE64/report.json`).

## Trucos de CodeWarrior que ya aparecieron

- **Un argumento `s64` va en un par de registros alineado** (r5:r6), así que `r4` queda sin usar.
  Por eso la firma real de `gsysGetData` es `(res, s64 offset, s32 length, void* dest, callback)`.
- **El orden de declaración de las variables locales** cambia qué registro guardado recibe cada una.
- **Un puntero local al estado global** (`gsysState* st = &gsys;`) y punteros a campos
  (`void** lock = &st->lock;`) producen el patrón `addi rX, r31, off` que se ve en el original.
- **`static inline`** con un puntero a estructura reproduce bloques repetidos idénticos en varias funciones
  (la reserva de trabajos de la cola circular).

## Estado

| Archivo | Funciones | Coinciden 100% | Resto |
|---|---|---|---|
| `src/gsys/gsys_resource.c` | 8 | 3 | 96,9–99,8% (asignación de registros) |
| `src/game/game_main.c` | 17 | 0 | todo escrito en C: máquina de estados y bucles de misión, intro, post-misión, hangar y briefing (90–99%, solo registros) |
| `src/game/game_level.c` | 21 | **20** | `gameUpdateTimers` 95%: es lo único que impide enlazar el módulo |

## Literales de texto

CodeWarrior pone los textos de 8 bytes o menos en `.sdata` (se acceden con `@sda21`) y los más largos
en la `.data` del módulo, direccionados desde un registro base (`@stringBase`). Por eso en los módulos
en progreso se pueden escribir como literales normales: `fcmp.py` ignora las relocaciones.

## Límites de los archivos fuente originales

Cada archivo `.c` original tiene su propio bloque de textos en `.vmdata` (se accede con un registro
base) y su propio bloque de constantes en `.sdata2`. **El compilador rellena el final de cada bloque de
constantes con `'0000'` (`0x30303030`)**, así que esos rellenos marcan dónde termina un archivo. Si una
función usa una constante o un texto de otro bloque, pertenece a ese otro archivo.

Ejemplo: `game_level.c` va de `0x7FC0232C` a `0x7FC02EE8`, usa los textos `0x7FD9F2C8–0x7FD9F3C8`
y las constantes `0x80096A78–0x80096A84`. La pantalla de carga que sigue (`0x7FC02EE8`…) usa
constantes desde `0x80096A88`, así que es otro archivo.

`tools/analysis/tu_map.py` automatiza esto: parte `.sdata2` por los rellenos, mira qué bloque usa cada
función y agrupa las funciones contiguas. Hoy estima **164 archivos** para el código del juego y deja
el mapa en `build/tu_map.json` (inicio, fin y funciones de cada grupo). Es una estimación: las
funciones sin constantes se pegan al grupo vecino, así que conviene confirmar cada límite a mano
antes de agregar el archivo a `splits.txt`.

## Trucos nuevos

- Un `switch` con un solo `case` genera `beq` + `b` (en `gameSessionBegin`); un ternario no.
- `u64` en variables globales: CodeWarrior accede a las dos mitades por separado y usa
  `__shl2i` / `__shr2u` (`fn_8004FA4C` / `fn_8004FA70`) para los desplazamientos.
