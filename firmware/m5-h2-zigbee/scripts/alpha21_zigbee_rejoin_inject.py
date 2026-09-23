Import("env")
from pathlib import Path

p = Path(env["PROJECT_DIR"]) / "src" / "main.cpp"
s = p.read_text(encoding="utf-8")

# Alpha.20 preserves Zigbee NVRAM and keeps the Arduino-Zigbee coordinator's
# connected state intact. A previous Alpha.19 pre-build injection accidentally
# placed Zigbee.openNetwork(30) after the first generic Zigbee.connected()
# assignment, which is inside the 1.5-second status loop. That repeatedly
# reopened the network and could trip a FreeRTOS critical-section assertion.
alpha19_block = '''  zigbeeReady = Zigbee.connected();

  // Alpha.19 post-start router rejoin window. Opening here preserves the
  // library's connected state, unlike setRebootOpenNetwork(30) on coordinator
  // reboot, while still giving powered routers time to re-establish the mesh.
  if (zigbeeReady) {
    Zigbee.openNetwork(30);
    Serial.println("[zigbee] post-start router rejoin window open for 30 seconds");
  }
'''
if alpha19_block in s:
    s = s.replace(alpha19_block, '  zigbeeReady = Zigbee.connected();\n')

if 'Zigbee.setRebootOpenNetwork(30);' in s:
    s = s.replace('  Zigbee.setRebootOpenNetwork(30);\n',
                  '  Zigbee.setRebootOpenNetwork(0);\n', 1)
elif 'Zigbee.setRebootOpenNetwork(0);' not in s:
    raise RuntimeError('Alpha.20 reboot-open-network anchor not found')

if 'Zigbee.begin(&coordinatorConfig, false)' not in s:
    old = '  if (!Zigbee.begin(&coordinatorConfig)) {\n'
    new = '  if (!Zigbee.begin(&coordinatorConfig, false)) {\n'
    if old not in s:
        raise RuntimeError('Alpha.20 Zigbee.begin anchor not found')
    s = s.replace(old, new, 1)

marker = 'Alpha.20 one-shot post-start router rejoin window'
if marker not in s:
    anchor = '''  startZigbee();
  sendNetworkStatus();
'''
    replacement = '''  startZigbee();

  // Alpha.20 one-shot post-start router rejoin window. This runs exactly once
  // per H2 boot, after the restored coordinator reports ready.
  if (zigbeeReady) {
    Zigbee.openNetwork(30);
    Serial.println("[zigbee] one-shot router rejoin window open for 30 seconds");
  }
  sendNetworkStatus();
'''
    if anchor not in s:
        raise RuntimeError('Alpha.20 setup rejoin anchor not found')
    s = s.replace(anchor, replacement, 1)

# Alpha.21: HelloAck must report the actual running H2 build. The Waveshare H2
# OTA client already treats ESPPLANTS-H2-* as authoritative for installed H2
# identity, so the old hard-coded 0.1-dev string prevented reliable comparison.
if '#include "build_version.h"' not in s:
    include_anchor = '#include "plantlink.h"\n'
    if include_anchor not in s:
        raise RuntimeError('Alpha.21 build-version include anchor not found')
    s = s.replace(include_anchor, include_anchor + '#include "build_version.h"\n', 1)

old_hello = '''void sendHelloAck() {
  static constexpr char kBuild[] = "m5-h2-zigbee/0.1-dev";
  sendFrame(plantlink::MessageType::HelloAck,
            reinterpret_cast<const uint8_t *>(kBuild), sizeof(kBuild) - 1,
            plantlink::FlagResponse);
  sendNetworkStatus();
}
'''
new_hello = '''void sendHelloAck() {
  static constexpr char kBuild[] = ESP_PLANTS_H2_BUILD_ID;
  sendFrame(plantlink::MessageType::HelloAck,
            reinterpret_cast<const uint8_t *>(kBuild), sizeof(kBuild) - 1,
            plantlink::FlagResponse);
  sendNetworkStatus();
}
'''
if old_hello in s:
    s = s.replace(old_hello, new_hello, 1)
elif 'static constexpr char kBuild[] = ESP_PLANTS_H2_BUILD_ID;' not in s:
    raise RuntimeError('Alpha.21 H2 HelloAck anchor not found')

p.write_text(s, encoding="utf-8")
