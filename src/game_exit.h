#pragma once

// Ends the process a few seconds after the engine starts terminating the title.
void StartTitleExitWatch();
// Ends the process a few seconds after the window is closed (if teardown hangs).
void StartExitWatchdog();
// Linux: deletes leftover /dev/shm/xenia_memory_* files (see game_exit.cpp).
void RemoveSharedMemoryFiles(const char* when);
