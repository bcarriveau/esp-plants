from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
UPDATE = ROOT / "firmware/waveshare-hub/src/update_service.cpp"
INSTALLER = ROOT / "firmware/waveshare-hub/src/plants_ota_installer.cpp"
POLICY = ROOT / "firmware/waveshare-hub/include/update_policy.h"
H2_CLIENT = ROOT / "firmware/waveshare-hub/src/h2_ota_client.cpp"
PIO = ROOT / "firmware/waveshare-hub/platformio.ini"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_network_worker_is_persistent_and_psram_backed():
    source = read(UPDATE)
    worker = function_body(source, "void otaNetworkWorkerTask(void *)")
    creator = function_body(source, "bool ensureOtaNetworkWorker(")
    assert "for (;;)" in worker
    assert "ulTaskNotifyTake(pdTRUE, portMAX_DELAY)" in worker
    assert "xTaskCreateWithCaps" in creator
    assert '"espplants-ota-network"' in creator
    assert "kOtaNetworkWorkerStackBytes" in creator
    assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in creator
    assert "vTaskDelete" not in worker


def test_flash_writer_stack_and_control_memory_are_internal_only():
    source = read(INSTALLER)
    starter = function_body(source, "bool startFlashWriter(")
    writer = function_body(source, "void flashWriterTask(void *parameter)")
    assert "xTaskCreateWithCaps" in starter
    assert '"espplants-ota-flash"' in starter
    assert "kFlashWriterStackBytes = 6144U" in source
    assert "MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT" in starter
    assert "MALLOC_CAP_SPIRAM" not in starter
    assert "esp_ota_write" in writer
    assert "internalWriteBuffer" in writer


def test_flash_write_never_passes_a_psram_pointer_to_esp_ota_write():
    source = read(INSTALLER)
    writer = function_body(source, "void flashWriterTask(void *parameter)")
    copy_pos = writer.index("memcpy(writer.internalWriteBuffer")
    write_pos = writer.index("esp_ota_write(writer.otaHandle, writer.internalWriteBuffer")
    assert copy_pos < write_pos
    assert "writer.psramRing" in writer
    assert "esp_ota_write(writer.otaHandle, writer.psramRing" not in writer


def test_internal_flash_buffer_and_psram_ring_are_bounded():
    source = read(INSTALLER)
    assert "kInternalFlashWriteBufferBytes = 1024U" in source
    assert "kOtaRingSlotBytes = 4096U" in source
    assert "kOtaRingSlotCount = 8U" in source
    assert "kOtaRingBytes = kOtaRingSlotBytes * kOtaRingSlotCount" in source
    assert "static_assert(sizeof(FlashWriterContext) <= 2048U" in source
    assert "heap_caps_malloc(\n      kOtaRingBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)" in source


def test_package_and_h2_downloads_remain_bounded():
    policy = read(POLICY)
    installer = read(INSTALLER)
    update = read(UPDATE)
    h2 = read(H2_CLIENT)
    assert "kMaximumPackageBytes = 7U * 1024U * 1024U" in policy
    assert "kMaxReleaseListBytes = 96U * 1024U" in update
    assert "class PsramText" in update
    assert "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT" in update[update.index("class PsramText"):update.index("class MetadataWorkspaceGuard")]
    assert "String releasesBody" not in update
    assert "String manifestBody" not in update
    assert "packageLayoutValid(release.packageSize, release.firmwareSize)" in installer
    assert "kMaxH2Bytes = 0xE0000u" in h2
    assert "heap_caps_malloc(kMaxH2Bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)" in h2


def test_preflight_logs_and_gates_current_internal_heap_and_largest_block():
    source = read(UPDATE)
    snapshot = function_body(source, "OtaMemorySnapshot otaMemorySnapshot(")
    gate = function_body(source, "bool otaMemorySafe(")
    assert "heap_caps_get_free_size" in snapshot
    assert "heap_caps_get_minimum_free_size" in snapshot
    assert "heap_caps_get_largest_free_block" in snapshot
    assert "ESP.getFreePsram()" in snapshot
    assert "kOtaMinInternalFreeBytes = 48U * 1024U" in source
    assert "kOtaMinLargestInternalBlockBytes = 32U * 1024U" in source
    assert "snapshot.internalFree >= kOtaMinInternalFreeBytes" in gate
    assert "snapshot.largestInternalBlock >= kOtaMinLargestInternalBlockBytes" in gate


def test_transport_recovery_is_bounded_to_one_retry():
    source = read(UPDATE)
    check = function_body(source, "void runUpdateCheckOnNetworkWorker()")
    install = function_body(source, "void runInstallOnNetworkWorker()")
    assert "for (uint8_t attempt = 0; attempt < 2; ++attempt)" in check
    assert "for (uint8_t attempt = 0; attempt < 2; ++attempt)" in install
    assert "recoveryUsed" in check and "recoveryUsed" in install
    assert check.count("recoverOtaNetwork(") == 1
    assert install.count("recoverOtaNetwork(") == 1


def test_network_recovery_preserves_credentials_and_user_wifi_policy():
    source = read(UPDATE)
    recovery = function_body(source, "bool recoverOtaNetwork(")
    assert "reconnectSuppressed || setupPortalRunning" in recovery
    assert "beginStationConnection(true)" in recovery
    assert "savedSsid" in recovery
    assert "networkPreferences.remove" not in recovery
    assert "networkPreferences.putString" not in recovery
    assert "WiFi.disconnect(true, false)" in function_body(source, "void beginStationConnection(")
    assert 'networkPreferences.remove("ssid")' in function_body(source, "void forgetWifi()")


def test_recovery_waits_for_ip_fresh_sntp_and_dns():
    source = read(UPDATE)
    recovery = function_body(source, "bool recoverOtaNetwork(")
    assert "validStationAddress()" in recovery
    assert "ntpSynchronized = false" in recovery
    assert 'configureTimeSync("OTA transport recovery")' in recovery
    assert "secureTimeReady()" in recovery
    assert "verifyGithubDns()" in recovery
    dns = function_body(source, "bool verifyGithubDns()")
    assert 'getaddrinfo("api.github.com"' in dns
    assert "freeaddrinfo(result)" in dns


def test_https_certificate_validation_remains_enabled_everywhere():
    for path in (UPDATE, INSTALLER, H2_CLIENT):
        source = read(path)
        assert "esp_crt_bundle_attach" in source
        assert "skip_cert_common_name_check = false" in source
        assert "setInsecure" not in source


def test_redirects_remain_bounded_and_allowlisted():
    update = read(UPDATE)
    installer = read(INSTALLER)
    policy = read(POLICY)
    h2 = read(H2_CLIENT)
    assert "kMaxMetadataRedirects = 3" in update
    assert "kMaxRedirects = 3" in installer
    assert "for (int redirects = 0; redirects <= 3; ++redirects)" in h2
    for host in (
        '"github.com"',
        '"objects.githubusercontent.com"',
        '"release-assets.githubusercontent.com"',
    ):
        assert host in policy
    assert "parseAllowedHttpsUrl" in installer
    assert "metadataHostAllowed" in update


def test_package_and_firmware_sha_are_required_before_commit():
    source = read(INSTALLER)
    finish = function_body(source, "bool finishPackage(")
    assert "actualPackageSha" in finish
    assert "release.packageSha256" in finish
    assert "actualFirmwareSha" in finish
    assert "release.firmwareSha256" in finish
    assert "workspace.packageHeader.firmwareSha256" in finish
    commit = finish.index("FlashWriterState::COMMIT_REQUESTED")
    assert finish.index("release.packageSha256") < commit
    assert finish.index("release.firmwareSha256") < commit


def test_h2_manifest_metadata_remains_authoritative():
    source = read(UPDATE)
    for token in (
        "h2Product != kH2ProductId",
        "h2Hardware != kH2HardwareId",
        "h2Protocol != plantlink::kProtocolVersion",
        "h2BuildId != expectedH2Build",
        "h2Asset != expectedH2Asset",
        "h2AssetNameValid(h2Asset.c_str())",
        "candidate.h2FirmwareSize = h2FirmwareSize",
        "candidate.h2FirmwareSha256",
    ):
        assert token in source
    client = read(H2_CLIENT)
    assert "release.h2BuildId" in client
    assert "release.h2Asset" in client
    assert "release.h2FirmwareSha256" in client
    assert '../../m5-h2-zigbee/include/build_version.h' not in client


def test_h2_distribution_and_build_validation_remain_required():
    h2_header = read(ROOT / "firmware/m5-h2-zigbee/include/build_version.h")
    client = read(H2_CLIENT)
    assert 'ESP_PLANTS_H2_RELEASE_MARKER "ESP-PLANTS-H2-DISTRIBUTION-BUILD"' in h2_header
    assert "H2 firmware SHA-256 mismatch" in client
    assert "release.h2BuildId" in client
    assert "release.h2FirmwareSha256" in client
    receiver = ROOT / "firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp"
    if receiver.exists():
        text = read(receiver)
        assert "kExpectedDistributionMarker[]=ESP_PLANTS_H2_RELEASE_MARKER" in text
        assert "!ota.buildSeen||!ota.markerSeen" in text


def test_boot_partition_switch_only_occurs_after_complete_validation():
    source = read(INSTALLER)
    writer = function_body(source, "void flashWriterTask(void *parameter)")
    assert source.count("esp_ota_set_boot_partition") == 1
    assert writer.index("esp_ota_end") < writer.index("esp_ota_set_boot_partition")
    assert "FlashWriterState::COMMIT_REQUESTED" in writer
    finish = function_body(source, "bool finishPackage(")
    assert "FlashWriterState::COMMIT_REQUESTED" in finish
    assert "esp_ota_set_boot_partition" not in finish


def test_com11_is_preserved():
    assert "upload_port = COM11" in read(PIO)


def test_repeated_attempts_do_not_recreate_network_worker_or_leak_http_clients():
    update = read(UPDATE)
    installer = read(INSTALLER)
    creator = function_body(update, "bool ensureOtaNetworkWorker(")
    assert "if (otaNetworkTaskHandle) return true;" in creator
    assert "xTaskCreate(installTask" not in update
    assert "vTaskDelete" not in function_body(update, "void otaNetworkWorkerTask(void *)")
    assert "esp_http_client_cleanup(client)" in update
    assert "releaseHttpClient(client" in installer
    assert "vTaskDeleteWithCaps(writer->writerTask)" in installer
    assert "abortAndDestroyFlashWriter(*workspace_)" in installer


def test_version_is_alpha38_and_h2_version_remains_independent():
    waveshare = read(ROOT / "firmware/waveshare-hub/include/build_version.h")
    h2 = read(ROOT / "firmware/m5-h2-zigbee/include/build_version.h")
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.38"' in waveshare
    assert read(ROOT / "VERSION").strip() == "0.2.0-alpha.38"
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.25"' in h2
