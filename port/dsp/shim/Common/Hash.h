#pragma once
#include "Common/CommonTypes.h"
#include <cstddef>
namespace Common {
inline u32 HashAdler32(const u8* d, size_t n) { u32 a = 1, b = 0; for (size_t i = 0; i < n; i++) { a = (a + d[i]) % 65521; b = (b + a) % 65521; } return (b << 16) | a; }
inline u32 HashEctor(const u8* d, size_t n) { u32 c = 0; for (size_t i = 0; i < n; i++) c = (c >> 8) ^ ((c ^ d[i]) * 0x9E3779B1u); return c; }
}
