// doritos_crash_course - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input_system.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/ui/keybinds.h>

#include "crash_handler.h"
#include "dcc_menu.h"
#include "frame_stats.h"
#include "game_exit.h"

class DoritosCrashCourseApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<DoritosCrashCourseApp>(new DoritosCrashCourseApp(ctx, "doritos_crash_course",
        PPCImageConfig));
  }

  void OnPreSetup(rex::RuntimeConfig& config) override {
    SetHighTimerResolution();
    RemoveSharedMemoryFiles("left over from earlier runs");
    // Graphics are off unless a GPU plugin is chosen; default to Xenos emulation.
    if (!config.graphics && config.gpu_plugin.empty())
      config.gpu_plugin = "xenos";
    // Full game, not the Xbox Live Arcade trial: report every license bit as
    // granted to XamContentGetLicenseMask (ReXGlue defaults to 0 = trial).
    rex::cvar::SetFlagByName("license_mask", "4294967295");
  }

  void OnPostSetup() override {
    if (rex::cvar::Query<bool>("dcc_crash_handler")) InstallCrashHandler();
    // Show the game's frame rate in the F3 debug overlay.
    SetGuestFrameStats(DccFrameStats);
    StartTitleExitWatch();
    RemoveSharedMemoryFiles("(this run's; freed automatically when the game closes)");
    // After the SDK set its own input rule, so the menu's rule replaces it.
    if (runtime() && runtime()->input_system())
      ConnectDccMenuInput(static_cast<rex::input::InputSystem*>(runtime()->input_system()));
  }

  // F1 (or LB+RB+X on a controller): the settings menu.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    InitDccMenu(drawer, window(),
                rex::filesystem::GetExecutableFolder() / "doritos_crash_course.toml");
    rex::ui::RegisterBind("bind_dcc_menu", "F1", "Toggle the settings menu",
                          [] { ToggleDccMenu(); });
  }

  // Closing the window runs the engine's full teardown, which can hang on a
  // game thread that never stops; end the process after a few seconds.
  bool OnWindowCloseRequested() override {
    StartExitWatchdog();
    return true;
  }
  void OnShutdown() override { StartExitWatchdog(); }

  // One-click launch: when --game_data_root isn't given, use the "game" folder
  // next to the executable (or the exe's own folder if default.xex sits there).
  void OnConfigurePaths(rex::PathConfig& paths) override {
    if (!paths.game_data_root.empty())
      return;
    auto exe_dir = rex::filesystem::GetExecutableFolder();
    std::error_code ec;
    if (std::filesystem::exists(exe_dir / "game" / "default.xex", ec))
      paths.game_data_root = exe_dir / "game";
    else if (std::filesystem::exists(exe_dir / "default.xex", ec))
      paths.game_data_root = exe_dir;
  }
};
