from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
def read(p): return (ROOT/p).read_text(encoding='utf-8')
def test_protocol_chunk_fits():
    s=read('shared/plantlink/plantlink_ota.h'); assert 'kChunkDataBytes=248' in s; assert 'kBeginBytes=133' in s
def test_h2_inactive_slot_and_validation_guards():
    s=read('firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp')
    for token in ['esp_ota_get_next_update_partition','esp_ota_get_running_partition','esp_ota_end','esp_ota_set_boot_partition','incoming.chip_id!=running.chip_id','ESP_PLANTS_H2_DISTRIBUTION_MARKER']:
        assert token in s
def test_h2_first_then_display_injection():
    s=read('firmware/waveshare-hub/scripts/phase2_inject.py')
    assert 'updateForRelease' in s and 'return Result::FAILED' in s
def test_release_builds_h2_first():
    s=read('tools/make-waveshare-release.cmd')
    assert s.index('m5_gateway_h2_release') < s.index('waveshare_s3_touch_lcd_7_release')
def test_manifest_keeps_schema_one_and_h2_metadata():
    s=read('firmware/waveshare-hub/scripts/build_plants_ota.py')
    assert '"schema":1' in s and '"h2":h2' in s and 'm5stack-unit-gateway-h2' in s
