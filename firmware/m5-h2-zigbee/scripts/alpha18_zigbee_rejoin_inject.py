Import("env")
from pathlib import Path

p = Path(env["PROJECT_DIR"]) / "src" / "main.cpp"
s = p.read_text(encoding="utf-8")

# Alpha.18 keeps coordinator NVRAM explicitly and allows a short post-reboot
# steering window. The network itself is not factory-reset or recreated; this
# window gives powered routers such as the Aeotec Range Extender Zi time to
# re-establish/rejoin after the H2 coordinator restarts.
if 'Zigbee.setRebootOpenNetwork(30);' not in s:
    old = '  Zigbee.setRebootOpenNetwork(0);\n'
    new = '  Zigbee.setRebootOpenNetwork(30);\n'
    if old not in s:
        raise RuntimeError('Alpha.18 reboot-open-network anchor not found')
    s = s.replace(old, new, 1)

if 'Zigbee.begin(&coordinatorConfig, false)' not in s:
    old = '  if (!Zigbee.begin(&coordinatorConfig)) {\n'
    new = '  if (!Zigbee.begin(&coordinatorConfig, false)) {\n'
    if old not in s:
        raise RuntimeError('Alpha.18 Zigbee.begin anchor not found')
    s = s.replace(old, new, 1)

p.write_text(s, encoding="utf-8")
