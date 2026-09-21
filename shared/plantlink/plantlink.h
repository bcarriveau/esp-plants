#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace plantlink {

constexpr uint8_t kProtocolVersion = 1;
constexpr size_t kMaxPayloadBytes = 256;
constexpr size_t kHeaderBytes = 7;  // version, type, flags, seq(2), payload_len(2)
constexpr size_t kCrcBytes = 4;
constexpr size_t kMaxDecodedBytes = kHeaderBytes + kMaxPayloadBytes + kCrcBytes;
constexpr size_t kMaxEncodedBytes = kMaxDecodedBytes + (kMaxDecodedBytes / 254) + 2;

enum class MessageType : uint8_t {
  Hello = 0x01,
  HelloAck = 0x02,
  Heartbeat = 0x03,

  NetworkStatus = 0x10,
  PermitJoin = 0x11,
  DeviceJoined = 0x12,
  DeviceLeft = 0x13,
  RemoveDevice = 0x14,
  InfrastructureReport = 0x15,

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

enum CapabilityFlags : uint32_t {
  CapabilityNone = 0,
  CapabilityZigbeeCoordinator = 1u << 0,
  CapabilityZg303zDecoder = 1u << 1,
  CapabilityRawZigbeeLog = 1u << 2,
  CapabilityInfrastructureRegistry = 1u << 3,
};

enum SensorFieldFlags : uint16_t {
  SensorHasTemperature = 1u << 0,
  SensorHasHumidity = 1u << 1,
  SensorHasSoilMoisture = 1u << 2,
  SensorHasBattery = 1u << 3,
  SensorHasWaterWarning = 1u << 4,
};

enum InfrastructureFlags : uint8_t {
  InfrastructureOnline = 1u << 0,
  InfrastructureDirectNeighbor = 1u << 1,
};

struct Frame {
  MessageType type = MessageType::Hello;
  uint8_t flags = 0;
  uint16_t sequence = 0;
  uint16_t payloadLength = 0;
  uint8_t payload[kMaxPayloadBytes]{};
};

// SensorReport payload, serialized explicitly with the helpers below.
// Wire layout (little endian):
// ieee[8], short_addr[2], field_flags[2], temp_centi_c[2],
// humidity_centi_pct[2], soil_pct[1], battery_pct[1], water_warning[1],
// lqi[1], rssi_dbm[1]
constexpr size_t kSensorReportPayloadBytes = 21;
constexpr int8_t kRssiUnavailableDbm = static_cast<int8_t>(-128);

struct SensorReportData {
  uint8_t ieee[8]{};
  uint16_t shortAddress = 0xffff;
  uint16_t fieldFlags = 0;
  int16_t temperatureCentiC = 0;
  uint16_t humidityCentiPct = 0;
  uint8_t soilMoisturePct = 0;
  uint8_t batteryPct = 0;
  uint8_t waterWarning = 0;
  uint8_t lqi = 0;
  int8_t rssiDbm = kRssiUnavailableDbm;
};

// InfrastructureReport payload:
// ieee[8], short_addr[2], flags[1], device_type[1], lqi[1], rssi_dbm[1]
constexpr size_t kInfrastructureReportPayloadBytes = 14;

struct InfrastructureReportData {
  uint8_t ieee[8]{};
  uint16_t shortAddress = 0xffff;
  uint8_t flags = 0;
  uint8_t deviceType = 0;
  uint8_t lqi = 0;
  int8_t rssiDbm = kRssiUnavailableDbm;
};

inline void putU16LE(uint8_t *p, uint16_t value) {
  p[0] = static_cast<uint8_t>(value & 0xffu);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xffu);
}

inline uint16_t getU16LE(const uint8_t *p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}

inline void putU32LE(uint8_t *p, uint32_t value) {
  p[0] = static_cast<uint8_t>(value & 0xffu);
  p[1] = static_cast<uint8_t>((value >> 8) & 0xffu);
  p[2] = static_cast<uint8_t>((value >> 16) & 0xffu);
  p[3] = static_cast<uint8_t>((value >> 24) & 0xffu);
}

inline uint32_t getU32LE(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) |
         (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}

inline uint32_t crc32(const uint8_t *data, size_t length) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u)));
      crc = (crc >> 1) ^ (0xedb88320u & mask);
    }
  }
  return ~crc;
}

inline size_t cobsEncode(const uint8_t *input, size_t length, uint8_t *output, size_t capacity) {
  if (capacity == 0) return 0;
  size_t readIndex = 0;
  size_t writeIndex = 1;
  size_t codeIndex = 0;
  uint8_t code = 1;

  while (readIndex < length) {
    if (input[readIndex] == 0) {
      if (codeIndex >= capacity) return 0;
      output[codeIndex] = code;
      code = 1;
      codeIndex = writeIndex++;
      if (writeIndex > capacity) return 0;
      ++readIndex;
    } else {
      if (writeIndex >= capacity) return 0;
      output[writeIndex++] = input[readIndex++];
      ++code;
      if (code == 0xff) {
        if (codeIndex >= capacity) return 0;
        output[codeIndex] = code;
        code = 1;
        codeIndex = writeIndex++;
        if (writeIndex > capacity) return 0;
      }
    }
  }

  if (codeIndex >= capacity) return 0;
  output[codeIndex] = code;
  return writeIndex;
}

inline size_t cobsDecode(const uint8_t *input, size_t length, uint8_t *output, size_t capacity) {
  if (length == 0) return 0;
  size_t readIndex = 0;
  size_t writeIndex = 0;

  while (readIndex < length) {
    const uint8_t code = input[readIndex];
    if (code == 0) return 0;
    ++readIndex;

    for (uint8_t i = 1; i < code; ++i) {
      if (readIndex >= length || writeIndex >= capacity) return 0;
      output[writeIndex++] = input[readIndex++];
    }

    if (code != 0xff && readIndex < length) {
      if (writeIndex >= capacity) return 0;
      output[writeIndex++] = 0;
    }
  }
  return writeIndex;
}

inline size_t encodeFrame(MessageType type, uint8_t flags, uint16_t sequence,
                          const uint8_t *payload, uint16_t payloadLength,
                          uint8_t *output, size_t outputCapacity) {
  if (payloadLength > kMaxPayloadBytes || outputCapacity < 2) return 0;

  uint8_t decoded[kMaxDecodedBytes]{};
  decoded[0] = kProtocolVersion;
  decoded[1] = static_cast<uint8_t>(type);
  decoded[2] = flags;
  putU16LE(decoded + 3, sequence);
  putU16LE(decoded + 5, payloadLength);
  if (payloadLength && payload) memcpy(decoded + kHeaderBytes, payload, payloadLength);

  const size_t crcOffset = kHeaderBytes + payloadLength;
  putU32LE(decoded + crcOffset, crc32(decoded, crcOffset));
  const size_t decodedLength = crcOffset + kCrcBytes;

  const size_t encodedLength = cobsEncode(decoded, decodedLength, output, outputCapacity - 1);
  if (!encodedLength || encodedLength >= outputCapacity) return 0;
  output[encodedLength] = 0;
  return encodedLength + 1;
}

inline bool decodeFrame(const uint8_t *encoded, size_t encodedLength, Frame &out) {
  uint8_t decoded[kMaxDecodedBytes]{};
  const size_t decodedLength = cobsDecode(encoded, encodedLength, decoded, sizeof(decoded));
  if (decodedLength < (kHeaderBytes + kCrcBytes)) return false;
  if (decoded[0] != kProtocolVersion) return false;

  const uint16_t payloadLength = getU16LE(decoded + 5);
  if (payloadLength > kMaxPayloadBytes) return false;
  if (decodedLength != kHeaderBytes + payloadLength + kCrcBytes) return false;

  const size_t crcOffset = kHeaderBytes + payloadLength;
  const uint32_t expectedCrc = getU32LE(decoded + crcOffset);
  if (expectedCrc != crc32(decoded, crcOffset)) return false;

  out.type = static_cast<MessageType>(decoded[1]);
  out.flags = decoded[2];
  out.sequence = getU16LE(decoded + 3);
  out.payloadLength = payloadLength;
  if (payloadLength) memcpy(out.payload, decoded + kHeaderBytes, payloadLength);
  return true;
}

class Decoder {
 public:
  bool feed(uint8_t byte, Frame &out) {
    if (byte == 0) {
      if (length_ == 0) return false;
      const bool ok = decodeFrame(encoded_, length_, out);
      length_ = 0;
      overflowed_ = false;
      return ok;
    }

    if (overflowed_) return false;
    if (length_ >= sizeof(encoded_)) {
      overflowed_ = true;
      return false;
    }
    encoded_[length_++] = byte;
    return false;
  }

  void reset() {
    length_ = 0;
    overflowed_ = false;
  }

 private:
  uint8_t encoded_[kMaxEncodedBytes]{};
  size_t length_ = 0;
  bool overflowed_ = false;
};

inline size_t serializeSensorReport(const SensorReportData &in, uint8_t *out, size_t capacity) {
  if (capacity < kSensorReportPayloadBytes) return 0;
  memcpy(out, in.ieee, 8);
  putU16LE(out + 8, in.shortAddress);
  putU16LE(out + 10, in.fieldFlags);
  putU16LE(out + 12, static_cast<uint16_t>(in.temperatureCentiC));
  putU16LE(out + 14, in.humidityCentiPct);
  out[16] = in.soilMoisturePct;
  out[17] = in.batteryPct;
  out[18] = in.waterWarning;
  out[19] = in.lqi;
  out[20] = static_cast<uint8_t>(in.rssiDbm);
  return kSensorReportPayloadBytes;
}

inline bool parseSensorReport(const uint8_t *payload, size_t length, SensorReportData &out) {
  if (!payload || length != kSensorReportPayloadBytes) return false;
  memcpy(out.ieee, payload, 8);
  out.shortAddress = getU16LE(payload + 8);
  out.fieldFlags = getU16LE(payload + 10);
  out.temperatureCentiC = static_cast<int16_t>(getU16LE(payload + 12));
  out.humidityCentiPct = getU16LE(payload + 14);
  out.soilMoisturePct = payload[16];
  out.batteryPct = payload[17];
  out.waterWarning = payload[18];
  out.lqi = payload[19];
  out.rssiDbm = static_cast<int8_t>(payload[20]);
  return true;
}

inline size_t serializeInfrastructureReport(const InfrastructureReportData &in,
                                            uint8_t *out, size_t capacity) {
  if (capacity < kInfrastructureReportPayloadBytes) return 0;
  memcpy(out, in.ieee, 8);
  putU16LE(out + 8, in.shortAddress);
  out[10] = in.flags;
  out[11] = in.deviceType;
  out[12] = in.lqi;
  out[13] = static_cast<uint8_t>(in.rssiDbm);
  return kInfrastructureReportPayloadBytes;
}

inline bool parseInfrastructureReport(const uint8_t *payload, size_t length,
                                      InfrastructureReportData &out) {
  if (!payload || length != kInfrastructureReportPayloadBytes) return false;
  memcpy(out.ieee, payload, 8);
  out.shortAddress = getU16LE(payload + 8);
  out.flags = payload[10];
  out.deviceType = payload[11];
  out.lqi = payload[12];
  out.rssiDbm = static_cast<int8_t>(payload[13]);
  return true;
}

inline void formatIeee(const uint8_t ieee[8], char *out, size_t outSize) {
  static const char hex[] = "0123456789ABCDEF";
  if (!out || outSize < 24) return;
  size_t p = 0;
  for (int i = 7; i >= 0; --i) {
    out[p++] = hex[(ieee[i] >> 4) & 0x0f];
    out[p++] = hex[ieee[i] & 0x0f];
    if (i != 0) out[p++] = ':';
  }
  out[p] = '\0';
}

}  // namespace plantlink
