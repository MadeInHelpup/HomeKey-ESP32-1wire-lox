#pragma once
#include <atomic>
#include <cstdint>

#include "driver/gpio.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "loxone_onewire/rom.hpp"
#include "loxone_onewire/settings.hpp"

namespace loxone {

// Emulates a single DS1990A iButton on a 1-Wire bus.
//
// The device is "absent" from the bus until activate() is called. After that it
// answers reset/presence and the ROM commands (Read ROM, Search ROM, Match ROM,
// Skip ROM) for `activeDurationMs`, then disappears again. A master that polls
// the bus (e.g. a Loxone 1-Wire Extension) sees this as the iButton being
// presented and removed.
class OneWireSlave {
 public:
  OneWireSlave() = default;
  ~OneWireSlave();
  OneWireSlave(const OneWireSlave &) = delete;
  OneWireSlave &operator=(const OneWireSlave &) = delete;

  // Configures the pin (open-drain, external pull-up required) and starts the
  // bus task. The caller is responsible for reserving the pin beforehand.
  esp_err_t begin(const Settings &settings);
  void end();

  // Make `rom` visible on the bus for the configured duration. Calling it again
  // while active replaces the ROM and restarts the window.
  void activate(const Rom &rom);

  bool active() const { return m_active.load(std::memory_order_acquire); }
  void task();  // task body, public only for the C trampoline

 private:
  static constexpr const char *TAG = "LoxoneOneWire";

  // 1-Wire timing constants (us)
  static constexpr uint32_t RESET_PULSE_MIN_US = 480;
  static constexpr uint32_t PRESENCE_DELAY_US = 30;
  static constexpr uint32_t PRESENCE_PULSE_US = 120;
  static constexpr uint32_t BIT_SAMPLE_US = 30;
  static constexpr uint32_t BIT_SLOT_US = 70;

  // DS1990A ROM command bytes
  static constexpr uint8_t CMD_READ_ROM = 0x33;
  static constexpr uint8_t CMD_SEARCH_ROM = 0xF0;
  static constexpr uint8_t CMD_SKIP_ROM = 0xCC;
  static constexpr uint8_t CMD_MATCH_ROM = 0x55;

  Settings m_settings{};
  gpio_num_t m_gpio = GPIO_NUM_NC;
  TaskHandle_t m_task = nullptr;
  SemaphoreHandle_t m_taskDone = nullptr;
  esp_timer_handle_t m_deactivateTimer = nullptr;

  std::atomic<bool> m_active{false};
  std::atomic<bool> m_stop{false};

  // The ROM is written from the event context and read by the bus task. The
  // task takes a snapshot per transaction so a concurrent activate() can never
  // produce a torn ROM on the wire.
  portMUX_TYPE m_romLock = portMUX_INITIALIZER_UNLOCKED;
  Rom m_rom{};
  Rom romSnapshot();

  bool detectResetAndPresence();
  uint8_t receiveByte();
  void sendByte(uint8_t byte);
  void sendBit(bool bit);
  bool receiveBit();
  void handleReadRom(const Rom &rom);
  void handleSearchRom(const Rom &rom);
  void handleMatchRom(const Rom &rom);

  static void taskEntry(void *arg);
  static void deactivateCallback(void *arg);
};

}  // namespace loxone
