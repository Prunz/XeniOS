#include "xenia/kernel/upnp.h"

// On iOS, UPnP is fully stubbed in the header.
// The real implementation lives in upnp_win.cc / upnp_linux.cc
// and is only compiled on those platforms.
#if !XE_PLATFORM_IOS

// Adrian's full upnp.cc implementation goes here on non-iOS platforms.
// For now this is intentionally empty until we wire up miniupnp
// on Windows/Linux builds.

#endif  // !XE_PLATFORM_IOS