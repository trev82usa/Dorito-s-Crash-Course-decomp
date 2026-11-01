// Minimal stand-ins for the parts of Xenia's base library that gibbed's avatar
// code uses, mapped onto ReXGlue's equivalents.
#pragma once

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>

#include <fmt/format.h>
#include <rex/logging.h>
#include <rex/types.h>

namespace xe {
using rex::byte_swap;
using rex::clamp_float;
using rex::endian_store;
template <typename T>
using be = rex::be<T>;
template <typename T>
using le = rex::le<T>;

using fourcc_t = uint32_t;
constexpr fourcc_t make_fourcc(char a, char b, char c, char d) {
  return (fourcc_t(uint8_t(a)) << 24) | (fourcc_t(uint8_t(b)) << 16) |
         (fourcc_t(uint8_t(c)) << 8) | fourcc_t(uint8_t(d));
}
constexpr fourcc_t make_fourcc(const char (&s)[5]) {
  return make_fourcc(s[0], s[1], s[2], s[3]);
}

template <typename T, typename V>
constexpr T align(T value, V alignment) {
  return (value + T(alignment) - 1) / T(alignment) * T(alignment);
}

template <typename T, size_t N>
constexpr size_t countof(T (&)[N]) {
  return N;
}

namespace filesystem {
inline FILE* OpenFile(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
  std::wstring wmode(mode, mode + std::strlen(mode));
  return _wfopen(path.c_str(), wmode.c_str());
#else
  return std::fopen(path.c_str(), mode);
#endif
}
}  // namespace filesystem
}  // namespace xe

#define static_assert_size(type, size) \
  static_assert(sizeof(type) == (size), "bad size of " #type)

#define assert_true(x) ((void)0)
#define assert_false(x) ((void)0)
#define assert_zero(x) ((void)0)
#define assert_always(...) ((void)0)
#define assert_unhandled_case(x) ((void)0)

#define XELOGE(...) REXLOG_ERROR(__VA_ARGS__)
#define XELOGW(...) REXLOG_WARN(__VA_ARGS__)
#define XELOGI(...) REXLOG_INFO(__VA_ARGS__)
#define XELOGD(...) REXLOG_DEBUG(__VA_ARGS__)
