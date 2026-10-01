#pragma once
// Pure, dependency-free helpers for the emulated DS1990A iButton ROM.
// Header-only and constexpr so they can be unit-tested on the host.
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace loxone {

// DS1990A ROM: [0x01 family code][6 byte serial][CRC8]
using Rom = std::array<uint8_t, 8>;

inline constexpr uint8_t kFamilyCode = 0x01;
inline constexpr size_t kSerialBytes = 6;

// Dallas/Maxim 1-Wire CRC8, polynomial x^8 + x^5 + x^4 + 1 (0x31, reflected 0x8C).
constexpr uint8_t crc8(std::span<const uint8_t> data) {
  uint8_t crc = 0;
  for (uint8_t byte : data) {
    for (int bit = 0; bit < 8; bit++) {
      const uint8_t mix = (crc ^ byte) & 0x01;
      crc >>= 1;
      if (mix) crc ^= 0x8C;
      byte >>= 1;
    }
  }
  return crc;
}

constexpr bool romValid(const Rom &rom) {
  return crc8(std::span<const uint8_t>(rom.data(), rom.size() - 1)) == rom.back();
}

// Derive the ROM from the first six bytes of `source` (HomeKey issuerId or
// endpointId). The result is deterministic: the same identifier always yields
// the same ROM. Returns nullopt if `source` is too short.
constexpr std::optional<Rom> makeRom(std::span<const uint8_t> source) {
  if (source.size() < kSerialBytes) return std::nullopt;
  Rom rom{};
  rom[0] = kFamilyCode;
  for (size_t i = 0; i < kSerialBytes; i++) rom[i + 1] = source[i];
  rom[7] = crc8(std::span<const uint8_t>(rom.data(), rom.size() - 1));
  return rom;
}

// Compile-time self-check against a fictional identifier.
static_assert(makeRom(std::array<uint8_t, 8>{0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x00, 0x11}) ==
              Rom{0x01, 0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF, 0x2F});

}  // namespace loxone
