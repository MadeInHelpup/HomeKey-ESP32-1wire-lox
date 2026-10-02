# Loxone 1-Wire Integration

## Überblick

Nach einer erfolgreichen Apple-HomeKey-Authentifizierung (iPhone, Apple Watch) zeigt der ESP32 für kurze Zeit
einen virtuellen Dallas **DS1990A iButton** auf dem 1-Wire-Bus. Eine Loxone 1-Wire Extension sieht das wie einen
physischen iButton, der aufgelegt und wieder entfernt wird, und löst über den Zugangs-Baustein die Tür aus.

Der Code liegt als eigene Komponente in `components/loxone_onewire/` (englische Details dort in der README).
Er ist optional und nur mit `CONFIG_LOXONE_ONEWIRE=y` im Build.

**ROM-Ableitung:** `[0x01][issuerId[0..5]][CRC8]`, deterministisch. Gleiche Apple-ID ergibt gleiche ROM, die
Loxone-Konfiguration überlebt Neustarts und Reflash. Alternativ aus der `endpointId` (pro Gerät).

Beispiel mit fiktiven Werten:

```
issuerId:  aa bb cc dd ee ff 00 11
ROM:       01 aa bb cc dd ee ff 2f      (2f = CRC8 über die ersten sieben Bytes)
```

## Hardware

| Komponente | Beschreibung |
|------------|-------------|
| ESP32 (WROOM, z. B. NodeMCU) | Mikrocontroller |
| PN532 NFC-Reader | liest Apple HomeKey per NFC |
| 4,7 kΩ Widerstand | Pull-up für den 1-Wire-Bus (GPIO27 nach 3,3 V) |
| 100 µF Elko | zwischen 3,3 V und GND, stabilisiert die Versorgung beim WLAN-Aufbau |
| Loxone 1-Wire Extension | liest den emulierten iButton |
| 5-V-Netzteil (≥ 1 A) | eigene Versorgung am Verbauort |

## Verdrahtung

Schaltplan: [`components/loxone_onewire/docs/wiring.svg`](../components/loxone_onewire/docs/wiring.svg)

```
ESP32 GPIO27 ──┬── 4,7 kΩ ──► 3,3 V (ESP32-Pin)
               │
          Loxone 1-Wire Extension, DATA-Klemme
               │
          GND (gemeinsame Masse ESP32 + Extension)
```

PN532 per SPI:

```
PN532 SCK  → GPIO18      PN532 MISO → GPIO19
PN532 MOSI → GPIO23      PN532 SS   → GPIO5
PN532 VCC  → 5 V         PN532 GND  → GND
```

> GPIO27 wird als Open-Drain betrieben, der externe Pull-up ist zwingend. GPIO34-39 sind für den Bus ungeeignet
> (nur Eingang). Der Pin wird beim GPIO-Allocator der Firmware reserviert.

## Firmware

Fertige Builds (nur klassischer ESP32) im Release
[`loxone-latest`](../../../releases/tag/loxone-latest), gebaut von der Workflow-Datei `.github/workflows/loxone-build.yml`:

| Datei | Verwendung |
|-------|-----------|
| `esp32-loxone.firmware.ota.bin` | OTA-Update der Firmware |
| `littlefs.bin` | OTA-Update der Web-UI (enthält die Loxone-Seite) |
| `esp32-loxone.firmware.factory.bin` | nur per USB, **löscht Einstellungen und HomeKit-Pairing** |

### Update per OTA (empfohlen)

1. Web-UI öffnen, Seite **OTA Update**.
2. `esp32-loxone.firmware.ota.bin` und `littlefs.bin` wählen, **Upload Both**.
3. Das Gerät startet neu. Browser mit Strg+Shift+R neu laden, sonst bleibt unter Umständen die alte Oberfläche.

Zeigt die Loxone-Seite `Invalid 'type' parameter`, läuft noch die alte Oberfläche: `littlefs.bin` einzeln erneut
hochladen, alternativ per `espota` (Standard-OTA-Passwort `homespan-ota`):

```bash
python espota.py -r -i <IP> -a homespan-ota -f littlefs.bin -s
```

### Erstinstallation per USB

```bash
esptool.py --chip esp32 -p <PORT> write_flash 0x0 esp32-loxone.firmware.factory.bin
```

Danach WLAN und HomeKit-Kopplung wie in der normalen Dokumentation einrichten ([docs/content/setup.md](content/setup.md)).

### Selbst bauen

In `menuconfig` unter "Loxone 1-Wire bridge" aktivieren oder eine `sdkconfig.defaults`-Ergänzung mit
`CONFIG_LOXONE_ONEWIRE=y` verwenden, siehe `components/loxone_onewire/README.md`.

## Einrichtung in der Web-UI

Seite **Loxone**. Speichern startet das Gerät neu, der Bus wird beim Boot aufgebaut.

| Einstellung | Standard | Beschreibung |
|-------------|----------|--------------|
| Enable | an | 1-Wire-Emulation aktivieren |
| GPIO Pin | 27 | Pin der 1-Wire-Leitung (ausgabefähig) |
| Active Window | 3000 ms | Zeit, die der iButton nach dem Tap sichtbar ist (500-4500 ms) |
| ROM Source | `issuerId` | `issuerId` = pro Apple-ID, `endpointId` = pro Gerät |

Das Active Window ist auf 4500 ms begrenzt: Der Bus-Task pollt den Pin ohne zu blockieren, länger würde den Idle-Task
verhungern lassen und den Task-Watchdog (Standard 5 s) auslösen. Loxone fragt etwa einmal pro Sekunde ab, 3000 ms
ergeben zwei bis drei Zyklen.

## Person in Loxone anlegen

1. Loxone Config, 1-Wire Extension, **1-Wire Suche** starten.
2. Apple-Gerät an den PN532 halten, während die Suche läuft.
3. Die ROM erscheint in den Ergebnissen (steht auch im Log: `HomeKey tap, ROM from issuerId: 01…`).
4. Dem Zugangs-Baustein zuweisen. Der nächste Tap öffnet die Tür.

## Betriebsablauf

```
Person hält iPhone oder Apple Watch an den PN532
        ↓
ESP32 prüft die HomeKey-Signatur (lokal, kein Internet)
        ↓
ROM = 0x01 + issuerId[0..5] + CRC8
        ↓
iButton antwortet auf Reset/Presence und die ROM-Kommandos für das Active Window
        ↓
Loxone erkennt den iButton (0 → 1), danach verschwindet er wieder (1 → 0)
        ↓
Zugangs-Baustein erkennt die Flanke und öffnet die Tür
```

Ohne WLAN funktioniert das vollständig: HomeKey-Prüfung und ROM-Ableitung laufen lokal. WLAN braucht nur die
Einrichtung und die Web-UI.

## Sicherheit

Eine iButton-ROM ist eine Kennung, kein Geheimnis, und ein 1-Wire-Bus hat keine Authentifizierung. Wer die ROM
kennt und an den Bus kommt, kann denselben iButton nachbilden. Die ROM steht deshalb im Log und liegt auf der
Leitung. Die Busverdrahtung muss physisch geschützt sein, und die Bridge sollte nicht dort eingesetzt werden, wo
die 1-Wire-Leitung von außen erreichbar ist.

| Aspekt | Hinweis |
|--------|---------|
| HomeKey | kryptografische Authentifizierung auf dem ESP32 |
| 1-Wire | kabelgebunden, kein Funk zwischen ESP32 und Loxone |
| Web-UI | nur lokales Netz, Zugangsdaten in den Misc-Einstellungen aktivierbar |
| ROM-Determinismus | gleiche Apple-ID = gleiche ROM, je Person eigene Apple-ID = eigene Loxone-Berechtigung |

## Troubleshooting

| Problem | Prüfung |
|---------|---------|
| Loxone erkennt den iButton nicht | Pull-up 4,7 kΩ vorhanden? Gemeinsame Masse? Richtiger Pin in der Loxone-Seite? |
| Zustandsänderung triggert nicht | Active Window erhöhen (max. 4500 ms), Loxone-Polling prüfen |
| Kein `HomeKey tap` im Log | PN532-SPI-Pins und Stromversorgung prüfen |
| Loxone-Seite zeigt `Invalid 'type' parameter` | alte Oberfläche, `littlefs.bin` erneut flashen und Browser hart neu laden |
| Loxone-Seite meldet "not part of this firmware" | Firmware ohne `CONFIG_LOXONE_ONEWIRE` gebaut |
| Timing-Probleme auf dem Bus | Leitung unter 30 m halten, Task-Core über `CONFIG_LOXONE_ONEWIRE_TASK_CORE` auf 0 stellen |
| Gerät bootet ohne USB nicht sauber | gilt die Upstream-Lösung (GPIO3-Pull-up, Konsole optional), `CONFIG_INIT_ARDU_SERIAL_LOGGING` prüfen |

## iButton-ROM Kurzreferenz

```
Byte 0:    0x01              Familiencode DS1990A
Bytes 1-6: issuerId[0..5]    erste sechs Bytes der HomeKey-issuerId (oder endpointId)
Byte 7:    CRC8              Dallas-CRC über die ersten sieben Bytes
```
