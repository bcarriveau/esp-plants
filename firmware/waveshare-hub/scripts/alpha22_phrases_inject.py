Import("env")
from pathlib import Path
project=Path(env["PROJECT_DIR"])

# Keep the current branch's existing injector behavior by refusing to operate on an unexpected source.
p=project/"src"/"main.cpp"
s=p.read_text(encoding="utf-8")
required=["Alpha.18 permit-join stale-status guard","Alpha.21 H2 firmware identity display"]
for marker in required:
    if marker not in s:
        raise RuntimeError("alpha.22 expects current waveshare-zigbee branch: missing "+marker)

if "Alpha.22 partial-report merge and phrase engine" not in s:
    a='#include "plantlink.h"\n'
    if a not in s: raise RuntimeError("include anchor missing")
    s=s.replace(a,a+'#include "phrase_engine.h"\n',1)

    a="  uint16_t fieldFlags = 0;\n  int16_t temperatureCentiC = 0;\n"
    b="  uint16_t fieldFlags = 0;  // merged values known in RAM\n  uint16_t reportedFieldFlagsThisBoot = 0;\n  espplants_phrases::Rotation phraseRotation{};\n  int16_t temperatureCentiC = 0;\n"
    if a not in s: raise RuntimeError("sensor struct anchor missing")
    s=s.replace(a,b,1)

    a="  return sensor.used && sensor.seenThisBoot &&\n         (sensor.fieldFlags & plantlink::SensorHasSoilMoisture);\n"
    b="  return sensor.used && sensor.seenThisBoot &&\n         (sensor.reportedFieldFlagsThisBoot & plantlink::SensorHasSoilMoisture);\n"
    if a not in s: raise RuntimeError("fresh moisture anchor missing")
    s=s.replace(a,b,1)

    start=s.find("const char *mood(const PlantSensor &s) {")
    end=s.find("\n}\n\nvoid sendFrame",start)
    if start<0 or end<0: raise RuntimeError("mood anchor missing")
    repl='''const char *mood(PlantSensor &s) {
  if (!s.seenThisBoot) return "Waiting for this plant to check in";
  if (!(s.reportedFieldFlagsThisBoot & plantlink::SensorHasSoilMoisture))
    return "Waiting for a moisture reading";
  const bool warning=(s.reportedFieldFlagsThisBoot & plantlink::SensorHasWaterWarning) && s.waterWarning;
  const auto state=espplants_phrases::stateFor(s.soilMoisturePct,warning);
  const size_t slot=static_cast<size_t>(&s-sensors);
  const uint32_t seed=static_cast<uint32_t>(slot*2654435761u)^static_cast<uint32_t>(s.soilMoisturePct*257u);
  return espplants_phrases::select(s.phraseRotation,espplants_phrases::Theme::FUNNY,state,seed,false);
}'''
    s=s[:start]+repl+s[end+2:]

    a='''  s->seenThisBoot = true;
  s->shortAddress = report.shortAddress;
  s->fieldFlags = report.fieldFlags;
  s->temperatureCentiC = report.temperatureCentiC;
  s->humidityCentiPct = report.humidityCentiPct;
  s->soilMoisturePct = report.soilMoisturePct;
  s->batteryPct = report.batteryPct;
  s->waterWarning = report.waterWarning;
  s->lqi = report.lqi;
'''
    b='''  s->seenThisBoot = true;
  s->shortAddress = report.shortAddress;

  // Alpha.22 partial-report merge and phrase engine.
  // ZG-303Z reports may contain only a subset of measurements.
  const bool moistureReported=(report.fieldFlags & plantlink::SensorHasSoilMoisture)!=0;
  s->fieldFlags |= report.fieldFlags;
  s->reportedFieldFlagsThisBoot |= report.fieldFlags;
  if (report.fieldFlags & plantlink::SensorHasTemperature) s->temperatureCentiC=report.temperatureCentiC;
  if (report.fieldFlags & plantlink::SensorHasHumidity) s->humidityCentiPct=report.humidityCentiPct;
  if (moistureReported) {
    s->soilMoisturePct=report.soilMoisturePct;
    const bool warning=(report.fieldFlags & plantlink::SensorHasWaterWarning) && report.waterWarning;
    const auto state=espplants_phrases::stateFor(report.soilMoisturePct,warning);
    const size_t phraseSlot=static_cast<size_t>(s-sensors);
    const uint32_t seed=static_cast<uint32_t>(phraseSlot*2654435761u)^static_cast<uint32_t>(report.soilMoisturePct*257u);
    (void)espplants_phrases::select(s->phraseRotation,espplants_phrases::Theme::FUNNY,state,seed,true);
  }
  if (report.fieldFlags & plantlink::SensorHasBattery) s->batteryPct=report.batteryPct;
  if (report.fieldFlags & plantlink::SensorHasWaterWarning) s->waterWarning=report.waterWarning;
  s->lqi = report.lqi;
'''
    if a not in s: raise RuntimeError("sensor report anchor missing")
    s=s.replace(a,b,1)
p.write_text(s,encoding="utf-8")
