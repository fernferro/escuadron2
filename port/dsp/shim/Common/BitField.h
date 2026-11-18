#pragma once
#include <cstddef>
#include "Common/CommonTypes.h"
/* Version minima del BitField de Dolphin (solo lectura, 16 bits). */
template <std::size_t position, std::size_t bits, typename T, typename S = u16>
struct BitField {
  S storage;
  constexpr operator T() const { return static_cast<T>((storage >> position) & ((1u << bits) - 1)); }
};
