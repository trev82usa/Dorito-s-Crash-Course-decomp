// doritos_crash_course - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <filesystem>

#include <rex/filesystem.h>
#include <rex/rex_app.h>

#include "frame_stats.h"

class DoritosCrashCourseApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<DoritosCrashCourseApp>(new DoritosCrashCourseApp(ctx, "doritos_crash_course",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  // Graphics are off unless a GPU plugin is chosen; default to Xenos emulation.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (!config.graphics && config.gpu_plugin.empty())
      config.gpu_plugin = "xenos";
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // Show the game's frame rate in the F3 debug overlay.
  void OnPostSetup() override { SetGuestFrameStats(DccFrameStats); }
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}

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
