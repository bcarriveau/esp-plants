Import("env")
from pathlib import Path

p = Path(env["PROJECT_DIR"]) / "src" / "main.cpp"
s = p.read_text(encoding="utf-8")

# Alpha.19 preserves Zigbee NVRAM while avoiding an Arduino-Zigbee reboot path
# that leaves Zigbee.connected() false when setRebootOpenNetwork() is nonzero.
# Restore the coordinator first with reboot-open disabled, then explicitly open
# a short router rejoin window only after the coordinator reports connected.
if 'Zigbee.setRebootOpenNetwork(30);' in s:
    s = s.replace('  Zigbee.setRebootOpenNetwork(30);\n',
                  '  Zigbee.setRebootOpenNetwork(0);\n', 1)
elif 'Zigbee.setRebootOpenNetwork(0);' not in s:
    raise RuntimeError('Alpha.19 reboot-open-network anchor not found')

if 'Zigbee.begin(&coordinatorConfig, false)' not in s:
    old = '  if (!Zigbee.begin(&coordinatorConfig)) {\n'
    new = '  if (!Zigbee.begin(&coordinatorConfig, false)) {\n'
    if old not in s:
        raise RuntimeError('Alpha.19 Zigbee.begin anchor not found')
    s = s.replace(old, new, 1)

marker = 'Alpha.19 post-start router rejoin window'
if marker not in s:
    anchor = '  zigbeeReady = Zigbee.connected();\n'
    replacement = '''  zigbeeReady = Zigbee.connected();

  // Alpha.19 post-start router rejoin window. Opening here preserves the
  // library's connected state, unlike setRebootOpenNetwork(30) on coordinator
  // reboot, while still giving powered routers time to re-establish the mesh.
  if (zigbeeReady) {
    Zigbee.openNetwork(30);
    Serial.println("[zigbee] post-start router rejoin window open for 30 seconds");
  }
'''
    if anchor not in s:
        raise RuntimeError('Alpha.19 coordinator-ready anchor not found')
    s = s.replace(anchor, replacement, 1)

p.write_text(s, encoding="utf-8")
