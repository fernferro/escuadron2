/*
 * DSP nativo: estado del DSP de la GameCube y funciones auxiliares de las instrucciones.
 *
 * El sonido del juego lo hace un programa del juego (el "microcodigo" de MusyX) que
 * corre en el DSP de la consola. Igual que el codigo del juego, ese programa se traduce a C
 * cuando se compila el port (port/recomp/dsprecomp.py); este archivo tiene lo que necesita el
 * codigo traducido: los registros del DSP y el significado exacto de cada instruccion.
 *
 * La semantica de las instrucciones sale del interprete del DSP de Dolphin
 * (Core/DSP/Interpreter, GPL-2.0-or-later, Copyright 2008 Dolphin Emulator Project y
 * 2004 Duddie & Tratax), convertida a C por /tmp/convert (ver docs/port.md).
 */
#ifndef DSP_CORE_H
#define DSP_CORE_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define DSPI static inline __attribute__((always_inline))
#else
#define DSPI static inline
#endif

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

enum {
    DSP_REG_AR0 = 0x00, DSP_REG_AR1, DSP_REG_AR2, DSP_REG_AR3,
    DSP_REG_IX0 = 0x04, DSP_REG_IX1, DSP_REG_IX2, DSP_REG_IX3,
    DSP_REG_WR0 = 0x08, DSP_REG_WR1, DSP_REG_WR2, DSP_REG_WR3,
    DSP_REG_ST0 = 0x0c, DSP_REG_ST1, DSP_REG_ST2, DSP_REG_ST3,
    DSP_REG_ACH0 = 0x10, DSP_REG_ACH1 = 0x11,
    DSP_REG_CR = 0x12, DSP_REG_SR = 0x13,
    DSP_REG_PRODL = 0x14, DSP_REG_PRODM = 0x15, DSP_REG_PRODH = 0x16, DSP_REG_PRODM2 = 0x17,
    DSP_REG_AXL0 = 0x18, DSP_REG_AXL1 = 0x19, DSP_REG_AXH0 = 0x1a, DSP_REG_AXH1 = 0x1b,
    DSP_REG_ACL0 = 0x1c, DSP_REG_ACL1 = 0x1d, DSP_REG_ACM0 = 0x1e, DSP_REG_ACM1 = 0x1f,
};

enum {
    SR_CARRY = 0x0001, SR_OVERFLOW = 0x0002, SR_ARITH_ZERO = 0x0004, SR_SIGN = 0x0008,
    SR_OVER_S32 = 0x0010, SR_TOP2BITS = 0x0020, SR_LOGIC_ZERO = 0x0040, SR_OVERFLOW_STICKY = 0x0080,
    SR_100 = 0x0100, SR_INT_ENABLE = 0x0200, SR_400 = 0x0400, SR_EXT_INT_ENABLE = 0x0800,
    SR_1000 = 0x1000, SR_MUL_MODIFY = 0x2000, SR_40_MODE_BIT = 0x4000, SR_MUL_UNSIGNED = 0x8000,
    SR_CMP_MASK = 0x3f
};

enum { CR_RESET = 0x0001, CR_EXTERNAL_INT = 0x0002, CR_HALT = 0x0004, CR_INIT_CODE = 0x0400, CR_INIT = 0x0800 };
enum { STACK_CALL = 0, STACK_DATA = 1, STACK_LOOPADDR = 2, STACK_LOOPCNT = 3 };
enum { EXC_ACC_RAW_READ = 3, EXC_ACC_RAW_WRITE = 4, EXC_ACC_SAMPLE_READ = 5, EXC_EXTERNAL = 7 };

typedef struct {
    u16 ar[4], ix[4], wr[4], st[4];
    u16 cr, sr;
    union { u64 val; struct { u16 l, m, h, m2; }; } prod;
    union { u32 val; struct { u16 l, h; }; } ax[2];
    union { u64 val; struct { u16 l, m; u32 h; }; } ac[2];
} DspRegs;

typedef struct {
    DspRegs r;
    u16 pc;
    u16 control_reg;
    u32 control_reg_init_code_clear_time;
    u8 reg_stack_ptrs[4];
    u8 exceptions;
    u16 reg_stacks[4][32];
    u16 iram[0x1000];
    u16 dram[0x1000];
    const u16* irom;
    const u16* coef;
    u16 ifx[256];
    /* acelerador (lectura de muestras de la ARAM) */
    u32 acc_start, acc_end, acc_cur;
    u16 acc_format;
    s16 acc_gain, acc_yn1, acc_yn2;
    u16 acc_pred_scale, acc_input;
    bool acc_reads_stopped;
    /* para el hilo: el programa espera un mensaje (no tiene sentido seguir girando) */
    int idle_wait;
} DspState;

extern DspState g;

/* memoria y registros de hardware (dsp_nativo.c) */
u16 dsp_read_ifx(u16 address);
void dsp_write_ifx(u16 address, u16 value);

DSPI u16 dsp_read_imem(u16 a) {
    switch (a >> 12) {
    case 0: return g.iram[a & 0x0fff];
    case 8: return g.irom[a & 0x0fff];
    default: return 0;
    }
}
DSPI u16 dsp_read_dmem(u16 a) {
    switch (a >> 12) {
    case 0x0: return g.dram[a & 0x0fff];
    case 0x1: return g.coef[a & 0x07ff];
    case 0xf: return dsp_read_ifx(a);
    default: return 0;
    }
}
DSPI void dsp_write_dmem(u16 a, u16 v) {
    switch (a >> 12) {
    case 0x0: g.dram[a & 0x0fff] = v; break;
    case 0xf: dsp_write_ifx(a, v); break;
    default: break;
    }
}
#define ReadDMEM(a) dsp_read_dmem(a)
#define WriteDMEM(a, v) dsp_write_dmem(a, v)
#define ReadIMEM(a) dsp_read_imem(a)

DSPI void StoreStack(int s, u16 val) {
    g.reg_stack_ptrs[s] = (u8)((g.reg_stack_ptrs[s] + 1) & 31);
    g.reg_stacks[s][g.reg_stack_ptrs[s]] = g.r.st[s];
    g.r.st[s] = val;
}
DSPI u16 PopStack(int s) {
    const u16 val = g.r.st[s];
    g.r.st[s] = g.reg_stacks[s][g.reg_stack_ptrs[s]];
    g.reg_stack_ptrs[s] = (u8)((g.reg_stack_ptrs[s] - 1) & 31);
    return val;
}

DSPI void SetSRFlag(u16 f) { g.r.sr |= f; }
DSPI bool IsSRFlagSet(u16 f) { return (g.r.sr & f) != 0; }

DSPI bool CheckCondition(u8 c) {
    const bool carry = IsSRFlagSet(SR_CARRY), ovf = IsSRFlagSet(SR_OVERFLOW), over32 = IsSRFlagSet(SR_OVER_S32);
    const bool less = IsSRFlagSet(SR_OVERFLOW) != IsSRFlagSet(SR_SIGN), zero = IsSRFlagSet(SR_ARITH_ZERO);
    const bool lzero = IsSRFlagSet(SR_LOGIC_ZERO);
    const bool condb = (!(over32 || IsSRFlagSet(SR_TOP2BITS))) || zero;
    switch (c & 0xf) {
    case 0xf: return true;
    case 0x0: return !less;
    case 0x1: return less;
    case 0x2: return !less && !zero;
    case 0x3: return less || zero;
    case 0x4: return !zero;
    case 0x5: return zero;
    case 0x6: return !carry;
    case 0x7: return carry;
    case 0x8: return !over32;
    case 0x9: return over32;
    case 0xa: return !condb;
    case 0xb: return condb;
    case 0xc: return !lzero;
    case 0xd: return lzero;
    case 0xe: return ovf;
    default: return true;
    }
}

DSPI u16 IncrementAddressRegister(u16 reg) {
    const u32 ar = g.r.ar[reg], wr = g.r.wr[reg];
    u32 nar = ar + 1;
    if ((nar ^ ar) > ((wr | 1) << 1)) nar -= wr + 1;
    return (u16)nar;
}
DSPI u16 DecrementAddressRegister(u16 reg) {
    const u32 ar = g.r.ar[reg], wr = g.r.wr[reg];
    u32 nar = ar + wr;
    if (((nar ^ ar) & ((wr | 1) << 1)) > wr) nar -= wr + 1;
    return (u16)nar;
}
DSPI u16 IncreaseAddressRegister(u16 reg, s16 ix_) {
    const u32 ar = g.r.ar[reg], wr = g.r.wr[reg];
    const s32 ix = ix_;
    const u32 mx = (wr | 1) << 1;
    u32 nar = ar + (u32)ix;
    const u32 dar = (nar ^ ar ^ (u32)ix) & mx;
    if (ix >= 0) { if (dar > wr) nar -= wr + 1; }
    else { if ((((nar + wr + 1) ^ nar) & dar) <= wr) nar += wr + 1; }
    return (u16)nar;
}
DSPI u16 DecreaseAddressRegister(u16 reg, s16 ix_) {
    const u32 ar = g.r.ar[reg], wr = g.r.wr[reg];
    const s32 ix = ix_;
    const u32 mx = (wr | 1) << 1;
    u32 nar = ar - (u32)ix;
    const u32 dar = (nar ^ ar ^ ~(u32)ix) & mx;
    if ((u32)ix > 0xFFFF8000u) { if (dar > wr) nar -= wr + 1; }
    else { if ((((nar + wr + 1) ^ nar) & dar) <= wr) nar += wr + 1; }
    return (u16)nar;
}

DSPI s32 GetLongACX(s32 reg) { return (s32)(((u32)g.r.ax[reg].h << 16) | g.r.ax[reg].l); }
DSPI s16 GetAXLow(s32 reg) { return (s16)g.r.ax[reg].l; }
DSPI s16 GetAXHigh(s32 reg) { return (s16)g.r.ax[reg].h; }
DSPI s64 GetLongAcc(s32 reg) { return (s64)g.r.ac[reg].val; }
DSPI void SetLongAcc(s32 reg, s64 value) { g.r.ac[reg].val = (u64)((s64)((u64)value << 24) >> 24); }
DSPI s16 GetAccLow(s32 reg) { return (s16)g.r.ac[reg].l; }
DSPI s16 GetAccMid(s32 reg) { return (s16)g.r.ac[reg].m; }
DSPI s16 GetAccHigh(s32 reg) { return (s16)g.r.ac[reg].h; }

DSPI s64 GetLongProduct(void) {
    s64 val = (s8)(u8)g.r.prod.h;
    s64 low;
    val = (s64)((u64)val << 32);
    low = g.r.prod.m;
    low += g.r.prod.m2;
    low = (s64)((u64)low << 16);
    low |= g.r.prod.l;
    return val + low;
}
DSPI s64 GetLongProductRounded(void) {
    const s64 prod = GetLongProduct();
    if ((prod & 0x10000) != 0) return (prod + 0x8000) & ~(s64)0xffff;
    return (prod + 0x7fff) & ~(s64)0xffff;
}
DSPI void SetLongProduct(s64 value) { g.r.prod.val = (u64)value & 0x000000FFFFFFFFFFULL; }

DSPI s64 GetMultiplyProduct(u16 a, u16 b, u8 sign) {
    s64 prod;
    if (sign == 1 && IsSRFlagSet(SR_MUL_UNSIGNED)) prod = (u32)(a * b);
    else if (sign == 2 && IsSRFlagSet(SR_MUL_UNSIGNED)) prod = a * (s16)b;
    else prod = (s16)a * (s16)b;
    if (!IsSRFlagSet(SR_MUL_MODIFY)) prod = (s64)((u64)prod << 1);
    return prod;
}
DSPI s64 Multiply3(u16 a, u16 b, u8 sign) { return GetMultiplyProduct(a, b, sign); }
DSPI s64 MultiplyAdd3(u16 a, u16 b, u8 sign) { return GetLongProduct() + GetMultiplyProduct(a, b, sign); }
DSPI s64 MultiplySub3(u16 a, u16 b, u8 sign) { return GetLongProduct() - GetMultiplyProduct(a, b, sign); }
#define DSP_SEL3(_1, _2, _3, NAME, ...) NAME
#define Multiply(...) DSP_SEL3(__VA_ARGS__, Multiply3(__VA_ARGS__), Multiply3(__VA_ARGS__, 0), 0)
#define MultiplyAdd(...) DSP_SEL3(__VA_ARGS__, MultiplyAdd3(__VA_ARGS__), MultiplyAdd3(__VA_ARGS__, 0), 0)
#define MultiplySub(...) DSP_SEL3(__VA_ARGS__, MultiplySub3(__VA_ARGS__), MultiplySub3(__VA_ARGS__, 0), 0)
DSPI s64 MultiplyMulX(u8 axh0, u8 axh1, u16 val1, u16 val2) {
    if (axh0 == 0 && axh1 == 0) return Multiply3(val1, val2, 1);
    if (axh0 == 0 && axh1 == 1) return Multiply3(val1, val2, 2);
    if (axh0 == 1 && axh1 == 0) return Multiply3(val2, val1, 2);
    return Multiply3(val1, val2, 0);
}

DSPI bool isCarryAdd(u64 val, u64 result) { return val > result; }
DSPI bool isCarrySubtract(u64 val, u64 result) { return val >= result; }
DSPI bool isOverflow(s64 val1, s64 val2, s64 res) { return ((val1 ^ res) & (val2 ^ res)) < 0; }
DSPI bool isOverS32(s64 acc) { return acc != (s32)acc; }
DSPI s64 dsp_convert_long_acc(s64 val) { return (s64)((u64)val << 24) >> 24; }
DSPI s64 dsp_round_long_acc(s64 val) {
    if ((val & 0x10000) != 0) return (val + 0x8000) & ~(s64)0xffff;
    return (val + 0x7fff) & ~(s64)0xffff;
}

DSPI void UpdateSR16_4(s16 value, bool carry, bool overflow, bool over_s32) {
    g.r.sr &= ~SR_CMP_MASK;
    if (carry) g.r.sr |= SR_CARRY;
    if (overflow) g.r.sr |= SR_OVERFLOW | SR_OVERFLOW_STICKY;
    if (value == 0) g.r.sr |= SR_ARITH_ZERO;
    if (value < 0) g.r.sr |= SR_SIGN;
    if (over_s32) g.r.sr |= SR_OVER_S32;
    if ((((u16)value >> 14) == 0) || (((u16)value >> 14) == 3)) g.r.sr |= SR_TOP2BITS;
}
DSPI void UpdateSR64_3(s64 value, bool carry, bool overflow) {
    g.r.sr &= ~SR_CMP_MASK;
    if (carry) g.r.sr |= SR_CARRY;
    if (overflow) g.r.sr |= SR_OVERFLOW | SR_OVERFLOW_STICKY;
    if (value == 0) g.r.sr |= SR_ARITH_ZERO;
    if (value < 0) g.r.sr |= SR_SIGN;
    if (isOverS32(value)) g.r.sr |= SR_OVER_S32;
    if (((value & 0xc0000000) == 0) || ((value & 0xc0000000) == 0xc0000000)) g.r.sr |= SR_TOP2BITS;
}
#define DSP_SEL4(_1, _2, _3, _4, NAME, ...) NAME
/* "fl" = 0 cuando el traductor sabe que nadie lee estas banderas antes de que otra
   instruccion las pise (asi se ahorra calcularlas, como hace el JIT de Dolphin) */
#define UpdateSR16(...) if (fl) DSP_SEL4(__VA_ARGS__, UpdateSR16_4(__VA_ARGS__), UpdateSR16_4(__VA_ARGS__, false), \
                                 UpdateSR16_4(__VA_ARGS__, false, false), UpdateSR16_4(__VA_ARGS__, false, false, false), 0)
#define UpdateSR64(...) if (fl) DSP_SEL3(__VA_ARGS__, UpdateSR64_3(__VA_ARGS__), UpdateSR64_3(__VA_ARGS__, false), \
                                 UpdateSR64_3(__VA_ARGS__, false, false), 0)
DSPI void UpdateSR64Add_(s64 val1, s64 val2, s64 result) {
    UpdateSR64_3(result, isCarryAdd((u64)val1, (u64)result), isOverflow(val1, val2, result));
}
DSPI void UpdateSR64Sub_(s64 val1, s64 val2, s64 result) {
    UpdateSR64_3(result, isCarrySubtract((u64)val1, (u64)result), isOverflow(val1, -val2, result));
}
#define UpdateSR64Add(a, b, c) if (fl) UpdateSR64Add_(a, b, c)
#define UpdateSR64Sub(a, b, c) if (fl) UpdateSR64Sub_(a, b, c)
DSPI void UpdateSRLogicZero(bool value) {
    if (value) g.r.sr |= SR_LOGIC_ZERO;
    else g.r.sr &= ~SR_LOGIC_ZERO;
}

DSPI u16 OpReadRegister(int reg_) {
    const int reg = reg_ & 0x1f;
    switch (reg) {
    case DSP_REG_ST0: case DSP_REG_ST1: case DSP_REG_ST2: case DSP_REG_ST3: return PopStack(reg - DSP_REG_ST0);
    case DSP_REG_AR0: case DSP_REG_AR1: case DSP_REG_AR2: case DSP_REG_AR3: return g.r.ar[reg - DSP_REG_AR0];
    case DSP_REG_IX0: case DSP_REG_IX1: case DSP_REG_IX2: case DSP_REG_IX3: return g.r.ix[reg - DSP_REG_IX0];
    case DSP_REG_WR0: case DSP_REG_WR1: case DSP_REG_WR2: case DSP_REG_WR3: return g.r.wr[reg - DSP_REG_WR0];
    case DSP_REG_ACH0: case DSP_REG_ACH1: return (u16)g.r.ac[reg - DSP_REG_ACH0].h;
    case DSP_REG_CR: return g.r.cr;
    case DSP_REG_SR: return g.r.sr;
    case DSP_REG_PRODL: return g.r.prod.l;
    case DSP_REG_PRODM: return g.r.prod.m;
    case DSP_REG_PRODH: return g.r.prod.h;
    case DSP_REG_PRODM2: return g.r.prod.m2;
    case DSP_REG_AXL0: case DSP_REG_AXL1: return g.r.ax[reg - DSP_REG_AXL0].l;
    case DSP_REG_AXH0: case DSP_REG_AXH1: return g.r.ax[reg - DSP_REG_AXH0].h;
    case DSP_REG_ACL0: case DSP_REG_ACL1: return g.r.ac[reg - DSP_REG_ACL0].l;
    case DSP_REG_ACM0: case DSP_REG_ACM1:
        if (IsSRFlagSet(SR_40_MODE_BIT)) {
            const s64 acc = GetLongAcc(reg - DSP_REG_ACM0);
            if (acc != (s32)acc) return acc > 0 ? 0x7fff : 0x8000;
        }
        return g.r.ac[reg - DSP_REG_ACM0].m;
    default: return 0;
    }
}
DSPI void OpWriteRegister(int reg_, u16 val) {
    const int reg = reg_ & 0x1f;
    switch (reg) {
    case DSP_REG_ACH0: case DSP_REG_ACH1: g.r.ac[reg - DSP_REG_ACH0].h = (u32)(s32)(s8)val; break;
    case DSP_REG_ST0: case DSP_REG_ST1: case DSP_REG_ST2: case DSP_REG_ST3: StoreStack(reg - DSP_REG_ST0, val); break;
    case DSP_REG_AR0: case DSP_REG_AR1: case DSP_REG_AR2: case DSP_REG_AR3: g.r.ar[reg - DSP_REG_AR0] = val; break;
    case DSP_REG_IX0: case DSP_REG_IX1: case DSP_REG_IX2: case DSP_REG_IX3: g.r.ix[reg - DSP_REG_IX0] = val; break;
    case DSP_REG_WR0: case DSP_REG_WR1: case DSP_REG_WR2: case DSP_REG_WR3: g.r.wr[reg - DSP_REG_WR0] = val; break;
    case DSP_REG_CR: g.r.cr = val & 0x00ff; break;
    case DSP_REG_SR: g.r.sr = val & ~SR_100; break;
    case DSP_REG_PRODL: g.r.prod.l = val; break;
    case DSP_REG_PRODM: g.r.prod.m = val; break;
    case DSP_REG_PRODH: g.r.prod.h = val & 0x00ff; break;
    case DSP_REG_PRODM2: g.r.prod.m2 = val; break;
    case DSP_REG_AXL0: case DSP_REG_AXL1: g.r.ax[reg - DSP_REG_AXL0].l = val; break;
    case DSP_REG_AXH0: case DSP_REG_AXH1: g.r.ax[reg - DSP_REG_AXH0].h = val; break;
    case DSP_REG_ACL0: case DSP_REG_ACL1: g.r.ac[reg - DSP_REG_ACL0].l = val; break;
    case DSP_REG_ACM0: case DSP_REG_ACM1: g.r.ac[reg - DSP_REG_ACM0].m = val; break;
    }
}
DSPI void ConditionalExtendAccum(int reg) {
    if (reg != DSP_REG_ACM0 && reg != DSP_REG_ACM1) return;
    if (!IsSRFlagSet(SR_40_MODE_BIT)) return;
    {
        const u16 val = g.r.ac[reg - DSP_REG_ACM0].m;
        g.r.ac[reg - DSP_REG_ACM0].h = (val & 0x8000) != 0 ? 0xFFFFFFFFu : 0x0000;
        g.r.ac[reg - DSP_REG_ACM0].l = 0;
    }
}

/* Escrituras de las instrucciones extendidas: se anotan y se aplican despues de la
   instruccion principal (que lee los registros viejos), como hace Dolphin. Van en una
   estructura local de cada instruccion para que el compilador las resuelva. */
typedef struct { int idx[5]; u16 val[5]; } DspWB;
#define DSP_WB_INIT { { -1, -1, -1, -1, -1 }, { 0, 0, 0, 0, 0 } }
DSPI void dsp_wb_put(DspWB* wb, int i, int idx, u16 value) { wb->val[i] = value; wb->idx[i] = idx; }
DSPI void dsp_wb_apply(DspWB* wb) {
    int i;
    for (i = 0; i < 5 && wb->idx[i] != -1; i++) OpWriteRegister(wb->idx[i], wb->val[i]);
}
DSPI void ZeroWriteBackLog(void) {}
DSPI void ZeroWriteBackLogPreserveAcc(u8 acc) { (void)acc; }

DSPI void op_nop(u16 opc, u16 imm_, int fl) { (void)opc; (void)imm_; (void)fl; }

/* ciclo de los bucles por hardware (Interpreter::HandleLoop); devuelve 1 si salta al inicio */
DSPI int HandleLoop(u16 next_pc) {
    const u16 call_addr = g.r.st[0], loop_addr = g.r.st[2];
    if (loop_addr > 0 && g.r.st[3] > 0 && (u16)(next_pc - 1) == loop_addr) {
        g.r.st[3]--;
        if (g.r.st[3] > 0) { g.pc = call_addr; return 1; }
        PopStack(STACK_CALL); PopStack(STACK_LOOPADDR); PopStack(STACK_LOOPCNT);
    }
    g.pc = next_pc;
    return 0;
}

/* excepciones pendientes (SDSP::CheckExceptions); g.pc tiene que ser la proxima instruccion */
DSPI int CheckExceptions(void) {
    int i;
    if (!g.exceptions) return 0;
    for (i = 7; i > 0; i--) {
        if (g.exceptions & (1u << i)) {
            if (IsSRFlagSet(SR_INT_ENABLE) || i == EXC_EXTERNAL) {
                StoreStack(STACK_CALL, g.pc);
                StoreStack(STACK_DATA, g.r.sr);
                g.pc = (u16)(i * 2);
                g.exceptions &= (u8)~(1u << i);
                if (i == EXC_EXTERNAL) g.r.sr &= ~SR_EXT_INT_ENABLE;
                else g.r.sr &= ~SR_INT_ENABLE;
                return 1;
            }
        }
    }
    return 0;
}

#endif
