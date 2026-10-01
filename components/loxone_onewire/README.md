# Loxone 1-Wire bridge

After a successful HomeKey authentication the device presents a virtual Dallas
**DS1990A iButton** on a 1-Wire bus for a few seconds. A 1-Wire master such as a
Loxone 1-Wire Extension sees an iButton being presented and removed, and can use
it like a physical key to open a door.

The feature is optional and self-contained: it is off unless
`CONFIG_LOXONE_ONEWIRE=y`, and it only depends on the application through the
small adapter in `main/LoxoneBridge.cpp`.

## How it works

```
HomeKey tap (validated locally by the reader)
        |
        v
ROM = 0x01 | first 6 bytes of issuerId (or endpointId) | CRC8
        |
        v
iButton answers reset/presence and the ROM commands on the GPIO
for `activeDurationMs`, then disappears again
        |
        v
Loxone 1-Wire Extension detects the iButton -> access block triggers
```

* The ROM is derived deterministically, so the same Apple ID always yields the
  same ROM. It survives reboots and reflashing, and no mapping table is needed.
* **ROM source** (setting): `issuerId` is identical for all devices of one Apple
  ID (iPhone, Apple Watch, iPad), `endpointId` is unique per physical device and
  allows granting access per device.
* Works without WiFi. Only the settings page needs the network.

Example (fictional values):

```
issuerId:  aa bb cc dd ee ff 00 11
ROM:       01 aa bb cc dd ee ff 2f      (2f = CRC8 over the first seven bytes)
```

## Security

An iButton ROM is an identifier, not a secret, and a 1-Wire bus has no
authentication. Anyone who knows the ROM and can reach the bus can present the
same iButton. The ROM is therefore logged (so it can be registered in Loxone)
and visible on the bus. Keep the bus wiring physically protected, and do not
rely on the bridge where the 1-Wire cable is reachable from outside.

## Hardware

* Any ESP32 target with an output-capable GPIO for the DATA line (not GPIO34-39
  on the classic ESP32). Default: GPIO27 on the ESP32, GPIO4 otherwise.
* External **4.7 kOhm pull-up** from DATA to 3.3 V. The pin is driven open-drain
  and the internal pull-up is not used.
* Common ground with the 1-Wire master.

Wiring diagram: [`docs/wiring.svg`](docs/wiring.svg).

## Enable

Set the option in `menuconfig` ("Loxone 1-Wire bridge") or in an
`sdkconfig.defaults` overlay:

```
CONFIG_LOXONE_ONEWIRE=y
```

e.g. `idf.py -D SDKCONFIG_DEFAULTS="sdkconfig.defaults;sdkconfig.loxone" build`.

The Kconfig values are only the initial defaults. The **Loxone** page of the web
UI stores enabled state, GPIO, active window and ROM source in NVS (namespace
`loxone`). Saving reboots the device, the bus is set up at boot.

| Setting            | Default            | Notes                                              |
|--------------------|--------------------|----------------------------------------------------|
| `enabled`          | on                 | `CONFIG_LOXONE_ONEWIRE_DEFAULT_ENABLED`            |
| `gpioPin`          | 27 (ESP32), 4      | Reserved through the GPIO allocator                |
| `activeDurationMs` | 3000               | 500-4500, see below                                |
| `romSource`        | 0 (`issuerId`)     | 1 = `endpointId`                                   |

## Register a person in Loxone

1. In Loxone Config open the 1-Wire Extension and start the 1-Wire search.
2. Tap the Apple device on the reader while the search is running.
3. The ROM (also printed as `HomeKey tap, ROM from issuerId: 01...` in the log)
   shows up. Assign it to the access block.

## Behaviour and limits

* The bus task polls the pin directly (no ISR) to hit the 15-60 us presence
  window. While the iButton is "present" it never blocks, which starves the idle
  task of its core. That is why the active window is capped at 4500 ms, below
  the default 5 s task watchdog timeout. It runs at priority 20 on the core set
  by `CONFIG_LOXONE_ONEWIRE_TASK_CORE` (default 1; the application pins several
  background tasks there as well, switch to core 0 if the bus timing suffers).
* One virtual iButton at a time. A second tap while active replaces the ROM and
  restarts the window.
* Single-core targets (ESP32-C3/C6) are supported but untested: the busy-polling
  task then competes with the whole application.

## Layout

```
components/loxone_onewire/
  include/loxone_onewire/rom.hpp            ROM derivation + CRC8 (pure, constexpr)
  include/loxone_onewire/settings.hpp       settings struct, NVS + JSON helpers
  include/loxone_onewire/onewire_slave.hpp  DS1990A emulation on a GPIO
  include/loxone_onewire/http.hpp           GET/POST /loxone_config
  test/host/                                host unit test, no ESP-IDF needed
```

Outside the component, the feature touches exactly these places:

| File                                         | Change                                        |
|----------------------------------------------|-----------------------------------------------|
| `main/LoxoneBridge.{hpp,cpp}`                | adapter: tap event -> ROM, GPIO lease, routes |
| `main/main.cpp`                              | `LoxoneBridge::begin()`                       |
| `main/WebServerManager.cpp`                  | `LoxoneBridge::registerRoutes()`              |
| `main/CMakeLists.txt`                        | source file and component requirement         |
| `data/src/routes/loxone`, `AppLoxone.svelte`, `services/loxone.ts`, `NavigationMenu.svelte` | web UI page |

All of the firmware-side code is compiled out of `main` when
`CONFIG_LOXONE_ONEWIRE` is not set.

## Tests

The pure logic (ROM derivation, CRC8, settings validation) has a host test:

```
cd components/loxone_onewire/test/host
g++ -std=c++20 -Wall -Wextra -I../../include -Istubs test_loxone.cpp -o test_loxone && ./test_loxone
```

The 1-Wire timing itself can only be verified on hardware with a 1-Wire master.
