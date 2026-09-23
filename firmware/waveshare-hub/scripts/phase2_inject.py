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

# Alpha.21: retain the H2's actual reported build and show it on Network & Updates.
# The H2 build is authoritative; do not infer it from the Waveshare release.
marker = 'Alpha.21 H2 firmware identity display'
if marker not in s:
    globals_anchor = '''uint8_t permitJoinRemaining = 0;
'''
    globals_replacement = '''uint8_t permitJoinRemaining = 0;
// Alpha.21 H2 firmware identity display. Populated only from H2 HelloAck.
char h2BuildId[96]{};
'''
    if globals_anchor not in s:
        raise RuntimeError('Alpha.21 H2 build global anchor not found')
    s = s.replace(globals_anchor, globals_replacement, 1)

    ui_anchor = '''lv_obj_t *updateCurrentVersion = nullptr;
lv_obj_t *updateLatestVersion = nullptr;
'''
    ui_replacement = '''lv_obj_t *updateCurrentVersion = nullptr;
lv_obj_t *updateLatestVersion = nullptr;
lv_obj_t *updateH2Version = nullptr;
'''
    if ui_anchor not in s:
        raise RuntimeError('Alpha.21 update label global anchor not found')
    s = s.replace(ui_anchor, ui_replacement, 1)

    software_anchor = '''  updateLatestVersion = lv_label_create(software);
  lv_obj_set_style_text_font(updateLatestVersion, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(updateLatestVersion, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(updateLatestVersion, 190, 78);
  lv_obj_set_width(updateLatestVersion, 150);
  lv_label_set_long_mode(updateLatestVersion, LV_LABEL_LONG_DOT);

  updateStatus = lv_label_create(software);
'''
    software_replacement = '''  updateLatestVersion = lv_label_create(software);
  lv_obj_set_style_text_font(updateLatestVersion, &lv_font_montserrat_18, 0);
  lv_obj_set_style_text_color(updateLatestVersion, lv_color_hex(0xE5ECE7), 0);
  lv_obj_set_pos(updateLatestVersion, 190, 78);
  lv_obj_set_width(updateLatestVersion, 150);
  lv_label_set_long_mode(updateLatestVersion, LV_LABEL_LONG_DOT);

  // Alpha.21 H2 firmware identity display.  y=104, font 14 => bottom ~121;
  // updateStatus begins at y=128, so the rows do not overlap.
  updateH2Version = lv_label_create(software);
  lv_label_set_text(updateH2Version, "H2 GATEWAY: unavailable");
  lv_obj_set_style_text_font(updateH2Version, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(updateH2Version, lv_color_hex(0x9DB5A5), 0);
  lv_obj_set_pos(updateH2Version, 20, 104);
  lv_obj_set_width(updateH2Version, 326);
  lv_label_set_long_mode(updateH2Version, LV_LABEL_LONG_DOT);

  updateStatus = lv_label_create(software);
'''
    if software_anchor not in s:
        raise RuntimeError('Alpha.21 software-card anchor not found')
    s = s.replace(software_anchor, software_replacement, 1)

    status_pos_anchor = '''  lv_obj_set_pos(updateStatus, 20, 120);
  lv_obj_set_width(updateStatus, 326);
  lv_obj_set_height(updateStatus, 60);
'''
    status_pos_replacement = '''  lv_obj_set_pos(updateStatus, 20, 128);
  lv_obj_set_width(updateStatus, 326);
  lv_obj_set_height(updateStatus, 60);
'''
    if status_pos_anchor not in s:
        raise RuntimeError('Alpha.21 update-status position anchor not found')
    s = s.replace(status_pos_anchor, status_pos_replacement, 1)

    hello_anchor = '''    case plantlink::MessageType::HelloAck: {
      char build[plantlink::kMaxPayloadBytes + 1]{};
      const size_t n = frame.payloadLength < sizeof(build) - 1 ? frame.payloadLength : sizeof(build) - 1;
      memcpy(build, frame.payload, n);
      Serial.printf("[plantlink] H2 hello: %s\\n", build);
      break;
    }
'''
    hello_replacement = '''    case plantlink::MessageType::HelloAck: {
      char build[plantlink::kMaxPayloadBytes + 1]{};
      const size_t n = frame.payloadLength < sizeof(build) - 1 ? frame.payloadLength : sizeof(build) - 1;
      memcpy(build, frame.payload, n);
      strncpy(h2BuildId, build, sizeof(h2BuildId) - 1);
      h2BuildId[sizeof(h2BuildId) - 1] = '\\0';
      Serial.printf("[plantlink] H2 hello: %s\\n", build);
      uiDirty = true;
      break;
    }
'''
    if hello_anchor not in s:
        raise RuntimeError('Alpha.21 HelloAck anchor not found')
    s = s.replace(hello_anchor, hello_replacement, 1)

    service_anchor = '''  if (!h2Online && now - lastHelloMs >= kHelloIntervalMs) { lastHelloMs = now; sendHello(); }
'''
    service_replacement = '''  if ((!h2Online || !h2BuildId[0]) && now - lastHelloMs >= kHelloIntervalMs) {
    lastHelloMs = now;
    sendHello();
  }
'''
    if service_anchor not in s:
        raise RuntimeError('Alpha.21 Hello retry anchor not found')
    s = s.replace(service_anchor, service_replacement, 1)

    refresh_anchor = '''  snprintf(text, sizeof(text), "v%s", espplants_update::currentVersion());
  label(updateCurrentVersion, text);
  if (strcmp(espplants_update::latestVersion(), "--") == 0) {
'''
    refresh_replacement = '''  snprintf(text, sizeof(text), "v%s", espplants_update::currentVersion());
  label(updateCurrentVersion, text);

  if (!h2Online) {
    label(updateH2Version, "H2 GATEWAY: unavailable");
  } else if (strncmp(h2BuildId, "ESPPLANTS-H2-", 13) == 0 && h2BuildId[13]) {
    snprintf(text, sizeof(text), "H2 GATEWAY: v%s", h2BuildId + 13);
    label(updateH2Version, text);
  } else if (h2BuildId[0]) {
    snprintf(text, sizeof(text), "H2 GATEWAY: %s", h2BuildId);
    label(updateH2Version, text);
  } else {
    label(updateH2Version, "H2 GATEWAY: reading version...");
  }

  if (strcmp(espplants_update::latestVersion(), "--") == 0) {
'''
    if refresh_anchor not in s:
        raise RuntimeError('Alpha.21 update refresh anchor not found')
    s = s.replace(refresh_anchor, refresh_replacement, 1)

p.write_text(s, encoding="utf-8")
