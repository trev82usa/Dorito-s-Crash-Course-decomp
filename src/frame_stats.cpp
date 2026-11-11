// Feeds the F3 debug overlay's FPS line. The overlay shows the frame rate only
// when the app supplies it, so count the game's presents (VdSwap) here and
// pass them on to ReXGlue's own VdSwap. Also the optional frame limiter and,
// on Windows, the timer resolution (both from the Trials HD port).

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#pragma comment(lib, "winmm.lib")
#else
#include <dlfcn.h>
#endif

#include "frame_stats.h"

REXCVAR_DEFINE_INT32(dcc_fps_cap, 0, "DoritosCrashCourse",
                     "Frame limiter: 0 = off (vsync paces the game), 60 = hold an even 60 fps. "
                     "Can smooth stutter on high-refresh screens / some NVIDIA setups")
    .range(0, 240);

namespace {

using PPCFunc = void (*)(PPCContext&, uint8_t*);

PPCFunc OriginalVdSwap() {
  static PPCFunc fn = [] {
    void* p = nullptr;
#ifdef _WIN32
    if (HMODULE m = GetModuleHandleW(L"rexruntime.dll"))
      p = reinterpret_cast<void*>(GetProcAddress(m, "__imp__VdSwap"));
#else
    if (void* m = dlopen("librexruntime.so", RTLD_NOW | RTLD_NOLOAD))
      p = dlsym(m, "__imp__VdSwap");
#endif
    if (!p) REXLOG_ERROR("Frame stats: couldn't find the runtime's VdSwap");
    return reinterpret_cast<PPCFunc>(p);
  }();
  return fn;
}

std::mutex g_mutex;
uint64_t g_frames = 0;
std::chrono::steady_clock::time_point g_window_start = std::chrono::steady_clock::now();
uint64_t g_window_frames = 0;
double g_fps = 0, g_frame_ms = 0;

// Frame limiter: waits until the next frame slot before the game presents, so
// frames leave at an even interval. Sleeps most of the wait and spins the last
// ~1.5 ms for accuracy. If the game falls behind by more than a frame, the
// schedule restarts instead of trying to catch up.
void FrameCap() {
  static int cached_cap = 0;
  static int counter = 0;
  if ((counter++ & 31) == 0) cached_cap = REXCVAR_GET(dcc_fps_cap);
  const int cap = cached_cap;
  using clock = std::chrono::steady_clock;
  static clock::time_point next{};
  if (cap <= 0) {
    next = {};
    return;
  }
  const auto interval = std::chrono::duration_cast<clock::duration>(
      std::chrono::duration<double>(1.0 / cap));
  auto now = clock::now();
  if (next.time_since_epoch().count() == 0 || now > next + interval) {
    next = now + interval;
    return;
  }
  if (now < next) {
    const auto spin = std::chrono::microseconds(1500);
    if (next - now > spin) std::this_thread::sleep_until(next - spin);
    while (clock::now() < next) std::this_thread::yield();
  }
  next += interval;
}

}  // namespace

REX_HOOK_RAW(__imp__VdSwap) {
  FrameCap();
  {
    std::lock_guard lock(g_mutex);
    ++g_frames;
    ++g_window_frames;
    auto now = std::chrono::steady_clock::now();
    double secs = std::chrono::duration<double>(now - g_window_start).count();
    if (secs >= 0.5) {
      g_fps = g_window_frames / secs;
      g_frame_ms = g_fps > 0 ? 1000.0 / g_fps : 0;
      g_window_frames = 0;
      g_window_start = now;
    }
  }
  if (auto fn = OriginalVdSwap()) fn(ctx, base);
}
static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_vdswap("__imp__VdSwap", &__imp__VdSwap);

rex::ui::FrameStats DccFrameStats() {
  std::lock_guard lock(g_mutex);
  rex::ui::FrameStats s;
  s.frame_count = g_frames;
  s.fps = g_fps;
  s.frame_time_ms = g_frame_ms;
  return s;
}

void SetHighTimerResolution() {
#ifdef _WIN32
  // Windows rounds sleeps and timed waits up to the system timer tick (15.6 ms
  // by default). The game and the runtime wait in short steps every frame, so
  // ask for 1 ms, and 0.5 ms through ntdll where available.
  timeBeginPeriod(1);
  using NtSetTimerResolutionFn = LONG(NTAPI*)(ULONG, BOOLEAN, PULONG);
  if (auto fn = reinterpret_cast<NtSetTimerResolutionFn>(
          GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtSetTimerResolution"))) {
    ULONG actual = 0;
    fn(5000, TRUE, &actual);  // 100 ns units
  }
#endif
}
