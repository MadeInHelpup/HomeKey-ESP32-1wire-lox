#include "loxone_onewire/onewire_slave.hpp"

#include "esp_log.h"
#include "rom/ets_sys.h"

#ifndef CONFIG_LOXONE_ONEWIRE_TASK_CORE
#define CONFIG_LOXONE_ONEWIRE_TASK_CORE 1
#endif

namespace loxone {

OneWireSlave::~OneWireSlave() { end(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

esp_err_t OneWireSlave::begin(const Settings &settings) {
  if (m_task) return ESP_ERR_INVALID_STATE;
  if (!settings.valid()) return ESP_ERR_INVALID_ARG;
  const gpio_num_t pin = static_cast<gpio_num_t>(settings.gpioPin);
  if (!GPIO_IS_VALID_OUTPUT_GPIO(pin)) return ESP_ERR_INVALID_ARG;

  m_settings = settings;
  m_gpio = pin;

  const gpio_config_t io = {
      .pin_bit_mask = 1ULL << m_gpio,
      .mode = GPIO_MODE_INPUT_OUTPUT_OD,
      .pull_up_en = GPIO_PULLUP_DISABLE,  // external 4.7k pull-up required
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,  // polling, no ISR needed
  };
  if (esp_err_t err = gpio_config(&io); err != ESP_OK) return err;
  gpio_set_level(m_gpio, 1);  // release bus

  const esp_timer_create_args_t timerArgs = {
      .callback = deactivateCallback,
      .arg = this,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "ow_deactivate",
      .skip_unhandled_events = false,
  };
  if (esp_err_t err = esp_timer_create(&timerArgs, &m_deactivateTimer); err != ESP_OK) return err;

  m_taskDone = xSemaphoreCreateBinary();
  if (!m_taskDone) {
    esp_timer_delete(m_deactivateTimer);
    m_deactivateTimer = nullptr;
    return ESP_ERR_NO_MEM;
  }

  m_stop.store(false);
  m_active.store(false);

  // Polls the GPIO directly for timing accuracy, so it needs a high priority.
#if CONFIG_FREERTOS_UNICORE
  const BaseType_t ok = xTaskCreate(taskEntry, "ow_slave", 4096, this, 20, &m_task);
#else
  const BaseType_t ok =
      xTaskCreatePinnedToCore(taskEntry, "ow_slave", 4096, this, 20, &m_task, CONFIG_LOXONE_ONEWIRE_TASK_CORE);
#endif
  if (ok != pdPASS) {
    m_task = nullptr;
    vSemaphoreDelete(m_taskDone);
    m_taskDone = nullptr;
    esp_timer_delete(m_deactivateTimer);
    m_deactivateTimer = nullptr;
    return ESP_ERR_NO_MEM;
  }

  ESP_LOGI(TAG, "1-Wire slave started on GPIO%d (polling mode)", m_gpio);
  return ESP_OK;
}

void OneWireSlave::end() {
  if (!m_task) return;

  m_stop.store(true);
  m_active.store(false);
  if (xSemaphoreTake(m_taskDone, pdMS_TO_TICKS(500)) != pdTRUE) {
    // The task exits on its own; deleting it from here could leave the bus
    // driven low or interrupts masked, so leave everything allocated instead.
    ESP_LOGE(TAG, "Bus task did not stop in time");
    return;
  }
  m_task = nullptr;
  vSemaphoreDelete(m_taskDone);
  m_taskDone = nullptr;
  esp_timer_stop(m_deactivateTimer);
  esp_timer_delete(m_deactivateTimer);
  m_deactivateTimer = nullptr;
  gpio_set_level(m_gpio, 1);
}

void OneWireSlave::activate(const Rom &rom) {
  if (!m_task) return;
  portENTER_CRITICAL(&m_romLock);
  m_rom = rom;
  portEXIT_CRITICAL(&m_romLock);

  esp_timer_stop(m_deactivateTimer);
  esp_timer_start_once(m_deactivateTimer, static_cast<uint64_t>(m_settings.activeDurationMs) * 1000ULL);
  m_active.store(true, std::memory_order_release);
  ESP_LOGI(TAG, "ROM active for %u ms: %02X %02X %02X %02X %02X %02X %02X %02X", m_settings.activeDurationMs, rom[0],
           rom[1], rom[2], rom[3], rom[4], rom[5], rom[6], rom[7]);
}

Rom OneWireSlave::romSnapshot() {
  portENTER_CRITICAL(&m_romLock);
  const Rom copy = m_rom;
  portEXIT_CRITICAL(&m_romLock);
  return copy;
}

void OneWireSlave::deactivateCallback(void *arg) {
  auto *self = static_cast<OneWireSlave *>(arg);
  self->m_active.store(false, std::memory_order_release);
  ESP_LOGI(TAG, "1-Wire active window expired, device now absent on bus");
}

void OneWireSlave::taskEntry(void *arg) {
  auto *self = static_cast<OneWireSlave *>(arg);
  self->task();
  xSemaphoreGive(self->m_taskDone);
  vTaskDelete(nullptr);
}

// ---------------------------------------------------------------------------
// 1-Wire slave state machine
//
// Uses direct GPIO polling instead of ISR+semaphore. Reason: the ISR fires on
// the core where gpio_install_isr_service was called, but this task runs on a
// different core. FreeRTOS inter-core wakeup latency is 50-200us, far beyond
// the 15-60us presence-detect window. Polling inside the task eliminates this
// latency entirely: the presence pulse is sent from the same execution context
// that detected the rising edge of the reset pulse.
// ---------------------------------------------------------------------------

void OneWireSlave::task() {
  ESP_LOGI(TAG, "1-Wire slave task running on core %d", xPortGetCoreID());

  while (!m_stop.load(std::memory_order_acquire)) {
    if (!m_active.load(std::memory_order_acquire)) {
      vTaskDelay(pdMS_TO_TICKS(5));  // sleep when there is no device to emulate
      continue;
    }

    // Poll for reset pulse and send presence immediately on detection
    if (!detectResetAndPresence()) continue;

    const Rom rom = romSnapshot();

    // Disable scheduler preemption during timing-critical bit exchange
    portDISABLE_INTERRUPTS();

    const uint8_t cmd = receiveByte();
    ESP_EARLY_LOGI(TAG, "OW cmd: 0x%02X", cmd);

    switch (cmd) {
      case CMD_READ_ROM:
        handleReadRom(rom);
        break;
      case CMD_SEARCH_ROM:
        handleSearchRom(rom);
        break;
      case CMD_MATCH_ROM:
        handleMatchRom(rom);
        break;
      case CMD_SKIP_ROM:
        ESP_EARLY_LOGD(TAG, "Skip ROM");
        break;
      default:
        ESP_EARLY_LOGW(TAG, "Unknown OW cmd: 0x%02X", cmd);
        break;
    }

    portENABLE_INTERRUPTS();
  }
}

// ---------------------------------------------------------------------------
// Reset detection + presence pulse (polling, no ISR)
// ---------------------------------------------------------------------------

bool OneWireSlave::detectResetAndPresence() {
  // Drain any ongoing LOW on the bus before looking for a new reset
  {
    uint32_t drain = 100000;
    while (gpio_get_level(m_gpio) == 0 && --drain) {
      ets_delay_us(1);
    }
    if (!drain) return false;
  }

  // Wait for falling edge (start of reset pulse). 1us poll keeps timing
  // accurate without hammering the GPIO register.
  while (gpio_get_level(m_gpio) == 1) {
    if (!m_active.load(std::memory_order_acquire) || m_stop.load(std::memory_order_acquire)) return false;
    ets_delay_us(1);
  }
  const int64_t t_fall = esp_timer_get_time();

  // Wait for rising edge (reset pulse ends)
  while (gpio_get_level(m_gpio) == 0) {
    if (esp_timer_get_time() - t_fall > 10000) return false;  // 10ms sanity
  }
  const int64_t dur = esp_timer_get_time() - t_fall;

  if (dur < RESET_PULSE_MIN_US) return false;  // spurious glitch
  if (!m_active.load(std::memory_order_acquire)) return false;

  // Valid reset: send presence pulse immediately (window 15-60us)
  ets_delay_us(PRESENCE_DELAY_US);
  gpio_set_level(m_gpio, 0);  // pull bus LOW (presence)
  ets_delay_us(PRESENCE_PULSE_US);
  gpio_set_level(m_gpio, 1);  // release
  ets_delay_us(10);

  return true;
}

// ---------------------------------------------------------------------------
// 1-Wire protocol primitives
// ---------------------------------------------------------------------------

bool OneWireSlave::receiveBit() {
  // Wait for master's falling edge (bit slot start)
  uint32_t timeout = 10000;
  while (gpio_get_level(m_gpio) == 1 && --timeout) {
    ets_delay_us(1);
  }
  if (!timeout) return false;

  // Sample 30us into the slot
  ets_delay_us(BIT_SAMPLE_US);
  const bool bit = gpio_get_level(m_gpio) == 1;

  // Wait for remaining slot time
  ets_delay_us(BIT_SLOT_US - BIT_SAMPLE_US);
  return bit;
}

uint8_t OneWireSlave::receiveByte() {
  uint8_t byte = 0;
  for (int i = 0; i < 8; i++) {
    if (receiveBit()) byte |= (1 << i);  // LSB first
  }
  return byte;
}

void OneWireSlave::sendBit(bool bit) {
  // Wait for master's falling edge (read slot start)
  uint32_t timeout = 10000;
  while (gpio_get_level(m_gpio) == 1 && --timeout) {
    ets_delay_us(1);
  }
  if (!timeout) return;

  if (!bit) {
    // Send 0: extend LOW past master's 30us sample point
    ets_delay_us(5);
    gpio_set_level(m_gpio, 0);
    ets_delay_us(55);
    gpio_set_level(m_gpio, 1);
    ets_delay_us(10);
  } else {
    // Send 1: release, the pull-up holds HIGH through the sample point
    ets_delay_us(BIT_SLOT_US);
  }
}

void OneWireSlave::sendByte(uint8_t byte) {
  for (int i = 0; i < 8; i++) {
    sendBit((byte >> i) & 1);  // LSB first
  }
}

// ---------------------------------------------------------------------------
// ROM command handlers
// ---------------------------------------------------------------------------

void OneWireSlave::handleReadRom(const Rom &rom) {
  ESP_EARLY_LOGI(TAG, "Read ROM: %02X %02X %02X %02X %02X %02X %02X %02X", rom[0], rom[1], rom[2], rom[3], rom[4],
                 rom[5], rom[6], rom[7]);
  for (uint8_t b : rom) sendByte(b);
}

void OneWireSlave::handleSearchRom(const Rom &rom) {
  for (int bit_idx = 0; bit_idx < 64; bit_idx++) {
    const bool rom_bit = (rom[bit_idx / 8] >> (bit_idx % 8)) & 1;

    sendBit(rom_bit);
    sendBit(!rom_bit);

    if (receiveBit() != rom_bit) {
      ESP_EARLY_LOGD(TAG, "Search ROM: diverged at bit %d", bit_idx);
      return;
    }
  }
  ESP_EARLY_LOGI(TAG, "Search ROM: device fully selected");
}

void OneWireSlave::handleMatchRom(const Rom &rom) {
  // Receive 8-byte ROM from master and check if it is us
  Rom received{};
  for (auto &b : received) b = receiveByte();
  ESP_EARLY_LOGI(TAG, "Match ROM: %s", received == rom ? "selected" : "not us");
  // DS1990A has no function commands after Match ROM, nothing more to do
}

}  // namespace loxone
