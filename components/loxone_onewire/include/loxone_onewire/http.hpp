#pragma once
#include <functional>

#include "esp_err.h"
#include "esp_http_server.h"

namespace loxone {

// Returns true if the request may proceed. On false the guard must already have
// sent the HTTP response (e.g. 401), the handler then returns without touching
// the request again.
using RequestGuard = std::function<bool(httpd_req_t *)>;

// Registers the settings endpoints on `server`:
//   GET  /loxone_config   -> {"success":true,"data":{...settings...}}
//   POST /loxone_config   -> partial JSON update, persisted to NVS
//                            -> {"success":true,"message":"...","data":{...}}
// Changes take effect after a reboot. Must be called before any catch-all
// ("/*") route is registered, as the HTTP server matches in registration order.
esp_err_t registerHttpHandlers(httpd_handle_t server, RequestGuard guard);

}  // namespace loxone
