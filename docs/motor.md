# El motor del juego: mapa inicial

Lo que se sabe hasta ahora del ejecutable, obtenido del análisis automático.
Las direcciones son de la versión USA (`GSWE64`).

## 1. Arquitectura general

```
main.dol (cargador, memoria principal 0x80003100-0x8009C1E0)
├── Arranque CodeWarrior (__start, __init_registers, __init_data)
├── SDK de Nintendo: OS, EXI, SI, PAD, DVD, DSP, AR/ARQ (ARAM), GX, VI, THP (video)
├── MetroTRK (depurador), zlib 1.1.3 (inflate), runtime C++ (std::exception, RTTI)
├── GSYS VM: memoria virtual paginada desde la ARAM
│     gsysVMInit 0x80009214 · gsysVMLoadImage 0x800090D8
│     gsysVMDSIHandler 0x80008554 / gsysVMISIHandler 0x80008720 (fallos de página)
└── rogue2vm.dol embebido (0x80100000) ──► se copia a la ARAM al arrancar

Código del juego (VM, 0x7FC00000-0x7FE1EBA0, se pagina a demanda)
├── .vmtext  0x7FC00000  ~1,5 MB  motor + lógica (5.919 funciones)
├── .vmtext2 0x7FD88000  ~90 KB   (413 funciones)
├── .vmdata  0x7FD9F000  ~290 KB  vtables, RTTI, textos, tablas
└── .vmbss   0x7FDE8000  ~220 KB
```

La memoria virtual funciona con la MMU de la GameCube. Cuando el código salta a una dirección
`0x7F...` que no está cargada, el procesador genera un fallo de página (DSI/ISI). El manejador
del cargador trae esa página desde la ARAM (los 16 MB de RAM de audio) y reintenta la instrucción.
Así el juego consiguió ejecutar mucho más código del que entraba en los 24 MB de memoria principal.

**Para el port a PC:** todo esto desaparece. En PC hay memoria de sobra, así que el código del VM
se carga en memoria normal y los manejadores de fallos no hacen falta.

## 2. Sistemas identificados

| Sistema | Funciones con nombre | Notas |
|---|---|---|
| Recursos (`gsys*`) | `gsysRegisterResource`, `gsysLoadResource`, `gsysGetData`, `gsysUnloadResource`, `gsysGetResourceLength`, `_gsysLoadResource`, `_gsysGetData`, `__gsysFindResource` | Archivos empaquetados del disco (`resource.inf`, `.dat`, `.dir`) con carga asíncrona por "jobs". Es la primera pieza a portar. |
| Paquetes estáticos | `opkgRegisterStaticPackage`, `opkgDuplicateStaticPackage` | Registro de datos de objetos por nombre. |
| Objetos del mundo | `WObject` + ~150 clases `OBJ*` | Clase base de todo lo que existe en un nivel. 19 métodos virtuales (`WObject_vfunc00..18`). |
| IA | `AI`, `AiGenericMovingObject`, `AiGenericMovingFiringObject…`, `AIMOVE` | Naves enemigas, tropas, torretas. |
| Naves del jugador | `PLAYERcraft`, `PLAYERcockpit`, `PLAYERWeapons`, `PLAYERconfigEntry*` | todas las naves pilotables |
| Física | `PHYSICSforce*`, `PHYSICScollisionObject`, `PHYSICSparticleSystem`, `PHYSICScloth` | |
| Efectos | `FXCraftGlow*`, `FXCraftPlasma*`, `PARTICLEemitter`, `SMOKEforces` | |
| Audio | `AUD::*` (motores, ambiente 3D, torretas) | Sobre MusyX . |
| Hangar y menús | `OBJhangar*`, `OBJfrontend*`, `OBJcraftSelect`, `OBJcredits` | |
| Cinemáticas | `OBJcutscene*`, `CUTsoundObject`, `MOV*` (video `.movie`) | |

El árbol completo de clases está en [`clases.md`](clases.md).

## 3. Flujo principal del juego (`src/game/game_main.c`)

`gameMain` (`0x7FC00000`) es el punto de entrada del código del juego. Hace, en orden:

1. Pone a cero el estado global `gGame` (`0x7FDE8000`, 0x294 bytes).
2. Inicializa video, el sistema de recursos (`gsysInit`, raíz `"main"`), cámara/proyección
   (FOV 1.5, aspecto 0.75, planos 400 / 3000 / 20000), efectos, tarjeta de memoria, audio, texto
   (`text/English`) y la pantalla legal.
3. Registra los recursos `"main"` y `"l00"`…`"l06"` (uno por grupo de niveles).
4. Precarga datos fijos: `movieInfo`, `staticGame/us`, `pauseMenu`.
5. Imprime `"resident setup done %d bytes free"` y entra al bucle de estados:

| Estado | Función | Qué hace | Si la función deja el estado en 0 |
|---|---|---|---|
| 5 | `gameStateIntro` | intro al encender | pasa a 6 |
| 6, 7 | `gameStateFrontend` | menús principales | — |
| 1 | `gameStateHangar` | hangar: elección de nave | pasa a 2 |
| 2 | `gameStateBriefing` | briefing de la misión | pasa a 6 |
| 3 | `gameStateLevel` | `levelLoad` → juego → `levelUnload` | pasa a 1 |
| 4 | — | espera | — |

En cada vuelta se llama a `gameCheckMemoryLeaks`, que compara la memoria libre con la del arranque.

**Para el port:** este es el `main()` del juego en PC. Los subsistemas que inicializa son la lista
de cosas que la capa de compatibilidad tiene que proveer (video, archivos, audio, entrada, texto).

## 4. Qué tiene nombre y qué no

- 396 descriptores RTTI (`__RTTI__…`) y 368 vtables (`__vt__…`): nombres reales de CodeWarrior.
- 1.504 métodos virtuales: `Clase_vfuncNN`. La clase es real; `NN` es la posición en la vtable.
  El nombre real del método todavía no se conoce. Al decompilar cada uno se renombra.
- 15 funciones con nombre tomado de sus propios mensajes de error, más 8 del cargador.
- SDK de Nintendo: 241 funciones reconocidas por firma.
- El resto, unas 5.900 funciones, sigue como `fn_XXXXXXXX`.

Las herramientas que generan esto están en `tools/analysis/` y se pueden volver a correr:

```sh
python tools/analysis/xref.py      build/GSWE64/asm orig/GSWE64/combined.dol config/GSWE64/symbols.txt build/xref.json
python tools/analysis/classes.py   orig/GSWE64/combined.dol config/GSWE64/symbols.txt build/GSWE64/asm build/classes.json
python tools/analysis/apply_names.py build/classes.json build/xref.json config/GSWE64/symbols.txt
python tools/analysis/class_tree.py  build/classes.json docs/clases.md
```

`apply_names.py` solo renombra símbolos automáticos (`fn_`/`lbl_`). Nunca pisa un nombre puesto a mano.

## 5. Camino hacia el port a PC

1. **Hecho:** reconstrucción exacta de `main.dol`, que verifica cada cambio.
2. **Hecho (primera pasada):** mapa de clases y sistemas.
3. **Decompilar el SDK del cargador.** Es código de Nintendo conocido y hay mucho escrito en otros
   proyectos. Es lo más rápido para subir el porcentaje, aunque para el port no se usa: se reemplaza.
4. **Decompilar el núcleo del juego** en este orden: recursos (`gsys*`), bucle principal y carga
   de niveles, `WObject`, cámara y entrada, después el resto de los objetos.
5. **Capa de compatibilidad para PC.** Hay que reemplazar:
   - GX → OpenGL/Vulkan. Es la parte más difícil, porque el juego exprime el TEV. Se puede
     reutilizar Aurora, la capa de GX que hizo el proyecto de Metroid Prime.
   - MusyX/DSP/AX → mezclador por software.
   - PAD → SDL, DVD → archivos extraídos, ARAM/VM → memoria normal.
   - Video `.movie`/THP → decodificador en software.
6. **Compilar para PC** el código decompilado (no *matching*) contra esa capa.

Los pasos 4 a 6 se pueden hacer de a poco: un port no necesita que todo esté decompilado *matching*,
pero sí que todo esté en C/C++ compilable.
