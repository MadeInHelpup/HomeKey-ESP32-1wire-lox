#include "loxone_onewire/settings.hpp"

#include <cmath>

#include "cJSON.h"
#include "esp_log.h"
#include "nvs.h"

namespace loxone {

namespace {
constexpr const char *TAG = "LoxoneSettings";
constexpr const char *kNamespace = "loxone";
constexpr const char *kKey = "settings";

// Fixed-layout blob so the stored format does not depend on struct padding.
struct __attribute__((packed)) Stored {
  uint8_t version;
  uint8_t enabled;
  uint8_t gpioPin;
  uint8_t romSource;
  uint16_t activeDurationMs;
};
constexpr uint8_t kVersion = 1;

// Reads an integral member that must be a whole number within [lo, hi].
bool readInt(const cJSON *obj, const char *key, double lo, double hi, double &out, std::string &error) {
  const cJSON *item = cJSON_GetObjectItemCaseSensitive(obj, key);
  if (!item) return true;  // absent: keep current value
  if (!cJSON_IsNumber(item) || item->valuedouble != std::floor(item->valuedouble) || item->valuedouble < lo ||
      item->valuedouble > hi) {
    error = std::string("invalid value for '") + key + "'";
    return false;
  }
  out = item->valuedouble;
  return true;
}
}  // namespace

Settings loadSettings() {
  Settings settings;
  nvs_handle_t handle;
  if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) return settings;

  Stored stored{};
  size_t size = sizeof(stored);
  const esp_err_t err = nvs_get_blob(handle, kKey, &stored, &size);
  nvs_close(handle);
  if (err != ESP_OK || size != sizeof(stored) || stored.version != kVersion) return settings;

  Settings loaded;
  loaded.enabled = stored.enabled != 0;
  loaded.gpioPin = stored.gpioPin;
  loaded.activeDurationMs = stored.activeDurationMs;
  loaded.romSource = stored.romSource;
  const char *why = nullptr;
  if (!loaded.valid(&why)) {
    ESP_LOGW(TAG, "Ignoring stored settings: %s", why);
    return settings;
  }
  return loaded;
}

esp_err_t saveSettings(const Settings &settings) {
  if (!settings.valid()) return ESP_ERR_INVALID_ARG;

  const Stored stored{kVersion, static_cast<uint8_t>(settings.enabled), settings.gpioPin, settings.romSource,
                      settings.activeDurationMs};
  nvs_handle_t handle;
  esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
  if (err != ESP_OK) return err;
  err = nvs_set_blob(handle, kKey, &stored, sizeof(stored));
  if (err == ESP_OK) err = nvs_commit(handle);
  nvs_close(handle);
  return err;
}

std::string toJson(const Settings &settings) {
  cJSON *obj = cJSON_CreateObject();
  cJSON_AddBoolToObject(obj, "enabled", settings.enabled);
  cJSON_AddNumberToObject(obj, "gpioPin", settings.gpioPin);
  cJSON_AddNumberToObject(obj, "activeDurationMs", settings.activeDurationMs);
  cJSON_AddNumberToObject(obj, "romSource", settings.romSource);
  char *raw = cJSON_PrintUnformatted(obj);
  std::string out = raw ? raw : "{}";
  cJSON_free(raw);
  cJSON_Delete(obj);
  return out;
}

std::optional<Settings> applyJson(const Settings &base, const char *json, std::string &error) {
  cJSON *obj = cJSON_Parse(json);
  if (!obj || !cJSON_IsObject(obj)) {
    cJSON_Delete(obj);
    error = "invalid JSON";
    return std::nullopt;
  }

  Settings next = base;
  bool ok = true;
  if (const cJSON *enabled = cJSON_GetObjectItemCaseSensitive(obj, "enabled")) {
    if (cJSON_IsBool(enabled)) {
      next.enabled = cJSON_IsTrue(enabled);
    } else {
      error = "invalid value for 'enabled'";
      ok = false;
    }
  }
  double v = 0;
  if (ok) {
    v = next.gpioPin;
    ok = readInt(obj, "gpioPin", 0, Settings::kMaxGpio, v, error);
    next.gpioPin = static_cast<uint8_t>(v);
  }
  if (ok) {
    v = next.activeDurationMs;
    ok = readInt(obj, "activeDurationMs", Settings::kMinActiveMs, Settings::kMaxActiveMs, v, error);
    next.activeDurationMs = static_cast<uint16_t>(v);
  }
  if (ok) {
    v = next.romSource;
    ok = readInt(obj, "romSource", 0, static_cast<double>(RomSource::EndpointId), v, error);
    next.romSource = static_cast<uint8_t>(v);
  }
  cJSON_Delete(obj);
  if (!ok) return std::nullopt;

  const char *why = nullptr;
  if (!next.valid(&why)) {
    error = why;
    return std::nullopt;
  }
  return next;
}

}  // namespace loxone
