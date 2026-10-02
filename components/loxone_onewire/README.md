# loxone_onewire

Technical reference for the Loxone 1-Wire bridge component.
User guide: [Deutsch](../../docs/LOXONE_INTEGRATION.md) | [English](../../docs/LOXONE_INTEGRATION.en.md)

After a successful HomeKey authentication the device presents a virtual Dallas **DS1990A iButton** on a 1-Wire bus
for a few seconds, so a 1-Wire master such as a Loxone 1-Wire Extension can use it as a key. The component is
self-contained: it is off unless `CONFIG_LOXONE_ONEWIRE=y`, and it only depends on the application through the small
adapter in `main/LoxoneBridge.cpp`.

## Enable

Set the option in `menuconfig` (**Loxone 1-Wire bridge**) or in an `sdkconfig.defaults` overlay. This repository ships
one for convenience:

```
idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.defaults.loxone" build
```

The Kconfig values are only the initial defaults. The **Loxone** page of the web UI stores the settings in NVS
(namespace `loxone`). Saving reboots the device, the bus is set up at boot.

| Setting | Default | Notes |
|---------|---------|-------|
| `enabled` | on | `CONFIG_LOXONE_ONEWIRE_DEFAULT_ENABLED` |
| `gpioPin` | 27 on the ESP32, 4 otherwise | reserved through the GPIO allocator |
| `activeDurationMs` | 3000 | 500 to 4500, see below |
| `romSource` | 0 (`issuerId`) | 1 = `endpointId` |

## How it works

```
HomeKey tap (validated by the reader)
  -> ROM = 0x01 | first 6 bytes of issuerId (or endpointId) | CRC8
  -> iButton answers reset/presence and the ROM commands for `activeDurationMs`, then disappears again
  -> the 1-Wire master detects the iButton
```

The ROM is derived deterministically, so the same identifier always yields the same ROM and no mapping table is
needed. Example with fictional values:

```
issuerId:  aa bb cc dd ee ff 00 11
ROM:       01 aa bb cc dd ee ff 2f      (2f = CRC8 over the first seven bytes)
```

## Behaviour and limits

- The bus task polls the pin directly (no ISR) to hit the 15 to 60 us presence window. While the iButton is present it
  never blocks, which starves the idle task of its core. That is why the active window is capped at 4500 ms, below
  the default 5 s task watchdog timeout.
- The task runs at priority 20 on the core set by `CONFIG_LOXONE_ONEWIRE_TASK_CORE` (default 1). The application pins
  several background tasks to core 1 as well; switch to core 0 if the bus timing suffers.
- One virtual iButton at a time. A second tap while active replaces the ROM and restarts the window.
- Single-core targets (ESP32-C3/C6) build but are untested: the busy-polling task then competes with the whole
  application.
- An iButton ROM is an identifier, not a secret, and a 1-Wire bus has no authentication. Keep the bus wiring
  physically protected.

## Layout

```
components/loxone_onewire/
  include/loxone_onewire/rom.hpp            ROM derivation and CRC8 (pure, constexpr)
  include/loxone_onewire/settings.hpp       settings struct, NVS and JSON helpers
  include/loxone_onewire/onewire_slave.hpp  DS1990A emulation on a GPIO
  include/loxone_onewire/http.hpp           GET/POST /loxone_config
  test/host/                                host unit test, no ESP-IDF needed
  docs/wiring.de.svg, wiring.en.svg         wiring diagram
```

Outside the component the feature touches exactly these places:

| File | Change |
|------|--------|
| `main/LoxoneBridge.{hpp,cpp}` | adapter: tap event to ROM, GPIO lease, routes |
| `main/main.cpp` | `LoxoneBridge::begin()` |
| `main/WebServerManager.cpp` | `LoxoneBridge::registerRoutes()` |
| `main/CMakeLists.txt` | source file and component requirement |
| `data/src/routes/loxone`, `AppLoxone.svelte`, `services/loxone.ts`, `NavigationMenu.svelte` | web UI page |

All firmware-side code in `main` is compiled out when `CONFIG_LOXONE_ONEWIRE` is not set.

## Tests

The pure logic (ROM derivation, CRC8, settings validation) has a host test:

```
cd components/loxone_onewire/test/host
g++ -std=c++20 -Wall -Wextra -I../../include -Istubs test_loxone.cpp -o test_loxone && ./test_loxone
```

The 1-Wire timing itself can only be verified on hardware with a 1-Wire master.
