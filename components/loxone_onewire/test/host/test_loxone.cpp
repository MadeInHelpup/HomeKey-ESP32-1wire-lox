// Host-side unit test for the pure parts of the component (ROM derivation and
// settings validation). No ESP-IDF needed:
//
//   g++ -std=c++20 -Wall -Wextra -I../../include -Istubs test_loxone.cpp -o test_loxone && ./test_loxone
#include <array>
#include <cstdio>
#include <cstdlib>

#include "loxone_onewire/rom.hpp"
#include "loxone_onewire/settings.hpp"

#define CHECK(cond)                                                       \
  do {                                                                    \
    if (!(cond)) {                                                        \
      std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); \
      std::exit(1);                                                       \
    }                                                                     \
  } while (0)

int main() {
  using namespace loxone;

  // Reference vector from the Maxim 1-Wire application notes:
  // family 0x02, serial 1C B8 01 00 00 00 -> CRC 0xA2.
  const std::array<uint8_t, 7> maxim{0x02, 0x1C, 0xB8, 0x01, 0x00, 0x00, 0x00};
  CHECK(crc8(maxim) == 0xA2);
  CHECK(crc8({}) == 0x00);

  // Derivation uses family code 0x01 + first six bytes + CRC.
  const std::array<uint8_t, 8> issuer{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11};
  const auto rom = makeRom(issuer);
  CHECK(rom.has_value());
  CHECK((*rom == Rom{0x01, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x2F}));
  CHECK(romValid(*rom));

  // Bytes beyond the sixth do not influence the ROM, earlier ones do.
  std::array<uint8_t, 8> other = issuer;
  other[6] = 0x42;
  CHECK(*makeRom(other) == *rom);
  other[5] = 0x00;
  CHECK(*makeRom(other) != *rom);

  // Too short sources are rejected instead of being zero padded.
  CHECK(!makeRom(std::span<const uint8_t>(issuer.data(), 5)).has_value());
  CHECK(!makeRom({}).has_value());

  // A corrupted ROM fails the CRC check.
  Rom broken = *rom;
  broken[3] ^= 0x01;
  CHECK(!romValid(broken));

  // Settings validation.
  Settings s;
  CHECK(s.valid());
  s.activeDurationMs = Settings::kMinActiveMs - 1;
  CHECK(!s.valid());
  s.activeDurationMs = Settings::kMaxActiveMs + 1;
  CHECK(!s.valid());
  s.activeDurationMs = Settings::kMaxActiveMs;
  CHECK(s.valid());
  s.romSource = 2;
  CHECK(!s.valid());
  s.romSource = 1;
  s.gpioPin = Settings::kMaxGpio + 1;
  const char *why = nullptr;
  CHECK(!s.valid(&why) && why != nullptr);

  std::puts("ok");
  return 0;
}
