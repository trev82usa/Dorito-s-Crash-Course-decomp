// Xbox Live Vision camera (XUsbcam*) imports.
//
// The ReXGlue kernel doesn't provide these. The game supports the optional
// camera, so it needs the symbols to link; at runtime they report that no
// camera is connected, which is the normal state on most consoles.

#include <atomic>

#include <rex/hook.h>
#include <rex/logging.h>

namespace {

constexpr uint32_t kErrorDeviceNotConnected = 1167;  // ERROR_DEVICE_NOT_CONNECTED
constexpr uint32_t kUsbcamStateNotPresent = 0;

void NoteCameraUse(const char* name) {
  static std::atomic<bool> logged{false};
  if (!logged.exchange(true))
    REXLOG_INFO("{}: no Xbox Live Vision camera; reporting it as disconnected", name);
}

}  // namespace

#define DCC_USBCAM_STUB(name, retval)                                        \
  REX_HOOK_RAW(__imp__##name) {                                              \
    (void)base;                                                              \
    NoteCameraUse(#name);                                                    \
    ctx.r3.u64 = (retval);                                                   \
  }                                                                          \
  static rex::ppc::detail::PPCFuncRegistrar _dcc_reg_##name("__imp__" #name, \
                                                            &__imp__##name);

DCC_USBCAM_STUB(XUsbcamCreate, kErrorDeviceNotConnected)
DCC_USBCAM_STUB(XUsbcamDestroy, 0)
DCC_USBCAM_STUB(XUsbcamGetState, kUsbcamStateNotPresent)
DCC_USBCAM_STUB(XUsbcamSetConfig, kErrorDeviceNotConnected)
DCC_USBCAM_STUB(XUsbcamSetView, kErrorDeviceNotConnected)
DCC_USBCAM_STUB(XUsbcamSetCaptureMode, kErrorDeviceNotConnected)
DCC_USBCAM_STUB(XUsbcamReadFrame, kErrorDeviceNotConnected)
