Doritos Crash Course - native PC build (ReXGlue static recompilation)
=====================================================================

To play: double-click DoritosCrashCourse.exe

Keep the "game" folder next to the exe. It holds default.xex and data.pak,
which the exe loads at startup.

Requirements
  - Windows 10 or 11, 64-bit
  - A DirectX 12 capable graphics card with current drivers
  - An Xbox controller is recommended (Xbox 360 / One / Series pads work
    through XInput). Keyboard also works:
      Move: W A S D      A: Space      B: Backspace     X: L     Y: P
      Start: Enter       Back: Tab     Triggers: Q / E  Bumpers: 1 / 3
      Camera: arrow keys               D-pad: Shift + arrow keys
    Turn keyboard mode off by setting mnk_mode = false in
    doritos_crash_course.toml.

Playing
  - Start (Enter) pauses. F3 shows the frame rate.
  - On the "not signed in to Xbox LIVE" message press B (Backspace) to
    carry on. Online features aren't available.

Your Xbox avatar (optional - without it you play as a stand-in character)
  1. Install the free "Xbox Original Avatars" app from the Microsoft Store:
     https://apps.microsoft.com/detail/9nblgggz5qdq
  2. Open it once, signed in with your Xbox account, and let your avatar
     load.
  3. Start the game. If it shows the wrong avatar, add your gamertag to
     doritos_crash_course.toml:   avatar_gamertag = "YourTag"
  The other racers use your friends' avatars.

If something goes wrong
  - Logs are written to the "logs" folder next to the exe. The latest log
    shows how far the game got.
  - Settings can be placed in doritos_crash_course.toml next to the exe.
  - Run from a command prompt with --log_level=debug for more detail.

This is a work-in-progress recompilation. Expect the odd bug.
