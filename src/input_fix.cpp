// Controller state fix.
//
// Each frame the game reads every pad twice, XamInputGetState(i, 1, &pad) and
// XamInputGetState(i, 8, &tmp), and ORs tmp's buttons into pad's. The runtime
// rejects the second call (no gamepad flag) without touching tmp, so the game
// merges whatever was left on its stack into the button state. Those ghost
// bits look like buttons held down forever, and the game only acts on a
// button when it goes from up to down, so Start (pause) and B (back) can stop
// working. The console clears the state on failure; do the same here.

#include <cstring>

#include <rex/hook.h>
#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

using PPCFunc = void (*)(PPCContext&, uint8_t*);

PPCFunc OriginalGetState() {
  static PPCFunc fn = [] {
    void* p = nullptr;
#ifdef _WIN32
    if (HMODULE m = GetModuleHandleW(L"rexruntime.dll"))
      p = reinterpret_cast<void*>(GetProcAddress(m, "__imp__XamInputGetState"));
#else
    if (void* m = dlopen("librexruntime.so", RTLD_NOW | RTLD_NOLOAD))
      p = dlsym(m, "__imp__XamInputGetState");
#endif
    if (!p) REXLOG_ERROR("Input fix: couldn't find the runtime's XamInputGetState");
    return reinterpret_cast<PPCFunc>(p);
  }();
  return fn;
}

constexpr uint32_t kXInputStateSize = 16;  // packet number + XINPUT_GAMEPAD

}  // namespace

// XamInputGetState(user_index, flags, state*)
REX_HOOK_RAW(__imp__XamInputGetState) {
  uint32_t state = ctx.r5.u32;
  if (auto fn = OriginalGetState()) {
    fn(ctx, base);
  } else {
    ctx.r3.u64 = 1167;  // ERROR_DEVICE_NOT_CONNECTED
  }
  if (ctx.r3.u32 != 0 && state != 0)
    std::memset(base + state, 0, kXInputStateSize);
}
static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_getstate_fix("__imp__XamInputGetState",
                                                                &__imp__XamInputGetState);
