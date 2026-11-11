#pragma once

#include <mutex>

// Held around every call into ReXGlue's InputSystem (see input_fix.cpp): it
// isn't safe to use from two threads at once.
std::recursive_mutex& InputLock();
