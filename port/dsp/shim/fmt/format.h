#pragma once
#include <string>
#include <cstdio>
namespace fmt { inline std::string format(const char*, int v) { char b[16]; snprintf(b, sizeof b, "0x%04x", v); return b; } }
