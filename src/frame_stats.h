#pragma once
#include <rex/ui/overlay/debug_overlay.h>

// Frame rate of the game's own presents, for the F3 overlay.
rex::ui::FrameStats DccFrameStats();

// Windows: 1 ms (or finer) timer resolution so short sleeps aren't rounded to 15.6 ms.
void SetHighTimerResolution();
