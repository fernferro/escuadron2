// Deteccion minima de la CPU para el emisor x64 de Dolphin (JIT del DSP).
#include "Common/CPUDetect.h"
#include <cpuid.h>

CPUInfo cpu_info;

CPUInfo::CPUInfo() { Detect(); }

void CPUInfo::Detect()
{
  unsigned a, b, c, d;
  if (!__get_cpuid(1, &a, &b, &c, &d))
    return;
  bSSE3 = (c >> 0) & 1;
  bSSSE3 = (c >> 9) & 1;
  bSSE4_1 = (c >> 19) & 1;
  bSSE4_2 = (c >> 20) & 1;
  bMOVBE = (c >> 22) & 1;
  bAES = (c >> 25) & 1;
  bFMA = (c >> 12) & 1;
  bAVX = ((c >> 28) & 1) && ((c >> 27) & 1);
  if (__get_cpuid_count(7, 0, &a, &b, &c, &d))
  {
    bBMI1 = (b >> 3) & 1;
    bBMI2 = (b >> 8) & 1;
  }
  if (__get_cpuid(0x80000001, &a, &b, &c, &d))
    bLZCNT = (c >> 5) & 1;
}

std::string CPUInfo::Summarize() { return ""; }
