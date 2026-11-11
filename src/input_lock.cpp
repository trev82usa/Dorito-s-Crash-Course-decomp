#include "input_fix.h"

// Shared by the XamInput hooks (input_fix.cpp) and the F1 menu (dcc_menu.cpp).
std::recursive_mutex& InputLock() {
  static std::recursive_mutex m;
  return m;
}
