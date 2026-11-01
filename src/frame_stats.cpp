// Feeds the F3 debug overlay's FPS line. The overlay shows the frame rate only
// when the app supplies it, so count the game's presents (VdSwap) here and
// pass them on to ReXGlue's own VdSwap.

#include <atomic>
#include <chrono>
#include <mutex>

#include <rex/hook.h>
#include <rex/logging.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include "frame_stats.h"

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

}  // namespace

REX_HOOK_RAW(__imp__VdSwap) {
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
