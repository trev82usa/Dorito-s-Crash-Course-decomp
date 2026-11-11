// In-game settings menu (F1 or LB+RB+X on a controller): avatar, graphics, sound and
// controls, saved to doritos_crash_course.toml next to the game.
#pragma once

#include <filesystem>

namespace rex::ui {
class ImGuiDrawer;
class Window;
}  // namespace rex::ui
namespace rex::input {
class InputSystem;
}

// Call from OnCreateDialogs (UI thread): remembers where to draw and starts the controller
// watcher.
void InitDccMenu(rex::ui::ImGuiDrawer* drawer, rex::ui::Window* window,
                    const std::filesystem::path& config_path);
// Call once the runtime exists (OnPostSetup): lets the menu read controllers and hide them
// from the game while it's open.
void ConnectDccMenuInput(rex::input::InputSystem* input);
// Opens the menu if closed, closes it if open (UI thread).
void ToggleDccMenu();
bool DccMenuOpen();
// True while the menu itself is reading the controllers (on this thread).
bool DccMenuReadingPads();
