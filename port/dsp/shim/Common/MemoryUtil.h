#pragma once
/* Version minima de Common/MemoryUtil de Dolphin para el DSP (interprete y JIT). */
#include <cstdlib>
#include <cstddef>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
namespace Common {
inline void* AllocateExecutableMemory(size_t size) {
#ifdef _WIN32
  return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
  void* p = mmap(nullptr, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_ANON | MAP_PRIVATE, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
#endif
}
inline void JITPageWriteEnableExecuteDisable() {}
inline void JITPageWriteDisableExecuteEnable() {}
struct ScopedJITPageWriteAndNoExecute {
  ScopedJITPageWriteAndNoExecute() {}
  ~ScopedJITPageWriteAndNoExecute() {}
};
inline void* AllocateMemoryPages(size_t n) {
#ifdef _WIN32
  return VirtualAlloc(nullptr, n, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
#else
  void* p = mmap(nullptr, n, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
  return p == MAP_FAILED ? nullptr : p;
#endif
}
inline bool FreeMemoryPages(void* p, size_t n) {
  if (!p) return true;
#ifdef _WIN32
  (void)n; return VirtualFree(p, 0, MEM_RELEASE) != 0;
#else
  return munmap(p, n) == 0;
#endif
}
inline bool WriteProtectMemory(void*, size_t, bool = false) { return true; }
inline bool UnWriteProtectMemory(void*, size_t, bool = false) { return true; }
inline bool ReadProtectMemory(void*, size_t) { return true; }
inline void* AllocateAlignedMemory(size_t size, size_t align) {
#ifdef _WIN32
  return _aligned_malloc(size, align);
#else
  void* p = nullptr; return posix_memalign(&p, align, size) ? nullptr : p;
#endif
}
inline void FreeAlignedMemory(void* p) {
#ifdef _WIN32
  _aligned_free(p);
#else
  free(p);
#endif
}
}  // namespace Common
