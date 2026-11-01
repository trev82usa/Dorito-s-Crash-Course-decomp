Doritos Crash Course - native PC build (ReXGlue static recompilation)
=====================================================================

No game files are included: you need your own copy of Doritos Crash
Course 2 (Xbox Live Arcade).

To play:
  1. Copy default.xex and data.pak from your copy of the game into the
     "game" folder next to the exe.
  2. Install the free "Xbox Original Avatars" app (see "Your Xbox avatar"
     below) so the player character has an avatar to show.
  3. Double-click DoritosCrashCourse.exe.

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

Your Xbox avatar
  1. Install the free "Xbox Original Avatars" app from the Microsoft Store:
     https://apps.microsoft.com/detail/9nblgggz5qdq
  2. Open it once, signed in with your Xbox account, and let your avatar
     load.
  3. Start the game. If it shows the wrong avatar, add your gamertag to
     doritos_crash_course.toml:   avatar_gamertag = "YourTag"
  The other racers use your friends' avatars. Without the app, the
  character is only drawn if game\standin_avatar.bin is present (built from
  your data.pak by tools/make_standin_avatar.py in the source repository).

If something goes wrong
  - Logs are written to the "logs" folder next to the exe. The latest log
    shows how far the game got.
  - Settings can be placed in doritos_crash_course.toml next to the exe.
  - Run from a command prompt with --log_level=debug for more detail.

This is a work-in-progress recompilation. Expect the odd bug.
