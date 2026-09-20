#pragma once

#include <stddef.h>
#include <stdint.h>

namespace zg303z {

constexpr uint16_t kTuyaClusterId = 0xef00;
constexpr uint16_t kTemperatureClusterId = 0x0402;
constexpr uint16_t kHumidityClusterId = 0x0405;
constexpr uint16_t kPowerConfigClusterId = 0x0001;

enum class TuyaDataType : uint8_t {
  Raw = 0x00,
  Bool = 0x01,
  Value = 0x02,
  String = 0x03,
  Enum = 0x04,
  Bitmap = 0x05,
};

enum class Metric : uint8_t {
  Unknown = 0,
  Temperature,
  SoilMoisture,
  Battery,
  Humidity,
  WaterWarning,
  Setting,
};

struct Datapoint {
  uint8_t id = 0;
  uint8_t type = 0;
  const uint8_t *data = nullptr;
  uint16_t length = 0;
  bool numericValid = false;
  int32_t numeric = 0;
  Metric metric = Metric::Unknown;
};

struct TuyaFrameInfo {
  uint8_t commandId = 0;
  uint8_t status = 0;
  uint8_t transactionId = 0;
  uint8_t datapointCount = 0;
};

struct NormalizedUpdate {
  bool hasTemperature = false;
  int16_t temperatureCentiC = 0;

  bool hasHumidity = false;
  uint16_t humidityCentiPct = 0;

  bool hasSoilMoisture = false;
  uint8_t soilMoisturePct = 0;

  bool hasBattery = false;
  uint8_t batteryPct = 0;

  bool hasWaterWarning = false;
  bool waterWarning = false;
};

inline Metric metricForDp(uint8_t dp) {
  // Known stock ZG-303Z families observed in current community drivers.
  // We intentionally keep legacy + newer IDs and log every unknown DP in the
  // H2 firmware because HOBEIAN has shipped more than one firmware mapping.
  switch (dp) {
    case 5:
    case 101:
      return Metric::Temperature;
    case 3:
    case 107:
      return Metric::SoilMoisture;
    case 15:
    case 108:
      return Metric::Battery;
    case 109:
      return Metric::Humidity;
    case 1:
    case 14:
      return Metric::WaterWarning;
    case 102:  // soil calibration
    case 104:  // temperature calibration
    case 105:  // humidity calibration
    case 106:  // temperature unit
    case 110:  // soil warning threshold
    case 111:  // temperature sampling interval
    case 112:  // soil sampling interval
      return Metric::Setting;
    default:
      return Metric::Unknown;
  }
}

inline int32_t readSignedBigEndian(const uint8_t *data, uint16_t length) {
  if (!data || length == 0) return 0;
  if (length >= 4) {
    const uint32_t raw = (static_cast<uint32_t>(data[0]) << 24) |
                         (static_cast<uint32_t>(data[1]) << 16) |
                         (static_cast<uint32_t>(data[2]) << 8) |
                         static_cast<uint32_t>(data[3]);
    return static_cast<int32_t>(raw);
  }
  if (length == 2) {
    const uint16_t raw = (static_cast<uint16_t>(data[0]) << 8) | data[1];
    return static_cast<int16_t>(raw);
  }
  return static_cast<int8_t>(data[0]);
}

inline bool numericValue(uint8_t type, const uint8_t *data, uint16_t length, int32_t &out) {
  if (!data || length == 0) return false;
  switch (static_cast<TuyaDataType>(type)) {
    case TuyaDataType::Bool:
    case TuyaDataType::Enum:
      out = data[0];
      return true;
    case TuyaDataType::Value:
    case TuyaDataType::Bitmap:
      out = readSignedBigEndian(data, length);
      return true;
    default:
      return false;
  }
}

inline uint8_t clampPercent(int32_t value) {
  if (value < 0) return 0;
  if (value > 100) return 100;
  return static_cast<uint8_t>(value);
}

inline void applyDatapoint(const Datapoint &dp, NormalizedUpdate &out) {
  if (!dp.numericValid) return;
  switch (dp.metric) {
    case Metric::Temperature:
      out.hasTemperature = true;
      // Stock ZG-303Z reports tenths of a degree C on known temperature DPs.
      out.temperatureCentiC = static_cast<int16_t>(dp.numeric * 10);
      break;
    case Metric::SoilMoisture:
      out.hasSoilMoisture = true;
      out.soilMoisturePct = clampPercent(dp.numeric);
      break;
    case Metric::Battery:
      out.hasBattery = true;
      out.batteryPct = clampPercent(dp.numeric);
      break;
    case Metric::Humidity:
      out.hasHumidity = true;
      out.humidityCentiPct = static_cast<uint16_t>(clampPercent(dp.numeric)) * 100u;
      break;
    case Metric::WaterWarning:
      out.hasWaterWarning = true;
      out.waterWarning = (dp.numeric != 0);
      break;
    default:
      break;
  }
}

inline size_t zclHeaderLength(const uint8_t *frame, size_t length) {
  if (!frame || length < 3) return 0;
  const bool manufacturerSpecific = (frame[0] & 0x04u) != 0;
  return manufacturerSpecific ? 5u : 3u;
}

template <typename Callback>
inline bool decodeTuyaFrame(const uint8_t *frame, size_t length, TuyaFrameInfo &info,
                            NormalizedUpdate &normalized, Callback callback) {
  const size_t headerLength = zclHeaderLength(frame, length);
  if (!headerLength || length < headerLength + 2) return false;

  info.commandId = frame[headerLength - 1];
  size_t offset = headerLength;
  info.status = frame[offset++];
  info.transactionId = frame[offset++];
  info.datapointCount = 0;

  while (length - offset >= 4) {
    Datapoint dp;
    dp.id = frame[offset];
    dp.type = frame[offset + 1];
    dp.length = (static_cast<uint16_t>(frame[offset + 2]) << 8) | frame[offset + 3];
    offset += 4;
    if (length - offset < dp.length) return false;

    dp.data = frame + offset;
    dp.metric = metricForDp(dp.id);
    dp.numericValid = numericValue(dp.type, dp.data, dp.length, dp.numeric);
    applyDatapoint(dp, normalized);
    callback(dp);
    ++info.datapointCount;
    offset += dp.length;
  }

  return info.datapointCount > 0;
}

// ZCL Report Attributes (command 0x0A) parser for standard measurement clusters.
// This intentionally handles only the attributes we need from the ZG-303Z.
inline bool decodeStandardReport(uint16_t clusterId, const uint8_t *frame, size_t length,
                                 NormalizedUpdate &out) {
  const size_t headerLength = zclHeaderLength(frame, length);
  if (!headerLength || length <= headerLength) return false;
  if (frame[headerLength - 1] != 0x0a) return false;  // Report Attributes

  size_t offset = headerLength;
  bool changed = false;
  while (length - offset >= 3) {
    const uint16_t attrId = static_cast<uint16_t>(frame[offset]) |
                            (static_cast<uint16_t>(frame[offset + 1]) << 8);
    const uint8_t dataType = frame[offset + 2];
    offset += 3;

    size_t valueBytes = 0;
    switch (dataType) {
      case 0x10:  // bool
      case 0x18:  // bitmap8
      case 0x20:  // uint8
      case 0x28:  // int8
      case 0x30:  // enum8
        valueBytes = 1;
        break;
      case 0x19:  // bitmap16
      case 0x21:  // uint16
      case 0x29:  // int16
      case 0x31:  // enum16
        valueBytes = 2;
        break;
      case 0x23:  // uint32
      case 0x2b:  // int32
        valueBytes = 4;
        break;
      default:
        // Unknown variable/fixed type: stop rather than desynchronize records.
        return changed;
    }
    if (length - offset < valueBytes) return changed;

    if (clusterId == kTemperatureClusterId && attrId == 0x0000 && dataType == 0x29 && valueBytes == 2) {
      const uint16_t raw = static_cast<uint16_t>(frame[offset]) |
                           (static_cast<uint16_t>(frame[offset + 1]) << 8);
      out.hasTemperature = true;
      out.temperatureCentiC = static_cast<int16_t>(raw);
      changed = true;
    } else if (clusterId == kHumidityClusterId && attrId == 0x0000 && dataType == 0x21 && valueBytes == 2) {
      const uint16_t raw = static_cast<uint16_t>(frame[offset]) |
                           (static_cast<uint16_t>(frame[offset + 1]) << 8);
      out.hasHumidity = true;
      out.humidityCentiPct = raw;
      changed = true;
    } else if (clusterId == kPowerConfigClusterId && attrId == 0x0021 && dataType == 0x20 && valueBytes == 1) {
      // Zigbee BatteryPercentageRemaining is in half-percent units (0..200).
      const uint8_t raw = frame[offset];
      if (raw != 0xff) {
        out.hasBattery = true;
        out.batteryPct = raw > 200 ? 100 : static_cast<uint8_t>((raw + 1u) / 2u);
        changed = true;
      }
    }

    offset += valueBytes;
  }
  return changed;
}


// HOBEIAN ZG-303Z legacy stock firmware uses standard cluster 0x0405 as a
// second copy of soil moisture rather than air RH. The encoded value still
// follows the standard RelativeHumidity measured-value scale (hundredths of a
// percent), so 9800 means 98% soil moisture. Real air humidity is Tuya DP109.
//
// Keep this model quirk separate from decodeStandardReport() so the generic
// standard-cluster decoder remains standards-correct.
inline bool decodeZg303zSoilMirrorReport(const uint8_t *frame, size_t length,
                                         NormalizedUpdate &out) {
  NormalizedUpdate standard;
  if (!decodeStandardReport(kHumidityClusterId, frame, length, standard) ||
      !standard.hasHumidity) {
    return false;
  }

  const uint32_t roundedPct =
      (static_cast<uint32_t>(standard.humidityCentiPct) + 50u) / 100u;
  out.hasSoilMoisture = true;
  out.soilMoisturePct =
      roundedPct > 100u ? 100u : static_cast<uint8_t>(roundedPct);
  return true;
}
}  // namespace zg303z
