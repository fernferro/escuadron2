# DSP de bajo nivel

`Core/DSP/` es una copia del DSP de Dolphin
(https://github.com/dolphin-emu/dolphin, `Source/Core/Core/DSP`, licencia GPL-2.0-or-later):
el interprete y el **JIT x86-64** (`Core/DSP/Jit/`, que se usa por defecto; `--dsp-interprete`
vuelve al interprete). Cambios: sin estado guardado, y el bit `INIT_CODE` del registro de
control se apaga en la segunda lectura. `Common/` tiene el emisor x64 de Dolphin
(`x64Emitter`, `x64ABI`, `CodeBlock`, `BitSet`, `CPUDetect`; `cpu_detect.cpp` es una deteccion
minima propia). `shim/` tiene versiones minimas de las demas cabeceras de `Common/`.
`rom/` son las ROM libres del DSP hechas por el equipo de Dolphin; `dsp_rom_data.c` las embebe
en el ejecutable. `dsp_lle.cpp` es la union con el runtime del port (al cargar microcodigo
nuevo hay que llamar a `ClearIRAM()` para que el JIT recompile).
