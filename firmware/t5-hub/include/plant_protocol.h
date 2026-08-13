#pragma once

#include <Arduino.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace plant {

constexpr uint32_t PACKET_MAGIC = 0x504C4E54UL;  // "PLNT"
constexpr uint8_t PROTOCOL_VERSION = 3;

constexpr uint8_t PROVISION_ESPNOW_CHANNEL = 1;
constexpr uint16_t T5_UDP_PORT = 42100;
constexpr uint16_t SENSOR_UDP_PORT = 42101;

enum class MoistureState : uint8_t {
  Unknown = 0,
  Normal = 1,
  AlmostDry = 2,
  Dry = 3,
  Wet = 4,
  SensorError = 5
};

struct __attribute__((packed)) ReadingPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  uint32_t sequence;
  uint16_t moisture_raw;
  uint8_t moisture_percent;
  MoistureState moisture_state;
  uint16_t battery_mv;
  uint8_t battery_percent;
  int8_t reserved_rssi;
  uint32_t awake_ms;
  uint32_t checksum;
};

struct __attribute__((packed)) AckPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  uint32_t sequence;
  uint8_t accepted;
  uint32_t next_wake_seconds;
  uint32_t checksum;
};

struct __attribute__((packed)) ProvisionPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  char wifi_ssid[33];
  char wifi_password[65];
  uint16_t t5_udp_port;
  uint32_t checksum;
};

struct __attribute__((packed)) ProvisionAckPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  uint8_t accepted;
  uint32_t checksum;
};

// Identification command used by either setup-mode ESP-NOW or the normal
// home-Wi-Fi UDP path. It does not alter provisioning, calibration, or readings.
struct __attribute__((packed)) LocatePacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  uint16_t flash_ms;
  uint32_t checksum;
};

// T5-authoritative physical slot identity. This is additive protocol-v3
// traffic; the existing Reading/Ack/Provision/Locate packet layouts remain
// unchanged. Slot 0 is used only with Clear; Assign accepts slots 1..16.
enum class IdentityCommand : uint8_t {
  Assign = 1,
  Clear = 2
};

struct __attribute__((packed)) IdentityPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  IdentityCommand command;
  uint8_t slot;
  uint16_t request_id;
  uint32_t checksum;
};

struct __attribute__((packed)) IdentityAckPacket {
  uint32_t magic;
  uint8_t version;
  uint8_t packet_size;
  uint32_t sensor_id;
  uint8_t slot;
  uint8_t accepted;
  uint16_t request_id;
  uint32_t checksum;
};

inline uint32_t fnv1a(const uint8_t* data, size_t length) {
  uint32_t hash = 2166136261UL;

  for (size_t i = 0; i < length; ++i) {
    hash ^= data[i];
    hash *= 16777619UL;
  }

  return hash;
}

template <typename Packet>
inline uint32_t checksumFor(const Packet& packet) {
  Packet copy = packet;
  copy.checksum = 0;

  return fnv1a(
      reinterpret_cast<const uint8_t*>(&copy),
      sizeof(copy));
}

template <typename Packet>
inline void finalizePacket(Packet& packet) {
  packet.checksum = 0;
  packet.checksum = checksumFor(packet);
}

template <typename Packet>
inline bool validatePacket(const Packet& packet) {
  if (packet.magic != PACKET_MAGIC) return false;
  if (packet.version != PROTOCOL_VERSION) return false;
  if (packet.packet_size != sizeof(Packet)) return false;

  return packet.checksum == checksumFor(packet);
}

inline const char* stateName(MoistureState state) {
  switch (state) {
    case MoistureState::Normal: return "NORMAL";
    case MoistureState::AlmostDry: return "ALMOST DRY";
    case MoistureState::Dry: return "DRY";
    case MoistureState::Wet: return "WET";
    case MoistureState::SensorError: return "SENSOR ERROR";
    case MoistureState::Unknown:
    default: return "UNKNOWN";
  }
}

static_assert(sizeof(ReadingPacket) == 30, "ReadingPacket layout changed.");
static_assert(sizeof(AckPacket) == 23, "AckPacket layout changed.");
static_assert(sizeof(ProvisionPacket) == 114, "ProvisionPacket layout changed.");
static_assert(sizeof(ProvisionAckPacket) == 15, "ProvisionAckPacket layout changed.");
static_assert(sizeof(LocatePacket) == 16, "LocatePacket layout changed.");
static_assert(sizeof(IdentityPacket) == 18, "IdentityPacket layout changed.");
static_assert(sizeof(IdentityAckPacket) == 18, "IdentityAckPacket layout changed.");

static_assert(
    sizeof(ProvisionPacket) <= 255,
    "ProvisionPacket packet_size field is only 8 bits.");

}  // namespace plant
