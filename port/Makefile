# Port a PC.
#
#   make -C port                         Linux/macOS (gcc o clang). Con SDL2 instalado
#                                        (sdl2-config) se compila con ventana y OpenGL.
#   make -C port SDL2_DIR=/ruta          SDL2 instalado en otra carpeta
#   make -C port TARGET=win MINGW=/ruta/llvm-mingw SDL2_WIN=/ruta/SDL2-x.y.z/x86_64-w64-mingw32
#                                        escuadron2.exe para Windows (compilacion cruzada; ver docs/port.md)
#
# Requiere orig/GSWE64/combined.dol (python tools/setup_rl.py ...) para generar el codigo.
ROOT    := $(abspath ..)
GEN     := $(ROOT)/build/port/gen
CFLAGS  ?= -O2 -g
RTFLAGS ?= -O2 -g     # el runtime (rasterizador, interprete) se optimiza mas
CPPFLAGS := -I$(ROOT)/port/runtime -I$(GEN)
PY      ?= python3
GL_INC  := -I$(ROOT)/port/third_party
LIBS    :=
LDFLAGS :=

ifeq ($(TARGET),win)
OUT     := $(ROOT)/build/port-win
CC      := $(MINGW)/bin/x86_64-w64-mingw32-clang
CXX     := $(MINGW)/bin/x86_64-w64-mingw32-clang++
EXE     := .exe
CPPFLAGS += -DRL_GL -I$(SDL2_WIN)/include/SDL2 $(GL_INC)
LIBS    += $(SDL2_WIN)/lib/libSDL2.a -lsetupapi -lwinmm -limm32 -lversion -lole32 -loleaut32 -lgdi32 \
           -luser32 -lshell32 -lcfgmgr32 -luuid -ladvapi32
LDFLAGS += -static -Wl,--stack,67108864
else
OUT     := $(ROOT)/build/port
CC      ?= gcc
CXX     ?= g++
EXE     :=
SDL2_CONFIG ?= $(if $(SDL2_DIR),$(SDL2_DIR)/bin/sdl2-config,sdl2-config)
SDL_CFLAGS := $(shell $(SDL2_CONFIG) --cflags 2>/dev/null)
SDL_LIBS   := $(shell $(SDL2_CONFIG) --libs 2>/dev/null)
ifneq ($(SDL_CFLAGS),)
CPPFLAGS += -DRL_GL $(SDL_CFLAGS) $(GL_INC)
LIBS    += $(SDL_LIBS)
endif
endif
OBJ     := $(OUT)/obj
.DEFAULT_GOAL := all
ifeq ($(TARGET),win)
JUEGO_RES := $(OBJ)/juego_rc.o   # icono y version de escuadron2.exe
$(OBJ)/juego_rc.o: $(ROOT)/port/launcher/juego.rc $(ROOT)/port/launcher/icono.ico
	@mkdir -p $(OBJ)
	$(MINGW)/bin/x86_64-w64-mingw32-windres -I$(ROOT)/port/launcher $< -o $@
endif

RUNTIME := memory runtime hw loader os disc gx render render_gl vtx video input dsp_hle interp audio prof cputime memcard controles texturas_hd
RT_OBJS := $(addprefix $(OBJ)/rt_,$(addsuffix .o,$(RUNTIME)))

# DSP de bajo nivel: interprete del DSP de Dolphin (port/dsp, GPL-2.0-or-later)
DSPDIR   := $(ROOT)/port/dsp
# todo el interprete en una sola unidad de compilacion (dsp_unity.cpp) para que se
# puedan expandir en linea las funciones chicas que se llaman en cada instruccion
# el JIT x86-64 del DSP (de Dolphin) va en otra unidad: dsp_jit_unity.cpp
DSP_SRCS := $(DSPDIR)/dsp_unity.cpp $(DSPDIR)/dsp_jit_unity.cpp $(DSPDIR)/dsp_lle.cpp
DSP_OBJS := $(patsubst $(DSPDIR)/%.cpp,$(OBJ)/dsp/%.o,$(DSP_SRCS)) $(OBJ)/dsp/dsp_rom_data.o
DSPFLAGS := -O3 -g -std=c++20 -w -D_M_X86_64 -I$(DSPDIR)/shim -I$(DSPDIR)
RT_OBJS  += $(DSP_OBJS)

# DSP nativo: los programas del DSP (sonido de MusyX) traducidos a C (port/recomp/dsprecomp.py)
NATDIR   := $(ROOT)/port/dsp_nativo
DSP_MUSYX := dol:2091392:6624:badc6015333328edb10e72f25b5afbf3ef903090
DSP_INIT  := dol:2099416:128:f5a04aac3392caf001fca12bf61563ac21d86a1f
$(GEN)/dsp_programas.c: $(ROOT)/port/recomp/dsprecomp.py $(ROOT)/port/recomp/dsp_tablas.py $(DSPDIR)/rom/dsp_rom.bin
	@mkdir -p $(GEN)
	$(PY) -I $(ROOT)/port/recomp/dsprecomp.py $@ --dol=$(ROOT)/orig/GSWE64/combined.dol \
	    musyx=$(DSP_MUSYX)@0 init=$(DSP_INIT)@0 rom=$(DSPDIR)/rom/dsp_rom.bin@8000
NATFLAGS := -O2 -g -std=gnu11 -fno-strict-aliasing -w -Werror=implicit-function-declaration -I$(NATDIR)
$(OBJ)/nat_dsp_nativo.o: $(NATDIR)/dsp_nativo.c $(wildcard $(NATDIR)/*.h)
	@mkdir -p $(OBJ)
	$(CC) $(NATFLAGS) -c $< -o $@
$(GEN)/dsp_programas_paso.c: $(GEN)/dsp_programas.c
$(OBJ)/nat_dsp_programas.o: $(GEN)/dsp_programas.c $(wildcard $(NATDIR)/*.h)
	@mkdir -p $(OBJ)
	$(CC) $(NATFLAGS) -c $< -o $@
$(OBJ)/nat_dsp_programas_paso.o: $(GEN)/dsp_programas_paso.c $(wildcard $(NATDIR)/*.h)
	@mkdir -p $(OBJ)
	$(CC) $(NATFLAGS) -O1 -c $< -o $@
RT_OBJS  += $(OBJ)/nat_dsp_nativo.o $(OBJ)/nat_dsp_programas.o $(OBJ)/nat_dsp_programas_paso.o
# copias "rapidas" de funciones traducidas (port/tools/rapidas.py): el decodificador de
# video de los mensajes de radio, con paired singles sin redondear en cada paso
RAPIDAS := 80055F60 8005AD80
$(GEN)/rapidas.c: $(GEN)/stamp $(ROOT)/port/tools/rapidas.py
	$(PY) -I $(ROOT)/port/tools/rapidas.py $(GEN) $@ $(RAPIDAS)
$(OBJ)/rapidas.o: $(GEN)/rapidas.c $(ROOT)/port/runtime/ppc.h
	@mkdir -p $(OBJ)
	$(CC) -O3 -g -fno-math-errno -ffp-contract=fast -w $(CPPFLAGS) -c $< -o $@
RT_OBJS  += $(OBJ)/rapidas.o

$(OBJ)/dsp/%.o: $(DSPDIR)/%.cpp $(wildcard $(DSPDIR)/Core/DSP/*.h $(DSPDIR)/Core/DSP/*.cpp $(DSPDIR)/Core/DSP/Interpreter/* $(DSPDIR)/Core/DSP/Jit/*.* $(DSPDIR)/Core/DSP/Jit/x64/* $(DSPDIR)/Common/*)
	@mkdir -p $(dir $@)
	$(CXX) $(DSPFLAGS) -c $< -o $@

$(OBJ)/dsp/dsp_rom_data.o: $(DSPDIR)/dsp_rom_data.c
	@mkdir -p $(dir $@)
	$(CC) -O1 -c $< -o $@

all: $(OUT)/test_recomp$(EXE) $(OUT)/escuadron2$(EXE)

$(GEN)/stamp: $(ROOT)/port/recomp/recomp.py $(ROOT)/config/GSWE64/symbols.txt $(wildcard $(ROOT)/port/hle.txt)
	$(PY) -I $(ROOT)/port/recomp/recomp.py $(ROOT)/orig/GSWE64/combined.dol $(ROOT)/config/GSWE64/symbols.txt $(GEN) \
	    $(if $(wildcard $(ROOT)/port/hle.txt),--hle $(ROOT)/port/hle.txt)
	touch $@

GEN_SRCS = $(wildcard $(GEN)/code_*.c) $(GEN)/table.c
GEN_OBJS = $(patsubst $(GEN)/%.c,$(OBJ)/%.o,$(GEN_SRCS))

$(OBJ)/%.o: $(GEN)/%.c $(GEN)/stamp $(ROOT)/port/runtime/ppc.h
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) -w $(CPPFLAGS) -c $< -o $@

$(OBJ)/rt_%.o: $(ROOT)/port/runtime/%.c $(wildcard $(ROOT)/port/runtime/*.h) $(wildcard $(ROOT)/port/runtime/*.inc) $(GEN)/stamp
	@mkdir -p $(OBJ)
	$(CC) $(RTFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/test_recomp.o: $(ROOT)/port/tests/test_recomp.c $(GEN)/stamp
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

$(OBJ)/main.o: $(ROOT)/port/runtime/main.c $(wildcard $(ROOT)/port/runtime/*.h) $(GEN)/stamp
	@mkdir -p $(OBJ)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

gen: $(GEN)/stamp

$(OUT)/test_recomp$(EXE): gen $(OUT)/escuadron2$(EXE)
	$(MAKE) $(OBJ)/test_recomp.o $(RT_OBJS) $(GEN_OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ)/test_recomp.o $(RT_OBJS) $(GEN_OBJS) $(LIBS) -lm -lpthread

$(OUT)/escuadron2$(EXE): gen
	$(MAKE) $(OBJ)/main.o $(RT_OBJS) $(GEN_OBJS) $(JUEGO_RES)
	$(CXX) $(LDFLAGS) -o $@ $(OBJ)/main.o $(RT_OBJS) $(GEN_OBJS) $(JUEGO_RES) $(LIBS) -lm -lpthread

test: $(OUT)/test_recomp$(EXE)
	cd $(ROOT) && $(OUT)/test_recomp$(EXE)

.PHONY: all gen test

# ---- launcher (Escuadron2 / Escuadron2.exe): Dear ImGui + SDL2 + OpenGL 3 ----
#   make -C port launcher        (mismas variables que el juego: TARGET=win MINGW=... SDL2_WIN=...)
IMGUI    := $(ROOT)/port/third_party/imgui
LAUNCHER := $(ROOT)/port/launcher
LNC_OBJ  := $(OBJ)/launcher
LNC_SRCS := $(LAUNCHER)/launcher.cpp $(IMGUI)/imgui.cpp $(IMGUI)/imgui_draw.cpp $(IMGUI)/imgui_tables.cpp \
            $(IMGUI)/imgui_widgets.cpp $(IMGUI)/backends/imgui_impl_sdl2.cpp $(IMGUI)/backends/imgui_impl_opengl3.cpp
LNC_OBJS := $(addprefix $(LNC_OBJ)/,$(notdir $(LNC_SRCS:.cpp=.o)))
ifeq ($(TARGET),win)
LNC_RES  := $(LNC_OBJ)/recursos.o
LNC_LIBS := $(SDL2_WIN)/lib/libSDL2main.a $(LIBS) -lcomdlg32 -lopengl32 -mwindows
else
LNC_RES  :=
LNC_LIBS := $(LIBS) $(if $(wildcard /usr/lib/x86_64-linux-gnu/libGL.so),-lGL,-l:libGL.so.1) -ldl
endif
LNC_FLAGS := -O2 -std=c++17 $(filter -I% -D%,$(CPPFLAGS)) -I$(IMGUI) -I$(IMGUI)/backends -I$(LNC_OBJ)

$(LNC_OBJ)/fuentes.inc: $(ROOT)/port/tools/incrustar.py $(wildcard $(LAUNCHER)/fuentes/*.ttf)
	@mkdir -p $(LNC_OBJ)
	$(PY) -I $< $@ k_poppins_regular=$(LAUNCHER)/fuentes/Poppins-Regular.ttf \
	    k_poppins_medium=$(LAUNCHER)/fuentes/Poppins-Medium.ttf k_poppins_bold=$(LAUNCHER)/fuentes/Poppins-Bold.ttf

$(LNC_OBJ)/launcher.o: $(LAUNCHER)/launcher.cpp $(wildcard $(LAUNCHER)/*.h) $(ROOT)/port/runtime/trucos.h $(LNC_OBJ)/fuentes.inc
	@mkdir -p $(LNC_OBJ)
	$(CXX) $(LNC_FLAGS) -c $< -o $@
$(LNC_OBJ)/%.o: $(IMGUI)/%.cpp
	@mkdir -p $(LNC_OBJ)
	$(CXX) $(LNC_FLAGS) -c $< -o $@
$(LNC_OBJ)/%.o: $(IMGUI)/backends/%.cpp
	@mkdir -p $(LNC_OBJ)
	$(CXX) $(LNC_FLAGS) -c $< -o $@
$(LNC_OBJ)/recursos.o: $(LAUNCHER)/recursos.rc $(LAUNCHER)/icono.ico $(LAUNCHER)/manifiesto.xml
	@mkdir -p $(LNC_OBJ)
	$(MINGW)/bin/x86_64-w64-mingw32-windres -I$(LAUNCHER) $< -o $@

$(OUT)/Escuadron2$(EXE): $(LNC_OBJS) $(LNC_RES)
	$(CXX) $(LDFLAGS) -s -o $@ $^ $(LNC_LIBS)

launcher: $(OUT)/Escuadron2$(EXE)

# ---- instalador: programa (con zlib para descomprimir) + archivos pegados al final ----
ZLIBDIR  := $(ROOT)/port/third_party/zlib
ZLIB_OBJS := $(addprefix $(LNC_OBJ)/z_,adler32.o crc32.o inffast.o inflate.o inftrees.o uncompr.o zutil.o deflate.o trees.o)
INST_OBJS := $(LNC_OBJ)/instalador.o $(filter-out $(LNC_OBJ)/launcher.o,$(LNC_OBJS)) $(ZLIB_OBJS)
ifeq ($(TARGET),win)
INST_LIBS := $(LNC_LIBS) -luuid -lole32
else
INST_LIBS := $(LNC_LIBS)
endif
$(LNC_OBJ)/z_%.o: $(ZLIBDIR)/%.c
	@mkdir -p $(LNC_OBJ)
	$(CC) -O2 -w -c $< -o $@
$(LNC_OBJ)/instalador.o: $(LAUNCHER)/instalador.cpp $(LAUNCHER)/estilo.h $(LNC_OBJ)/fuentes.inc
	@mkdir -p $(LNC_OBJ)
	$(CXX) $(LNC_FLAGS) -I$(ZLIBDIR) -c $< -o $@
$(OUT)/instalador_programa$(EXE): $(INST_OBJS) $(LNC_RES)
	$(CXX) $(LDFLAGS) -s -o $@ $^ $(INST_LIBS)

# archivos que se instalan (destino=origen); el juego va sin informacion de depuracion
INST_ARCH = escuadron2$(EXE)=$(OUT)/escuadron2_dist$(EXE) Escuadron2$(EXE)=$(OUT)/Escuadron2$(EXE) \
            LEEME.txt=$(LAUNCHER)/LEEME.txt licencias/imgui.txt=$(IMGUI)/LICENSE.txt \
            licencias/poppins_OFL.txt=$(LAUNCHER)/fuentes/OFL.txt licencias/zlib.txt=$(ZLIBDIR)/LICENSE \
            licencias/dolphin_dsp_GPL2.txt=$(DSPDIR)/COPYING licencias/SDL2.txt=$(LAUNCHER)/licencia_SDL2.txt \
            $(foreach f,$(wildcard $(ROOT)/build/arte/*),arte/$(notdir $(f))=$(f))
# build/arte/ (opcional, fuera del repo): fondo*.jpg, portada, control, disco e instalador.jpg
$(OUT)/escuadron2_dist$(EXE): $(OUT)/escuadron2$(EXE)
	$(if $(MINGW),$(MINGW)/bin/llvm-strip,strip) -o $@ $<
$(OUT)/Instalar-Escuadron2$(EXE): $(OUT)/instalador_programa$(EXE) $(OUT)/escuadron2_dist$(EXE) $(OUT)/Escuadron2$(EXE) \
        $(ROOT)/port/tools/empaquetar_instalador.py $(LAUNCHER)/LEEME.txt $(wildcard $(ROOT)/build/arte/*)
	$(PY) -I $(ROOT)/port/tools/empaquetar_instalador.py $< $@ $(INST_ARCH)

instalador: $(OUT)/Instalar-Escuadron2$(EXE)
.PHONY: launcher instalador

# ---- menu dentro del juego (F1): Dear ImGui, el mismo estilo y opciones que el launcher ----
ifneq ($(findstring -DRL_GL,$(CPPFLAGS)),)
MENU_OBJS := $(OBJ)/menu.o $(filter-out $(LNC_OBJ)/launcher.o,$(LNC_OBJS))
$(OBJ)/menu.o: $(ROOT)/port/runtime/menu.cpp $(LAUNCHER)/ajustes.h $(LAUNCHER)/estilo.h $(ROOT)/port/runtime/trucos.h $(LNC_OBJ)/fuentes.inc
	@mkdir -p $(OBJ)
	$(CXX) $(LNC_FLAGS) -I$(LAUNCHER) -c $< -o $@
RT_OBJS += $(MENU_OBJS)
endif
