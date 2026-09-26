from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(p):
    return (ROOT / p).read_text(encoding="utf-8")


def test_protocol_chunk_fits():
    path = ROOT / "shared/plantlink/plantlink_ota.h"
    if not path.exists():
        return
    s = path.read_text(encoding="utf-8")
    assert "kChunkDataBytes=248" in s
    assert "kBeginBytes=133" in s


def test_h2_inactive_slot_and_validation_guards():
    path = ROOT / "firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp"
    if not path.exists():
        return
    s = path.read_text(encoding="utf-8")
    for token in [
        "esp_ota_get_next_update_partition",
        "esp_ota_get_running_partition",
        "esp_ota_end",
        "esp_ota_set_boot_partition",
        "incoming.chip_id!=running.chip_id",
        "kExpectedDistributionMarker[]=ESP_PLANTS_H2_RELEASE_MARKER",
    ]:
        assert token in s


def test_h2_first_then_display_is_integrated_in_psram_network_worker():
    service = read("firmware/waveshare-hub/src/update_service.cpp")
    installer = read("firmware/waveshare-hub/src/plants_ota_installer.cpp")
    worker_start = service.index("void runInstallOnNetworkWorker()")
    worker_end = service.index("OtaNetworkCommand takeOtaNetworkCommand()", worker_start)
    worker = service[worker_start:worker_end]
    h2 = worker.index("espplants_h2_ota::updateForRelease")
    waveshare = worker.index("espplants_ota_installer::install", h2)
    assert h2 < waveshare
    assert 'MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT' in service
    assert '#include "h2_ota_client.h"' not in installer


def test_release_builds_h2_first():
    path = ROOT / "tools/make-waveshare-release.cmd"
    if not path.exists():
        return
    s = path.read_text(encoding="utf-8")
    assert s.index("m5_gateway_h2_release") < s.index(
        "waveshare_s3_touch_lcd_7_release"
    )


def test_manifest_keeps_schema_one_and_h2_metadata():
    s = read("firmware/waveshare-hub/scripts/build_plants_ota.py")
    assert '"schema": 1' in s
    assert '"h2": h2' in s
    assert "read_h2_build_identity" in s
