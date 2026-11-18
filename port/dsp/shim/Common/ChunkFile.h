#pragma once
#include <cstddef>
class PointerWrap {
public:
  template <class T> void Do(T&) {}
  template <class T> void DoArray(T*, size_t) {}
  template <class T> void DoArray(T&) {}
  bool IsReadMode() const { return false; }
};
