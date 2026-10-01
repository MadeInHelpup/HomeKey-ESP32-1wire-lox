#pragma once
#include <cstdint>
#include <optional>
#include <string>

#include "esp_err.h"

// Defaults come from Kconfig; the fallbacks keep the header usable when the
// component is compiled without the Loxone options enabled.
#ifndef CONFIG_LOXONE_ONEWIRE_GPIO
#define CONFIG_LOXONE_ONEWIRE_GPIO 27
#endif
#ifndef CONFIG_LOXONE_ONEWIRE_ACTIVE_MS
#define CONFIG_LOXONE_ONEWIRE_ACTIVE_MS 3000
#endif

namespace loxone {

enum class RomSource : uint8_t {
  IssuerId = 0,    // identical for all devices of one Apple ID
  EndpointId = 1,  // unique per physical device
};

struct Settings {
  bool enabled =
#ifdef CONFIG_LOXONE_ONEWIRE_DEFAULT_ENABLED
      true;
#else
      false;
#endif
  uint8_t gpioPin = CONFIG_LOXONE_ONEWIRE_GPIO;
  uint16_t activeDurationMs = CONFIG_LOXONE_ONEWIRE_ACTIVE_MS;
  uint8_t romSource = static_cast<uint8_t>(RomSource::IssuerId);

  static constexpr uint16_t kMinActiveMs = 500;
  // The bus task busy-polls the pin while the iButton is "present" and never
  // blocks, so the idle task on its core is starved for the whole window. Stay
  // below the default 5 s task watchdog timeout.
  static constexpr uint16_t kMaxActiveMs = 4500;
  static constexpr uint8_t kMaxGpio = 48;

  // Range checks only. Whether the pin is usable on the current chip (output
  // capable, not reserved) is decided when the bus is started.
  constexpr bool valid(const char **why = nullptr) const {
    auto fail = [&](const char *msg) {
      if (why) *why = msg;
      return false;
    };
    if (gpioPin > kMaxGpio) return fail("gpioPin out of range");
    if (activeDurationMs < kMinActiveMs || activeDurationMs > kMaxActiveMs)
      return fail("activeDurationMs must be 500-4500");
    if (romSource > static_cast<uint8_t>(RomSource::EndpointId)) return fail("romSource must be 0 or 1");
    return true;
  }
};

// Persisted in its own NVS namespace ("loxone"), independent of the host
// application's configuration. Missing or invalid data yields the defaults.
Settings loadSettings();
esp_err_t saveSettings(const Settings &settings);

std::string toJson(const Settings &settings);

// Merge a (partial) JSON object over `base`. Unknown keys are ignored.
// On failure returns nullopt and sets `error`.
std::optional<Settings> applyJson(const Settings &base, const char *json, std::string &error);

}  // namespace loxone
