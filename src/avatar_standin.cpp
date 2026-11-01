// Xbox avatars for the game's XamAvatar* imports.
//
// The game's characters are Xbox avatars: the player's own, plus random ones.
// On a console XAM builds them from the system AvatarAssetPack; ReXGlue has no
// such pack, so its XamAvatar* exports report failure and the game draws nobody.
// These hooks replace the imports the game uses and serve:
//
//  1. Real avatars, when the Windows "Xbox Original Avatars" app is installed
//     and has been opened: its download cache holds the same avatar parts as
//     the 360's pack (avatar_real.cpp). The player gets the avatar the app
//     showed; random avatars are put together from other parts in the cache.
//  2. Otherwise a stand-in built from the game's own placeholder character
//     (game/standin_avatar.bin, made by tools/make_standin_avatar.py).
//
// Struct layouts and calling conventions: gibbed's xenia avatars branch, and
// the game's avatar wrappers at 0x825EEC88..0x825EF1F0.

#include <array>
#include <cctype>
#include <atomic>
#include <bit>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <random>
#include <sstream>
#include <vector>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include "avatar_real.h"
#include "standin_maker.h"

REXCVAR_DEFINE_STRING(avatar_mode, "auto", "Avatars",
                      "auto: use your avatar from the Xbox Original Avatars app if it is "
                      "installed, else the stand-in; standin: always the stand-in");
REXCVAR_DEFINE_STRING(avatar_colors, "", "Avatars",
                      "Nine ARGB hex colours for the real avatar: skin, hair, lips, eyes, "
                      "eyebrows, eye shadow, facial hair, skin features 1 and 2 "
                      "(e.g. ffe6bc8e,ff4f463d,...). Empty = defaults.");
REXCVAR_DEFINE_STRING(avatar_gamertag, "", "Avatars",
                      "Gamertag whose avatar you play as (it must be in the app's cached "
                      "friends list); empty = the signed-in account");
REXCVAR_DEFINE_BOOL(avatar_random_outfits, true, "Avatars",
                    "Dress other avatars in random parts from the app's cache");
// Troubleshooting switches for real avatars.
REXCVAR_DEFINE_INT32(avatar_debug_parts, 0x1fff, "Avatars",
                     "Category mask of avatar parts to load (debugging)");
REXCVAR_DEFINE_BOOL(avatar_fittings, true, "Avatars",
                    "Apply the extra body-fitting shapes to clothes");
REXCVAR_DEFINE_BOOL(avatar_face_shapes, true, "Avatars",
                    "Apply face shape adjustments");

namespace {

using dcc_avatar::AssetId;
using dcc_avatar::X_AVATAR_METADATA;

constexpr uint32_t kSuccess = 0;
constexpr uint32_t kIoPending = 997;          // ERROR_IO_PENDING
constexpr uint32_t kFail = 0x80004005;         // E_FAIL
constexpr uint32_t kInvalidArg = 0x80070057;   // E_INVALIDARG
constexpr uint32_t kMetadataSize = 1000;
constexpr uint32_t kCategoryBody = 1u << 1;
constexpr uint64_t kStandinXuid = 0x0DCC0DCC0DCC0DCCull;  // tags stand-in metadata
static_assert(sizeof(X_AVATAR_METADATA) == kMetadataSize);

struct Variant {
  std::vector<uint8_t> cpu, gpu;
  std::vector<uint32_t> cpu_relocs, gpu_relocs;
};

struct State {
  bool standin_loaded = false;
  std::array<uint8_t, kMetadataSize> standin_metadata{};
  Variant standin[4];  // [with_body * 2 + coordinate_system]

  bool real = false;
  dcc_avatar::AssetCache cache;
  X_AVATAR_METADATA user{};
  std::vector<AssetId> user_shapes;
  std::vector<dcc_avatar::FriendAvatar> others;  // friends' avatars for opponents
  size_t next_other = 0;
  std::mutex random_mutex;
  std::mt19937 rng{0xD0217705u};
};

State g;
std::once_flag g_load_once;
std::atomic<uint32_t> g_coordinate_system{0};

uint32_t ReadLE32(const uint8_t*& p) {
  uint32_t v = uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 |
               uint32_t(p[3]) << 24;
  p += 4;
  return v;
}

void LoadStandin() {
  auto exe_dir = rex::filesystem::GetExecutableFolder();
  std::filesystem::path candidates[] = {
      exe_dir / "game" / "standin_avatar.bin",
      exe_dir / "standin_avatar.bin",
  };
  std::vector<uint8_t> file;
  for (auto& path : candidates) {
    std::ifstream in(path, std::ios::binary);
    if (!in)
      continue;
    file.assign(std::istreambuf_iterator<char>(in), {});
    REXLOG_INFO("Avatar: loading stand-in avatar from {}", path.string());
    break;
  }
  if (file.empty()) {
    // Not shipped: build it from the game's own data.pak and keep a copy.
    auto pak = exe_dir / "game" / "data.pak";
    if (dcc_avatar::MakeStandinFromPak(pak, file)) {
      REXLOG_INFO("Avatar: made the stand-in avatar from {}", pak.string());
      std::ofstream save(candidates[0], std::ios::binary);
      if (save) save.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
    } else {
      file.clear();
    }
  }
  if (file.size() < 8 + kMetadataSize || std::memcmp(file.data(), "DCSA", 4) != 0) {
    REXLOG_ERROR("Avatar: game/standin_avatar.bin is missing or invalid");
    return;
  }
  const uint8_t* p = file.data() + 8;
  const uint8_t* end = file.data() + file.size();
  std::memcpy(g.standin_metadata.data(), p, kMetadataSize);
  X_AVATAR_METADATA md;
  std::memcpy(&md, g.standin_metadata.data(), kMetadataSize);
  md.owner_xuid = kStandinXuid;
  std::memcpy(g.standin_metadata.data(), &md, kMetadataSize);
  p += kMetadataSize;
  for (auto& v : g.standin) {
    if (end - p < 16)
      return;
    uint32_t cpu_size = ReadLE32(p), gpu_size = ReadLE32(p);
    uint32_t ncr = ReadLE32(p), ngr = ReadLE32(p);
    if (uint64_t(end - p) < uint64_t(ncr + ngr) * 4 + cpu_size + gpu_size)
      return;
    for (uint32_t i = 0; i < ncr; ++i) v.cpu_relocs.push_back(ReadLE32(p));
    for (uint32_t i = 0; i < ngr; ++i) v.gpu_relocs.push_back(ReadLE32(p));
    v.cpu.assign(p, p + cpu_size);
    p += cpu_size;
    v.gpu.assign(p, p + gpu_size);
    p += gpu_size;
  }
  g.standin_loaded = true;
}

void ApplyColorOverrides(X_AVATAR_METADATA& md) {
  std::string text = REXCVAR_GET(avatar_colors);
  if (text.empty()) return;
  std::stringstream ss(text);
  std::string item;
  for (int i = 0; i < 9 && std::getline(ss, item, ','); ++i) {
    try {
      md.colors[i] = uint32_t(std::stoul(item, nullptr, 16));
    } catch (...) {
    }
  }
}

void LoadReal() {
  if (REXCVAR_GET(avatar_mode) == "standin") {
    REXLOG_INFO("Avatar: avatar_mode = standin");
    return;
  }
  auto root = dcc_avatar::DefaultCachePath();
  if (root.empty()) {
    REXLOG_INFO("Avatar: Xbox Original Avatars app cache not found; using the stand-in");
    return;
  }
  if (!g.cache.Open(root)) {
    REXLOG_INFO("Avatar: no avatar parts in {}; using the stand-in", root.string());
    return;
  }
  std::string description;
  uint64_t signed_in = 0;
  auto friends = dcc_avatar::LoadFriendAvatars(root, signed_in);
  std::string wanted = REXCVAR_GET(avatar_gamertag);
  auto lower = [](std::string v) {
    for (auto& c : v) c = char(std::tolower(uint8_t(c)));
    return v;
  };
  int chosen = -1;
  for (size_t i = 0; i < friends.size(); ++i) {
    bool match = wanted.empty() ? friends[i].xuid == signed_in
                                : lower(friends[i].gamertag) == lower(wanted);
    if (match) chosen = int(i);
  }
  if (chosen >= 0) {
    g.user = friends[chosen].metadata;
    g.user_shapes.clear();
    description = fmt::format("{}'s avatar description", friends[chosen].gamertag);
  } else {
    if (!wanted.empty())
      REXLOG_WARN("Avatar: gamertag '{}' isn't in the app's friends list", wanted);
    if (!g.cache.DetectUserAvatar(g.user, g.user_shapes, description)) {
      REXLOG_WARN("Avatar: couldn't find your avatar in the app cache ({}); open the "
                  "Xbox Original Avatars app once, then restart the game", root.string());
      return;
    }
  }
  for (size_t i = 0; i < friends.size(); ++i)
    if (int(i) != chosen) g.others.push_back(friends[i]);
  REXLOG_INFO("Avatar: {} friends' avatars available for other characters", g.others.size());
  ApplyColorOverrides(g.user);
  // Make sure it actually builds before committing to it.
  dcc_avatar::BuiltAssets test;
  if (!dcc_avatar::BuildAssets(g.cache, g.user, g.user_shapes, 0x1fff, 0, 2, test)) {
    REXLOG_ERROR("Avatar: your avatar's parts failed to load; using the stand-in");
    return;
  }
  REXLOG_INFO("Avatar: using your avatar from {}: {}", root.string(), description);
  REXLOG_INFO("Avatar: built {} + {} bytes", test.cpu.size(), test.gpu.size());
  g.real = true;
}

void Load() {
  LoadStandin();
  LoadReal();
}

bool EnsureLoaded() {
  std::call_once(g_load_once, Load);
  return g.standin_loaded || g.real;
}

const Variant& PickStandin(uint32_t category_mask) {
  uint32_t body = (category_mask & kCategoryBody) ? 1 : 0;
  uint32_t coord = g_coordinate_system.load() ? 1 : 0;
  return g.standin[body * 2 + coord];
}

// Real-avatar buffer sizes the game should allocate (XAM's own per-category
// budget, as in gibbed's XamAvatarGetAssetsResultSize).
void RealSizes(uint32_t category_mask, uint32_t& cpu, uint32_t& gpu) {
  static const uint32_t kInfo[13][2] = {
      {0x800, 0x53800}, {0x400, 0x12C00}, {0x800, 0xE000}, {0x800, 0x14400},
      {0x800, 0xF000},  {0x800, 0xD400},  {0x800, 0xA400}, {0x800, 0x6800},
      {0x800, 0x8400},  {0x800, 0x5C00},  {0x800, 0x5800}, {0x800, 0x5C00},
      {0x800, 0x19000}};
  cpu = (category_mask & 0x1000) ? 0x3147C : 0x1F80;
  gpu = 0;
  for (int i = 0; i < 13; ++i) {
    if (category_mask & (1u << i)) {
      cpu += (kInfo[i][0] + 15) & ~15u;
      gpu += (kInfo[i][1] + 4095) & ~4095u;
    }
  }
}

// Host pointer for a guest address. On Windows the 0xE0000000+ physical
// views are mapped 4 KB further along (Windows' 64 KB allocation granularity),
// exactly as the recompiled code's REX_RAW_ADDR does.
uint8_t* Host(uint8_t* base, uint32_t guest_addr) {
#if defined(_WIN32) || (defined(__APPLE__) && defined(__aarch64__))
  if (guest_addr >= 0xE0000000u)
    return base + guest_addr + 0x1000u;
#endif
  return base + guest_addr;
}

void StoreBE32(uint8_t* base, uint32_t guest_addr, uint32_t value) {
  value = std::byteswap(value);
  std::memcpy(Host(base, guest_addr), &value, 4);
}

uint32_t LoadBE32(const uint8_t* p) {
  uint32_t v;
  std::memcpy(&v, p, 4);
  return std::byteswap(v);
}

void WriteVariant(uint8_t* base, const Variant& v, uint32_t cpu_buf, uint32_t gpu_buf) {
  std::memcpy(Host(base, cpu_buf), v.cpu.data(), v.cpu.size());
  if (!v.gpu.empty())
    std::memcpy(Host(base, gpu_buf), v.gpu.data(), v.gpu.size());
  for (uint32_t off : v.cpu_relocs)
    StoreBE32(base, cpu_buf + off, LoadBE32(Host(base, cpu_buf + off)) + cpu_buf);
  for (uint32_t off : v.gpu_relocs)
    StoreBE32(base, cpu_buf + off, LoadBE32(Host(base, cpu_buf + off)) + gpu_buf);
}

// Finishes an XAM call: completes the overlapped (if any) and returns what the
// caller expects (ERROR_IO_PENDING for overlapped calls).
uint32_t Finish(uint32_t overlapped, uint32_t result) {
  if (!overlapped)
    return result;
  rex::system::kernel_state()->CompleteOverlappedImmediate(overlapped, result);
  return kIoPending;
}

void WriteMetadata(uint8_t* base, uint32_t dest, const void* md) {
  std::memcpy(Host(base, dest), md, kMetadataSize);
}

void WriteUserMetadata(uint8_t* base, uint32_t dest) {
  if (g.real)
    WriteMetadata(base, dest, &g.user);
  else
    WriteMetadata(base, dest, g.standin_metadata.data());
}

// A random avatar dressed from parts in the app's cache.
X_AVATAR_METADATA RandomMetadata() {
  std::lock_guard lock(g.random_mutex);
  X_AVATAR_METADATA md = g.user;
  if (!REXCVAR_GET(avatar_random_outfits))
    return md;
  if (!g.others.empty()) {
    // Your friends' own avatars, in turn.
    return g.others[g.next_other++ % g.others.size()].metadata;
  }
  auto pick = [&](const std::vector<AssetId>& list, AssetId& out) {
    if (list.empty()) return false;
    out = list[std::uniform_int_distribution<size_t>(0, list.size() - 1)(g.rng)];
    return true;
  };
  uint16_t gender = std::uniform_int_distribution<int>(1, 2)(g.rng);
  auto bodies = g.cache.List(0x2, gender);
  if (bodies.empty()) {
    gender = uint16_t(g.user.body_component.asset_id.c);
    bodies = g.cache.List(0x2, gender);
  }
  if (!pick(bodies, md.body_component.asset_id))
    return g.user;
  // Clothes and hair: one part per category, keeping the user's if none fits.
  int n = 0;
  std::memset(md.components, 0, sizeof(md.components));
  for (uint32_t cat : {0x4u, 0x8u, 0x10u, 0x20u}) {
    AssetId id{};
    bool ok = pick(g.cache.List(cat, gender), id);
    if (!ok) {
      for (const auto& c : g.user.components)
        if (uint32_t(c.categories) == cat) { id = c.asset_id; ok = true; }
    }
    if (ok && n < 13) {
      md.components[n].asset_id = id;
      md.components[n].categories = uint16_t(cat);
      ++n;
    }
  }
  static const uint32_t kSkin[] = {0xfff0c8a0, 0xffe6bc8e, 0xffd7aa71, 0xffb98257,
                                   0xff8d5a3b, 0xff5e3a28};
  static const uint32_t kHair[] = {0xff0d0d0d, 0xff4f463d, 0xff753e1f, 0xffb85e22,
                                   0xffd8b26a, 0xff9a9a9a};
  md.colors[0] = kSkin[std::uniform_int_distribution<int>(0, 5)(g.rng)];
  uint32_t hair = kHair[std::uniform_int_distribution<int>(0, 5)(g.rng)];
  md.colors[1] = md.colors[4] = md.colors[6] = hair;
  md.owner_xuid = 0;
  return md;
}

}  // namespace

#define DCC_AVATAR_HOOK(name)                                                \
  static void dcc_avatar_##name(PPCContext& ctx, uint8_t* base);              \
  REX_HOOK_RAW(__imp__##name) {                                               \
    REXLOG_DEBUG("Avatar: " #name "({:#x}, {:#x}, {:#x}, {:#x})", ctx.r3.u32, \
                 ctx.r4.u32, ctx.r5.u32, ctx.r6.u32);                         \
    dcc_avatar_##name(ctx, base);                                             \
    REXLOG_DEBUG("Avatar: " #name " -> {:#x}", ctx.r3.u32);                   \
  }                                                                           \
  static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_##name("__imp__" #name, \
                                                            &__imp__##name);  \
  static void dcc_avatar_##name(PPCContext& ctx, uint8_t* base)

// XamAvatarInitialize(version, coordinate_system, processor, texture_fns,
//                     work_buffer, work_buffer_size)
DCC_AVATAR_HOOK(XamAvatarInitialize) {
  (void)base;
  g_coordinate_system = ctx.r4.u32;
  bool ok = EnsureLoaded();
  REXLOG_INFO("Avatar: XamAvatarInitialize(version {}, coordinate system {}) -> {}",
              ctx.r3.u32, ctx.r4.u32,
              !ok ? "failed" : g.real ? "your avatar" : "stand-in avatar");
  ctx.r3.u64 = ok ? kSuccess : kFail;
}

DCC_AVATAR_HOOK(XamAvatarShutdown) {
  (void)ctx;
  (void)base;
}

// XamAvatarGetAssetsResultSize(category_mask, out_cpu_size*, out_gpu_size*)
DCC_AVATAR_HOOK(XamAvatarGetAssetsResultSize) {
  if (!EnsureLoaded() || !ctx.r4.u32 || !ctx.r5.u32) {
    ctx.r3.u64 = kInvalidArg;
    return;
  }
  uint32_t cpu = 0, gpu = 0;
  if (g.real)
    RealSizes(ctx.r3.u32, cpu, gpu);
  if (g.standin_loaded) {
    const auto& v = PickStandin(ctx.r3.u32);
    cpu = std::max(cpu, uint32_t(v.cpu.size()));
    gpu = std::max(gpu, uint32_t(v.gpu.size()));
  }
  StoreBE32(base, ctx.r4.u32, cpu);
  StoreBE32(base, ctx.r5.u32, gpu);
  ctx.r3.u64 = kSuccess;
}

// XamAvatarGetAssets(metadata*, category_mask, flags, cpu_buffer, gpu_buffer,
//                    overlapped*)
DCC_AVATAR_HOOK(XamAvatarGetAssets) {
  uint32_t md_ptr = ctx.r3.u32, mask = ctx.r4.u32, cpu_buf = ctx.r6.u32,
           gpu_buf = ctx.r7.u32;
  uint32_t overlapped = ctx.r8.u32;
  if (!EnsureLoaded() || !cpu_buf || !gpu_buf || !md_ptr) {
    ctx.r3.u64 = Finish(overlapped, kFail);
    return;
  }
  X_AVATAR_METADATA md;
  std::memcpy(&md, Host(base, md_ptr), kMetadataSize);
  bool served = false;
  if (g.real && uint64_t(md.owner_xuid) != kStandinXuid) {
    uint32_t cpu_max, gpu_max;
    RealSizes(mask, cpu_max, gpu_max);
    dcc_avatar::BuiltAssets built;
    // The user's extra fitting shapes only apply to the parts they target.
    static const std::vector<AssetId> kNoShapes;
    if (!REXCVAR_GET(avatar_face_shapes))
      std::memset(md.blend_shapes, 0, sizeof(md.blend_shapes));
    uint32_t build_mask = mask & uint32_t(REXCVAR_GET(avatar_debug_parts));
    if (dcc_avatar::BuildAssets(g.cache, md,
                                REXCVAR_GET(avatar_fittings) ? g.user_shapes : kNoShapes,
                                build_mask, g_coordinate_system, 2, built) &&
        built.cpu.size() <= cpu_max && built.gpu.size() <= gpu_max) {
      Variant v{std::move(built.cpu), std::move(built.gpu), std::move(built.cpu_relocs),
                std::move(built.gpu_relocs)};
      WriteVariant(base, v, cpu_buf, gpu_buf);
      served = true;
      REXLOG_DEBUG("Avatar: built avatar ({} + {} bytes)", v.cpu.size(), v.gpu.size());
    } else {
      REXLOG_WARN("Avatar: an avatar failed to build; using the stand-in for it");
    }
  }
  if (!served && g.standin_loaded) {
    WriteVariant(base, PickStandin(mask), cpu_buf, gpu_buf);
    served = true;
  }
  if (gpu_buf & 0xFFF)
    REXLOG_WARN("Avatar: GPU buffer {:#010x} is not 4 KB aligned; textures may be wrong", gpu_buf);
  ctx.r3.u64 = Finish(overlapped, served ? kSuccess : kFail);
}

// XamAvatarGetManifestLocalUser(user_index, metadata*, overlapped*)
DCC_AVATAR_HOOK(XamAvatarGetManifestLocalUser) {
  uint32_t overlapped = ctx.r5.u32;
  if (!EnsureLoaded() || !ctx.r4.u32) {
    ctx.r3.u64 = Finish(overlapped, kFail);
    return;
  }
  WriteUserMetadata(base, ctx.r4.u32);
  REXLOG_INFO("Avatar: serving {} for user {}", g.real ? "your avatar" : "the stand-in",
              ctx.r3.u32);
  ctx.r3.u64 = Finish(overlapped, kSuccess);
}

// XamAvatarGetMetadataSignedOutProfileCount(out_count*, unknown)
DCC_AVATAR_HOOK(XamAvatarGetMetadataSignedOutProfileCount) {
  if (ctx.r3.u32)
    StoreBE32(base, ctx.r3.u32, EnsureLoaded() ? 1 : 0);
  ctx.r3.u64 = kSuccess;
}

// XamAvatarGetMetadataSignedOutProfile(index, metadata*, overlapped*)
DCC_AVATAR_HOOK(XamAvatarGetMetadataSignedOutProfile) {
  uint32_t overlapped = ctx.r5.u32;
  if (!EnsureLoaded() || !ctx.r4.u32) {
    ctx.r3.u64 = Finish(overlapped, kFail);
    return;
  }
  WriteUserMetadata(base, ctx.r4.u32);
  ctx.r3.u64 = Finish(overlapped, kSuccess);
}

// XamAvatarGetMetadataRandom(category_mask, count, metadatas*, overlapped*)
DCC_AVATAR_HOOK(XamAvatarGetMetadataRandom) {
  uint32_t count = ctx.r4.u32, dest = ctx.r5.u32, overlapped = ctx.r6.u32;
  if (!EnsureLoaded() || !dest) {
    ctx.r3.u64 = Finish(overlapped, kFail);
    return;
  }
  for (uint32_t i = 0; i < count; ++i) {
    if (g.real) {
      X_AVATAR_METADATA md = RandomMetadata();
      WriteMetadata(base, dest + i * kMetadataSize, &md);
    } else {
      WriteMetadata(base, dest + i * kMetadataSize, g.standin_metadata.data());
    }
  }
  ctx.r3.u64 = Finish(overlapped, kSuccess);
}

// XamAvatarLoadAnimation(asset_id*, flags, animation*, overlapped*): the game
// animates avatars with its own Cal3D animations; system avatar animations
// aren't provided.
DCC_AVATAR_HOOK(XamAvatarLoadAnimation) {
  (void)base;
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true))
    REXLOG_INFO("Avatar: XamAvatarLoadAnimation isn't provided");
  ctx.r3.u64 = Finish(ctx.r6.u32, kFail);
}
