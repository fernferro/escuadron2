/*
 * DSP de bajo nivel (LLE): el interprete del DSP de Dolphin (Core/DSP, GPL-2.0-or-later)
 * corre el microcodigo real de MusyX que manda el juego. Esto es lo que hace sonar
 * el audio y avanzar las voces/streams (sin esto, el juego se queda esperando a que
 * termine una voz al descargar el nivel).
 *
 * El DSP corre en su propio hilo, limitado a la velocidad real del DSP (81 MHz);
 * cuando el microcodigo espera un mensaje (bucle detectado por el analizador de
 * Dolphin), el hilo duerme hasta que la CPU escribe algo.
 */
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>

#include "Core/DSP/DSPCore.h"
#include "Core/DSP/DSPHost.h"
#include "Core/DSP/DSPTables.h"
#include "Core/DSP/Interpreter/DSPInterpreter.h"

extern "C" {
#include <stdint.h>
extern const uint16_t dsp_irom_data[0x1000];
extern const uint16_t dsp_coef_data[0x800];
uint8_t* hw_aram_ptr(void);
uint8_t* mem_ptr(uint32_t addr);
void hw_dsp_interrupt_from_dsp(void);
void hw_dsp_escribio(uint32_t addr, uint32_t len);
uint64_t os_now_us(void);
void rt_log(const char* fmt, ...);
}

using namespace DSP;

static DSPCore* s_core;
static std::mutex s_m;
static std::condition_variable s_cv;
static std::atomic<int> s_kick;
static std::thread s_thread;
static std::atomic<uint64_t> s_cycles_run;

namespace DSP::Host
{
u8 ReadHostMemory(u32 addr) { return hw_aram_ptr()[addr & 0xFFFFFF]; }
void WriteHostMemory(u8 value, u32 addr)
{
  static uint8_t seen[0x100];
  if (!seen[(addr >> 16) & 0xFF]) {
    seen[(addr >> 16) & 0xFF] = 1;
    rt_log("[dsp] escritura del DSP en ARAM %08X\n", addr);
  }
  hw_aram_ptr()[addr & 0xFFFFFF] = value;
}
void DMAToDSP(u16* dst, u32 addr, u32 size)
{
  const uint8_t* p = mem_ptr(addr);
  if (!p) { rt_log("[dsp] DMA a DSP invalido %08X\n", addr); return; }
  for (u32 i = 0; i < size / 2; i++) dst[i] = (u16)(p[2 * i] << 8 | p[2 * i + 1]);
}
void DMAFromDSP(const u16* src, u32 addr, u32 size)
{
  static uint8_t seen[0x10000];
  if (!seen[(addr >> 16) & 0xFFFF]) {
    seen[(addr >> 16) & 0xFFFF] = 1;
    rt_log("[dsp] DMA DSP->RAM nueva zona %08X (%u bytes, pc %04X)\n", addr, size, s_core ? s_core->DSPState().pc : 0);
  }
  uint8_t* p = mem_ptr(addr);
  if (!p) { rt_log("[dsp] DMA desde DSP invalido %08X\n", addr); return; }
  for (u32 i = 0; i < size / 2; i++) { p[2 * i] = (u8)(src[i] >> 8); p[2 * i + 1] = (u8)src[i]; }
  hw_dsp_escribio(addr, size);
}
void OSD_AddMessage(std::string, u32) {}
bool OnThread() { return true; }
bool IsWiiHost() { return false; }
void InterruptRequest() { hw_dsp_interrupt_from_dsp(); }
void CodeLoaded(DSPCore& dsp, u32 addr, size_t size)
{
  const uint8_t* p = mem_ptr(addr);
  CodeLoaded(dsp, p ? p : reinterpret_cast<const u8*>(dsp.DSPState().iram), size);
}
void CodeLoaded(DSPCore& dsp, const u8* ptr, size_t size)
{
  uint32_t crc = 0;
  for (size_t i = 0; i < size; i++) crc = crc * 31 + ptr[i];
  dsp.DSPState().SetIRAMCRC(crc);
  rt_log("[dsp] microcodigo cargado (%u bytes, suma %08X)\n", (unsigned)size, crc);
  if (getenv("RL_DSP_DUMP")) {   /* guarda el microcodigo (para estudiarlo) */
    char path[64];
    snprintf(path, sizeof path, "build/dsp_%08X.bin", crc);
    FILE* f = fopen(path, "wb");
    if (f) { fwrite(ptr, 1, size, f); fclose(f); }
  }
  dsp.ClearIRAM();   /* el JIT tiene que volver a compilar el codigo nuevo */
  dsp.DSPState().GetAnalyzer().Analyze(dsp.DSPState());
}
}  // namespace DSP::Host

namespace DSP { extern void (*g_dsp_on_cpu_mail_read)(); }
static std::mutex s_mail_m;
static std::condition_variable s_mail_cv;
static void on_cpu_mail_read() {
  std::lock_guard<std::mutex> lk(s_mail_m);
  s_mail_cv.notify_all();
}

static void kick() {
  s_kick.store(1);
  s_cv.notify_one();
}

extern "C" void cpu_register_thread(int cat);
static void dsp_thread_main() {
  cpu_register_thread(2);
  const uint64_t HZ = 81000000ull;  /* reloj del DSP: 486 MHz / 6 */
  uint64_t t_prev = os_now_us();
  int64_t budget = 0;
  for (;;) {
    uint64_t now = os_now_us();
    budget += (int64_t)((now - t_prev) * HZ / 1000000ull);
    t_prev = now;
    if (budget > (int64_t)(HZ / 100)) budget = HZ / 100;  /* no acumular mas de 10 ms */
    bool idle;
    {
      std::unique_lock<std::mutex> lk(s_m);
      SDSP& st = s_core->DSPState();
      idle = (st.control_reg & CR_HALT) != 0;
      /* si la CPU espera que el DSP lea su mensaje, no se lo hace esperar por el limite */
      const bool mail_waiting = (st.PeekMailbox(Mailbox::CPU) & 0x80000000) != 0;
      if (mail_waiting && budget < 20000) budget = 20000;
      if (!idle && budget > 0) {
        int n = budget > 20000 ? 20000 : (int)budget;
        int left = s_core->RunCycles(n);
        if (left < 0 || left > n) left = 0;   /* el JIT puede pasarse unos ciclos del bloque */
        int ran = n - left;
        budget -= n;
        s_cycles_run += (uint64_t)ran;
        /* el analizador corta en los bucles de espera: dormir hasta que pase algo */
        idle = st.GetAnalyzer().IsIdleSkip(st.pc);
      } else if (budget <= 0) {
        idle = true;
      }
      if (idle) {
        /* Ojo: en Windows esta espera vuelve enseguida (menos de 1 ms) y el hilo gira; se probo
           esperar 1-2 ms y el DSP quedaba lento (esperas de ~15 ms) y el juego se colgaba. */
        if (!s_kick.exchange(0)) s_cv.wait_for(lk, std::chrono::microseconds(500));
        budget = budget > 0 ? budget : 0;
      }
    }
  }
}

extern "C" {

extern int dsp_lle_enabled;      /* dsp_nativo.c */
extern int dsp_use_interpreter;

void dsp_dolphin_init(void) {
  static DSPInitOptions opts;
  std::memcpy(opts.irom_contents.data(), dsp_irom_data, sizeof dsp_irom_data);
  std::memcpy(opts.coef_contents.data(), dsp_coef_data, sizeof dsp_coef_data);
  /* JIT x86-64 de Dolphin (varias veces mas rapido); --dsp-interprete vuelve al interprete */
  opts.core_type = dsp_use_interpreter ? DSPInitOptions::CoreType::Interpreter : DSPInitOptions::CoreType::JIT64;
  rt_log("[dsp] %s\n", dsp_use_interpreter ? "interprete" : "JIT x86-64");
  s_core = new DSPCore();
  if (!s_core->Initialize(opts)) { rt_log("[dsp] no se pudo iniciar el DSP LLE\n"); return; }
  s_core->Reset();
  InitInstructionTable();
  g_dsp_on_cpu_mail_read = on_cpu_mail_read;
  dsp_lle_enabled = 1;
  s_thread = std::thread(dsp_thread_main);
  s_thread.detach();
}

uint16_t dsp_dolphin_read_cr(void) {
  std::lock_guard<std::mutex> lk(s_m);
  return s_core->GetInterpreter().ReadControlRegister();
}

uint16_t dsp_dolphin_write_cr(uint16_t v) {
  {
    std::lock_guard<std::mutex> lk(s_m);
    s_core->GetInterpreter().WriteControlRegister(v);
    if (v & CR_EXTERNAL_INT) {
      s_core->CheckExternalInterrupt();
      s_core->CheckExceptions();
    }
  }
  kick();
  return dsp_dolphin_read_cr();
}

uint32_t dsp_dolphin_peek_mail_nowait(int cpu) { return s_core->PeekMailbox(cpu ? Mailbox::CPU : Mailbox::DSP); }
uint32_t dsp_dolphin_peek_mail(int cpu) {
  uint32_t m = s_core->PeekMailbox(cpu ? Mailbox::CPU : Mailbox::DSP);
  if (cpu && (m & 0x80000000)) {
    /* la CPU esta esperando (DSPCheckMailToDSP en un bucle): en vez de girar, dormir
       hasta que el DSP lea el mensaje (como mucho 1 ms por consulta) */
    std::unique_lock<std::mutex> lk(s_mail_m);
    kick();
    s_mail_cv.wait_for(lk, std::chrono::microseconds(1000), [] {
      return (s_core->PeekMailbox(Mailbox::CPU) & 0x80000000) == 0;
    });
    m = s_core->PeekMailbox(Mailbox::CPU);
  }
  return m;
}
uint16_t dsp_dolphin_read_mail_hi(int cpu) { return s_core->ReadMailboxHigh(cpu ? Mailbox::CPU : Mailbox::DSP); }
uint16_t dsp_dolphin_read_mail_lo(int cpu) { return s_core->ReadMailboxLow(cpu ? Mailbox::CPU : Mailbox::DSP); }
void dsp_dolphin_write_mail_hi(uint16_t v) { s_core->WriteMailboxHigh(Mailbox::CPU, v); }
void dsp_dolphin_write_mail_lo(uint16_t v) { s_core->WriteMailboxLow(Mailbox::CPU, v); kick(); }
uint64_t dsp_dolphin_cycles(void) { return s_cycles_run.load(); }

}  // extern "C"

/* ---- para --dsp-verificar: el DSP de Dolphin paso a paso, sin hilo propio ---- */
extern "C" {
typedef struct {
  uint16_t pc, sr, cr, control_reg;
  uint16_t ar[4], ix[4], wr[4], st[4];
  uint64_t ac[2], prod;
  uint32_t ax[2];
  uint8_t exceptions, ptrs[4];
  uint32_t mail[2];
} DspFoto;

void dsp_dolphin_init_sin_hilo(void) {
  static DSPInitOptions opts;
  std::memcpy(opts.irom_contents.data(), dsp_irom_data, sizeof dsp_irom_data);
  std::memcpy(opts.coef_contents.data(), dsp_coef_data, sizeof dsp_coef_data);
  opts.core_type = DSPInitOptions::CoreType::Interpreter;
  s_core = new DSPCore();
  s_core->Initialize(opts);
  s_core->Reset();
  InitInstructionTable();
}
/* una instruccion; 0 si el DSP esta detenido */
int dsp_dolphin_paso(void) {
  std::lock_guard<std::mutex> lk(s_m);
  SDSP& st = s_core->DSPState();
  if (st.control_reg & CR_HALT) return 0;
  if (st.external_interrupt_waiting.exchange(false)) s_core->CheckExternalInterrupt();
  s_core->GetInterpreter().Step();
  return 1;
}
void dsp_dolphin_tomar_excepciones(void) {
  std::lock_guard<std::mutex> lk(s_m);
  s_core->CheckExceptions();
}
void dsp_dolphin_foto(DspFoto* f) {
  std::lock_guard<std::mutex> lk(s_m);
  SDSP& st = s_core->DSPState();
  f->pc = st.pc; f->sr = st.r.sr; f->cr = st.r.cr; f->control_reg = st.control_reg;
  for (int i = 0; i < 4; i++) { f->ar[i] = st.r.ar[i]; f->ix[i] = st.r.ix[i]; f->wr[i] = st.r.wr[i]; f->st[i] = st.r.st[i]; f->ptrs[i] = st.reg_stack_ptrs[i]; }
  f->ac[0] = st.r.ac[0].val; f->ac[1] = st.r.ac[1].val; f->prod = st.r.prod.val;
  f->ax[0] = st.r.ax[0].val; f->ax[1] = st.r.ax[1].val;
  f->exceptions = st.exceptions;
  f->mail[0] = st.PeekMailbox(Mailbox::CPU); f->mail[1] = st.PeekMailbox(Mailbox::DSP);
}
const uint16_t* dsp_dolphin_dram(void) { return s_core->DSPState().dram; }
}
