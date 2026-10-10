"""Host-level regression tests for Zigbee packet freshness and IEEE identity.

The compiled exercise extracts the ACTUAL functions from the H2 firmware,
using only lightweight test doubles for Arduino/ESP-Zigbee types. No real
radio, flash, or PIO build is simulated by these tests.
"""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
H2 = ROOT / "firmware/m5-h2-zigbee/src/main.cpp"
WS = ROOT / "firmware/waveshare-hub/src/main.cpp"


def function(source: str, declaration: str) -> str:
    start = source.find(declaration)
    if start < 0:
        raise AssertionError("Missing function: " + declaration)
    brace = source.find("{", start)
    depth = 0
    for i in range(brace, len(source)):
        if source[i] == "{":
            depth += 1
        if source[i] == "}":
            depth -= 1
            if depth == 0:
                return source[start:i+1]
    raise AssertionError("Incomplete function: " + declaration)


class ZigbeeFreshnessIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.h2 = H2.read_text(encoding="utf-8")
        cls.ws = WS.read_text(encoding="utf-8")

    def test_runtime_wiring_and_rejoin_invalidation(self):
        send = function(self.h2, "void sendSensorReport(")
        decode = function(self.h2, "void processApsEvent(")
        self.assertIn("report.fieldFlags = updatedFieldFlags;", send)
        self.assertNotIn("report.fieldFlags = sensor.fieldFlags;", send)
        self.assertIn("const uint16_t updatedFields = applyNormalized(*sensor, normalized);", decode)
        self.assertIn("sendSensorReport(*sensor, updatedFields)", decode)
        self.assertNotIn("if (!sensor) sensor = findSensorByShort(event.shortAddress);", decode)

        leave = function(self.ws, "void handleDeviceLeft(")
        report = function(self.ws, "void handleSensorReport(")
        self.assertIn("s->reportedFieldFlagsThisBoot = 0;", leave)
        self.assertIn("s->fieldFlags = 0;", leave)
        self.assertIn("s->phraseDisplayPending = false;", leave)
        self.assertIn("s->phraseRotation = {};", leave)
        self.assertIn("const bool moistureReported=(report.fieldFlags & plantlink::SensorHasSoilMoisture)!=0;", report)
        self.assertIn("espplants_phrases::select(s->phraseRotation,phraseTheme,state,seed,true)", report)
        self.assertNotIn("previousSoilMoisture != report.soilMoisturePct) {", report)

    def test_actual_h2_functions_with_native_test_doubles(self):
        compiler = shutil.which("g++") or shutil.which("clang++")
        if not compiler:
            self.skipTest("No host C++ compiler; source wiring checks still run")
        pieces = [function(self.h2, x) for x in [
            "InfrastructureState *findInfrastructureByIeee(",
            "InfrastructureState *findInfrastructureByShort(",
            "InfrastructureState *findOrCreateInfrastructure(",
            "SensorState *findSensorByIeee(",
            "SensorState *findOrCreateSensor(",
            "uint16_t applyNormalized(",
        ]]
        code = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
#include <cstdio>
namespace plantlink {
constexpr uint16_t SensorHasTemperature=1, SensorHasHumidity=2;
constexpr uint16_t SensorHasSoilMoisture=4, SensorHasBattery=8, SensorHasWaterWarning=16;
}
namespace zg303z {
struct NormalizedUpdate {
 bool hasTemperature=false; int16_t temperatureCentiC=0;
 bool hasHumidity=false; uint16_t humidityCentiPct=0;
 bool hasSoilMoisture=false; uint8_t soilMoisturePct=0;
 bool hasBattery=false; uint8_t batteryPct=0;
 bool hasWaterWarning=false; bool waterWarning=false;
};
}
struct ApsEvent { uint8_t ieee[8]{}; uint16_t shortAddress=0xffff; };
struct SensorState {
 bool used=false; uint8_t ieee[8]{}; uint16_t shortAddress=0xffff; uint16_t fieldFlags=0;
 int16_t temperatureCentiC=0; uint16_t humidityCentiPct=0; uint8_t soilMoisturePct=0;
 uint8_t batteryPct=0,waterWarning=0;
};
struct InfrastructureState {
 bool used=false,online=false; uint8_t ieee[8]{}; uint16_t shortAddress=0xffff;
};
SensorState sensors[4]; InfrastructureState infrastructure[4];
uint8_t sensorCount=0, infrastructureCount=0;
bool ieeeIsZero(const uint8_t ieee[8]) {for(int i=0;i<8;i++)if(ieee[i])return false;return true;}
bool ieeeEqual(const uint8_t a[8],const uint8_t b[8]) {return memcmp(a,b,8)==0;}
'''
        # Definitions can appear in a different order in the firmware; the
        # extracted native code is compiled with harmless forward declarations.
        code += '''
SensorState *findSensorByIeee(const uint8_t ieee[8]);
InfrastructureState *findInfrastructureByIeee(const uint8_t ieee[8]);
InfrastructureState *findInfrastructureByShort(uint16_t shortAddress);
'''
        code += "\n".join(pieces)
        code += r'''
int main() {
 auto& a=sensors[0];a.used=true;a.ieee[0]=0x11;a.shortAddress=0x1234;sensorCount=1;
 zg303z::NormalizedUpdate m;m.hasSoilMoisture=true;m.soilMoisturePct=42;
 assert(applyNormalized(a,m)==plantlink::SensorHasSoilMoisture);
 assert(applyNormalized(a,m)==plantlink::SensorHasSoilMoisture); // same value, fresh report
 zg303z::NormalizedUpdate temp;temp.hasTemperature=true;temp.temperatureCentiC=2210;
 assert(applyNormalized(a,temp)==plantlink::SensorHasTemperature); // no stale moisture flag
 assert(a.fieldFlags==(plantlink::SensorHasSoilMoisture|plantlink::SensorHasTemperature));
 zg303z::NormalizedUpdate nothing;assert(applyNormalized(a,nothing)==0);

 bool created=false;ApsEvent b;b.ieee[0]=0x22;b.shortAddress=0x1234;
 SensorState* news=findOrCreateSensor(b,created);
 assert(created && news && news!=&a && news->ieee[0]==0x22);
 assert(a.ieee[0]==0x11 && a.shortAddress==0xffff);
 assert(news->shortAddress==0x1234);
 ApsEvent missing;missing.shortAddress=0x1234;
 created=false;assert(findOrCreateSensor(missing,created)==nullptr && !created);
 ApsEvent again;again.ieee[0]=0x11;again.shortAddress=0x5678;
 created=false;assert(findOrCreateSensor(again,created)==&a && !created);
 assert(a.shortAddress==0x5678 && a.ieee[0]==0x11);

 uint8_t r1[8]={0x31},r2[8]={0x32};
 created=false;auto* old=findOrCreateInfrastructure(r1,0x9988,created);
 assert(created && old);
 old->online=true;
 created=false;auto* repl=findOrCreateInfrastructure(r2,0x9988,created);
 assert(created && repl && repl!=old);
 assert(old->ieee[0]==0x31 && old->shortAddress==0xffff && !old->online);
 assert(repl->ieee[0]==0x32 && repl->shortAddress==0x9988);
 uint8_t empty[8]{};
 created=false;assert(findOrCreateInfrastructure(empty,0x9988,created)==nullptr);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as directory:
            src = Path(directory) / "h2_contract.cpp"
            binary = Path(directory) / "h2_contract"
            src.write_text(code, encoding="utf-8")
            build = subprocess.run([compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror",
                                    str(src), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
