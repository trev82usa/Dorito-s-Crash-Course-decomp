// Test-only scripted controller (CMake option DCC_TEST_PAD, off by default).
//
// Replaces XamInputGetState/XamInputGetKeystrokeEx with a virtual pad whose
// state is read from the file named by $DCC_TEST_PAD, so automated tests can
// drive the game without a window-system keyboard. File format (one line):
//   <buttons hex> [lx] [ly] [right_trigger]
// e.g. "1000" = A held, "0" = released, "0 -32767 0" = stick left.
// Button and stick-direction changes also produce VK_PAD keystrokes.

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>

#include <rex/hook.h>
#include <rex/logging.h>

namespace {

struct Pad {
  uint32_t buttons = 0;
  int lx = 0, ly = 0, rt = 0;
};

std::mutex g_mutex;
Pad g_last;
uint32_t g_packet = 0;
std::deque<uint16_t> g_keys;  // vk | 0x10000 for key down

Pad ReadPad() {
  Pad p;
  const char* path = std::getenv("DCC_TEST_PAD");
  if (!path)
    return p;
  if (FILE* f = std::fopen(path, "r")) {
    unsigned b = 0;
    if (std::fscanf(f, "%x %d %d %d", &b, &p.lx, &p.ly, &p.rt) >= 1)
      p.buttons = b;
    std::fclose(f);
  }
  return p;
}

uint32_t Dirs(const Pad& p) {
  constexpr int t = 16000;
  return (p.lx < -t ? 1 : 0) | (p.lx > t ? 2 : 0) | (p.ly > t ? 4 : 0) | (p.ly < -t ? 8 : 0);
}

void Update() {
  Pad p = ReadPad();
  static const struct { uint32_t bit; uint16_t vk; } kButtons[] = {
      {0x1000, 0x5800}, {0x2000, 0x5801}, {0x4000, 0x5802}, {0x8000, 0x5803},
      {0x0001, 0x5810}, {0x0002, 0x5811}, {0x0004, 0x5812}, {0x0008, 0x5813},
      {0x0010, 0x5814}, {0x0020, 0x5815},
  };
  uint32_t changed = p.buttons ^ g_last.buttons;
  for (auto& b : kButtons) {
    if (changed & b.bit) {
      g_keys.push_back(b.vk);
      g_keys.push_back((p.buttons & b.bit) ? 1 : 0);
    }
  }
  static const uint16_t kDirVk[] = {0x5823, 0x5822, 0x5820, 0x5821};  // left right up down
  uint32_t d_old = Dirs(g_last), d_new = Dirs(p);
  for (int i = 0; i < 4; ++i) {
    if ((d_old ^ d_new) & (1u << i)) {
      g_keys.push_back(kDirVk[i]);
      g_keys.push_back((d_new >> i) & 1);
    }
  }
  if (std::memcmp(&p, &g_last, sizeof(p)) != 0)
    ++g_packet;
  g_last = p;
}

void StoreBE16(uint8_t* p, uint16_t v) { p[0] = uint8_t(v >> 8); p[1] = uint8_t(v); }
void StoreBE32(uint8_t* p, uint32_t v) {
  p[0] = uint8_t(v >> 24); p[1] = uint8_t(v >> 16); p[2] = uint8_t(v >> 8); p[3] = uint8_t(v);
}

}  // namespace

// XamInputGetState(user_index, flags, state*)
REX_HOOK_RAW(__imp__XamInputGetState) {
  uint32_t user = ctx.r3.u32 & 0xFF;
  if ((user != 0 && user != 0xFF) || !(ctx.r4.u32 & 1)) {
    if (ctx.r5.u32) std::memset(base + ctx.r5.u32, 0, 16);  // as input_fix.cpp
    ctx.r3.u64 = 1167;  // ERROR_DEVICE_NOT_CONNECTED
    return;
  }
  std::lock_guard lock(g_mutex);
  Update();
  if (uint32_t s = ctx.r5.u32) {
    uint8_t* o = base + s;
    StoreBE32(o, g_packet);
    StoreBE16(o + 4, uint16_t(g_last.buttons));
    o[6] = 0;
    o[7] = uint8_t(g_last.rt);
    StoreBE16(o + 8, uint16_t(int16_t(g_last.lx)));
    StoreBE16(o + 10, uint16_t(int16_t(g_last.ly)));
    StoreBE16(o + 12, 0);
    StoreBE16(o + 14, 0);
  }
  ctx.r3.u64 = 0;
}
static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_getstate("__imp__XamInputGetState",
                                                            &__imp__XamInputGetState);

// XamInputGetKeystrokeEx(user_index*, flags, keystroke*)
REX_HOOK_RAW(__imp__XamInputGetKeystrokeEx) {
  std::lock_guard lock(g_mutex);
  Update();
  if (g_keys.size() < 2 || !ctx.r5.u32) {
    ctx.r3.u64 = 4306;  // ERROR_EMPTY
    return;
  }
  uint16_t vk = g_keys.front(); g_keys.pop_front();
  bool down = g_keys.front(); g_keys.pop_front();
  uint8_t* o = base + ctx.r5.u32;
  StoreBE16(o, vk);
  StoreBE16(o + 2, 0);
  StoreBE16(o + 4, down ? 1 : 2);  // KEYDOWN / KEYUP
  o[6] = 0;
  o[7] = 0;
  if (ctx.r3.u32) StoreBE32(base + ctx.r3.u32, 0);
  ctx.r3.u64 = 0;
}
static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_getks("__imp__XamInputGetKeystrokeEx",
                                                         &__imp__XamInputGetKeystrokeEx);
