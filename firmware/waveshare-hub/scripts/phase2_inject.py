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
