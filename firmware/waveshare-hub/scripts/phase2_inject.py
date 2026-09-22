Import("env")
from pathlib import Path
p=Path(env["PROJECT_DIR"])/"src"/"plants_ota_installer.cpp"
s=p.read_text(encoding="utf-8")
needle='#include "update_policy.h"\n'
if '#include "h2_ota_client.h"' not in s:
    s=s.replace(needle, needle+'#include "h2_ota_client.h"\n')
needle='  message[0] = 0;\n\n'
block='''  message[0] = 0;\n\n  // Phase 2 Update All: H2 must validate, switch its inactive OTA slot, reboot,\n  // and report the target build before the Waveshare inactive slot is touched.\n  {\n    const auto h2Result = espplants_h2_ota::updateForRelease(\n        release, progress, message, messageCapacity);\n    if (h2Result != espplants_h2_ota::Result::OK) return Result::FAILED;\n  }\n\n'''
if 'Phase 2 Update All: H2 must validate' not in s:
    if needle not in s: raise RuntimeError('Phase 2 injection point not found')
    s=s.replace(needle,block,1)
p.write_text(s,encoding="utf-8")
