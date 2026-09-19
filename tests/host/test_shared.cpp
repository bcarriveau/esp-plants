#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "plantlink.h"
#include "zg303z_tuya.h"

static void testPlantLinkRoundTrip() {
  uint8_t payload[] = {0x00, 0x11, 0x22, 0x00, 0xff, 0x42};
  uint8_t encoded[plantlink::kMaxEncodedBytes]{};
  const size_t n = plantlink::encodeFrame(plantlink::MessageType::Hello, 0x5a, 0x1234,
                                          payload, sizeof(payload), encoded, sizeof(encoded));
  assert(n > 1);
  assert(encoded[n - 1] == 0);

  plantlink::Decoder decoder;
  plantlink::Frame frame;
  bool got = false;
  for (size_t i = 0; i < n; ++i) {
    if (decoder.feed(encoded[i], frame)) got = true;
  }
  assert(got);
  assert(frame.type == plantlink::MessageType::Hello);
  assert(frame.flags == 0x5a);
  assert(frame.sequence == 0x1234);
  assert(frame.payloadLength == sizeof(payload));
  assert(std::memcmp(frame.payload, payload, sizeof(payload)) == 0);
}

static void testPlantLinkRejectsCorruptFrame() {
  uint8_t payload[] = {1, 2, 3, 4};
  uint8_t encoded[plantlink::kMaxEncodedBytes]{};
  const size_t n = plantlink::encodeFrame(plantlink::MessageType::Heartbeat, 0, 7,
                                          payload, sizeof(payload), encoded, sizeof(encoded));
  assert(n > 3);
  encoded[2] ^= 0x55;
  plantlink::Decoder decoder;
  plantlink::Frame frame;
  bool got = false;
  for (size_t i = 0; i < n; ++i) {
    if (decoder.feed(encoded[i], frame)) got = true;
  }
  assert(!got);
}

static void testSensorReportWireLayout() {
  plantlink::SensorReportData in;
  const uint8_t ieee[8] = {0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11};
  std::memcpy(in.ieee, ieee, 8);
  in.shortAddress = 0xabcd;
  in.fieldFlags = plantlink::SensorHasTemperature | plantlink::SensorHasSoilMoisture |
                  plantlink::SensorHasBattery;
  in.temperatureCentiC = -125;
  in.soilMoisturePct = 47;
  in.batteryPct = 86;
  in.lqi = 201;
  in.rssiDbm = -61;

  uint8_t payload[plantlink::kSensorReportPayloadBytes]{};
  assert(plantlink::serializeSensorReport(in, payload, sizeof(payload)) == sizeof(payload));
  plantlink::SensorReportData out;
  assert(plantlink::parseSensorReport(payload, sizeof(payload), out));
  assert(std::memcmp(out.ieee, ieee, 8) == 0);
  assert(out.shortAddress == 0xabcd);
  assert(out.temperatureCentiC == -125);
  assert(out.soilMoisturePct == 47);
  assert(out.batteryPct == 86);
  assert(out.lqi == 201);
  assert(out.rssiDbm == -61);
}

static void testTuyaNewMapping() {
  // ZCL header: FC=0x01, seq=0x22, cmd=0x01; Tuya status/transid; then DPs.
  std::vector<uint8_t> f = {
    0x01,0x22,0x01, 0x00,0x01,
    101,0x02,0x00,0x04, 0x00,0x00,0x00,0xD7, // 21.5 C
    107,0x02,0x00,0x04, 0x00,0x00,0x00,0x2F, // 47% soil
    108,0x02,0x00,0x04, 0x00,0x00,0x00,0x56, // 86% battery
    109,0x02,0x00,0x04, 0x00,0x00,0x00,0x3C  // 60% RH
  };
  zg303z::TuyaFrameInfo info;
  zg303z::NormalizedUpdate n;
  int seen = 0;
  assert(zg303z::decodeTuyaFrame(f.data(), f.size(), info, n, [&](const zg303z::Datapoint&){ ++seen; }));
  assert(seen == 4);
  assert(n.hasTemperature && n.temperatureCentiC == 2150);
  assert(n.hasSoilMoisture && n.soilMoisturePct == 47);
  assert(n.hasBattery && n.batteryPct == 86);
  assert(n.hasHumidity && n.humidityCentiPct == 6000);
}

static void testTuyaLegacyAndUnknown() {
  std::vector<uint8_t> f = {
    0x01,0x33,0x01, 0x00,0x02,
    5,0x02,0x00,0x04, 0x00,0x00,0x00,0xC8, // 20.0 C
    3,0x02,0x00,0x04, 0x00,0x00,0x00,0x21, // 33% soil
    200,0x04,0x00,0x01,0x07                  // unknown enum stays observable
  };
  zg303z::TuyaFrameInfo info;
  zg303z::NormalizedUpdate n;
  bool sawUnknown = false;
  assert(zg303z::decodeTuyaFrame(f.data(), f.size(), info, n, [&](const zg303z::Datapoint& dp){
    if (dp.metric == zg303z::Metric::Unknown && dp.id == 200 && dp.numeric == 7) sawUnknown = true;
  }));
  assert(n.hasTemperature && n.temperatureCentiC == 2000);
  assert(n.hasSoilMoisture && n.soilMoisturePct == 33);
  assert(sawUnknown);
}

static void testStandardReports() {
  // Temperature cluster attr 0x0000, int16, 2375 = 23.75 C.
  uint8_t t[] = {0x18,0x01,0x0a, 0x00,0x00,0x29, 0x47,0x09};
  zg303z::NormalizedUpdate tn;
  assert(zg303z::decodeStandardReport(zg303z::kTemperatureClusterId, t, sizeof(t), tn));
  assert(tn.hasTemperature && tn.temperatureCentiC == 2375);

  // BatteryPercentageRemaining 170 half-percent units = 85%.
  uint8_t b[] = {0x18,0x02,0x0a, 0x21,0x00,0x20, 170};
  zg303z::NormalizedUpdate bn;
  assert(zg303z::decodeStandardReport(zg303z::kPowerConfigClusterId, b, sizeof(b), bn));
  assert(bn.hasBattery && bn.batteryPct == 85);
}

int main() {
  testPlantLinkRoundTrip();
  testPlantLinkRejectsCorruptFrame();
  testSensorReportWireLayout();
  testTuyaNewMapping();
  testTuyaLegacyAndUnknown();
  testStandardReports();
  std::cout << "shared host tests: PASS\n";
  return 0;
}
