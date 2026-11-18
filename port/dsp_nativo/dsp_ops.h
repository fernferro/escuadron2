/*
 * DSP nativo: instrucciones del DSP (generado una vez desde el interprete de Dolphin,
 * Core/DSP/Interpreter/DSPInt*.cpp, GPL-2.0-or-later, Copyright 2008 Dolphin Emulator Project,
 * 2004 Duddie & Tratax; convertido de C++ a C para el codigo traducido del DSP).
 * Cada instruccion es una funcion op_nombre(opc, imm): opc es la instruccion e imm la
 * segunda palabra (si la tiene). Las extendidas anotan sus escrituras (WriteToBackLog).
 */
#ifndef DSP_OPS_H
#define DSP_OPS_H
#include "dsp_core.h"
/* ---- DSPIntArithmetic.cpp ---- */

// CLR $acR
// 1000 r001 xxxx xxxx
// Clears accumulator $acR
//
// flags out: --10 0100
DSPI void op_clr(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 11) & 0x1;

  SetLongAcc(reg, 0);
  UpdateSR64(0);
  ZeroWriteBackLog();
}

// CLRL $acR.l
// 1111 110r xxxx xxxx
// Clears (and rounds!) $acR.l - low 16 bits of accumulator $acR.
//
// flags out: --xx xx00
DSPI void op_clrl(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 8) & 0x1;
  const s64 acc = dsp_round_long_acc(GetLongAcc(reg));

  ZeroWriteBackLog();

  SetLongAcc(reg, acc);
  UpdateSR64(acc);
}

//----

// ANDCF $acD.m, #I
// 0000 001d 1100 0000
// iiii iiii iiii iiii
// Set logic zero (LZ) flag in status register $sr if result of logic AND of
// accumulator mid part $acD.m with immediate value I is equal to I.
//
// flags out: -x-- ----
DSPI void op_andcf(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = imm_;
  const u16 val = GetAccMid(reg);

  UpdateSRLogicZero((val & imm) == imm);
}

// ANDF $acD.m, #I
// 0000 001d 1010 0000
// iiii iiii iiii iiii
// Set logic zero (LZ) flag in status register $sr if result of logical AND
// operation of accumulator mid part $acD.m with immediate value I is equal
// to immediate value 0.
//
// flags out: -x-- ----
DSPI void op_andf(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = imm_;
  const u16 val = GetAccMid(reg);

  UpdateSRLogicZero((val & imm) == 0);
}

//----

// TST
// 1011 r001 xxxx xxxx
// Test accumulator $acR.
//
// flags out: --xx xx00
DSPI void op_tst(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 11) & 0x1;
  const s64 acc = GetLongAcc(reg);

  UpdateSR64(acc);
  ZeroWriteBackLog();
}

// TSTAXH $axR.h
// 1000 011r xxxx xxxx
// Test high part of secondary accumulator $axR.h.
//
// flags out: --x0 xx00
DSPI void op_tstaxh(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 8) & 0x1;
  const s16 val = GetAXHigh(reg);

  UpdateSR16(val);
  ZeroWriteBackLog();
}

//----

// CMP
// 1000 0010 xxxx xxxx
// Compares accumulator $ac0 with accumulator $ac1.
//
// flags out: x-xx xxxx
DSPI void op_cmp(u16 opc, u16 imm_, int fl)
{
  const s64 acc0 = GetLongAcc(0);
  const s64 acc1 = GetLongAcc(1);
  const s64 res = dsp_convert_long_acc(acc0 - acc1);

  UpdateSR64Sub(acc0, acc1, res);
  ZeroWriteBackLog();
}

// CMPAXH $acS, $axR.h
// 110r s001 xxxx xxxx
// Compares accumulator $acS with high part of secondary accumulator $axR.h.
//
// flags out: x-xx xxxx
DSPI void op_cmpaxh(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 12) & 0x1;
  const u8 sreg = (opc >> 11) & 0x1;

  const s64 acc = GetLongAcc(sreg);
  s64 ax = GetAXHigh(rreg);
  ax <<= 16;
  const s64 res = dsp_convert_long_acc(acc - ax);

  UpdateSR64Sub(acc, ax, res);
  ZeroWriteBackLog();
}

// CMPI $acD, #I
// 0000 001d 1000 0000
// iiii iiii iiii iiii
// Compares accumulator with immediate. Comparison is executed
// by subtracting the immediate (16-bit sign extended) from mid accumulator
// $acD.hm and computing flags based on whole accumulator $acD.
//
// flags out: x-xx xxxx
DSPI void op_cmpi(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 8) & 0x1;

  const s64 val = GetLongAcc(reg);
  // Immediate is considered to be at M level in the 40-bit accumulator.
  s64 imm = ((s16)(imm_));
  imm <<= 16;
  const s64 res = dsp_convert_long_acc(val - imm);

  UpdateSR64Sub(val, imm, res);
}

// CMPIS $acD, #I
// 0000 011d iiii iiii
// Compares accumulator with short immediate. Comparison is executed
// by subtracting the short immediate (8-bit sign extended) from mid accumulator
// $acD.hm and computing flags based on whole accumulator $acD.
//
// flags out: x-xx xxxx
DSPI void op_cmpis(u16 opc, u16 imm_, int fl)
{
  const u8 areg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(areg);
  s64 imm = ((s8)(opc));
  imm <<= 16;
  const s64 res = dsp_convert_long_acc(acc - imm);

  UpdateSR64Sub(acc, imm, res);
}

//----

// XORR $acD.m, $axS.h
// 0011 00sd 0xxx xxxx
// Logic XOR (exclusive or) middle part of accumulator $acD.m with
// high part of secondary accumulator $axS.h.
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_xorr(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const u16 accm = g.r.ac[dreg].m ^ g.r.ax[sreg].h;
  ZeroWriteBackLogPreserveAcc(dreg);

  g.r.ac[dreg].m = accm;
  UpdateSR16(((s16)(accm)), false, false, isOverS32(GetLongAcc(dreg)));
}

// ANDR $acD.m, $axS.h
// 0011 01sd 0xxx xxxx
// Logic AND middle part of accumulator $acD.m with high part of
// secondary accumulator $axS.h.
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_andr(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const u16 accm = g.r.ac[dreg].m & g.r.ax[sreg].h;
  ZeroWriteBackLogPreserveAcc(dreg);

  g.r.ac[dreg].m = accm;
  UpdateSR16(((s16)(accm)), false, false, isOverS32(GetLongAcc(dreg)));
}

// ORR $acD.m, $axS.h
// 0011 10sd 0xxx xxxx
// Logic OR middle part of accumulator $acD.m with high part of
// secondary accumulator $axS.h.
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_orr(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const u16 accm = g.r.ac[dreg].m | g.r.ax[sreg].h;
  ZeroWriteBackLogPreserveAcc(dreg);

  g.r.ac[dreg].m = accm;
  UpdateSR16(((s16)(accm)), false, false, isOverS32(GetLongAcc(dreg)));
}

// ANDC $acD.m, $ac(1-D).m
// 0011 110d 0xxx xxxx
// Logic AND middle part of accumulator $acD.m with middle part of
// accumulator $ac(1-D).m
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_andc(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u16 accm = g.r.ac[dreg].m & g.r.ac[1 - dreg].m;

  ZeroWriteBackLogPreserveAcc(dreg);

  g.r.ac[dreg].m = accm;
  UpdateSR16(((s16)(accm)), false, false, isOverS32(GetLongAcc(dreg)));
}

// ORC $acD.m, $ac(1-D).m
// 0011 111d 0xxx xxxx
// Logic OR middle part of accumulator $acD.m with middle part of
// accumulator $ac(1-D).m.
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_orc(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u16 accm = g.r.ac[dreg].m | g.r.ac[1 - dreg].m;

  ZeroWriteBackLogPreserveAcc(dreg);

  g.r.ac[dreg].m = accm;
  UpdateSR16(((s16)(accm)), false, false, isOverS32(GetLongAcc(dreg)));
}

// XORC $acD.m
// 0011 000d 1xxx xxxx
// Logic XOR (exclusive or) middle part of accumulator $acD.m with $ac(1-D).m
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_xorc(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u16 accm = g.r.ac[dreg].m ^ g.r.ac[1 - dreg].m;

  ZeroWriteBackLogPreserveAcc(dreg);

  g.r.ac[dreg].m = accm;
  UpdateSR16(((s16)(accm)), false, false, isOverS32(GetLongAcc(dreg)));
}

// NOT $acD.m
// 0011 001d 1xxx xxxx
// Invert all bits in dest reg, aka xor with 0xffff
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_notc(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u16 accm = g.r.ac[dreg].m ^ 0xffff;

  ZeroWriteBackLogPreserveAcc(dreg);

  g.r.ac[dreg].m = accm;
  UpdateSR16(((s16)(accm)), false, false, isOverS32(GetLongAcc(dreg)));
}

// XORI $acD.m, #I
// 0000 001d 0010 0000
// iiii iiii iiii iiii
// Logic exclusive or (XOR) of accumulator mid part $acD.m with
// immediate value I.
//
// flags out: --xx xx00
DSPI void op_xori(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = imm_;
  g.r.ac[reg].m ^= imm;

  UpdateSR16(((s16)(g.r.ac[reg].m)), false, false, isOverS32(GetLongAcc(reg)));
}

// ANDI $acD.m, #I
// 0000 001d 0100 0000
// iiii iiii iiii iiii
// Logic AND of accumulator mid part $acD.m with immediate value I.
//
// flags out: --xx xx00
DSPI void op_andi(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = imm_;

  g.r.ac[reg].m &= imm;

  UpdateSR16(((s16)(g.r.ac[reg].m)), false, false, isOverS32(GetLongAcc(reg)));
}

// ORI $acD.m, #I
// 0000 001d 0110 0000
// iiii iiii iiii iiii
// Logic OR of accumulator mid part $acD.m with immediate value I.
//
// flags out: --xx xx00
DSPI void op_ori(u16 opc, u16 imm_, int fl)
{
  const u8 reg = (opc >> 8) & 0x1;
  const u16 imm = imm_;

  g.r.ac[reg].m |= imm;

  UpdateSR16(((s16)(g.r.ac[reg].m)), false, false, isOverS32(GetLongAcc(reg)));
}

//----

// ADDR $acD.M, $axS.L
// 0100 0ssd xxxx xxxx
// Adds register $axS.L to accumulator $acD.M register.
//
// flags out: x-xx xxxx
DSPI void op_addr(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = ((opc >> 9) & 0x3) + DSP_REG_AXL0;

  const s64 acc = GetLongAcc(dreg);
  s64 ax = 0;

  switch (sreg)
  {
  case DSP_REG_AXL0:
  case DSP_REG_AXL1:
    ax = ((s16)(g.r.ax[sreg - DSP_REG_AXL0].l));
    break;
  case DSP_REG_AXH0:
  case DSP_REG_AXH1:
    ax = ((s16)(g.r.ax[sreg - DSP_REG_AXH0].h));
    break;
  default:
    ax = 0;
    break;
  }

  ax <<= 16;
  const s64 res = acc + ax;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Add(acc, ax, GetLongAcc(dreg));
}

// ADDAX $acD, $axS
// 0100 10sd xxxx xxxx
// Adds secondary accumulator $axS to accumulator register $acD.
//
// flags out: x-xx xxxx
DSPI void op_addax(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 ax = GetLongACX(sreg);
  const s64 res = acc + ax;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Add(acc, ax, GetLongAcc(dreg));
}

// ADD $acD, $ac(1-D)
// 0100 110d xxxx xxxx
// Adds accumulator $ac(1-D) to accumulator register $acD.
//
// flags out: x-xx xxxx
DSPI void op_add(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc0 = GetLongAcc(dreg);
  const s64 acc1 = GetLongAcc(1 - dreg);
  const s64 res = acc0 + acc1;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Add(acc0, acc1, GetLongAcc(dreg));
}

// ADDP $acD
// 0100 111d xxxx xxxx
// Adds product register to accumulator register.
//
// flags out: x-xx xxxx
DSPI void op_addp(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 prod = GetLongProduct();
  const s64 res = acc + prod;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Add(acc, prod, GetLongAcc(dreg));
}

// ADDAXL $acD, $axS.l
// 0111 00sd xxxx xxxx
// Adds secondary accumulator $axS.l to accumulator register $acD.
// should be unsigned values!!
//
// flags out: x-xx xxxx
DSPI void op_addaxl(u16 opc, u16 imm_, int fl)
{
  const u8 sreg = (opc >> 9) & 0x1;
  const u8 dreg = (opc >> 8) & 0x1;

  const u64 acc = GetLongAcc(dreg);
  const u16 acx = ((u16)(GetAXLow(sreg)));
  const u64 res = acc + acx;

  ZeroWriteBackLog();

  SetLongAcc(dreg, ((s64)(res)));
  UpdateSR64Add(acc, acx, GetLongAcc(dreg));
}

// ADDI $acD, #I
// 0000 001d 0000 0000
// iiii iiii iiii iiii
// Adds immediate (16-bit sign extended) to mid accumulator $acD.hm.
//
// flags out: x-xx xxxx
DSPI void op_addi(u16 opc, u16 imm_, int fl)
{
  const u8 areg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(areg);
  s64 imm = ((s16)(imm_));
  imm <<= 16;
  const s64 res = acc + imm;

  SetLongAcc(areg, res);
  UpdateSR64Add(acc, imm, GetLongAcc(areg));
}

// ADDIS $acD, #I
// 0000 010d iiii iiii
// Adds short immediate (8-bit sign extended) to mid accumulator $acD.hm.
//
// flags out: x-xx xxxx
DSPI void op_addis(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  s64 imm = ((s8)(opc));
  imm <<= 16;
  const s64 res = acc + imm;

  SetLongAcc(dreg, res);
  UpdateSR64Add(acc, imm, GetLongAcc(dreg));
}

// INCM $acsD
// 0111 010d xxxx xxxx
// Increment 24-bit mid-accumulator $acsD.
//
// flags out: x-xx xxxx
DSPI void op_incm(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 sub = 0x10000;
  const s64 acc = GetLongAcc(dreg);
  const s64 res = acc + sub;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Add(acc, sub, GetLongAcc(dreg));
}

// INC $acD
// 0111 011d xxxx xxxx
// Increment accumulator $acD.
//
// flags out: x-xx xxxx
DSPI void op_inc(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 res = acc + 1;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Add(acc, 1, GetLongAcc(dreg));
}

//----

// SUBR $acD.M, $axS.L
// 0101 0ssd xxxx xxxx
// Subtracts register $axS.L from accumulator $acD.M register.
//
// flags out: x-xx xxxx
DSPI void op_subr(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = ((opc >> 9) & 0x3) + DSP_REG_AXL0;

  const s64 acc = GetLongAcc(dreg);
  s64 ax = 0;

  switch (sreg)
  {
  case DSP_REG_AXL0:
  case DSP_REG_AXL1:
    ax = ((s16)(g.r.ax[sreg - DSP_REG_AXL0].l));
    break;
  case DSP_REG_AXH0:
  case DSP_REG_AXH1:
    ax = ((s16)(g.r.ax[sreg - DSP_REG_AXH0].h));
    break;
  default:
    ax = 0;
    break;
  }

  ax <<= 16;
  const s64 res = acc - ax;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Sub(acc, ax, GetLongAcc(dreg));
}

// SUBAX $acD, $axS
// 0101 10sd xxxx xxxx
// Subtracts secondary accumulator $axS from accumulator register $acD.
//
// flags out: x-xx xxxx
DSPI void op_subax(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 acx = GetLongACX(sreg);
  const s64 res = acc - acx;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Sub(acc, acx, GetLongAcc(dreg));
}

// SUB $acD, $ac(1-D)
// 0101 110d xxxx xxxx
// Subtracts accumulator $ac(1-D) from accumulator register $acD.
//
// flags out: x-xx xxxx
DSPI void op_sub(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc1 = GetLongAcc(dreg);
  const s64 acc2 = GetLongAcc(1 - dreg);
  const s64 res = acc1 - acc2;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Sub(acc1, acc2, GetLongAcc(dreg));
}

// SUBP $acD
// 0101 111d xxxx xxxx
// Subtracts product register from accumulator register.
//
// flags out: x-xx xxxx
DSPI void op_subp(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 prod = GetLongProduct();
  const s64 res = acc - prod;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Sub(acc, prod, GetLongAcc(dreg));
}

// DECM $acsD
// 0111 100d xxxx xxxx
// Decrement 24-bit mid-accumulator $acsD.
//
// flags out: x-xx xxxx
DSPI void op_decm(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x01;

  const s64 sub = 0x10000;
  const s64 acc = GetLongAcc(dreg);
  const s64 res = acc - sub;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Sub(acc, sub, GetLongAcc(dreg));
}

// DEC $acD
// 0111 101d xxxx xxxx
// Decrement accumulator $acD.
//
// flags out: x-xx xxxx
DSPI void op_dec(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x01;

  const s64 acc = GetLongAcc(dreg);
  const s64 res = acc - 1;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Sub(acc, 1, GetLongAcc(dreg));
}

//----

// NEG $acD
// 0111 110d xxxx xxxx
// Negate accumulator $acD.
//
// flags out: x-xx xxxx
//
// The carry flag is set only if $acD was zero.
// The overflow flag is set only if $acD was 0x8000000000 (the minimum value),
// as -INT_MIN is INT_MIN in two's complement. In both of these cases,
// the value of $acD after the operation is the same as it was before.
DSPI void op_neg(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  const s64 acc = GetLongAcc(dreg);
  const s64 res = 0 - acc;

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  UpdateSR64Sub(0, acc, GetLongAcc(dreg));
}

// ABS  $acD
// 1010 d001 xxxx xxxx
// absolute value of $acD
//
// flags out: --xx xx00
DSPI void op_abs(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 11) & 0x1;

  s64 acc = GetLongAcc(dreg);

  if (acc < 0)
    acc = 0 - acc;

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(GetLongAcc(dreg));  // TODO: Is this right?
}
//----

// MOVR $acD, $axS.R
// 0110 0srd xxxx xxxx
// Moves register $axS.R (sign extended) to middle accumulator $acD.hm.
// Sets $acD.l to 0.
//
// flags out: --xx xx00
DSPI void op_movr(u16 opc, u16 imm_, int fl)
{
  const u8 areg = (opc >> 8) & 0x1;
  const u8 sreg = ((opc >> 9) & 0x3) + DSP_REG_AXL0;

  s64 ax = 0;
  switch (sreg)
  {
  case DSP_REG_AXL0:
  case DSP_REG_AXL1:
    ax = ((s16)(g.r.ax[sreg - DSP_REG_AXL0].l));
    break;
  case DSP_REG_AXH0:
  case DSP_REG_AXH1:
    ax = ((s16)(g.r.ax[sreg - DSP_REG_AXH0].h));
    break;
  default:
    ax = 0;
    break;
  }
  ax <<= 16;

  ZeroWriteBackLog();

  SetLongAcc(areg, ax);
  UpdateSR64(ax);
}

// MOVAX $acD, $axS
// 0110 10sd xxxx xxxx
// Moves secondary accumulator $axS to accumulator $acD.
//
// flags out: --xx xx00
DSPI void op_movax(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const s64 acx = GetLongACX(sreg);

  ZeroWriteBackLog();

  SetLongAcc(dreg, acx);
  UpdateSR64(acx);
}

// MOV $acD, $ac(1-D)
// 0110 110d xxxx xxxx
// Moves accumulator $ac(1-D) to accumulator $acD.
//
// flags out: --x0 xx00
DSPI void op_mov(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u64 acc = GetLongAcc(1 - dreg);

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(acc);
}

//----

// LSL16 $acR
// 1111 000r xxxx xxxx
// Logically shifts left accumulator $acR by 16.
//
// flags out: --xx xx00
DSPI void op_lsl16(u16 opc, u16 imm_, int fl)
{
  const u8 areg = (opc >> 8) & 0x1;

  s64 acc = GetLongAcc(areg);
  acc <<= 16;

  ZeroWriteBackLog();

  SetLongAcc(areg, acc);
  UpdateSR64(GetLongAcc(areg));
}

// LSR16 $acR
// 1111 010r xxxx xxxx
// Logically shifts right accumulator $acR by 16.
//
// flags out: --xx xx00
DSPI void op_lsr16(u16 opc, u16 imm_, int fl)
{
  const u8 areg = (opc >> 8) & 0x1;

  u64 acc = GetLongAcc(areg);
  // Lop off the extraneous sign extension our 64-bit fake accum causes
  acc &= 0x000000FFFFFFFFFFULL;
  acc >>= 16;

  ZeroWriteBackLog();

  SetLongAcc(areg, ((s64)(acc)));
  UpdateSR64(GetLongAcc(areg));
}

// ASR16 $acR
// 1001 r001 xxxx xxxx
// Arithmetically shifts right accumulator $acR by 16.
//
// flags out: --xx xx00
DSPI void op_asr16(u16 opc, u16 imm_, int fl)
{
  const u8 areg = (opc >> 11) & 0x1;

  s64 acc = GetLongAcc(areg);
  acc >>= 16;

  ZeroWriteBackLog();

  SetLongAcc(areg, acc);
  UpdateSR64(GetLongAcc(areg));
}

// LSL $acR, #I
// 0001 010r 00ii iiii
// Logically shifts left accumulator $acR by number specified by value I.
//
// flags out: --xx xx00
DSPI void op_lsl(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x01;
  const u16 shift = opc & 0x3f;
  u64 acc = GetLongAcc(rreg);

  acc <<= shift;

  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// LSR $acR, #I
// 0001 010r 01ii iiii
// Logically shifts right accumulator $acR by number specified by value
// calculated by negating sign extended bits 0-6.
//
// flags out: --xx xx00
DSPI void op_lsr(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x01;
  u16 shift;
  u64 acc = GetLongAcc(rreg);
  // Lop off the extraneous sign extension our 64-bit fake accum causes
  acc &= 0x000000FFFFFFFFFFULL;

  if ((opc & 0x3f) == 0)
    shift = 0;
  else
    shift = 0x40 - (opc & 0x3f);

  acc >>= shift;

  SetLongAcc(rreg, ((s64)(acc)));
  UpdateSR64(GetLongAcc(rreg));
}

// ASL $acR, #I
// 0001 010r 10ii iiii
// Logically shifts left accumulator $acR by number specified by value I.
//
// flags out: --xx xx00
DSPI void op_asl(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x01;
  const u16 shift = opc & 0x3f;
  u64 acc = GetLongAcc(rreg);

  acc <<= shift;

  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// ASR $acR, #I
// 0001 010r 11ii iiii
// Arithmetically shifts right accumulator $acR by number specified by
// value calculated by negating sign extended bits 0-6.
//
// flags out: --xx xx00
DSPI void op_asr(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x01;
  u16 shift;

  if ((opc & 0x3f) == 0)
    shift = 0;
  else
    shift = 0x40 - (opc & 0x3f);

  // arithmetic shift
  s64 acc = GetLongAcc(dreg);
  acc >>= shift;

  SetLongAcc(dreg, acc);
  UpdateSR64(GetLongAcc(dreg));
}

// LSRN  (fixed parameters)
// 0000 0010 1100 1010
// Logically shifts right accumulator $ACC0 by lower 7-bit (signed) value in $AC1.M
// (if value negative, becomes left shift).
//
// flags out: --xx xx00
DSPI void op_lsrn(u16 opc, u16 imm_, int fl)
{
  s16 shift;
  const u16 accm = ((u16)(GetAccMid(1)));
  u64 acc = GetLongAcc(0);
  acc &= 0x000000FFFFFFFFFFULL;

  if ((accm & 0x3f) == 0)
    shift = 0;
  else if ((accm & 0x40) != 0)
    shift = -0x40 + (accm & 0x3f);
  else
    shift = accm & 0x3f;

  if (shift > 0)
  {
    acc >>= shift;
  }
  else if (shift < 0)
  {
    acc <<= -shift;
  }

  SetLongAcc(0, ((s64)(acc)));
  UpdateSR64(GetLongAcc(0));
}

// ASRN  (fixed parameters)
// 0000 0010 1100 1011
// Arithmetically shifts right accumulator $ACC0 by lower 7-bit (signed) value in $AC1.M
// (if value negative, becomes left shift).
//
// flags out: --xx xx00
DSPI void op_asrn(u16 opc, u16 imm_, int fl)
{
  s16 shift;
  const u16 accm = ((u16)(GetAccMid(1)));
  s64 acc = GetLongAcc(0);

  if ((accm & 0x3f) == 0)
    shift = 0;
  else if ((accm & 0x40) != 0)
    shift = -0x40 + (accm & 0x3f);
  else
    shift = accm & 0x3f;

  if (shift > 0)
  {
    acc >>= shift;
  }
  else if (shift < 0)
  {
    acc <<= -shift;
  }

  SetLongAcc(0, acc);
  UpdateSR64(GetLongAcc(0));
}

// LSRNRX $acD, $axS.h
// 0011 01sd 1xxx xxxx
// Logically shifts left/right accumulator $ACC[D] by lower 7-bit (signed) value in $AX[S].H
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_lsrnrx(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  s16 shift;
  const u16 axh = g.r.ax[sreg].h;
  u64 acc = GetLongAcc(dreg);
  acc &= 0x000000FFFFFFFFFFULL;

  if ((axh & 0x3f) == 0)
    shift = 0;
  else if ((axh & 0x40) != 0)
    shift = -0x40 + (axh & 0x3f);
  else
    shift = axh & 0x3f;

  if (shift > 0)
  {
    acc <<= shift;
  }
  else if (shift < 0)
  {
    acc >>= -shift;
  }

  ZeroWriteBackLog();

  SetLongAcc(dreg, ((s64)(acc)));
  UpdateSR64(GetLongAcc(dreg));
}

// ASRNRX $acD, $axS.h
// 0011 10sd 1xxx xxxx
// Arithmetically shifts left/right accumulator $ACC[D] by lower 7-bit (signed) value in $AX[S].H
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_asrnrx(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  s16 shift;
  const u16 axh = g.r.ax[sreg].h;
  s64 acc = GetLongAcc(dreg);

  if ((axh & 0x3f) == 0)
    shift = 0;
  else if ((axh & 0x40) != 0)
    shift = -0x40 + (axh & 0x3f);
  else
    shift = axh & 0x3f;

  if (shift > 0)
  {
    acc <<= shift;
  }
  else if (shift < 0)
  {
    acc >>= -shift;
  }

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(GetLongAcc(dreg));
}

// LSRNR  $acD
// 0011 110d 1xxx xxxx
// Logically shifts left/right accumulator $ACC[D] by lower 7-bit (signed) value in $AC[1-D].M
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_lsrnr(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  s16 shift;
  const u16 accm = ((u16)(GetAccMid(1 - dreg)));
  u64 acc = GetLongAcc(dreg);
  acc &= 0x000000FFFFFFFFFFULL;

  if ((accm & 0x3f) == 0)
    shift = 0;
  else if ((accm & 0x40) != 0)
    shift = -0x40 + (accm & 0x3f);
  else
    shift = accm & 0x3f;

  if (shift > 0)
    acc <<= shift;
  else if (shift < 0)
    acc >>= -shift;

  ZeroWriteBackLog();

  SetLongAcc(dreg, ((s64)(acc)));
  UpdateSR64(GetLongAcc(dreg));
}

// ASRNR  $acD
// 0011 111d 1xxx xxxx
// Arithmetically shift left/right accumulator $ACC[D] by lower 7-bit (signed) value in $AC[1-D].M
// x = extension (7 bits!!)
//
// flags out: --xx xx00
DSPI void op_asrnr(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;

  s16 shift;
  const u16 accm = ((u16)(GetAccMid(1 - dreg)));
  s64 acc = GetLongAcc(dreg);

  if ((accm & 0x3f) == 0)
    shift = 0;
  else if ((accm & 0x40) != 0)
    shift = -0x40 + (accm & 0x3f);
  else
    shift = accm & 0x3f;

  if (shift > 0)
    acc <<= shift;
  else if (shift < 0)
    acc >>= -shift;

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(GetLongAcc(dreg));
}

/* ---- DSPIntMultiplier.cpp ---- */

// CLRP
// 1000 0100 xxxx xxxx
// Clears product register $prod.
// Magic numbers taken from duddie's doc
//
// 00ff_(fff0 + 0010)_0000 = 0100_0000_0000, conveniently, lower 40bits = 0
//
// It's not ok, to just zero all of them, correct values should be set because of
// direct use of prod regs by AX/AXWII (look @that part of ucode).
DSPI void op_clrp(u16 opc, u16 imm_, int fl)
{
  ZeroWriteBackLog();
  g.r.prod.l = 0x0000;
  g.r.prod.m = 0xfff0;
  g.r.prod.h = 0x00ff;
  g.r.prod.m2 = 0x0010;
}

// TSTPROD
// 1000 0101 xxxx xxxx
// Test prod regs value.
//
// flags out: --xx xx0x
DSPI void op_tstprod(u16 opc, u16 imm_, int fl)
{
  const s64 prod = GetLongProduct();
  UpdateSR64(prod);
  ZeroWriteBackLog();
}

//----

// MOVP $acD
// 0110 111d xxxx xxxx
// Moves multiply product from $prod register to accumulator $acD register.
//
// flags out: --xx xx0x
DSPI void op_movp(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const s64 acc = GetLongProduct();

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(acc);
}

// MOVNP $acD
// 0111 111d xxxx xxxx
// Moves negative of multiply product from $prod register to accumulator
// $acD register.
//
// flags out: --xx xx0x
DSPI void op_movnp(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const s64 acc = -GetLongProduct();

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(acc);
}

// MOVPZ $acD
// 1111 111d xxxx xxxx
// Moves multiply product from $prod register to accumulator $acD
// register and sets (rounds) $acD.l to 0
//
// flags out: --xx xx0x
DSPI void op_movpz(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x01;
  const s64 acc = GetLongProductRounded();

  ZeroWriteBackLog();

  SetLongAcc(dreg, acc);
  UpdateSR64(acc);
}

// ADDPAXZ $acD, $axS
// 1111 10sd xxxx xxxx
// Adds secondary accumulator $axS to product register and stores result
// in accumulator register. Low 16-bits of $acD ($acD.l) are set (round) to 0.
//
// TODO: ugly code and still small error here (+/- 1 in .m - randomly)
// flags out: --xx xx0x
DSPI void op_addpaxz(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const s64 oldprod = GetLongProduct();
  const s64 prod = GetLongProductRounded();
  const s64 ax = GetLongACX(sreg);
  s64 res = prod + (ax & ~0xffff);

  ZeroWriteBackLog();

  SetLongAcc(dreg, res);
  res = GetLongAcc(dreg);
  UpdateSR64(res, isCarryAdd(oldprod, res), false);  // TODO: Why doesn't this set the overflow bit?
}

//----

// MULAXH
// 1000 0011 xxxx xxxx
// Multiply $ax0.h by $ax0.h
DSPI void op_mulaxh(u16 opc, u16 imm_, int fl)
{
  const s16 value = GetAXHigh(0);
  const s64 prod = Multiply(value, value);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

//----

// MUL $axS.l, $axS.h
// 1001 s000 xxxx xxxx
// Multiply low part $axS.l of secondary accumulator $axS by high part
// $axS.h of secondary accumulator $axS (treat them both as signed).
DSPI void op_mul(u16 opc, u16 imm_, int fl)
{
  const u8 sreg = (opc >> 11) & 0x1;
  const u16 axl = GetAXLow(sreg);
  const u16 axh = GetAXHigh(sreg);
  const s64 prod = Multiply(axh, axl);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

// MULAC $axS.l, $axS.h, $acR
// 1001 s10r xxxx xxxx
// Add product register to accumulator register $acR. Multiply low part
// $axS.l of secondary accumulator $axS by high part $axS.h of secondary
// accumulator $axS (treat them both as signed).
//
// flags out: --xx xx0x
DSPI void op_mulac(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 11) & 0x1;

  const s64 acc = GetLongAcc(rreg) + GetLongProduct();
  const u16 axl = GetAXLow(sreg);
  const u16 axh = GetAXHigh(sreg);
  const s64 prod = Multiply(axl, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// MULMV $axS.l, $axS.h, $acR
// 1001 s11r xxxx xxxx
// Move product register to accumulator register $acR. Multiply low part
// $axS.l of secondary accumulator $axS by high part $axS.h of secondary
// accumulator $axS (treat them both as signed).
//
// flags out: --xx xx0x
DSPI void op_mulmv(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x1;
  const u8 sreg = ((opc >> 11) & 0x1);

  const s64 acc = GetLongProduct();
  const u16 axl = GetAXLow(sreg);
  const u16 axh = GetAXHigh(sreg);
  const s64 prod = Multiply(axl, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// MULMVZ $axS.l, $axS.h, $acR
// 1001 s01r xxxx xxxx
// Move product register to accumulator register $acR and clear (round) low part
// of accumulator register $acR.l. Multiply low part $axS.l of secondary
// accumulator $axS by high part $axS.h of secondary accumulator $axS (treat
// them both as signed).
//
// flags out: --xx xx0x
DSPI void op_mulmvz(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 11) & 0x1;

  const s64 acc = GetLongProductRounded();
  const u16 axl = GetAXLow(sreg);
  const u16 axh = GetAXHigh(sreg);
  const s64 prod = Multiply(axl, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

//----

// MULX $ax0.S, $ax1.T
// 101s t000 xxxx xxxx
// Multiply one part $ax0 by one part $ax1.
// Part is selected by S and T bits. Zero selects low part, one selects high part.
DSPI void op_mulx(u16 opc, u16 imm_, int fl)
{
  const u8 treg = ((opc >> 11) & 0x1);
  const u8 sreg = ((opc >> 12) & 0x1);

  const u16 val1 = (sreg == 0) ? GetAXLow(0) : GetAXHigh(0);
  const u16 val2 = (treg == 0) ? GetAXLow(1) : GetAXHigh(1);
  const s64 prod = MultiplyMulX(sreg, treg, val1, val2);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

// MULXAC $ax0.S, $ax1.T, $acR
// 101s t10r xxxx xxxx
// Add product register to accumulator register $acR. Multiply one part
// $ax0 by one part $ax1. Part is selected by S and
// T bits. Zero selects low part, one selects high part.
//
// flags out: --xx xx0x
DSPI void op_mulxac(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x1;
  const u8 treg = (opc >> 11) & 0x1;
  const u8 sreg = (opc >> 12) & 0x1;

  const s64 acc = GetLongAcc(rreg) + GetLongProduct();
  const u16 val1 = (sreg == 0) ? GetAXLow(0) : GetAXHigh(0);
  const u16 val2 = (treg == 0) ? GetAXLow(1) : GetAXHigh(1);
  const s64 prod = MultiplyMulX(sreg, treg, val1, val2);

  ZeroWriteBackLog();

  SetLongProduct(prod);
  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// MULXMV $ax0.S, $ax1.T, $acR
// 101s t11r xxxx xxxx
// Move product register to accumulator register $acR. Multiply one part
// $ax0 by one part $ax1. Part is selected by S and
// T bits. Zero selects low part, one selects high part.
//
// flags out: --xx xx0x
DSPI void op_mulxmv(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = ((opc >> 8) & 0x1);
  const u8 treg = (opc >> 11) & 0x1;
  const u8 sreg = (opc >> 12) & 0x1;

  const s64 acc = GetLongProduct();
  const u16 val1 = (sreg == 0) ? GetAXLow(0) : GetAXHigh(0);
  const u16 val2 = (treg == 0) ? GetAXLow(1) : GetAXHigh(1);
  const s64 prod = MultiplyMulX(sreg, treg, val1, val2);

  ZeroWriteBackLog();

  SetLongProduct(prod);
  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// MULXMVZ $ax0.S, $ax1.T, $acR
// 101s t01r xxxx xxxx
// Move product register to accumulator register $acR and clear (round) low part
// of accumulator register $acR.l. Multiply one part $ax0 by one part $ax1
// Part is selected by S and T bits. Zero selects low part,
// one selects high part.
//
// flags out: --xx xx0x
DSPI void op_mulxmvz(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x1;
  const u8 treg = (opc >> 11) & 0x1;
  const u8 sreg = (opc >> 12) & 0x1;

  const s64 acc = GetLongProductRounded();
  const u16 val1 = (sreg == 0) ? GetAXLow(0) : GetAXHigh(0);
  const u16 val2 = (treg == 0) ? GetAXLow(1) : GetAXHigh(1);
  const s64 prod = MultiplyMulX(sreg, treg, val1, val2);

  ZeroWriteBackLog();

  SetLongProduct(prod);
  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

//----

// MULC $acS.m, $axT.h
// 110s t000 xxxx xxxx
// Multiply mid part of accumulator register $acS.m by high part $axS.h of
// secondary accumulator $axS (treat them both as signed).
DSPI void op_mulc(u16 opc, u16 imm_, int fl)
{
  const u8 treg = (opc >> 11) & 0x1;
  const u8 sreg = (opc >> 12) & 0x1;

  const u16 accm = GetAccMid(sreg);
  const u16 axh = GetAXHigh(treg);
  const s64 prod = Multiply(accm, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

// MULCAC $acS.m, $axT.h, $acR
// 110s t10r xxxx xxxx
// Multiply mid part of accumulator register $acS.m by high part $axS.h of
// secondary accumulator $axS  (treat them both as signed). Add product
// register before multiplication to accumulator $acR.
//
// flags out: --xx xx0x
DSPI void op_mulcac(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x1;
  const u8 treg = (opc >> 11) & 0x1;
  const u8 sreg = (opc >> 12) & 0x1;

  const s64 acc = GetLongAcc(rreg) + GetLongProduct();
  const u16 accm = GetAccMid(sreg);
  const u16 axh = GetAXHigh(treg);
  const s64 prod = Multiply(accm, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// MULCMV $acS.m, $axT.h, $acR
// 110s t11r xxxx xxxx
// Multiply mid part of accumulator register $acS.m by high part $axT.h of
// secondary accumulator $axT  (treat them both as signed). Move product
// register before multiplication to accumulator $acR.
//
// flags out: --xx xx0x
DSPI void op_mulcmv(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x1;
  const u8 treg = (opc >> 11) & 0x1;
  const u8 sreg = (opc >> 12) & 0x1;

  const s64 acc = GetLongProduct();
  const u16 accm = GetAccMid(sreg);
  const u16 axh = GetAXHigh(treg);
  const s64 prod = Multiply(accm, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

// MULCMVZ $acS.m, $axT.h, $acR
// 110s t01r xxxx xxxx
// Multiply mid part of accumulator register $acS.m by high part $axT.h of
// secondary accumulator $axT  (treat them both as signed). Move product
// register before multiplication to accumulator $acR, set (round) low part of
// accumulator $acR.l to zero.
//
// flags out: --xx xx0x
DSPI void op_mulcmvz(u16 opc, u16 imm_, int fl)
{
  const u8 rreg = (opc >> 8) & 0x1;
  const u8 treg = (opc >> 11) & 0x1;
  const u8 sreg = (opc >> 12) & 0x1;

  const s64 acc = GetLongProductRounded();
  const u16 accm = GetAccMid(sreg);
  const u16 axh = GetAXHigh(treg);
  const s64 prod = Multiply(accm, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
  SetLongAcc(rreg, acc);
  UpdateSR64(GetLongAcc(rreg));
}

//----

// MADDX ax0.S ax1.T
// 1110 00st xxxx xxxx
// Multiply one part of secondary accumulator $ax0 (selected by S) by
// one part of secondary accumulator $ax1 (selected by T) (treat them both as
// signed) and add result to product register.
DSPI void op_maddx(u16 opc, u16 imm_, int fl)
{
  const u8 treg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const u16 val1 = (sreg == 0) ? GetAXLow(0) : GetAXHigh(0);
  const u16 val2 = (treg == 0) ? GetAXLow(1) : GetAXHigh(1);
  const s64 prod = MultiplyAdd(val1, val2);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

// MSUBX $(0x18+S*2), $(0x19+T*2)
// 1110 01st xxxx xxxx
// Multiply one part of secondary accumulator $ax0 (selected by S) by
// one part of secondary accumulator $ax1 (selected by T) (treat them both as
// signed) and subtract result from product register.
DSPI void op_msubx(u16 opc, u16 imm_, int fl)
{
  const u8 treg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const u16 val1 = (sreg == 0) ? GetAXLow(0) : GetAXHigh(0);
  const u16 val2 = (treg == 0) ? GetAXLow(1) : GetAXHigh(1);
  const s64 prod = MultiplySub(val1, val2);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

// MADDC $acS.m, $axT.h
// 1110 10st xxxx xxxx
// Multiply middle part of accumulator $acS.m by high part of secondary
// accumulator $axT.h (treat them both as signed) and add result to product
// register.
DSPI void op_maddc(u16 opc, u16 imm_, int fl)
{
  const u8 treg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const u16 accm = GetAccMid(sreg);
  const u16 axh = GetAXHigh(treg);
  const s64 prod = MultiplyAdd(accm, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

// MSUBC $acS.m, $axT.h
// 1110 11st xxxx xxxx
// Multiply middle part of accumulator $acS.m by high part of secondary
// accumulator $axT.h (treat them both as signed) and subtract result from
// product register.
DSPI void op_msubc(u16 opc, u16 imm_, int fl)
{
  const u8 treg = (opc >> 8) & 0x1;
  const u8 sreg = (opc >> 9) & 0x1;

  const u16 accm = GetAccMid(sreg);
  const u16 axh = GetAXHigh(treg);
  const s64 prod = MultiplySub(accm, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

// MADD $axS.l, $axS.h
// 1111 001s xxxx xxxx
// Multiply low part $axS.l of secondary accumulator $axS by high part
// $axS.h of secondary accumulator $axS (treat them both as signed) and add
// result to product register.
DSPI void op_madd(u16 opc, u16 imm_, int fl)
{
  const u8 sreg = (opc >> 8) & 0x1;
  const u16 axl = GetAXLow(sreg);
  const u16 axh = GetAXHigh(sreg);
  const s64 prod = MultiplyAdd(axl, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

// MSUB $axS.l, $axS.h
// 1111 011s xxxx xxxx
// Multiply low part $axS.l of secondary accumulator $axS by high part
// $axS.h of secondary accumulator $axS (treat them both as signed) and
// subtract result from product register.
DSPI void op_msub(u16 opc, u16 imm_, int fl)
{
  const u8 sreg = (opc >> 8) & 0x1;
  const u16 axl = GetAXLow(sreg);
  const u16 axh = GetAXHigh(sreg);
  const s64 prod = MultiplySub(axl, axh);

  ZeroWriteBackLog();

  SetLongProduct(prod);
}

/* ---- DSPIntLoadStore.cpp ---- */

// SRSH @M, $acS.h
// 0010 10ss mmmm mmmm
// Move value from register $acS.h to data memory pointed by address
// CR[0-7] | M. That is, the upper 8 bits of the address are the
// bottom 8 bits from CR, and the lower 8 bits are from the 8-bit immediate.
DSPI void op_srsh(u16 opc, u16 imm_, int fl)
{
  const int reg = ((u8)(((opc >> 8) & 0x1) + DSP_REG_ACH0));
  const int addr = ((u16)((g.r.cr << 8) | (opc & 0xFF)));

  WriteDMEM(addr, OpReadRegister(reg));
}

// SRS @M, $(0x1C+S)
// 0010 11ss mmmm mmmm
// Move value from register $(0x1C+S) to data memory pointed by address
// CR[0-7] | M. That is, the upper 8 bits of the address are the
// bottom 8 bits from CR, and the lower 8 bits are from the 8-bit immediate.
DSPI void op_srs(u16 opc, u16 imm_, int fl)
{
  const int reg = ((u8)(((opc >> 8) & 0x3) + DSP_REG_ACL0));
  const int addr = ((u16)((g.r.cr << 8) | (opc & 0xFF)));

  WriteDMEM(addr, OpReadRegister(reg));
}

// LRS $(0x18+D), @M
// 0010 0ddd mmmm mmmm
// Move value from data memory pointed by address CR[0-7] | M to register
// $(0x18+D).  That is, the upper 8 bits of the address are the bottom 8 bits
// from CR, and the lower 8 bits are from the 8-bit immediate.
DSPI void op_lrs(u16 opc, u16 imm_, int fl)
{
  const int reg = ((u8)(((opc >> 8) & 0x7) + 0x18));
  const int addr = ((u16)((g.r.cr << 8) | (opc & 0xFF)));

  OpWriteRegister(reg, ReadDMEM(addr));
  ConditionalExtendAccum(reg);
}

// LR $D, @M
// 0000 0000 110d dddd
// mmmm mmmm mmmm mmmm
// Move value from data memory pointed by address M to register $D.
DSPI void op_lr(u16 opc, u16 imm_, int fl)
{
  const u8 reg = opc & 0x1F;
  const u16 addr = imm_;
  const u16 val = ReadDMEM(addr);

  OpWriteRegister(reg, val);
  ConditionalExtendAccum(reg);
}

// SR @M, $S
// 0000 0000 111s ssss
// mmmm mmmm mmmm mmmm
// Store value from register $S to a memory pointed by address M.
DSPI void op_sr(u16 opc, u16 imm_, int fl)
{
  const u8 reg = opc & 0x1F;
  const u16 addr = imm_;

  WriteDMEM(addr, OpReadRegister(reg));
}

// SI @M, #I
// 0001 0110 mmmm mmmm
// iiii iiii iiii iiii
// Store 16-bit immediate value I to a memory location pointed by address
// M (M is 8-bit value sign extended).
DSPI void op_si(u16 opc, u16 imm_, int fl)
{
  const u16 addr = ((u16)(((s8)(opc))));
  const u16 imm = imm_;

  WriteDMEM(addr, imm);
}

// LRR $D, @$arS
// 0001 1000 0ssd dddd
// Move value from data memory pointed by addressing register $arS to register $D.
DSPI void op_lrr(u16 opc, u16 imm_, int fl)
{
  const u8 sreg = (opc >> 5) & 0x3;
  const u8 dreg = opc & 0x1f;

  const u16 val = ReadDMEM(OpReadRegister(sreg));
  OpWriteRegister(dreg, val);
  ConditionalExtendAccum(dreg);
}

// LRRD $D, @$arS
// 0001 1000 1ssd dddd
// Move value from data memory pointed by addressing register $arS to register $D.
// Decrement register $arS.
DSPI void op_lrrd(u16 opc, u16 imm_, int fl)
{
  const u8 sreg = (opc >> 5) & 0x3;
  const u8 dreg = opc & 0x1f;

  const u16 val = ReadDMEM(OpReadRegister(sreg));
  OpWriteRegister(dreg, val);
  ConditionalExtendAccum(dreg);
  g.r.ar[sreg] = DecrementAddressRegister(sreg);
}

// LRRI $D, @$arS
// 0001 1001 0ssd dddd
// Move value from data memory pointed by addressing register $arS to register $D.
// Increment register $arS.
DSPI void op_lrri(u16 opc, u16 imm_, int fl)
{
  const u8 sreg = (opc >> 5) & 0x3;
  const u8 dreg = opc & 0x1f;

  const u16 val = ReadDMEM(OpReadRegister(sreg));
  OpWriteRegister(dreg, val);
  ConditionalExtendAccum(dreg);
  g.r.ar[sreg] = IncrementAddressRegister(sreg);
}

// LRRN $D, @$arS
// 0001 1001 1ssd dddd
// Move value from data memory pointed by addressing register $arS to register $D.
// Add corresponding indexing register $ixS to register $arS.
DSPI void op_lrrn(u16 opc, u16 imm_, int fl)
{
  const u8 sreg = (opc >> 5) & 0x3;
  const u8 dreg = opc & 0x1f;

  const u16 val = ReadDMEM(OpReadRegister(sreg));
  OpWriteRegister(dreg, val);
  ConditionalExtendAccum(dreg);
  g.r.ar[sreg] = IncreaseAddressRegister(sreg, ((s16)(g.r.ix[sreg])));
}

// SRR @$arD, $S
// 0001 1010 0dds ssss
// Store value from source register $S to a memory location pointed by
// addressing register $arD.
DSPI void op_srr(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 5) & 0x3;
  const u8 sreg = opc & 0x1f;

  WriteDMEM(g.r.ar[dreg], OpReadRegister(sreg));
}

// SRRD @$arD, $S
// 0001 1010 1dds ssss
// Store value from source register $S to a memory location pointed by
// addressing register $arD. Decrement register $arD.
DSPI void op_srrd(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 5) & 0x3;
  const u8 sreg = opc & 0x1f;

  WriteDMEM(g.r.ar[dreg], OpReadRegister(sreg));

  g.r.ar[dreg] = DecrementAddressRegister(dreg);
}

// SRRI @$arD, $S
// 0001 1011 0dds ssss
// Store value from source register $S to a memory location pointed by
// addressing register $arD. Increment register $arD.
DSPI void op_srri(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 5) & 0x3;
  const u8 sreg = opc & 0x1f;

  WriteDMEM(g.r.ar[dreg], OpReadRegister(sreg));

  g.r.ar[dreg] = IncrementAddressRegister(dreg);
}

// SRRN @$arD, $S
// 0001 1011 1dds ssss
// Store value from source register $S to a memory location pointed by
// addressing register $arD. Add corresponding indexing register $ixD to register $arD.
DSPI void op_srrn(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = (opc >> 5) & 0x3;
  const u8 sreg = opc & 0x1f;

  WriteDMEM(g.r.ar[dreg], OpReadRegister(sreg));

  g.r.ar[dreg] = IncreaseAddressRegister(dreg, ((s16)(g.r.ix[dreg])));
}

// ILRR $acD.m, @$arS
// 0000 001d 0001 00ss
// Move value from instruction memory pointed by addressing register
// $arS to mid accumulator register $acD.m.
DSPI void op_ilrr(u16 opc, u16 imm_, int fl)
{
  const u16 reg = opc & 0x3;
  const u16 dreg = DSP_REG_ACM0 + ((opc >> 8) & 1);

  g.r.ac[dreg - DSP_REG_ACM0].m = ReadIMEM(g.r.ar[reg]);
  ConditionalExtendAccum(dreg);
}

// ILRRD $acD.m, @$arS
// 0000 001d 0001 01ss
// Move value from instruction memory pointed by addressing register
// $arS to mid accumulator register $acD.m. Decrement addressing register $arS.
DSPI void op_ilrrd(u16 opc, u16 imm_, int fl)
{
  const u16 reg = opc & 0x3;
  const u16 dreg = DSP_REG_ACM0 + ((opc >> 8) & 1);

  g.r.ac[dreg - DSP_REG_ACM0].m = ReadIMEM(g.r.ar[reg]);
  ConditionalExtendAccum(dreg);
  g.r.ar[reg] = DecrementAddressRegister(reg);
}

// ILRRI $acD.m, @$S
// 0000 001d 0001 10ss
// Move value from instruction memory pointed by addressing register
// $arS to mid accumulator register $acD.m. Increment addressing register $arS.
DSPI void op_ilrri(u16 opc, u16 imm_, int fl)
{
  const u16 reg = opc & 0x3;
  const u16 dreg = DSP_REG_ACM0 + ((opc >> 8) & 1);

  g.r.ac[dreg - DSP_REG_ACM0].m = ReadIMEM(g.r.ar[reg]);
  ConditionalExtendAccum(dreg);
  g.r.ar[reg] = IncrementAddressRegister(reg);
}

// ILRRN $acD.m, @$arS
// 0000 001d 0001 11ss
// Move value from instruction memory pointed by addressing register
// $arS to mid accumulator register $acD.m. Add corresponding indexing
// register $ixS to addressing register $arS.
DSPI void op_ilrrn(u16 opc, u16 imm_, int fl)
{
  const u16 reg = opc & 0x3;
  const u16 dreg = DSP_REG_ACM0 + ((opc >> 8) & 1);

  g.r.ac[dreg - DSP_REG_ACM0].m = ReadIMEM(g.r.ar[reg]);
  ConditionalExtendAccum(dreg);
  g.r.ar[reg] = IncreaseAddressRegister(reg, ((s16)(g.r.ix[reg])));
}

/* ---- DSPIntMisc.cpp ---- */

// MRR $D, $S
// 0001 11dd ddds ssss
// Move value from register $S to register $D.
DSPI void op_mrr(u16 opc, u16 imm_, int fl)
{
  const u8 sreg = opc & 0x1f;
  const u8 dreg = (opc >> 5) & 0x1f;

  OpWriteRegister(dreg, OpReadRegister(sreg));

  ConditionalExtendAccum(dreg);
}

// LRI $D, #I
// 0000 0000 100d dddd
// iiii iiii iiii iiii
// Load immediate value I to register $D.
//
// DSPSpy discovery: This, and possibly other instructions that load a
// register, has a different behaviour in S40 mode if loaded to AC0.M: The
// value gets sign extended to the whole accumulator! This does not happen in
// S16 mode.
DSPI void op_lri(u16 opc, u16 imm_, int fl)
{
  const u8 reg = opc & 0x1F;
  const u16 imm = imm_;

  OpWriteRegister(reg, imm);
  ConditionalExtendAccum(reg);
}

// LRIS $(0x18+D), #I
// 0000 1ddd iiii iiii
// Load immediate value I (8-bit sign extended) to accumulator register.
DSPI void op_lris(u16 opc, u16 imm_, int fl)
{
  const u8 reg = ((opc >> 8) & 0x7) + DSP_REG_AXL0;
  const u16 imm = ((u16)(((s8)(opc))));

  OpWriteRegister(reg, imm);
  ConditionalExtendAccum(reg);
}

//----

// NX
// 1000 -000 xxxx xxxx
// No operation, but can be extended with extended opcode.
// This opcode is supposed to do nothing - it's used if you want to use
// an opcode extension but not do anything. At least according to duddie.
DSPI void op_nx(u16 opc, u16 imm_, int fl)
{
  ZeroWriteBackLog();
}

//----

// DAR $arD
// 0000 0000 0000 01dd
// Decrement address register $arD.
DSPI void op_dar(u16 opc, u16 imm_, int fl)
{
  const u16 index = opc & 3;

  g.r.ar[index] = DecrementAddressRegister(index);
}

// IAR $arD
// 0000 0000 0000 10dd
// Increment address register $arD.
DSPI void op_iar(u16 opc, u16 imm_, int fl)
{
  const u16 index = opc & 3;

  g.r.ar[index] = IncrementAddressRegister(index);
}

// SUBARN $arD
// 0000 0000 0000 11dd
// Subtract indexing register $ixD from an addressing register $arD.
// used only in IPL-NTSC ucode
DSPI void op_subarn(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = opc & 0x3;

  g.r.ar[dreg] = DecreaseAddressRegister(dreg, ((s16)(g.r.ix[dreg])));
}

// ADDARN $arD, $ixS
// 0000 0000 0001 ssdd
// Adds indexing register $ixS to an addressing register $arD.
// It is critical for the Zelda ucode that this one wraps correctly.
DSPI void op_addarn(u16 opc, u16 imm_, int fl)
{
  const u8 dreg = opc & 0x3;
  const u8 sreg = (opc >> 2) & 0x3;

  g.r.ar[dreg] = IncreaseAddressRegister(dreg, ((s16)(g.r.ix[sreg])));
}

//----

// SBCLR #I
// 0001 0010 aaaa aiii
// Clear bit of status register $sr. Bit number is calculated by adding 6 to immediate value I;
// thus, bits 6 through 13 (LZ through AM) can be cleared with this instruction.
DSPI void op_sbclr(u16 opc, u16 imm_, int fl)
{
  const u8 bit = (opc & 0x7) + 6;

  g.r.sr &= ~(1U << bit);
}

// SBSET #I
// 0001 0011 aaaa aiii
// Set bit of status register $sr. Bit number is calculated by adding 6 to immediate value I;
// thus, bits 6 through 13 (LZ through AM) can be set with this instruction.
DSPI void op_sbset(u16 opc, u16 imm_, int fl)
{
  const u8 bit = (opc & 0x7) + 6;

  g.r.sr |= (1U << bit);
}

// This is a bunch of flag setters, flipping bits in SR.
DSPI void op_srbith(u16 opc, u16 imm_, int fl)
{

  ZeroWriteBackLog();
  switch ((opc >> 8) & 0x7)
  {
  case 2:  // M2
    g.r.sr &= ~SR_MUL_MODIFY;
    break;
  case 3:  // M0
    g.r.sr |= SR_MUL_MODIFY;
    break;
  case 4:  // CLR15
    g.r.sr &= ~SR_MUL_UNSIGNED;
    break;
  case 5:  // SET15
    g.r.sr |= SR_MUL_UNSIGNED;
    break;
  case 6:  // SET16 (CLR40)
    g.r.sr &= ~SR_40_MODE_BIT;
    break;
  case 7:  // SET40
    g.r.sr |= SR_40_MODE_BIT;
    break;
  default:
    break;
  }
}

/* ---- DSPIntExtOps.cpp ---- */

DSPI bool IsSameMemArea(u16 a, u16 b)
{
  // LM: tested on Wii
  return (a >> 10) == (b >> 10);
}

// DR $arR
// xxxx xxxx 0000 01rr
// Decrement addressing register $arR.
DSPI void op_dr(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  dsp_wb_put(wb, 0, opc & 0x3, DecrementAddressRegister(opc & 0x3));
}

// IR $arR
// xxxx xxxx 0000 10rr
// Increment addressing register $arR.
DSPI void op_ir(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  dsp_wb_put(wb, 0, opc & 0x3, IncrementAddressRegister(opc & 0x3));
}

// NR $arR
// xxxx xxxx 0000 11rr
// Add corresponding indexing register $ixR to addressing register $arR.
DSPI void op_nr(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 reg = opc & 0x3;

  dsp_wb_put(wb, 0, reg, IncreaseAddressRegister(reg, ((s16)(g.r.ix[reg]))));
}

// MV $axD.D, $acS.S
// xxxx xxxx 0001 ddss
// Move value of $acS.S to the $axD.D.
DSPI void op_mv(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc & 0x3) + DSP_REG_ACL0;
  const u8 dreg = ((opc >> 2) & 0x3);

  dsp_wb_put(wb, 0, dreg + DSP_REG_AXL0, OpReadRegister(sreg));
}

// S @$arD, $acS.S
// xxxx xxxx 001s s0dd
// Store value of $acS.S in the memory pointed by register $arD.
// Post increment register $arD.
DSPI void op_s(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 dreg = opc & 0x3;
  const u8 sreg = ((opc >> 3) & 0x3) + DSP_REG_ACL0;

  WriteDMEM(g.r.ar[dreg], OpReadRegister(sreg));
  dsp_wb_put(wb, 0, dreg, IncrementAddressRegister(dreg));
}

// SN @$arD, $acS.S
// xxxx xxxx 001s s1dd
// Store value of register $acS.S in the memory pointed by register $arD.
// Add indexing register $ixD to register $arD.
DSPI void op_sn(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 dreg = opc & 0x3;
  const u8 sreg = ((opc >> 3) & 0x3) + DSP_REG_ACL0;

  WriteDMEM(g.r.ar[dreg], OpReadRegister(sreg));
  dsp_wb_put(wb, 0, dreg, IncreaseAddressRegister(dreg, ((s16)(g.r.ix[dreg]))));
}

// L $axD.D, @$arS
// xxxx xxxx 01dd d0ss
// Load $axD.D/$acD.D with value from memory pointed by register $arS.
// Post increment register $arS.
DSPI void op_l(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = opc & 0x3;
  const u8 dreg = ((opc >> 3) & 0x7) + DSP_REG_AXL0;

  if (dreg >= DSP_REG_ACM0 && IsSRFlagSet(SR_40_MODE_BIT))
  {
    const u16 val = ReadDMEM(g.r.ar[sreg]);
    dsp_wb_put(wb, 0, dreg - DSP_REG_ACM0 + DSP_REG_ACH0, (val & 0x8000) != 0 ? 0xFFFF : 0x0000);
    dsp_wb_put(wb, 1, dreg, val);
    dsp_wb_put(wb, 2, dreg - DSP_REG_ACM0 + DSP_REG_ACL0, 0);
    dsp_wb_put(wb, 3, sreg, IncrementAddressRegister(sreg));
  }
  else
  {
    dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[sreg]));
    dsp_wb_put(wb, 1, sreg, IncrementAddressRegister(sreg));
  }
}

// LN $axD.D, @$arS
// xxxx xxxx 01dd d1ss
// Load $axD.D/$acD.D with value from memory pointed by register $arS.
// Add indexing register $ixS to register $arS.
DSPI void op_ln(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = opc & 0x3;
  const u8 dreg = ((opc >> 3) & 0x7) + DSP_REG_AXL0;

  if (dreg >= DSP_REG_ACM0 && IsSRFlagSet(SR_40_MODE_BIT))
  {
    const u16 val = ReadDMEM(g.r.ar[sreg]);
    dsp_wb_put(wb, 0, dreg - DSP_REG_ACM0 + DSP_REG_ACH0, (val & 0x8000) != 0 ? 0xFFFF : 0x0000);
    dsp_wb_put(wb, 1, dreg, val);
    dsp_wb_put(wb, 2, dreg - DSP_REG_ACM0 + DSP_REG_ACL0, 0);
    dsp_wb_put(wb, 3, sreg, IncreaseAddressRegister(sreg, ((s16)(g.r.ix[sreg]))));
  }
  else
  {
    dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[sreg]));
    dsp_wb_put(wb, 1, sreg, IncreaseAddressRegister(sreg, ((s16)(g.r.ix[sreg]))));
  }
}

// LS $axD.D, $acS.m
// xxxx xxxx 10dd 000s
// Load register $axD.D with value from memory pointed by register
// $ar0. Store value from register $acS.m to memory location pointed by
// register $ar3. Increment both $ar0 and $ar3.
DSPI void op_ls(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc & 0x1) + DSP_REG_ACM0;
  const u8 dreg = ((opc >> 4) & 0x3) + DSP_REG_AXL0;

  WriteDMEM(g.r.ar[3], OpReadRegister(sreg));

  dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[0]));
  dsp_wb_put(wb, 1, DSP_REG_AR3, IncrementAddressRegister(DSP_REG_AR3));
  dsp_wb_put(wb, 2, DSP_REG_AR0, IncrementAddressRegister(DSP_REG_AR0));
}

// LSN $axD.D, $acS.m
// xxxx xxxx 10dd 010s
// Load register $axD.D with value from memory pointed by register
// $ar0. Store value from register $acS.m to memory location pointed by
// register $ar3. Add corresponding indexing register $ix0 to addressing
// register $ar0 and increment $ar3.
DSPI void op_lsn(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc & 0x1) + DSP_REG_ACM0;
  const u8 dreg = ((opc >> 4) & 0x3) + DSP_REG_AXL0;

  WriteDMEM(g.r.ar[3], OpReadRegister(sreg));

  dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[0]));
  dsp_wb_put(wb, 1, DSP_REG_AR3, IncrementAddressRegister(DSP_REG_AR3));
  dsp_wb_put(wb, 2, DSP_REG_AR0,
                 IncreaseAddressRegister(DSP_REG_AR0, ((s16)(g.r.ix[0]))));
}

// LSM $axD.D, $acS.m
// xxxx xxxx 10dd 100s
// Load register $axD.D with value from memory pointed by register
// $ar0. Store value from register $acS.m to memory location pointed by
// register $ar3. Add corresponding indexing register $ix3 to addressing
// register $ar3 and increment $ar0.
DSPI void op_lsm(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc & 0x1) + DSP_REG_ACM0;
  const u8 dreg = ((opc >> 4) & 0x3) + DSP_REG_AXL0;

  WriteDMEM(g.r.ar[3], OpReadRegister(sreg));

  dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[0]));
  dsp_wb_put(wb, 1, DSP_REG_AR3,
                 IncreaseAddressRegister(DSP_REG_AR3, ((s16)(g.r.ix[3]))));
  dsp_wb_put(wb, 2, DSP_REG_AR0, IncrementAddressRegister(DSP_REG_AR0));
}

// LSMN $axD.D, $acS.m
// xxxx xxxx 10dd 110s
// Load register $axD.D with value from memory pointed by register
// $ar0. Store value from register $acS.m to memory location pointed by
// register $ar3. Add corresponding indexing register $ix0 to addressing
// register $ar0 and add corresponding indexing register $ix3 to addressing
// register $ar3.
DSPI void op_lsnm(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc & 0x1) + DSP_REG_ACM0;
  const u8 dreg = ((opc >> 4) & 0x3) + DSP_REG_AXL0;

  WriteDMEM(g.r.ar[3], OpReadRegister(sreg));

  dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[0]));
  dsp_wb_put(wb, 1, DSP_REG_AR3,
                 IncreaseAddressRegister(DSP_REG_AR3, ((s16)(g.r.ix[3]))));
  dsp_wb_put(wb, 2, DSP_REG_AR0,
                 IncreaseAddressRegister(DSP_REG_AR0, ((s16)(g.r.ix[0]))));
}

// SL $acS.m, $axD.D
// xxxx xxxx 10dd 001s
// Store value from register $acS.m to memory location pointed by register
// $ar0. Load register $axD.D with value from memory pointed by register
// $ar3. Increment both $ar0 and $ar3.
DSPI void op_sl(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc & 0x1) + DSP_REG_ACM0;
  const u8 dreg = ((opc >> 4) & 0x3) + DSP_REG_AXL0;

  WriteDMEM(g.r.ar[0], OpReadRegister(sreg));

  dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[3]));
  dsp_wb_put(wb, 1, DSP_REG_AR3, IncrementAddressRegister(DSP_REG_AR3));
  dsp_wb_put(wb, 2, DSP_REG_AR0, IncrementAddressRegister(DSP_REG_AR0));
}

// SLN $acS.m, $axD.D
// xxxx xxxx 10dd 011s
// Store value from register $acS.m to memory location pointed by register
// $ar0. Load register $axD.D with value from memory pointed by register
// $ar3. Add corresponding indexing register $ix0 to addressing register $ar0
// and increment $ar3.
DSPI void op_sln(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc & 0x1) + DSP_REG_ACM0;
  const u8 dreg = ((opc >> 4) & 0x3) + DSP_REG_AXL0;

  WriteDMEM(g.r.ar[0], OpReadRegister(sreg));

  dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[3]));
  dsp_wb_put(wb, 1, DSP_REG_AR3, IncrementAddressRegister(DSP_REG_AR3));
  dsp_wb_put(wb, 2, DSP_REG_AR0,
                 IncreaseAddressRegister(DSP_REG_AR0, ((s16)(g.r.ix[0]))));
}

// SLM $acS.m, $axD.D
// xxxx xxxx 10dd 101s
// Store value from register $acS.m to memory location pointed by register
// $ar0. Load register $axD.D with value from memory pointed by register
// $ar3. Add corresponding indexing register $ix3 to addressing register $ar3
// and increment $ar0.
DSPI void op_slm(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc & 0x1) + DSP_REG_ACM0;
  const u8 dreg = ((opc >> 4) & 0x3) + DSP_REG_AXL0;

  WriteDMEM(g.r.ar[0], OpReadRegister(sreg));

  dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[3]));
  dsp_wb_put(wb, 1, DSP_REG_AR3,
                 IncreaseAddressRegister(DSP_REG_AR3, ((s16)(g.r.ix[3]))));
  dsp_wb_put(wb, 2, DSP_REG_AR0, IncrementAddressRegister(DSP_REG_AR0));
}

// SLMN $acS.m, $axD.D
// xxxx xxxx 10dd 111s
// Store value from register $acS.m to memory location pointed by register
// $ar0. Load register $axD.D with value from memory pointed by register
// $ar3. Add corresponding indexing register $ix0 to addressing register $ar0
// and add corresponding indexing register $ix3 to addressing register $ar3.
DSPI void op_slnm(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc & 0x1) + DSP_REG_ACM0;
  const u8 dreg = ((opc >> 4) & 0x3) + DSP_REG_AXL0;

  WriteDMEM(g.r.ar[0], OpReadRegister(sreg));

  dsp_wb_put(wb, 0, dreg, ReadDMEM(g.r.ar[3]));
  dsp_wb_put(wb, 1, DSP_REG_AR3,
                 IncreaseAddressRegister(DSP_REG_AR3, ((s16)(g.r.ix[3]))));
  dsp_wb_put(wb, 2, DSP_REG_AR0,
                 IncreaseAddressRegister(DSP_REG_AR0, ((s16)(g.r.ix[0]))));
}

// LD $ax0.D, $ax1.R, @$arS
// xxxx xxxx 11dr 00ss
// Load register $ax0.D (either $ax0.l or $ax0.h) with value from memory pointed by register $arS.
// Load register $ax1.R (either $ax1.l or $ax1.h) with value from memory pointed by register $ar3.
// Increment both $arS and $ar3.
// S cannot be 3, as that encodes LDAX.  Thus $arS and $ar3 are known to be distinct.
// If $ar0 and $ar3 point into the same memory page (upper 6 bits of addr are the same -> games are
// not doing that!) then the value pointed by $ar0 is loaded to BOTH $ax0.D and $ax1.R.
// If $ar0 points into an invalid memory page (ie 0x2000), then $ax0.D keeps its old value. (not
// implemented yet)
// If $ar3 points into an invalid memory page, then $ax1.R gets the same value as $ax0.D. (not
// implemented yet)
DSPI void op_ld(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 dreg = (opc >> 5) & 0x1;
  const u8 rreg = (opc >> 4) & 0x1;
  const u8 sreg = opc & 0x3;

  dsp_wb_put(wb, 0, (dreg << 1) + DSP_REG_AXL0, ReadDMEM(g.r.ar[sreg]));

  if (IsSameMemArea(g.r.ar[sreg], g.r.ar[3]))
    dsp_wb_put(wb, 1, (rreg << 1) + DSP_REG_AXL1, ReadDMEM(g.r.ar[sreg]));
  else
    dsp_wb_put(wb, 1, (rreg << 1) + DSP_REG_AXL1, ReadDMEM(g.r.ar[3]));

  dsp_wb_put(wb, 2, sreg, IncrementAddressRegister(sreg));

  dsp_wb_put(wb, 3, DSP_REG_AR3, IncrementAddressRegister(DSP_REG_AR3));
}

// LDAX $axR, @$arS
// xxxx xxxx 11sr 0011
// Load register $axR.h with value from memory pointed by register $arS.
// Load register $axR.l with value from memory pointed by register $ar3.
// Increment both $arS and $ar3.
DSPI void op_ldax(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc >> 5) & 0x1;
  const u8 rreg = (opc >> 4) & 0x1;

  dsp_wb_put(wb, 0, rreg + DSP_REG_AXH0, ReadDMEM(g.r.ar[sreg]));

  if (IsSameMemArea(g.r.ar[sreg], g.r.ar[3]))
    dsp_wb_put(wb, 1, rreg + DSP_REG_AXL0, ReadDMEM(g.r.ar[sreg]));
  else
    dsp_wb_put(wb, 1, rreg + DSP_REG_AXL0, ReadDMEM(g.r.ar[3]));

  dsp_wb_put(wb, 2, sreg, IncrementAddressRegister(sreg));

  dsp_wb_put(wb, 3, DSP_REG_AR3, IncrementAddressRegister(DSP_REG_AR3));
}

// LDN $ax0.D, $ax1.R, @$arS
// xxxx xxxx 11dr 01ss
DSPI void op_ldn(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 dreg = (opc >> 5) & 0x1;
  const u8 rreg = (opc >> 4) & 0x1;
  const u8 sreg = opc & 0x3;

  dsp_wb_put(wb, 0, (dreg << 1) + DSP_REG_AXL0, ReadDMEM(g.r.ar[sreg]));

  if (IsSameMemArea(g.r.ar[sreg], g.r.ar[3]))
    dsp_wb_put(wb, 1, (rreg << 1) + DSP_REG_AXL1, ReadDMEM(g.r.ar[sreg]));
  else
    dsp_wb_put(wb, 1, (rreg << 1) + DSP_REG_AXL1, ReadDMEM(g.r.ar[3]));

  dsp_wb_put(wb, 2, sreg, IncreaseAddressRegister(sreg, ((s16)(g.r.ix[sreg]))));

  dsp_wb_put(wb, 3, DSP_REG_AR3, IncrementAddressRegister(DSP_REG_AR3));
}

// LDAXN $axR, @$arS
// xxxx xxxx 11sr 0111
DSPI void op_ldaxn(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc >> 5) & 0x1;
  const u8 rreg = (opc >> 4) & 0x1;

  dsp_wb_put(wb, 0, rreg + DSP_REG_AXH0, ReadDMEM(g.r.ar[sreg]));

  if (IsSameMemArea(g.r.ar[sreg], g.r.ar[3]))
    dsp_wb_put(wb, 1, rreg + DSP_REG_AXL0, ReadDMEM(g.r.ar[sreg]));
  else
    dsp_wb_put(wb, 1, rreg + DSP_REG_AXL0, ReadDMEM(g.r.ar[3]));

  dsp_wb_put(wb, 2, sreg, IncreaseAddressRegister(sreg, ((s16)(g.r.ix[sreg]))));

  dsp_wb_put(wb, 3, DSP_REG_AR3, IncrementAddressRegister(DSP_REG_AR3));
}

// LDM $ax0.D, $ax1.R, @$arS
// xxxx xxxx 11dr 10ss
DSPI void op_ldm(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 dreg = (opc >> 5) & 0x1;
  const u8 rreg = (opc >> 4) & 0x1;
  const u8 sreg = opc & 0x3;

  dsp_wb_put(wb, 0, (dreg << 1) + DSP_REG_AXL0, ReadDMEM(g.r.ar[sreg]));

  if (IsSameMemArea(g.r.ar[sreg], g.r.ar[3]))
    dsp_wb_put(wb, 1, (rreg << 1) + DSP_REG_AXL1, ReadDMEM(g.r.ar[sreg]));
  else
    dsp_wb_put(wb, 1, (rreg << 1) + DSP_REG_AXL1, ReadDMEM(g.r.ar[3]));

  dsp_wb_put(wb, 2, sreg, IncrementAddressRegister(sreg));

  dsp_wb_put(wb, 3, DSP_REG_AR3,
                 IncreaseAddressRegister(DSP_REG_AR3, ((s16)(g.r.ix[3]))));
}

// LDAXM $axR, @$arS
// xxxx xxxx 11sr 1011
DSPI void op_ldaxm(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc >> 5) & 0x1;
  const u8 rreg = (opc >> 4) & 0x1;

  dsp_wb_put(wb, 0, rreg + DSP_REG_AXH0, ReadDMEM(g.r.ar[sreg]));

  if (IsSameMemArea(g.r.ar[sreg], g.r.ar[3]))
    dsp_wb_put(wb, 1, rreg + DSP_REG_AXL0, ReadDMEM(g.r.ar[sreg]));
  else
    dsp_wb_put(wb, 1, rreg + DSP_REG_AXL0, ReadDMEM(g.r.ar[3]));

  dsp_wb_put(wb, 2, sreg, IncrementAddressRegister(sreg));

  dsp_wb_put(wb, 3, DSP_REG_AR3,
                 IncreaseAddressRegister(DSP_REG_AR3, ((s16)(g.r.ix[3]))));
}

// LDNM $ax0.D, $ax1.R, @$arS
// xxxx xxxx 11dr 11ss
DSPI void op_ldnm(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 dreg = (opc >> 5) & 0x1;
  const u8 rreg = (opc >> 4) & 0x1;
  const u8 sreg = opc & 0x3;

  dsp_wb_put(wb, 0, (dreg << 1) + DSP_REG_AXL0, ReadDMEM(g.r.ar[sreg]));

  if (IsSameMemArea(g.r.ar[sreg], g.r.ar[3]))
    dsp_wb_put(wb, 1, (rreg << 1) + DSP_REG_AXL1, ReadDMEM(g.r.ar[sreg]));
  else
    dsp_wb_put(wb, 1, (rreg << 1) + DSP_REG_AXL1, ReadDMEM(g.r.ar[3]));

  dsp_wb_put(wb, 2, sreg, IncreaseAddressRegister(sreg, ((s16)(g.r.ix[sreg]))));

  dsp_wb_put(wb, 3, DSP_REG_AR3,
                 IncreaseAddressRegister(DSP_REG_AR3, ((s16)(g.r.ix[3]))));
}

// LDAXNM $axR, @$arS
// xxxx xxxx 11dr 1111
DSPI void op_ldaxnm(u16 opc, u16 imm_, int fl, DspWB* wb)
{
  const u8 sreg = (opc >> 5) & 0x1;
  const u8 rreg = (opc >> 4) & 0x1;

  dsp_wb_put(wb, 0, rreg + DSP_REG_AXH0, ReadDMEM(g.r.ar[sreg]));

  if (IsSameMemArea(g.r.ar[sreg], g.r.ar[3]))
    dsp_wb_put(wb, 1, rreg + DSP_REG_AXL0, ReadDMEM(g.r.ar[sreg]));
  else
    dsp_wb_put(wb, 1, rreg + DSP_REG_AXL0, ReadDMEM(g.r.ar[3]));

  dsp_wb_put(wb, 2, sreg, IncreaseAddressRegister(sreg, ((s16)(g.r.ix[sreg]))));

  dsp_wb_put(wb, 3, DSP_REG_AR3,
                 IncreaseAddressRegister(DSP_REG_AR3, ((s16)(g.r.ix[3]))));
}

DSPI void op_nop_ext(u16 opc, u16 imm_, int fl, DspWB* wb)
{
}

#endif
