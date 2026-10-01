#include "loxone_onewire/http.hpp"

#include <memory>
#include <string>
#include <utility>

#include "cJSON.h"
#include "esp_log.h"
#include "loxone_onewire/settings.hpp"

namespace loxone {

namespace {
constexpr const char *TAG = "LoxoneHttp";
constexpr size_t kMaxBody = 512;

struct Context {
  RequestGuard guard;
};

esp_err_t sendJson(httpd_req_t *req, const char *status, const std::string &body) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_status(req, status);
  httpd_resp_send(req, body.c_str(), HTTPD_RESP_USE_STRLEN);
  return ESP_OK;
}

esp_err_t sendError(httpd_req_t *req, const std::string &msg, const char *status = "400 Bad Request") {
  cJSON *obj = cJSON_CreateObject();
  cJSON_AddBoolToObject(obj, "success", false);
  cJSON_AddStringToObject(obj, "error", msg.c_str());
  char *raw = cJSON_PrintUnformatted(obj);
  const std::string body = raw ? raw : "{}";
  cJSON_free(raw);
  cJSON_Delete(obj);
  return sendJson(req, status, body);
}

bool authorised(httpd_req_t *req) {
  auto *ctx = static_cast<Context *>(req->user_ctx);
  return !ctx->guard || ctx->guard(req);
}

esp_err_t handleGet(httpd_req_t *req) {
  if (!authorised(req)) return ESP_OK;
  return sendJson(req, "200 OK", std::string("{\"success\":true,\"data\":") + toJson(loadSettings()) + "}");
}

esp_err_t handlePost(httpd_req_t *req) {
  if (!authorised(req)) return ESP_OK;
  if (req->content_len == 0) return sendError(req, "empty request body");
  if (req->content_len > kMaxBody) return sendError(req, "request body too large", "413 Payload Too Large");

  std::string body(req->content_len, '\0');
  size_t received = 0;
  while (received < body.size()) {
    const int ret = httpd_req_recv(req, body.data() + received, body.size() - received);
    if (ret <= 0) {
      if (ret == HTTPD_SOCK_ERR_TIMEOUT) continue;
      return sendError(req, "failed to read request body");
    }
    received += ret;
  }

  std::string error;
  const auto next = applyJson(loadSettings(), body.c_str(), error);
  if (!next) return sendError(req, error);

  if (esp_err_t err = saveSettings(*next); err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to persist settings: %s", esp_err_to_name(err));
    return sendError(req, "failed to save settings", "500 Internal Server Error");
  }
  return sendJson(req, "200 OK",
                  std::string("{\"success\":true,\"message\":\"Saved, reboot to apply\",\"data\":") + toJson(*next) +
                      "}");
}
}  // namespace

esp_err_t registerHttpHandlers(httpd_handle_t server, RequestGuard guard) {
  // The HTTP server can be restarted (routes are registered again each time),
  // so the context outlives any single server instance.
  static Context context;
  context.guard = std::move(guard);
  Context *ctx = &context;

  httpd_uri_t get = {};
  get.uri = "/loxone_config";
  get.method = HTTP_GET;
  get.handler = handleGet;
  get.user_ctx = ctx;

  httpd_uri_t post = get;
  post.method = HTTP_POST;
  post.handler = handlePost;

  esp_err_t err = httpd_register_uri_handler(server, &get);
  if (err == ESP_OK) err = httpd_register_uri_handler(server, &post);
  if (err != ESP_OK) ESP_LOGE(TAG, "Failed to register handlers: %s", esp_err_to_name(err));
  return err;
}

}  // namespace loxone
