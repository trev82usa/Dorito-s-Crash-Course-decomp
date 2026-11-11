// Controller fixes.
//
// 1. Cleared state on failure. Each frame the game reads every pad twice,
//    XamInputGetState(i, 1, &pad) and XamInputGetState(i, 8, &tmp), and ORs
//    tmp's buttons into pad's. The runtime rejects the second call (no gamepad
//    flag) without touching tmp, so the game merged whatever was left on its
//    stack into the button state. Those ghost bits looked like buttons held
//    down forever, and the game only acts on a button when it goes from up to
//    down, so Start (pause) and B (back) could stop working. The console clears
//    the state on failure; do the same here.
//
// 2. One lock for all controller access. ReXGlue's InputSystem refreshes its
//    device list on every call with no locking, so two threads reading pads at
//    once (game threads, or the F1 menu on the window thread) can corrupt it
//    and crash. Every XamInput call the game makes, and the menu's own reads,
//    go through InputLock().

#include "input_fix.h"

#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using RuntimeFn = void (*)(PPCContext&, uint8_t*);

RuntimeFn RuntimeExport(const char* name) {
  void* p = nullptr;
#ifdef _WIN32
  if (HMODULE m = GetModuleHandleW(L"rexruntime.dll"))
    p = reinterpret_cast<void*>(GetProcAddress(m, name));
#else
  if (void* m = dlopen("librexruntime.so", RTLD_NOW | RTLD_NOLOAD))
    p = dlsym(m, name);
#endif
  if (!p) REXLOG_ERROR("Input fix: couldn't find the runtime's {}", name);
  return reinterpret_cast<RuntimeFn>(p);
}

void CallLocked(RuntimeFn fn, PPCContext& ctx, uint8_t* base) {
  if (!fn) {
    ctx.r3.u64 = 1167;  // ERROR_DEVICE_NOT_CONNECTED
    return;
  }
  std::lock_guard lock(InputLock());
  fn(ctx, base);
}

constexpr uint32_t kXInputStateSize = 16;  // packet number + XINPUT_GAMEPAD

}  // namespace

// XamInputGetState(user_index, flags, state*)
REX_HOOK_RAW(__imp__XamInputGetState) {
  static RuntimeFn fn = RuntimeExport("__imp__XamInputGetState");
  uint32_t state = ctx.r5.u32;
  CallLocked(fn, ctx, base);
  if (ctx.r3.u32 != 0 && state != 0)
    std::memset(base + state, 0, kXInputStateSize);
}
static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_getstate_fix("__imp__XamInputGetState",
                                                                &__imp__XamInputGetState);

REX_HOOK_RAW(__imp__XamInputGetCapabilities) {
  static RuntimeFn fn = RuntimeExport("__imp__XamInputGetCapabilities");
  CallLocked(fn, ctx, base);
}
static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_getcaps_fix("__imp__XamInputGetCapabilities",
                                                               &__imp__XamInputGetCapabilities);

REX_HOOK_RAW(__imp__XamInputSetState) {
  static RuntimeFn fn = RuntimeExport("__imp__XamInputSetState");
  CallLocked(fn, ctx, base);
}
static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_setstate_fix("__imp__XamInputSetState",
                                                                &__imp__XamInputSetState);

REX_HOOK_RAW(__imp__XamInputGetKeystrokeEx) {
  static RuntimeFn fn = RuntimeExport("__imp__XamInputGetKeystrokeEx");
  CallLocked(fn, ctx, base);
}
static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_getks_fix("__imp__XamInputGetKeystrokeEx",
                                                             &__imp__XamInputGetKeystrokeEx);
