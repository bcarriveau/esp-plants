Import("env")
from pathlib import Path

p = Path(env["PROJECT_DIR"]) / "src" / "main.cpp"
s = p.read_text(encoding="utf-8")

# Alpha.20 preserves Zigbee NVRAM and keeps the Arduino-Zigbee coordinator's
# connected state intact. A previous Alpha.19 pre-build injection accidentally
# placed Zigbee.openNetwork(30) after the first generic Zigbee.connected()
# assignment, which is inside the 1.5-second status loop. That repeatedly
# reopened the network and could trip a FreeRTOS critical-section assertion.
# Remove that injected block if it is already present in a developer workspace.
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

# Reboot-open must remain disabled so Arduino-Zigbee marks a restored
# coordinator connected after a non-factory-reset reboot.
if 'Zigbee.setRebootOpenNetwork(30);' in s:
    s = s.replace('  Zigbee.setRebootOpenNetwork(30);\n',
                  '  Zigbee.setRebootOpenNetwork(0);\n', 1)
elif 'Zigbee.setRebootOpenNetwork(0);' not in s:
    raise RuntimeError('Alpha.20 reboot-open-network anchor not found')

# Preserve the existing Zigbee network/NVRAM across normal H2 reboot and OTA.
if 'Zigbee.begin(&coordinatorConfig, false)' not in s:
    old = '  if (!Zigbee.begin(&coordinatorConfig)) {\n'
    new = '  if (!Zigbee.begin(&coordinatorConfig, false)) {\n'
    if old not in s:
        raise RuntimeError('Alpha.20 Zigbee.begin anchor not found')
    s = s.replace(old, new, 1)

# Open the router rejoin window exactly once from setup(), after startZigbee()
# has restored the coordinator and established zigbeeReady. Do not place this
# in servicePlantLink() or any periodic status path.
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

p.write_text(s, encoding="utf-8")
