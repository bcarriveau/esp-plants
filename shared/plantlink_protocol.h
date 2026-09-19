#pragma once

#include <stdint.h>

namespace plantlink {

constexpr uint8_t PROTOCOL_VERSION = 1;
constexpr uint16_t MAX_PAYLOAD_BYTES = 512;

// PlantLink frames are COBS encoded and terminated by a zero byte on the wire.
// CRC/layout details live in docs/PLANTLINK_PROTOCOL.md.
enum class MessageType : uint8_t {
  Hello = 0x01,
  HelloAck = 0x02,
  Heartbeat = 0x03,

  NetworkStatus = 0x10,
  PermitJoin = 0x11,
  DeviceJoined = 0x12,
  DeviceLeft = 0x13,
  RemoveDevice = 0x14,

  SensorReport = 0x20,
  SetSensorOption = 0x21,
  CommandResult = 0x22,

  RawZigbeeEvent = 0x30,

  FactoryResetNetwork = 0x40,
};

enum FrameFlags : uint8_t {
  FlagNone = 0x00,
  FlagResponse = 0x01,
  FlagError = 0x02,
};

}  // namespace plantlink
