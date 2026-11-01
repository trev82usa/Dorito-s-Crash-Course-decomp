// Real Xbox avatars from the Windows "Xbox Original Avatars" app's download
// cache (see avatar_real.cpp).
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "xenia/avatars/asset_pack.h"
#include "xenia/avatars/guest_asset.h"

namespace dcc_avatar {

using xe::avatars::AssetId;
using xe::avatars::X_AVATAR_METADATA;

// Guest-ready asset data, laid out from offset 0, plus the offsets (in `cpu`)
// of pointer fields that need the cpu or gpu buffer base address added.
struct BuiltAssets {
  std::vector<uint8_t> cpu, gpu;
  std::vector<uint32_t> cpu_relocs, gpu_relocs;
};

class AssetCache {
 public:
  // Indexes every "<asset id>[n].bin" under `root` (recursively).
  bool Open(const std::filesystem::path& root);
  bool empty() const { return files_.empty(); }
  size_t size() const { return files_.size(); }
  const std::filesystem::path& root() const { return root_; }

  bool Read(const AssetId& id, std::vector<uint8_t>& out) const;
  bool Has(const AssetId& id) const;

  // The avatar the app showed when it last started: the parts it downloaded
  // together with its asset dependency table. False if it can't be found.
  // Cached model parts in exactly `category` that fit `gender` (1 male,
  // 2 female; parts marked 3 fit both).
  std::vector<AssetId> List(uint32_t category, uint16_t gender) const;

  bool DetectUserAvatar(X_AVATAR_METADATA& metadata,
                        std::vector<AssetId>& extra_shapes,
                        std::string& description) const;

 private:
  struct Entry {
    AssetId id;
    std::filesystem::path path;
    double mtime;
  };
  std::filesystem::path root_;
  std::unordered_map<std::string, Entry> files_;
  double toc_mtime_ = -1;
  bool have_toc_ = false;
};

// An avatar description the app cached for one of the signed-in user's
// friends (TempState/friends-<xuid>.cache).
struct FriendAvatar {
  std::string gamertag;
  uint64_t xuid = 0;
  X_AVATAR_METADATA metadata;
};

// Reads the app's friends list next to the cache; `signed_in_xuid` is taken
// from the file name. Empty if there is none.
std::vector<FriendAvatar> LoadFriendAvatars(const std::filesystem::path& cache_root,
                                            uint64_t& signed_in_xuid);

// Default location of the app's cache on this PC (empty if not found).
std::filesystem::path DefaultCachePath();

bool ParseAssetId(const std::string& text, AssetId& out);

// Builds the guest X_AVATAR_ASSETS for `metadata` (see gibbed's
// LoadAssetsToGuest). skeleton_version 2 is for title avatar API version >= 3.
bool BuildAssets(const AssetCache& cache, const X_AVATAR_METADATA& metadata,
                 const std::vector<AssetId>& extra_shapes, uint32_t category_mask,
                 uint32_t coordinate_system, uint32_t skeleton_version,
                 BuiltAssets& out);

}  // namespace dcc_avatar
