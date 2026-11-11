// Clean exits and Linux memory housekeeping (from the Trials HD port).
//
//  * Quitting: when the engine starts terminating the title (the game asked to
//    return to the dashboard), or the window is closed, the engine's full
//    teardown can hang on a game thread that never stops, leaving the process
//    running invisibly. Give it a few seconds, then save the log and end it.
//  * Linux: the engine keeps the game's 4.5 GB memory in a shared-memory file
//    (/dev/shm/xenia_memory_<n>, held in RAM) and only deletes it on a perfectly
//    clean shutdown. Every crash or quick exit leaves one behind still holding
//    RAM; after enough runs the machine slows down and the game dies with SIGBUS.
//    Removing the name is always safe: a running game keeps its memory through
//    its open handle, and the RAM is freed as soon as that process ends.
#include "game_exit.h"

#include <rex/logging.h>
#include <rex/system/kernel_state.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace {

[[noreturn]] void QuitNow(const char* why) {
  REXLOG_WARN("[exit] {}; closing", why);
  rex::FlushLogging();
  std::_Exit(0);
}

}  // namespace

void StartTitleExitWatch() {
  std::thread([] {
    for (;;) {
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      auto* ks = REX_KERNEL_STATE();
      if (ks && ks->is_terminating_title()) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        QuitNow("game asked to quit");
      }
    }
  }).detach();
}

void StartExitWatchdog() {
  static std::once_flag once;
  std::call_once(once, [] {
    std::thread([] {
      std::this_thread::sleep_for(std::chrono::seconds(3));
      QuitNow("window closed");
    }).detach();
  });
}

void RemoveSharedMemoryFiles(const char* when) {
#if defined(__linux__)
  std::error_code ec;
  int removed = 0;
  for (const auto& e : std::filesystem::directory_iterator("/dev/shm", ec)) {
    const std::string name = e.path().filename().string();
    if (name.rfind("xenia_memory_", 0) != 0) continue;
    std::error_code rm_ec;
    if (std::filesystem::remove(e.path(), rm_ec)) ++removed;
  }
  if (removed) REXLOG_INFO("[memory] removed {} shared-memory file(s) {}", removed, when);
#else
  (void)when;
#endif
}
