#pragma once
#include <limits>
namespace MathUtil {
template <typename T, typename F> constexpr T SaturatingCast(F v) {
  if (v < (F)std::numeric_limits<T>::min()) return std::numeric_limits<T>::min();
  if (v > (F)std::numeric_limits<T>::max()) return std::numeric_limits<T>::max();
  return (T)v;
}
}
