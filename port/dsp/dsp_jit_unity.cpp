// JIT x86-64 del DSP de Dolphin (Core/DSP/Jit, GPL-2.0-or-later) en una unidad aparte.
#include "Common/x64Emitter.cpp"
#include "Common/x64ABI.cpp"
#include "cpu_detect.cpp"
#include "Core/DSP/Jit/DSPEmitterBase.cpp"
#if defined(_M_X86_64)
#include "Core/DSP/Jit/x64/DSPEmitter.cpp"
#include "Core/DSP/Jit/x64/DSPJitArithmetic.cpp"
#include "Core/DSP/Jit/x64/DSPJitBranch.cpp"
#include "Core/DSP/Jit/x64/DSPJitCCUtil.cpp"
#include "Core/DSP/Jit/x64/DSPJitExtOps.cpp"
#include "Core/DSP/Jit/x64/DSPJitLoadStore.cpp"
#include "Core/DSP/Jit/x64/DSPJitMisc.cpp"
#include "Core/DSP/Jit/x64/DSPJitMultiplier.cpp"
#include "Core/DSP/Jit/x64/DSPJitRegCache.cpp"
#include "Core/DSP/Jit/x64/DSPJitTables.cpp"
#include "Core/DSP/Jit/x64/DSPJitUtil.cpp"
#endif
