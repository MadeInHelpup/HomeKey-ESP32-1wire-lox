#pragma once
#include <functional>

#include "esp_http_server.h"

// Thin adapter between the application and components/loxone_onewire.
// Everything is a no-op unless CONFIG_LOXONE_ONEWIRE is enabled, so call sites
// need no #ifdefs.
namespace LoxoneBridge {

#ifdef CONFIG_LOXONE_ONEWIRE
// Loads the persisted settings and, if enabled, starts the 1-Wire slave and
// subscribes to HomeKey taps. Call once, after NVS and the GPIO allocator are
// usable.
void begin();

// Registers the settings endpoints. Must be called before the catch-all route.
// `guard` authorises a request, see loxone::RequestGuard.
void registerRoutes(httpd_handle_t server, std::function<bool(httpd_req_t *)> guard);
#else
inline void begin() {}
inline void registerRoutes(httpd_handle_t, std::function<bool(httpd_req_t *)>) {}
#endif

}  // namespace LoxoneBridge
