Import("env")
from pathlib import Path

project = Path(env["PROJECT_DIR"])

# Existing Phase 2 H2-first installer injection.
p = project / "src" / "plants_ota_installer.cpp"
s = p.read_text(encoding="utf-8")
needle = '#include "update_policy.h"\n'
if '#include "h2_ota_client.h"' not in s:
    s = s.replace(needle, needle + '#include "h2_ota_client.h"\n')
needle = '  message[0] = 0;\n\n'
block = '''  message[0] = 0;

  // Phase 2 Update All: H2 must validate, switch its inactive OTA slot, reboot,
  // and report the target build before the Waveshare inactive slot is touched.
  {
    const auto h2Result = espplants_h2_ota::updateForRelease(
        release, progress, message, messageCapacity);
    if (h2Result != espplants_h2_ota::Result::OK) return Result::FAILED;
  }

'''
if 'Phase 2 Update All: H2 must validate' not in s:
    if needle not in s:
        raise RuntimeError('Phase 2 injection point not found')
    s = s.replace(needle, block, 1)
p.write_text(s, encoding="utf-8")

# GitHub release order is not semantic-version order. Scan every compatible
# release and select the highest semantic version before deciding update state.
p = project / "src" / "update_service.cpp"
s = p.read_text(encoding="utf-8")

if 'Phase 2 release selection: choose highest compatible semantic version' not in s:
    loop_anchor = '''  bool sawManifestRelease = false;
  bool manifestTransportFailed = false;
  bool manifestRejected = false;

  for (JsonObject githubRelease : releases.as<JsonArray>()) {
'''
    loop_replacement = '''  bool sawManifestRelease = false;
  bool manifestTransportFailed = false;
  bool manifestRejected = false;

  // Phase 2 release selection: choose highest compatible semantic version.
  bool haveBestRelease = false;
  espplants_ota_installer::Release bestRelease{};
  String bestVersion;

  for (JsonObject githubRelease : releases.as<JsonArray>()) {
'''
    if loop_anchor not in s:
        raise RuntimeError('Release-selection loop anchor not found')
    s = s.replace(loop_anchor, loop_replacement, 1)

    first_match_block = '''    pendingRelease = candidate;
    snprintf(latestVersionText, sizeof(latestVersionText), "%s", candidateVersion.c_str());
    const int comparison = compareVersions(String(ESP_PLANTS_WAVESHARE_VERSION), candidateVersion);
    if (comparison >= 0) {
      hasUpdate = false;
      setStatus("Up to date: v%s", ESP_PLANTS_WAVESHARE_VERSION);
    } else {
      hasUpdate = true;
      setStatus("Verified update available: v%s", latestVersionText);
    }
    rememberCheckTime();
    return true;
  }

  if (manifestTransportFailed) {
'''
    best_match_block = '''    if (!haveBestRelease || compareVersions(candidateVersion, bestVersion) > 0) {
      haveBestRelease = true;
      bestRelease = candidate;
      bestVersion = candidateVersion;
    }
  }

  if (haveBestRelease) {
    pendingRelease = bestRelease;
    snprintf(latestVersionText, sizeof(latestVersionText), "%s", bestVersion.c_str());
    const int comparison =
        compareVersions(String(ESP_PLANTS_WAVESHARE_VERSION), bestVersion);
    if (comparison >= 0) {
      hasUpdate = false;
      setStatus("Up to date: v%s", ESP_PLANTS_WAVESHARE_VERSION);
    } else {
      hasUpdate = true;
      setStatus("Verified update available: v%s", latestVersionText);
    }
    rememberCheckTime();
    return true;
  }

  if (manifestTransportFailed) {
'''
    if first_match_block not in s:
        raise RuntimeError('First-valid-release return block not found')
    s = s.replace(first_match_block, best_match_block, 1)

p.write_text(s, encoding="utf-8")

# Alpha.18 pairing-request guard.
# The display immediately paints the requested 120 seconds. An older H2
# NetworkStatus frame with permit_join=0 can already be queued on PlantLink and
# arrive just after that request. Without a guard, that stale zero overwrites
# 120 and the pairing dialog falsely changes to TRY AGAIN before the H2's
# positive acknowledgement arrives.
p = project / "src" / "main.cpp"
s = p.read_text(encoding="utf-8")

marker = 'Alpha.18 permit-join stale-status guard'
if marker not in s:
    globals_anchor = '''uint32_t pairStartedMs = 0;
'''
    globals_replacement = '''uint32_t pairStartedMs = 0;
// Alpha.18 permit-join stale-status guard: ignore a queued pre-request zero
// briefly while waiting for the H2 to acknowledge a new nonzero join window.
uint32_t permitJoinGuardUntilMs = 0;
'''
    if globals_anchor not in s:
        raise RuntimeError('Alpha.18 pairing global anchor not found')
    s = s.replace(globals_anchor, globals_replacement, 1)

    request_anchor = '''void requestJoin(uint8_t seconds) {
  sendFrame(plantlink::MessageType::PermitJoin, &seconds, 1);
  permitJoinRemaining = seconds;
  uiDirty = true;
  Serial.printf("[plantlink] permit join requested: %u s\\n", seconds);
}
'''
    request_replacement = '''void requestJoin(uint8_t seconds) {
  sendFrame(plantlink::MessageType::PermitJoin, &seconds, 1);
  permitJoinRemaining = seconds;
  permitJoinGuardUntilMs = seconds ? millis() + 3000u : 0;
  uiDirty = true;
  Serial.printf("[plantlink] permit join requested: %u s\\n", seconds);
}
'''
    if request_anchor not in s:
        raise RuntimeError('Alpha.18 requestJoin anchor not found')
    s = s.replace(request_anchor, request_replacement, 1)

    status_anchor = '''  h2SensorCount = frame.payload[2];
  permitJoinRemaining = frame.payload[3];
  h2InfrastructureCount = frame.payloadLength >= 5 ? frame.payload[4] : 0;
'''
    status_replacement = '''  h2SensorCount = frame.payload[2];
  const uint8_t reportedPermitJoin = frame.payload[3];
  const bool joinGuardActive =
      permitJoinGuardUntilMs != 0 &&
      static_cast<int32_t>(permitJoinGuardUntilMs - millis()) > 0;
  if (reportedPermitJoin > 0) {
    permitJoinRemaining = reportedPermitJoin;
    permitJoinGuardUntilMs = 0;
  } else if (!joinGuardActive || permitJoinRemaining == 0) {
    permitJoinRemaining = 0;
    permitJoinGuardUntilMs = 0;
  } else {
    Serial.println("[plantlink] ignored stale permit-join=0 while awaiting H2 acknowledgement");
  }
  h2InfrastructureCount = frame.payloadLength >= 5 ? frame.payload[4] : 0;
'''
    if status_anchor not in s:
        raise RuntimeError('Alpha.18 NetworkStatus anchor not found')
    s = s.replace(status_anchor, status_replacement, 1)

p.write_text(s, encoding="utf-8")
