# Port a PC (recompilación estática)

Este documento explica cómo está armado el port nativo, cómo compilarlo y correrlo,
y qué falta. Está pensado para que cualquiera (o una sesión nueva de Claude) pueda
retomar el trabajo sin adivinar.

## Idea general

En vez de esperar a que todo el juego esté decompilado a C "lindo", el port usa
**recompilación estática**: un script (`port/recomp/recomp.py`) traduce *cada función*
PowerPC del ejecutable a una función de C equivalente, instrucción por instrucción.
Ese C se compila para PC y se enlaza con un **runtime** (`port/runtime/`) que emula lo
que el juego espera de la GameCube: memoria, hilos del SO, interrupciones, disco,
control, audio (DSP) y gráficos (GX).

A medida que el decomp avance, se pueden reemplazar funciones recompiladas por las
versiones en C del decomp (o por implementaciones nativas, "HLE").

```
main.dol + rogue2vm.dol ──recomp.py──► build/port/gen/*.c ──gcc──► build/port/escuadron2
                                              ▲
                     port/runtime/*.c ────────┘ (memoria, SO, hardware, GX, video)
```

## Requisitos (fuera del repo)

- `orig/GSWE64/sys/*` (main.dol, boot.bin, bi2.bin, apploader.img, fst.bin) y
  `python tools/setup_rl.py orig/GSWE64/sys/main.dol` → `orig/GSWE64/combined.dol`.
- Los datos del disco en `build/disc/` con la estructura del disco:
  `build/disc/Rogue2/{data.dat,data.dir,resource.inf,dg00020.vsp,lvlPack00..06.dir,lvlPack00..06.dat}`
  (se sacan con Dolphin → Propiedades → Sistema de archivos → clic derecho en la
  raíz → Extraer archivos). Sin `lvlPack0X.dat` el juego se cae al cargar niveles.
- Linux con gcc, make, python3, libpng (`apt install libpng-dev`).

## Compilar y correr

Linux (con SDL2 para la ventana; sin SDL2 queda solo el rasterizador por software):

```sh
make -C port -j4                       # o SDL2_DIR=/ruta/a/sdl2 si no esta instalado
./build/port/test_recomp               # pruebas del recompilador (6/6)
./build/port/escuadron2 --game CARPETA     # CARPETA/sys/main.dol y CARPETA/Rogue2/...
```

Windows (compilacion cruzada desde Linux, como se hizo en la sesion de Claude):

```sh
# llvm-mingw (github.com/mstorsjo/llvm-mingw) y SDL2-devel-2.30.x-mingw (github.com/libsdl-org/SDL)
make -C port -j4 TARGET=win MINGW=/ruta/llvm-mingw-...-x86_64 SDL2_WIN=/ruta/SDL2-2.30.x/x86_64-w64-mingw32
llvm-strip -o escuadron2.exe build/port-win/escuadron2.exe
```

El .exe sale estatico (solo usa DLL del sistema). Contiene codigo del juego recompilado:
**no se sube al repo** ni se publica.

Pruebas sin ventana (para Claude): `--headless --frames N --dump build/frames --every M`.
`--frames`, `--every` y los guiones `--pad` cuentan **cuadros del juego** (cada cambio de XFB),
no retrazados, asi no dependen de la velocidad de la maquina.
Ejemplo para pasar el menu de la tarjeta de memoria y ver la intro:
`--pad "430:A,600:D,640:D,680:A"` (los numeros pueden variar un poco).

Opciones de `escuadron2` (ver `port/runtime/main.c`):

| opcion | que hace |
|---|---|
| `--game DIR` | carpeta del juego (`DIR/sys/main.dol`, `DIR/Rogue2/...`). Sin esto se usa la carpeta actual si tiene `sys/main.dol`, o `orig/GSWE64` + `build/disc` |
| `--sw` / `--gl` | rasterizador por software / OpenGL (por defecto OpenGL si se compilo con SDL2) |
| `--escala N` | resolucion interna de OpenGL: el EFB de 640x528 por N (por defecto 2 en la ventana, 1 sin ventana) |
| `--sin-vibracion` | no hacer vibrar el joystick (por defecto vibra cuando el juego lo pide) |
| `--pantalla-completa` | arranca en pantalla completa (F11 la alterna) |
| `--panoramico` | pantalla 16:9: el 3D se ve mas ancho sin deformarse (el HUD queda estirado); F10 lo prende y apaga |
| `--headless`, `--vsync` | ventana oculta (pruebas) / sincronizar con el monitor |
| `--frames N` | sale despues de N cuadros del juego |
| `--dump DIR --every N` | guarda la imagen cada N cuadros del juego |
| `--dump-efb`, `--dump-copies N` | ademas guarda el EFB / el EFB en cada N-esima copia (solo software) |
| `--pad GUION` | botones automaticos: `cuadro:BOTONES,...` (A B X Y START U D L R) |
| `--gx-sync` | decodificar GX en el hilo del juego (depuracion, solo software) |
| `--trace-hw`, `--trace-gx`, `--trace-dsp`, `--trace-threads`, `--trace-copies`, `--trace-interp` | registros |
| `--dump-fifo ARCH` | vuelca el flujo de comandos GX (`port/tools/fifo_dump.py`) |
| `--wav ARCH` | graba el audio que sale por el DMA de audio (pruebas sin parlantes) |
| `--sin-audio` | no abre la salida de audio |
| `--memcard ARCH`, `--sin-tarjeta`, `--trace-memcard` | tarjeta de memoria (por defecto `CARPETA/memcard_A.raw`, formato crudo de Dolphin) / sin tarjeta / registro del EXI |
| `--dsp-hle` | usa el DSP falso viejo (sin audio) en vez del DSP de bajo nivel |
| `--dsp-dolphin` | usa el DSP de Dolphin (JIT x86-64) en vez del DSP nativo |
| `--dsp-interprete` | DSP de Dolphin con su interprete (mas lento) |
| `--dsp-verificar` | corre el DSP nativo y el de Dolphin a la par, instruccion por instruccion, y anota la primera diferencia |

**Imagen del disco**: `escuadron2 --iso JUEGO.iso` (o `--game JUEGO.iso`; tambien `.gcm`) lee todo
directo de la imagen (el `main.dol` sale de ahi). La tarjeta de memoria queda en la carpeta
actual.

Las opciones tambien se pueden poner en un archivo `escuadron2.ini` en la carpeta del juego (o al
lado del programa), una o varias por renglon, con `#` para comentarios; por ejemplo:

```
--pantalla-completa
--panoramico
--escala 3
```

Variables de entorno de depuracion: `RL_NIVEL=N` (la primera mision que se elige en el menu
se cambia por el nivel N: 1, 2, 3, ...; 20 es el hangar de los
menus y 22 la presentacion; el registro anota `[nivel] cargando nivel N`),
`RL_CAPTURAR_CADA=K RL_CAPTURAR_N=M` (con `--capturar N`: M capturas cada K cuadros),
`RL_PROBE_PX=x,y` (con `RL_NO_BATCH=1`: anota cada dibujo que cambia el color o la Z de ese pixel del EFB),
`RL_TRACE_ALLV=1` (con `RL_DRAW_TRACE` y `RL_GL_XF=0`: todos los vertices del dibujo),
`RL_DRAW_SKIP=A-B` (al reproducir, saltea esos dibujos: sirve para buscar que dibujo pinta un pixel),
`RL_DRAW_TRACE=N` (registros de BP/XF, texturas y vertices del dibujo N),
`RL_DUMP_COPIES=1` (guarda en build/ cada copia del EFB a textura, color arriba y alfa abajo),
`RL_GL_NULL=1` (no dibuja nada en OpenGL),
`RL_TRACE_VOICES=1` (cambios de estado de las voces y streams de MusyX),
`RL_NO_LOOP_POLL=1` (no entregar interrupciones en los bucles), `RL_DRAW_LIMIT`, `RL_DRAW_TRACE`.

Controles: Enter=START, Espacio=A, Shift=B, U=X, I=Y, Q=L, E=R, Z=Z, WASD=palanca,
flechas=cruz, teclado numerico=palanca C, F10=16:9 / 4:3, F11=pantalla completa,
F12=captura del cuadro de GX (`capturas/cuadro_N.png` + `.gxc`, para `--reproducir`); Mayus+F12=foto comun (`Imagenes\Escuadron2\Escuadron2_FECHA.png`, sin menu ni contador; `RL_FOTO_CUADRO=N` la saca sola); joysticks via SDL.

Depurar: `sh port/run_gdb.sh [opciones]` corre bajo gdb y muestra la pila en un error
fatal. `sh port/run_debug.sh` corre N segundos y saca la pila de todos los hilos.

## Launcher e instalador

- `make -C port launcher` arma `Escuadron2(.exe)` (`port/launcher/launcher.cpp`, Dear ImGui en
  `port/third_party/imgui` + SDL2 + OpenGL 3, fuente Poppins incrustada con `port/tools/incrustar.py`).
  Pestanas Jugar / Controles / Tarjetas de memoria / Ajustes / Registros. Guarda todo en
  `%APPDATA%\Escuadron2` (Linux: `~/.local/share/Escuadron2`; para pruebas `RL_LAUNCHER_DATOS`)
  y arranca `escuadron2` con esa carpeta como carpeta actual: `--iso`, `--memcard tarjetas/X.raw`,
  `--pantalla-completa`, `--panoramico`, `--escala N`, `--vsync`, `--sin-vibracion` + argumentos extra.
  `controles.ini` tiene el mismo formato que lee `port/runtime/controles.c`.
- `make -C port instalador` arma `Instalar-Escuadron2(.exe)`: el programa
  (`port/launcher/instalador.cpp`, con zlib de `port/third_party/zlib` para descomprimir) mas los
  archivos pegados al final por `port/tools/empaquetar_instalador.py` (juego sin depuracion, launcher,
  LEEME y licencias; **nunca** archivos del juego). Instala solo para el usuario
  (`%LOCALAPPDATA%\Programs\Escuadron2`) o para todos (Archivos de programa, se relanza con
  permiso de administrador), crea accesos directos y la entrada de "Agregar o quitar programas", y
  deja `Desinstalar.exe` (el mismo programa sin los archivos; se copia a TEMP para poder borrar la carpeta).
- Arte opcional (fuera del repo, en `build/arte/` para el instalador o en `arte/` de los datos o del
  programa): `fondo*.jpg` (se turnan en Jugar), `portada`, `control`, `disco` (.jpg/.png) e
  `instalador.jpg` (fondo del instalador). Sin arte se ve el fondo de estrellas.
- Pruebas sin ventana: `RL_LAUNCHER_CAPTURAS=DIR` / `RL_INSTALADOR_CAPTURAS=DIR` guardan una imagen
  de cada pestana / paso; `RL_INSTALADOR_AUTO=DIR` instala ahi sin preguntar.
- `escuadron2.exe` lleva el interprete del DSP de Dolphin (GPL-2.0+): el instalador incluye `port/dsp/COPYING`.

## Opciones: launcher y menu dentro del juego (noviembre 2026)

- Las opciones de video, audio y juego viven en **`ajustes.ini`** (clave = valor). Las escriben
  el launcher (pestana *Opciones*) y el menu del juego (**F1** o el boton Back/Share del joystick);
  las dos pantallas son el mismo codigo: `port/launcher/ajustes.h` (titulos amarillos, panel con
  borde dorado, lista a la izquierda y explicacion a la derecha).
- El juego lo lee en `main.c` (`ajustes_leer`) de la carpeta actual o de al lado del programa,
  antes que `escuadron2.ini` y la linea de comandos (que mandan). El launcher ya no pasa opciones de
  video por argumentos: arranca el juego en su carpeta de datos, donde esta `ajustes.ini`.
- El menu (`port/runtime/menu.cpp`, Dear ImGui) se dibuja en `gl_present` encima de la imagen;
  con el menu abierto el juego recibe el control suelto. Se aplican en vivo: pantalla completa,
  formato, sincronia vertical, escalado de salida, contador de FPS, aviso de shaders, volumen,
  camara de la bomba y vibracion. Resolucion, antialiasing (MSAA, `--msaa N`: EFB multimuestra
  que se resuelve antes de cada copia, sin el recorte del juego: glBlitFramebuffer respeta el scissor y la computadora de punteria quedaba resuelta solo en el cuadrado de la copia anterior), anisotropico, texturas HD y "shaders al arrancar" valen
  al volver a abrir el juego (un punto naranja lo avisa).
- Nada de esto corre con `--headless`: las pruebas (`comparar.sh`) siguen dando igual.
- Pruebas del menu: `RL_MENU_ABRIR=N RL_MENU_PESTANA=P RL_MENU_CAPTURA=x.ppm` con
  `SDL_VIDEODRIVER=offscreen` (sin `--headless`).
- Pestana **TRUCOS**: los passcodes del juego como interruptores (`port/runtime/trucos.h`).
  El juego (`fn_7FD6D338`) pasa el codigo 1000 veces por SHA-1 (`fn_7FD7F244`), lo busca en una
  tabla de 71 resultados (`0x7FDE2350`) y prende bits en dos palabras de la partida:
  `0x7FE1CAF4` (+0x654 de `0x7FE1C4A0`) y `0x7FE1CAF8` (+0x658). Los de dos partes son una clave
  (indices 8..0x25) seguida de la palabra (indice = clave + 0x1E). El runtime mantiene prendidos
  en cada cuadro (video.c) los bits de los trucos elegidos; apagado no toca nada. Probado: blanco y
  negro y "Music Hall" en Special Features. Hay 13 indices de la tabla sin codigo conocido.
- `RL_VOLCAR_RAM=prefijo:N1,N2` guarda toda la memoria en esos cuadros (para buscar variables
  comparando volcados); asi se encontro el texto del passcode en el objeto de la pantalla.

### Panoramico 16:9 y el recorte del juego (noviembre 2026)

Antes solo se achicaba la proyeccion (x 0.75) y el juego seguia recortando el mundo como en 4:3:
lo que entraba por los costados aparecia tarde. Ahora (`video.c`, cada cuadro) las 4 camaras del
objeto apuntado por `0x80095DC8` (en +0x0, +0x384, +0x704, +0xA84) pasan su relacion de aspecto
(+0x1D0) a 16:9, y tambien la tangente horizontal (+0x188) y la escala x (+0x148) si estan
derivadas de ella. Las proyecciones que ya vienen en 16:9 no se achican (`gl_ws_aplastar`).
La clave era **+0x274**: es la relacion de aspecto de la que sale todo. `fn_7FC14CEC` (llamada
desde `fn_7FC14C8C` / `fn_7FC153C0` con +0x274 y el angulo +0x270) la copia a +0x1D0 y arma la
piramide de vision con la que el juego recorta el escenario. Con +0x274 en 16:9 el agua y el
terreno llegan hasta los bordes. (Cambiar todos los 1.3333 del objeto colgaba el juego: solo esos
campos.) `RL_WS_VIEJO=1`: como antes.

El HUD tiene partes en 3D (el globo del radar, la nave del indicador de vida, el blanco) dibujadas
con su propia perspectiva **descentrada** (p1 != 0) en una esquina. Esas no se achican: se estiran
como el HUD plano (sus marcos), si no quedaban corridas hacia el centro. `RL_TRAZA_PROY=1` anota
cada proyeccion distinta (util con `--reproducir` de una captura).

`RL_VIGILAR=DIR,LARGO[,r]` (+ `RL_VIGILAR_DESDE=cuadro`, solo Linux) anota que codigo del juego
escribe (o lee, con `r`) esa memoria: protege la pagina y anota cada instruccion que la toca
(`[vigilar] +desplazamiento`; con `nm -n build/port/escuadron2` se ve la funcion `f_...`). Asi se
encontro `fn_7FC14CEC`.

### Shaders al arrancar (por defecto)

`gl_preparar_shaders = 1` (opcion `preparar_todo` en ajustes.ini): se compilan y se calientan
**todos** los programas conocidos antes de jugar, no solo los usados antes.

### Preparacion de shaders la primera vez (noviembre 2026)

En una GTX 1050 Ti la preparacion tardaba 157 s: los hilos compilaban y enlazaban, pero NVIDIA
hace el trabajo de verdad recien al usar el programa o pedir su binario, y eso quedaba en el hilo
principal (~470 ms cada uno). Ahora cada hilo, despues de enlazar, configura el programa y saca
el binario (`prep_terminar`); el principal solo lo guarda. El registro dice "compilados en" y
"guardados en la cache en" para ver donde se va el tiempo.

## Instalador con texturas HD y musica (noviembre 2026)

El instalador trae ahora partes opcionales que se reconocen por la carpeta: `texturas_hd/...`
(texturas en alta definicion) y `arte/musica.*` (musica del launcher). En Opciones aparecen como
interruptores ("Extras") con su tamano; lo que no se elige no se instala. Las texturas quedan en
`<instalacion>/texturas_hd` (el juego las busca tambien al lado del programa) y la musica en
`<instalacion>/arte`. Posiciones de 64 bits (con texturas pasa de 2 GB). Lo ya comprimido
(png, jpg, flac, ogg, mp3) se guarda tal cual (`comp == tam`); el resto con zlib.

Como las texturas y la musica nunca van al repo, cada uno arma su instalador completo en su PC:
`Instalar-Escuadron2.exe --agregar SALIDA.exe destino=origen ...` (origen puede ser una carpeta).
`port/launcher/armar_instalador_completo.bat` lo hace solo con `texturas_hd\` y `musica.*`
puestos al lado del instalador.

## Velocidad del hilo grafico en el nivel 3 (octubre 2026)

En el nivel 3 el juego manda ~150 mil vertices y ~12.800 dibujos por cuadro; el hilo grafico tardaba
16 ms por cuadro (el limite) y el juego esperaba el fin de dibujo un 30% del tiempo.
`vtx_fetch_n` (vtx.c) arma los vertices de todo un dibujo por columnas, con formato y modo
ya elegidos y acceso directo a los arreglos: 45% menos instrucciones que vertice por vertice,
igual byte a byte en las 38 capturas del usuario (`RL_VTX_VIEJO=1` usa el camino anterior).
La sincronizacion vertical agrega espera en ese mismo hilo: el launcher la deja apagada.

## Tirones por shaders: calentar programas (octubre 2026)

Con NVIDIA cada programa de OpenGL traba 50-220 ms **la primera vez que se dibuja** con el, en
cada corrida, aunque venga de la cache binaria o se haya preparado antes (registro `[lento]
dibujo ... primera vez`). Despues no vuelve a trabar. Solucion (`render_gl.c`, `calentar_*`):

- Al arrancar se "calientan" (dibujo de prueba de un triangulo degenerado en un FBO de 4x4) los
  programas que el juego uso en partidas anteriores (`escuadron2_shaders.usados`, un hash de 32 bits
  por programa); sin ese archivo, todos los listos. Hasta `RL_CALENTAR_MAX_S` segundos (12), en
  varios hilos con contextos compartidos (`RL_CALENTAR_HILOS`, 0 = en el principal), esperando
  cada dibujo con `glFinish` (sin eso NVIDIA lo deja para despues: la primera imagen tardo 40 s).
- En el juego, un programa listo pero sin calentar se dibuja con el ubershader (mismo resultado)
  y se anota; los anotados se calientan en las pantallas de carga (60 ms por imagen).
- El registro marca `YA CALENTADO` si un programa calentado igual trabo en su primer dibujo real.
- `RL_SIN_CALENTAR=1` apaga todo esto. Con `--headless` esta apagado (las pruebas no cambian).

## Lectura de vertices en otros hilos (octubre 2026)

En el nivel 6 y el nivel 7 el juego espera al hilo grafico (~26 mil dibujos por cuadro, 13-24 ms).
Casi la mitad de ese trabajo era `vtx_fetch_n`. Ahora `gl_render_draw` lo encola (`vj_encolar`,
con una copia de la parte de `VtxSetup` que usa) y lo hacen hasta 3 hilos mas; el grafico sigue
leyendo comandos. Se espera (`gl_vtx_esperar`) antes de mandar un lote, de agrandar `s_vbuf` y
despues de cada `parse` (los datos apuntan a `s_fifo`, que se mueve). La pendiente de Z
(zfreeze) se saca leyendo aparte los 3 ultimos vertices. `RL_VTX_HILOS=N` (0 = como antes).
Las 38 capturas dan identico con `RL_VTX_HILOS=1`. Esta maquina tiene 2 nucleos y no sirve para
medir la ganancia: hay que verla en el registro del usuario (`grafico` en `[velocidad]`).

## Lote diferido en el hilo grafico (noviembre 2026)

En el nivel 6 (AT-AT en el agua: la escena se dibuja dos veces por el reflejo, ~36 mil dibujos y
~480 mil vertices por cuadro) el juego termina de mandar a los ~12 ms y el hilo grafico ~9 ms
despues. Ese hilo esperaba a los hilos de vertices en cada cambio de material (~300 veces por
cuadro) y en cada pedazo de comandos que leia. Ahora:
- el lote terminado queda "diferido" y se manda al cerrar el siguiente (`dif_cerrar`,
  `dif_mandar`); el estado de OpenGL (`apply_key`) se pone al mandarlo. `flush_batch` manda los
  dos en orden, asi que copias, texturas y mostrar no cambian. `RL_DIFERIR=0`: como antes.
- cada trabajo de vertices lleva su destino y el contador de su lote (`vj_esperar_cnt`).
- los vertices que venian en la cola de comandos se copian aparte (`s_vj_arena`), asi leer el
  pedazo siguiente no espera.
- fin de dibujo y tokens del PE esperan a todos los vertices antes de avisarle al juego.
- `RL_REPLAY_LOOPS=N` con `--reproducir` mide ms por cuadro (con `RL_GL_NULL=1 RL_ZSLOPE_NO=1`)
  y cuenta las esperas. En la VM de 2 nucleos no se ve la ganancia: medir con registros reales.
- No funciono: saltear imagenes cuando mostrar tarda (se activaba en escenas pesadas y se veia
  a 30 con el titulo diciendo 60). Lo de 15 FPS en el menu era la notebook a bateria (NVIDIA
  limita a 30).

## La "imagen encimada" en la cabina del nivel 6 (noviembre 2026)

No es un error del port: es la camara de la bomba del juego. Al tirar una bomba o torpedo en
vista de cabina, el disparo (`fn_7FC3A7A0`) prende la computadora de punteria en un modo
especial (`fn_7FCD812C(1)`, bandera `0x7FDF7D5D`); `fn_7FCD771C` dibuja la escena a 320x240 con
una camara que sigue al proyectil (con la bomba, mirando para abajo), la copia (RGB565 a
`0x130F240`) y se pega sobre toda la ventana de la cabina donde el rojo es mayor que 0 (tabla
de intercambio 2: alfa = rojo). Por eso el Star Destroyer hundido aparece "en otra posicion".
- Desde noviembre 2026 esa camara viene apagada: `hle_miraBomba` (envuelve `fn_7FCD812C`) no
  la prende cuando el disparo la pide (r3 = 1). `--camara-bomba` / "Camara de la bomba en la
  cabina" en Ajustes del launcher deja el comportamiento original. La computadora de punteria
  del boton Y llama con r3 = 0 y no cambia.
- El rectangulo amarillo que se veia al reproducir capturas era de la reproduccion: las copias
  del EFB restauradas vienen a la escala del usuario; reproducir con `--escala 2`.
- Herramientas: `RL_TRAZA_VIEWPORT=cuadro` (quien pone un viewport que no es la pantalla),
  `RL_TRAZA_MIRA=1` (la camara de `fn_7FCD771C`), `RL_TEX_BUSCAR=direccion` (que dibujos usan
  una textura), `RL_DUMP_COPIES=1` (ademas el EFB entero en cada copia y las lecturas con paleta).

## Texturas en alta definicion (paquetes de Dolphin)

`port/runtime/texturas_hd.c`: al arrancar busca `tex1_*.png/.dds` en `texturas_hd/` (o
`RL_TEXTURAS_HD`, o `Load/Textures/GSWE64`) y sus subcarpetas. El nombre de cada textura se
calcula igual que Dolphin (`TextureInfo::CalculateTextureName`: XXH64 del nivel 0 en la RAM,
XXH64 del rango de la paleta usado, `_m` si el modo tiene mipmaps, comodines `_$` y `$`).
Las imagenes se leen en dos hilos aparte la primera vez que se usan (mientras tanto se ve la
original) y se suben una sola vez a la placa (PNG con mipmaps generados, DDS BC1/2/3/7 con los suyos).
`--sin-texturas-hd` las apaga. `RL_VOLCAR_TEXTURAS=1` guarda cada textura en `texturas_volcadas/`
con ese nombre (para hacer paquetes o comprobar nombres). Probado con un paquete hecho con
texturas volcadas y tenidas: se reemplazan bien. Falta probar con un paquete real.

## Piezas del runtime

| archivo | qué emula |
|---|---|
| `ppc.h` | contexto de la CPU (registros), acceso a memoria big-endian, ayudas para CR/FPU/paired singles |
| `memory.c` | RAM plana 0x7F000000..0x81800000 (incluye la memoria virtual del juego en 0x7FC00000), espejos, registros de hardware, locked cache, cuantización de psq_l/psq_st |
| `runtime.c` | llamadas indirectas (tabla ordenada de funciones), errores, timebase |
| `interp.c` | **intérprete PPC** para código que el juego copia a la RAM en tiempo de ejecución (rutinas de MusyX en los niveles); misma semántica que el recompilador |
| `loader.c` | carga del DOL |
| `os.c` | HLE de interrupciones e hilos: cada OSThread corre en un pthread, con un único "testigo" (mutex) para que solo uno avance a la vez; solo se reemplaza SelectThread, el resto del SO es el original |
| `hw.c` | VI (retrazado 60 Hz), PI (FIFO), DI (lectura de disco), SI (controles), EXI (tarjeta de memoria, reloj y SRAM), DSP/ARAM/AI, CP/PE |
| `memcard.c` | tarjeta de memoria en la ranura A, guardada en un archivo |
| `dsp_hle.c` | DSP falso viejo (solo con `--dsp-hle`) |
| `../dsp_nativo/` | **DSP nativo** (el que se usa): los programas del DSP (el sonido de MusyX y el de arranque del SDK) se traducen a C al compilar, como el codigo PowerPC (`port/recomp/dsprecomp.py`, genera `build/port/gen/dsp_programas.c` desde `combined.dol` y la ROM libre del DSP). `dsp_core.h`/`dsp_ops.h`: registros y semantica de cada instruccion (convertida a C desde el interprete de Dolphin); `dsp_nativo.c`: buzones, DMA, acelerador ADPCM e hilo. Se verifico contra el DSP de Dolphin con `--dsp-verificar` (miles de millones de instrucciones iguales). Las banderas de comparacion que nadie lee no se calculan |
| `../dsp/` | DSP de Dolphin (solo con `--dsp-dolphin`/`--dsp-verificar`): el JIT x86-64 (o el intérprete) del DSP de Dolphin (`Core/DSP`, GPL-2.0-or-later, con cabeceras mínimas en `dsp/shim/`) corre el microcódigo real de MusyX en su propio hilo, limitado a 81 MHz. `dsp_lle.cpp` lo conecta con el runtime (buzones, DSPCR, DMA, ARAM). Usa las ROM libres del DSP hechas por Dolphin (`dsp/rom/`, embebidas en `dsp_rom_data.c`) |
| `audio.c` | salida de audio por SDL (32 kHz estéreo); el ritmo del DMA de audio se ajusta para mantener la cola cerca de 80 ms |
| `disc.c` | disco virtual armado desde `sys/` + la FST + los archivos sueltos |
| `gx.c/gx.h` | decodificador del flujo de comandos GX (CP/XF/BP, listas de display, dibujos); corre en su propio **hilo grafico** con una cola circular |
| `render_gl.c` | **backend de OpenGL 3.3**: TEV en un ubershader, cache de texturas, copias del EFB como texturas de OpenGL, XFB en la ventana (SDL2), teclado y joystick |
| `render.c` | **rasterizador por software**: vértices, matrices, iluminación, texgen, todos los formatos de textura, TEV completo, alpha test, Z, blend, recorte contra el plano cercano, líneas y puntos, copias EFB→XFB y EFB→textura |
| `video.c` | conversión YUYV→RGB y PNG |
| `input.c` | control en el puerto 1 (por ahora solo el guion `--pad`) |

`port/hle.txt` lista las funciones del juego que se reemplazan por versiones del runtime.

### Notas del audio (MusyX)

- MusyX mide cuánto tardó en armar la lista de comandos desde la interrupción del DMA de
  audio (`fn_80026D28`) y lo descuenta del tiempo de DSP disponible. Si se pasa, **corta todas
  las voces que quedan**, incluidos los streams de voz; el juego espera a que esos streams
  terminen al descargar el nivel y se colgaba antes de la misión. En el port esa función
  devuelve 0 (`port/hle.txt`).
- Las interrupciones se entregan también en los saltos hacia atrás (bucles) cada 2048
  vueltas (`RT_LOOP_POLL` en el código generado y en el intérprete), como en el hardware:
  hay esperas activas (por ejemplo, la inicialización del DSP) que si no nunca terminan.

## Velocidad: como medir

- La linea `[velocidad]` (cada 5 s, tambien en `escuadron2_log.txt` en Windows) dice cuadros del
  juego por segundo, que tan ocupado esta el hilo grafico, cuanto espera el juego, dibujos y
  llamadas a OpenGL, MHz del DSP y **ms de CPU por cuadro de cada parte** (juego = todos los
  hilos del juego, grafico = hilo de GX + OpenGL, DSP) y ns por ciclo del DSP.
- `RL_PROF=1` (Linux): perfilador por muestreo; `RL_PROF_DESDE=N` empieza en el cuadro N.
  `python3 port/tools/prof.py build/port/escuadron2 build/prof.raw` resume por hilo y funcion.
- `RL_REPLAY_LOOPS=N ./build/port/escuadron2 --reproducir CAPTURA.gxc salida.png` repite un cuadro
  capturado (`--capturar N`) N veces y da los ms por cuadro del hilo grafico.
- Lo que ya se hizo: la transformacion de vertices va en la GPU (`VS_XF` en `render_gl.c`, el
  estado de XF de cada dibujo en un buffer de texturas; `RL_GL_XF=0` vuelve a la CPU), los
  dibujos con el mismo estado se juntan, el tubo de GX se manda en lotes, las texturas se
  decodifican por bloques, el codigo recompilado va con `-O2` y `restrict`, el DSP en una
  sola unidad de compilacion con tabla de operaciones combinada.

## Estado (octubre 2026)

Funciona (probado en Windows 11 con una GTX 1660 Ti):
- Todo el juego desde el arranque: pantalla legal, intro, menus con pelicula de fondo,
  seleccion de mision y de nave, el texto inicial y la mision 1 con el HUD.
- **Audio** (musica, efectos, voces) con el DSP de bajo nivel.
- **Tarjeta de memoria** en la ranura A (`memcard_A.raw` al lado de los datos).
- Ventana con OpenGL 3.3, pantalla completa (F11), teclado y joystick.

Arreglado en esta etapa (octubre 2026, segunda sesion):
- Iluminacion de XF como el hardware/Dolphin: atenuacion 1 = especular, 3 = foco (antes el foco daba 0
  y las naves y AT-AT salian oscuros); difuso 1 = con signo, 2 = recortado (estaban cambiados).
- Texgen de relieve (emboss: copia la coordenada fuente; sin tangentes no hay corrimiento) y de color.
- Copias de Z a textura: z=1.0 desbordaba a 0 (las sombras de el nivel 3 cubrian todo el suelo).
- Texturas indirectas (BP 0x10.., 0x25-0x27, matrices 0x06..) en el ubershader, como Dolphin.
- Pantalla 16:9 (`--panoramico`, F10).
- Probadas las 10 misiones con `RL_NIVEL=1..10` (1 a 10): todas cargan, se juegan y terminan (fallo/game over) sin errores.
  Tambien los niveles extra 11, 12, 13, 14 (creditos)
  y 15 (Resistencia).

Tercera sesion: DSP con JIT, hilo grafico ~30% mas liviano (cache de formatos de vertice,
hash por muestras en texturas grandes), escrituras al tubo de GX mas rapidas, vibracion del
joystick, `escuadron2.ini`, y en Windows una caida deja `[caida] ... escuadron2.exe+0x...` en el registro.
En la maquina de pruebas (2 nucleos, sin placa de video) el nivel 3 va a ~58 cuadros/s sin dibujar.

Cuarta sesion: la intro ya no se traba en Windows (los avisos de "fin de dibujo" del PE van
en cola y no se pierden; antes se juntaban si el juego tardaba en atenderlos). Hilo grafico:
vertices, indices y uniforms en buffers anillo (`glBufferSubData` + `glDrawElementsBaseVertex`,
`RL_GL_SIMPLE=1` vuelve al camino viejo), el estado de XF se sube por bloques de 16 palabras
(solo lo que cambio: en el nivel 3 bajo de ~16 MB a ~0.3 MB por cuadro) y el estado de cada dibujo
se reusa si no cambio ningun registro de BP/XF (`RL_NO_KEY_CACHE=1` lo apaga). Las capturas
de el nivel 3 dan el mismo resultado pixel por pixel. Al reproducir, el registro cuenta estados de XF
y bloques de uniforms. Al cerrar la ventana el programa sale de golpe (`_Exit`) y en Windows
solo se puede abrir una copia.

**zfreeze** (genMode bit 19): con el bit prendido la profundidad de cada pixel sale del plano
del ultimo triangulo dibujado sin zfreeze (como `CalculateZSlope` de Dolphin), no de los
vertices. El juego lo usa para mandar el cielo y las nubes al fondo: sin esto el cielo, que se
dibuja despues del terreno, tapaba todo lo que estuviera a mas de ~100 unidades (el suelo no se
veia y uno chocaba). Hay una variante del shader con `ZFREEZE` que escribe `gl_FragDepth`.
`RL_NO_ZFREEZE=1` vuelve al comportamiento viejo. Herramientas nuevas: `RL_TEX_DUMP=1` (guarda
cada textura subida en build/tex), `RL_TRACE_BP=A-B` (escrituras de BP entre dibujos),
`RL_TRACE_NOZ=1` (el dibujo de `RL_DRAW_TRACE` sin prueba de Z).

**Shaders especializados**: el shader del TEV se arma en tres partes (`FS_PRE`, `FS_BODY`,
`FS_POST`); para cada configuracion de etapas que aparece se compila un shader con las etapas
como constantes (`prog_for`, cache de hasta 8192), en vez del "ubershader" que lee todo de los
uniforms (`RL_UBERSHADER=1` lo vuelve a usar). el nivel 3 usa ~35 por cuadro. Mismo resultado pixel
por pixel. `RL_LOG_COPIES=1` registra copias del EFB a textura y texturas grandes nuevas.
Nota: en el hangar, el holograma de la mision es un video que la CPU decodifica a texturas I8
(Y 640x480 con 16 videos en mosaico, U y V de 320x240) y el TEV pasa de YUV a RGB.
Los buffers anillo se escriben con `glMapBufferRange` sin sincronizar (`RL_GL_SUBDATA=1`
vuelve a `glBufferSubData`).

**Sin tirones por shaders**: el log del usuario mostraba caidas a 18-24 cuadros/s justo cuando
aparecian shaders nuevos (compilar uno tarda decenas de ms en el driver). Ahora, si la placa
tiene `GL_KHR/ARB_parallel_shader_compile`, el shader se compila en los hilos del driver y
mientras no esta listo se dibuja con el ubershader (mismo resultado, un poco mas lento). Ademas
los programas ya enlazados se guardan con `glGetProgramBinary` en `escuadron2_shaders.cache` (al
lado de la tarjeta de memoria) y se cargan al arrancar; la cache se invalida sola si cambia la
placa, el driver o el codigo de los shaders. `RL_SHADER_ASYNC=0/1` y `RL_SHADER_CACHE=0`
para probar.
Ademas el exe trae una lista de configuraciones de etapas conocidas (`port/runtime/shader_claves.inc`,
generada con `port/tools/claves_shaders.py` desde caches de jugadores): al arrancar se piden de a
6 por cuadro a los hilos del driver (`RL_SHADER_PRECALENTAR=0` lo apaga; si el driver compila en
el momento, se deja de hacer). Los shaders especializados tambien saben si la prueba de alfa
siempre pasa: entonces no tienen `discard` y la placa puede descartar por Z antes de pintar
(~20-40% menos en llvmpipe con la captura de el nivel 3). Las copias del EFB reusan su framebuffer.

**Lineas y puntos con corrimiento de textura** (BP 0x22 bits 16-21 y el bit 18/19 de cada
SU_SSIZE): faltaban. El juego dibuja asi los dibujos de alambre del HUD (la nave en el indicador
de escudo, el blanco en la mira) y el cable del snowspeeder.

**Buffers persistentes**: con `GL_ARB_buffer_storage` los anillos de vertices, indices y uniforms
quedan mapeados y cada lote es un memcpy (vallas cada cuarto de anillo; `RL_GL_PERSISTENTE=0` lo
apaga). En el nivel 3 hay ~1500 llamadas de dibujo por cuadro y el hilo grafico era el freno.

**Audio**: la cola de la placa de sonido se mantiene entre 60 y 120 ms (antes 30-90) para que
no se corte cuando el juego se atrasa un momento. La linea `[audio]` del registro dice cuantos
cortes hubo, la cola y el bloque de audio que se atendio mas tarde.

**Texturas en arreglos** (octubre 2026): todas las texturas que lee el TEV son
`GL_TEXTURE_2D_ARRAY`. Las de la RAM del mismo tamano y cantidad de mipmaps comparten un
arreglo (hasta 256 capas las chicas, menos las grandes; ver `arr_cap` y `tex_slot` en
`render_gl.c`) y cada vertice lleva la capa de cada unidad (atributo `a_lay`, 8 bits por
unidad). Asi los dibujos que solo cambian de textura se juntan: el suelo de el nivel 3 cambia de
textura de 32x32 en cada pedazo y eran ~2500-4500 llamadas por cuadro; ahora 300-900. En el
log del usuario el tiempo por cuadro en el nivel 3 era ~15 ms + ~15 us por llamada. Las copias del
EFB tienen un arreglo de una capa cada una. Las 29 capturas de prueba dan igual pixel por pixel.
`RL_PERFIL=1` (al reproducir una captura) dice cuantos pixeles pinta cada programa;
`RL_KEY_STATS=1` dice por que se corta cada lote.

**Audio sin "tics"**: los videos del usuario tenian ~2 clics por segundo, de a pares
separados 5 ms: un bloque de audio de 5 ms viejo (de la vuelta anterior de la ronda de 4
bloques del juego). Pasa cuando el juego todavia no atendio el aviso del bloque anterior y
se le avisa otra vez (se juntan y el DSP mezcla un bloque menos) o cuando el DSP todavia no
escribio el bloque. Ahora se anota cada escritura del DSP a la RAM (`hw_dsp_escribio`) y el
bloque espera mientras la placa de sonido tenga mas de 15 ms encolados (antes el limite era
8 ms fijos). La linea `[audio]` cuenta avisos juntados, bloques viejos, esperas y el hueco
mas largo sin atender el hardware. `RL_AUDIO_SIN_ESPERA_DSP=1` lo apaga.

**Esperas de Windows**: en el log del usuario los bloques de audio se atendian 16-25 ms tarde
cada 5 s y habia cuadros de 2 retrazados con el dibujo terminado a los 3 ms: las esperas
despertaban con el tic de 15,6 ms del reloj de Windows (Windows 11 ignora `timeBeginPeriod`
si decide ahorrar energia con el proceso). Ahora el bucle de espera usa un temporizador de
alta resolucion (`CreateWaitableTimerExW` con `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION`) y un
evento que lo despierta al llegar una interrupcion, y el proceso pide no ser frenado
(`ProcessPowerThrottling`). Al arrancar el registro dice cuanto duran las esperas de 1 ms.

**Cuelgues del terreno (niveles 3 y 4)**: `fn_7FCBE5F0` recorre la grilla del terreno cercano
celda por celda hasta una celda final que compara por igualdad, y pide cada fila nueva a
`fn_7FCBE108`. En la PC del usuario a veces la fila se pasa de la celda final y el bucle sigue
de a 2 bytes hacia atras por toda la memoria (miles de millones de vueltas, 0 FPS, cientos de
miles de "acceso invalido"). Las dos van envueltas (`hle.txt`): mas de 4000 filas por llamada
se cortan (`RL_TERRENO_LIMITE`) y si pasan mas de 3x2048 vueltas sin pedir fila nueva
(`RL_TERRENO_LIMITE_VUELTAS`; lo normal es 1) se da por terminada la fila acomodando la celda
actual y la final; si ni asi termina se sale de la funcion con `longjmp`. La linea
`[terreno]` lo cuenta. La causa de fondo (por que la fila se pasa) sigue sin encontrarse: aca
nunca pasa. Las lecturas mas alla de 24 MB ahora son espejo de la RAM, como en la consola.

**Piso y techo (FPSCR)**: el juego tiene funciones de piso y techo (`fn_7FC8F94C`,
`fn_7FC8F974`; 118 llamadas: terreno, choques, etc.) que hacen `mtfsfi 7,3` (o `7,2`) para
cambiar el modo de redondeo, `fctiw` y despues restauran el FPSCR. El recompilador ignoraba
`mtfsfi` y `fctiw` siempre redondeaba al mas cercano: el piso de 3.7 daba 4. Ahora el FPSCR
guarda el modo (`mtfsfi`, `mtfsb0/1`, `mtfsf` con mascara) y `fctiw` lo respeta
(`fctiw_rn`), tambien en el interprete. Candidato fuerte para el suelo cercano que faltaba
en los niveles 3 y 4 y para la fila de la grilla que se pasaba de largo.

**Sombras y luces proyectadas (dual tex)**: con la matriz de post-transformacion de las
texturas activada, la consola (y Dolphin) multiplican la matriz por (s,t,q) sin dividir por q;
la division la hace despues el rasterizador. El port dividia antes: el manchon de sombra/luz
que el juego proyecta sobre el suelo cuando la nave vuela bajo (textura I8 de 128x128 en la
Maw) cubria todo el suelo y lo dejaba negro. Arreglado en el shader (VS_XF) y en los caminos
de CPU (render.c, vtx.c), con el bit de normalizar. Herramientas nuevas: `--ensayo-terreno
CAPTURA` (corre el terreno sobre la RAM de una captura y deja `build/ensayo.gxc`),
`RL_CONTAR_DIBUJOS=1` (una linea por dibujo al reproducir), luces en `RL_DRAW_TRACE` y la
palanca en `--pad` (`"400:S128/40*90"`).

**Hilo grafico 2,5 veces mas liviano**: un aviso de diagnostico de `texture_for` ("textura
que empieza dentro de una copia del EFB") recorria las 4096 casillas de la cache de texturas en
cada busqueda mientras no se hubieran escrito 40 avisos (casi nunca se llegaba): con callgrind
era el 60% de las instrucciones del hilo grafico en el nivel 3. Ahora solo corre con
`RL_LOG_COPIES=1`. `RL_DUMP_TRAS_CARGA=N` guarda las N imagenes que siguen a cada carga de
nivel (para buscar el cuadro con ruido al entrar a elegir mision). 1956 claves de shaders.

**Pantalla quebrada al entrar a "Select Mission"**: en la transicion desde "Select Game"
el juego dibuja la ultima imagen leyendo la memoria de la XFB (0x0B8CC0) como textura RGB565
de 640x452. La imagen de la XFB solo vivia en la placa y en la RAM habia basura: salia ruido
de colores (con una partida guardada; sin partida el camino es otro y no pasaba). Ahora una
textura que empieza en una XFB se arma desde la imagen de la placa con el shader de las
copias (como Dolphin), `xfb_como_textura`. Se reprodujo con la tarjeta de memoria del
usuario (`--memcard`). Herramientas: `RL_DUMP_XFB_COPIAS=1` (cada copia a la XFB al
reproducir) y `[pres]` con el cuadro del juego en `RL_DUMP_PRESENTES`.

Falta, en orden de prioridad:
1. **Shaders en un hilo aparte**: con NVIDIA el driver compila en el momento aunque diga que
   lo hace en paralelo (no se precalienta y cada shader nuevo traba). Compilar en un contexto
   de OpenGL compartido en otro hilo.
2. **Velocidad** en PCs mas lentas: el DSP ahora va con el JIT de Dolphin (~3 veces menos CPU que el interprete); el hilo del juego
   y el grafico van en paralelo. Ver la linea `[velocidad]` del usuario para saber que frena.
3. Detalles de GX: filtros de copia; las nubes de el nivel 3 tienen bordes rectos (revisar si es asi en la consola).
4. Probar el resto de las misiones.
