#include "LoxoneBridge.hpp"

#ifdef CONFIG_LOXONE_ONEWIRE

#include <memory>
#include <span>
#include <system_error>

#include "GPIOAllocator.hpp"
#include "app_event_loop.hpp"
#include "esp_log.h"
#include "eventStructs.hpp"
#include "loxone_onewire/http.hpp"
#include "loxone_onewire/onewire_slave.hpp"
#include "loxone_onewire/rom.hpp"
#include "loxone_onewire/settings.hpp"

namespace LoxoneBridge {

namespace {
constexpr const char *TAG = "LoxoneBridge";

std::unique_ptr<loxone::OneWireSlave> slave;
GPIOAllocator::GPIOLease pinLease;
AppEventLoop::SubscriptionHandle tapSubscription;
loxone::RomSource romSource = loxone::RomSource::IssuerId;

void onNfcEvent(const uint8_t *data, size_t size) {
  if (size == 0 || data == nullptr) return;
  std::span<const uint8_t> payload(data, size);
  std::error_code ec;
  NfcEvent nfcEvent = alpaca::deserialize<NfcEvent>(payload, ec);
  if (ec || nfcEvent.type != HOMEKEY_TAP) return;

  EventHKTap tap = alpaca::deserialize<EventHKTap>(nfcEvent.data, ec);
  if (ec || !tap.status) return;

  const auto &id = romSource == loxone::RomSource::EndpointId ? tap.endpointId : tap.issuerId;
  const auto rom = loxone::makeRom(id);
  if (!rom) {
    ESP_LOGW(TAG, "Identifier too short (%u bytes), not presenting an iButton", static_cast<unsigned>(id.size()));
    return;
  }
  ESP_LOGI(TAG, "HomeKey tap, ROM from %s: %02X%02X%02X%02X%02X%02X%02X%02X",
           romSource == loxone::RomSource::EndpointId ? "endpointId" : "issuerId", (*rom)[0], (*rom)[1], (*rom)[2],
           (*rom)[3], (*rom)[4], (*rom)[5], (*rom)[6], (*rom)[7]);
  slave->activate(*rom);
}
}  // namespace

void begin() {
  const loxone::Settings settings = loxone::loadSettings();
  if (!settings.enabled) {
    ESP_LOGI(TAG, "Disabled");
    return;
  }

  // Reserve the pin so no other feature can claim it. The slave drives it with
  // its own fast GPIO access afterwards.
  auto lease = GPIOAllocator::instance().acquire(static_cast<gpio_num_t>(settings.gpioPin), GPIO_MODE_INPUT_OUTPUT_OD,
                                                 GPIOAllocator::PinRole::GpioOut,
                                                 GPIOAllocator::PinConsumer::Hardware, "LOXONE_1WIRE");
  if (!lease) {
    ESP_LOGE(TAG, "GPIO%u is not available, bridge not started", static_cast<unsigned>(settings.gpioPin));
    return;
  }
  pinLease = std::move(*lease);

  slave = std::make_unique<loxone::OneWireSlave>();
  if (esp_err_t err = slave->begin(settings); err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start 1-Wire slave: %s", esp_err_to_name(err));
    slave.reset();
    pinLease = {};
    return;
  }

  romSource = static_cast<loxone::RomSource>(settings.romSource);
  tapSubscription = AppEventLoop::subscribe(NFC_EVENT, NFC_TAP_EVENT, onNfcEvent);
}

void registerRoutes(httpd_handle_t server, std::function<bool(httpd_req_t *)> guard) {
  loxone::registerHttpHandlers(server, std::move(guard));
}

}  // namespace LoxoneBridge

#endif  // CONFIG_LOXONE_ONEWIRE
