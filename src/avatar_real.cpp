// Real Xbox avatars from the Windows "Xbox Original Avatars" app.
//
// The app downloads each avatar part from Microsoft as its own file, named
// after the part's asset id ("00000008-010f-0002-c1c8-f109a19cb2e0[1].bin"), into
// its web cache. The files are the same STRB assets as the Xbox 360's
// AvatarAssetPack, so gibbed's xenia avatar code (src/avatars) turns them into
// the guest structures the game expects.
//
// Nothing is copied out of the cache: the game reads it at startup on the PC
// where the app is installed.

#include "avatar_real.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <map>
#include <regex>

#include <rex/logging.h>

#include "xenia/avatars/guest_load_asset.h"
#include "xenia/avatars/memory_block.h"

namespace dcc_avatar {

using namespace xe::avatars;

namespace {

double FileTime(const std::filesystem::path& p) {
  std::error_code ec;
  auto t = std::filesystem::last_write_time(p, ec);
  if (ec) return 0;
  return std::chrono::duration<double>(t.time_since_epoch()).count();
}

int HexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

std::string Lower(std::string s) {
  for (auto& c : s) c = char(std::tolower(uint8_t(c)));
  return s;
}

}  // namespace

bool ParseAssetId(const std::string& text, AssetId& out) {
  // 8-4-4-4-12 hex digits
  std::string hex;
  for (char c : text) {
    if (c == '-') continue;
    if (HexVal(c) < 0) return false;
    hex.push_back(c);
  }
  if (hex.size() != 32 || text.size() != 36) return false;
  uint8_t bytes[16];
  for (int i = 0; i < 16; ++i)
    bytes[i] = uint8_t(HexVal(hex[2 * i]) * 16 + HexVal(hex[2 * i + 1]));
  std::memcpy(&out, bytes, 16);  // AssetId is stored big-endian
  return true;
}

std::filesystem::path DefaultCachePath() {
  if (const char* env = std::getenv("DCC_AVATAR_CACHE")) return env;
#ifdef _WIN32
  if (const char* local = std::getenv("LOCALAPPDATA")) {
    auto p = std::filesystem::path(local) / "Packages" /
             "Microsoft.Avatars_8wekyb3d8bbwe" / "AC" / "INetCache";
    std::error_code ec;
    if (std::filesystem::is_directory(p, ec)) return p;
  }
#endif
  return {};
}

namespace {
std::vector<uint8_t> Base64Decode(const std::string& in) {
  std::vector<uint8_t> out;
  int val = 0, bits = -8;
  for (char c : in) {
    int d;
    if (c >= 'A' && c <= 'Z') d = c - 'A';
    else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
    else if (c >= '0' && c <= '9') d = c - '0' + 52;
    else if (c == '+') d = 62;
    else if (c == '/') d = 63;
    else continue;
    val = (val << 6) | d;
    bits += 6;
    if (bits >= 0) {
      out.push_back(uint8_t((val >> bits) & 0xFF));
      bits -= 8;
    }
  }
  return out;
}
}  // namespace

std::vector<FriendAvatar> LoadFriendAvatars(const std::filesystem::path& cache_root,
                                            uint64_t& signed_in_xuid) {
  std::vector<FriendAvatar> out;
  signed_in_xuid = 0;
  // <package>/AC/INetCache -> <package>/TempState
  auto temp = cache_root.parent_path().parent_path() / "TempState";
  std::error_code ec;
  for (auto it = std::filesystem::directory_iterator(temp, ec);
       !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
    std::string name = it->path().filename().string();
    if (name.rfind("friends-", 0) != 0) continue;
    try {
      signed_in_xuid = std::stoull(name.substr(8));
    } catch (...) {
    }
    std::ifstream in(it->path(), std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    // {"Xuid":123,"Gamertag":"name","Manifest":"base64..."}, in any field order
    static const std::regex kObject(R"re(\{[^{}]*\})re");
    static const std::regex kXuid(R"re("Xuid"\s*:\s*(\d+))re");
    static const std::regex kTag(R"re("Gamertag"\s*:\s*"([^"]*)")re");
    static const std::regex kManifest(R"re("Manifest"\s*:\s*"([A-Za-z0-9+/=]+)")re");
    for (auto m = std::sregex_iterator(text.begin(), text.end(), kObject);
         m != std::sregex_iterator(); ++m) {
      std::string obj = m->str();
      std::smatch x, t, mf;
      if (!std::regex_search(obj, mf, kManifest)) continue;
      auto bytes = Base64Decode(mf[1].str());
      if (bytes.size() != sizeof(X_AVATAR_METADATA)) continue;
      FriendAvatar f;
      std::memcpy(&f.metadata, bytes.data(), bytes.size());
      if (std::regex_search(obj, t, kTag)) f.gamertag = t[1].str();
      if (std::regex_search(obj, x, kXuid)) f.xuid = std::stoull(x[1].str());
      out.push_back(std::move(f));
    }
  }
  return out;
}

bool AssetCache::Open(const std::filesystem::path& root) {
  root_ = root;
  files_.clear();
  toc_mtime_ = -1;
  have_toc_ = false;
  std::error_code ec;
  for (auto it = std::filesystem::recursive_directory_iterator(
           root, std::filesystem::directory_options::skip_permission_denied, ec);
       !ec && it != std::filesystem::recursive_directory_iterator(); it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    std::string name = it->path().filename().string();
    std::string lower = Lower(name);
    if (lower.rfind("tocassetdependencies", 0) == 0) {
      double t = FileTime(it->path());
      toc_mtime_ = have_toc_ ? std::max(toc_mtime_, t) : t;
      have_toc_ = true;
      continue;
    }
    if (lower.size() < 40 || lower.substr(lower.size() - 4) != ".bin") continue;
    AssetId id;
    if (!ParseAssetId(lower.substr(0, 36), id)) continue;
    std::string key = lower.substr(0, 36);
    double t = FileTime(it->path());
    auto existing = files_.find(key);
    // Keep the newest copy if the cache holds several ([1], [2], ...).
    if (existing == files_.end() || existing->second.mtime < t)
      files_[key] = Entry{id, it->path(), t};
  }
  return !files_.empty();
}

bool AssetCache::Has(const AssetId& id) const {
  return files_.count(id.to_string()) != 0;
}

bool AssetCache::Read(const AssetId& id, std::vector<uint8_t>& out) const {
  auto it = files_.find(id.to_string());
  if (it == files_.end()) return false;
  std::ifstream in(it->second.path, std::ios::binary);
  if (!in) return false;
  out.assign(std::istreambuf_iterator<char>(in), {});
  return !out.empty();
}

std::vector<AssetId> AssetCache::List(uint32_t category, uint16_t gender) const {
  std::vector<AssetId> out;
  for (const auto& [key, e] : files_) {
    if (uint32_t(e.id.a) != category) continue;
    uint16_t g = uint16_t(e.id.c);
    if (g == gender || g == 3) out.push_back(e.id);
  }
  std::sort(out.begin(), out.end(), [](const AssetId& a, const AssetId& b) {
    return a.to_string() < b.to_string();
  });
  return out;
}

bool AssetCache::DetectUserAvatar(X_AVATAR_METADATA& md,
                                  std::vector<AssetId>& extra_shapes,
                                  std::string& description) const {
  if (!have_toc_) return false;
  // When the app starts it fetches the asset dependency table and, in the
  // same burst, every part of the signed-in user's avatar.
  std::vector<const Entry*> burst;
  for (const auto& [key, e] : files_) {
    if (e.mtime >= toc_mtime_ - 2.0 && e.mtime <= toc_mtime_ + 4.0) burst.push_back(&e);
  }
  REXLOG_INFO("Avatar: {} cached parts, {} downloaded with the app's start-up burst",
              files_.size(), burst.size());
  std::sort(burst.begin(), burst.end(),
            [](const Entry* a, const Entry* b) { return a->mtime < b->mtime; });

  std::memset(&md, 0, sizeof(md));
  md.version = 0;
  md.weight_factor = 0.0f;
  md.height_factor = 0.0f;
  // Skin, hair, lips, iris, eyebrows, eye shadow, facial hair, skin features
  // 1 and 2 (ARGB). The app doesn't cache the user's colours, so these are
  // neutral defaults (see doritos_crash_course.toml to change them).
  static const uint32_t kColors[9] = {0xffe6bc8e, 0xff4f463d, 0xffb56157,
                                      0xff82501d, 0xff4f463d, 0xffc5496d,
                                      0xff4f463d, 0xffecb6be, 0xffecb6be};
  for (int i = 0; i < 9; ++i) md.colors[i] = kColors[i];

  static const std::map<uint32_t, int> kTextureSlots = {
      {0x8000, 0}, {0x2000, 1}, {0x4000, 2}, {0x10000, 3}, {0x40000, 4}, {0x20000, 5}};
  static const std::map<uint32_t, int> kShapeSlots = {
      {0x100000, 0}, {0x80000, 1}, {0x200000, 2}};

  bool have_body = false, have_head = false;
  int component_count = 0;
  uint32_t used_components = 0;
  description.clear();
  for (const Entry* e : burst) {
    uint32_t cat = uint32_t(e->id.a);
    auto describe = [&](const char* what) {
      description += fmt::format("{} {}; ", what, e->id.to_string());
    };
    if (cat == 0x2 && !have_body) {
      md.body_component.asset_id = e->id;
      md.body_component.categories = 0x2;
      have_body = true;
      describe("body");
    } else if (cat == 0x1 && !have_head) {
      md.head_component.asset_id = e->id;
      md.head_component.categories = 0x1;
      have_head = true;
      describe("head");
    } else if (cat >= 0x4 && cat <= 0x0FFF && !(cat & used_components) &&
               component_count < 13) {
      md.components[component_count].asset_id = e->id;
      md.components[component_count].categories = uint16_t(cat);
      ++component_count;
      used_components |= cat;
      describe("part");
    } else if (auto t = kTextureSlots.find(cat); t != kTextureSlots.end()) {
      if (md.textures[t->second].asset_id.is_zero()) {
        md.textures[t->second].asset_id = e->id;
        describe("face texture");
      }
    } else if (auto s = kShapeSlots.find(cat); s != kShapeSlots.end()) {
      if (md.blend_shapes[s->second].asset_id.is_zero()) {
        md.blend_shapes[s->second].asset_id = e->id;
        describe("face shape");
      }
    } else if (cat == 0x01000000) {
      extra_shapes.push_back(e->id);
      describe("fitting");
    }
  }
  return have_body && have_head;
}

bool BuildAssets(const AssetCache& cache, const X_AVATAR_METADATA& metadata,
                 const std::vector<AssetId>& extra_shapes, uint32_t category_mask,
                 uint32_t coordinate_system, uint32_t skeleton_version,
                 BuiltAssets& out) {
  std::vector<uint8_t> cpu(4 << 20), gpu(16 << 20);
  MemoryBlock cpu_memory(cpu.data(), 16);
  MemoryBlock gpu_memory(gpu.data(), 4096);
  AssetPack source = [&cache](const AssetId& id, std::vector<uint8_t>& bytes) {
    return cache.Read(id, bytes);
  };
  if (!LoadAssetsToGuest(metadata, category_mask, 0, &source, &cpu_memory,
                         &gpu_memory, skeleton_version, coordinate_system,
                         &extra_shapes)) {
    return false;
  }
  if (cpu_memory.size() > cpu.size() || gpu_memory.size() > gpu.size()) {
    REXLOG_ERROR("Avatar: built assets overflowed the build buffers");
    return false;
  }
  out.cpu_relocs = cpu_memory.PointerOffsets(cpu.data());
  out.gpu_relocs = gpu_memory.PointerOffsets(cpu.data());
  cpu.resize(cpu_memory.size());
  gpu.resize(xe::align(gpu_memory.size(), size_t(4096)));
  out.cpu = std::move(cpu);
  out.gpu = std::move(gpu);
  return true;
}

}  // namespace dcc_avatar
