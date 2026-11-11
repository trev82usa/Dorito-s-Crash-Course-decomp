Doritos Crash Course - native Linux build (ReXGlue static recompilation)
=======================================================================

To play: run ./DoritosCrashCourse from this folder (or double-click it in
your file manager; if it opens as a text file, right-click it, choose
"Run as a Program").

No game files are included: copy default.xex and data.pak from your own
copy of Doritos Crash Course 2 (Xbox Live Arcade) into the "game" folder
next to the program.

Requirements
  - 64-bit Linux with glibc 2.38 or newer: Ubuntu 24.04 or newer, Fedora 39+,
    Debian 13+, or similar
  - A graphics driver with Vulkan support. Ubuntu's standard drivers have it
    (Mesa for AMD/Intel; the proprietary driver for NVIDIA). If the game
    can't start its graphics, install it with:
        sudo apt install mesa-vulkan-drivers
  - An Xbox controller is recommended (Xbox 360 / One / Series pads work,
    wired or Bluetooth). Keyboard also works:
      Move: W A S D      A: Space      B: Backspace     X: L     Y: P
      Start: Enter       Back: Tab     Triggers: Q / E  Bumpers: 1 / 3
      Camera: arrow keys               D-pad: Shift + arrow keys
    Turn keyboard mode off by setting mnk_mode = false in
    doritos_crash_course.toml.

Playing
  - F1 (or LB + RB + X on a controller) opens the settings menu: avatar,
    resolution, fullscreen, frame limiter, vsync, texture filtering, sound
    and controls. Press Save to keep changes.
  - Start (Enter) pauses. F3 shows the frame rate.
  - If the game stutters on a high-refresh screen, turn on the frame
    limiter in F1 -> Graphics & Sound.
  - On the "not signed in to Xbox LIVE" message press B (Backspace) to
    carry on. Online features aren't available.

Avatars
  On Linux the player is a stand-in character: the "Xbox Original Avatars"
  app that supplies real Xbox avatars is Windows-only. If you have a copy of
  that app's folder from a Windows PC
  (%LOCALAPPDATA%\Packages\Microsoft.Avatars_8wekyb3d8bbwe), you can use it
  by starting the game with:
      DCC_AVATAR_CACHE=/path/to/Microsoft.Avatars_8wekyb3d8bbwe/AC/INetCache ./DoritosCrashCourse

If something goes wrong
  - If the game crashes it writes crash.txt next to the program; please
    send it along with the newest log.
  - Logs are written to the "logs" folder next to the program. The latest
    log shows how far the game got.
  - Settings can be placed in doritos_crash_course.toml next to the program.
  - Run from a terminal with --log_level=debug for more detail.

This is a work-in-progress recompilation. Expect the odd bug.
