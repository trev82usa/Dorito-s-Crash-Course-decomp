// Trimmed from gibbed's xenia avatars branch: only AssetId, plus a pluggable
// blob source in place of the AvatarAssetPack (see avatar_assets.cpp).
#ifndef XENIA_AVATARS_ASSET_PACK_H_
#define XENIA_AVATARS_ASSET_PACK_H_

#include <cstdint>
#include <cstring>
#include <functional>
#include <string>
#include <vector>

#include "xenia/base/compat.h"

namespace xe {
namespace avatars {

struct AssetId {
  be<uint32_t> a;
  be<uint16_t> b;
  be<uint16_t> c;
  uint8_t d[8];

  bool operator==(const AssetId& other) const {
    return a == other.a && b == other.b && c == other.c &&
           !std::memcmp(d, other.d, sizeof(d));
  }
  bool operator!=(const AssetId& other) const { return !(*this == other); }
  bool is_zero() const {
    static const uint8_t zero[16] = {};
    return !std::memcmp(this, zero, sizeof(zero));
  }
  std::string to_string() const {
    return fmt::format(
        "{:08x}-{:04x}-{:04x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}",
        uint32_t(a), uint16_t(b), uint16_t(c), d[0], d[1], d[2], d[3], d[4],
        d[5], d[6], d[7]);
  }
};
static_assert_size(AssetId, 16);

// Supplies the raw STRB blob for an asset; returns false if unavailable.
using AssetPack = std::function<bool(const AssetId&, std::vector<uint8_t>&)>;

}  // namespace avatars
}  // namespace xe

#endif
