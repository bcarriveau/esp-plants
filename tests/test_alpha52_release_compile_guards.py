from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
SOURCE = (ROOT / "firmware/waveshare-hub/src/update_service.cpp").read_text(encoding="utf-8")

def test_distribution_marker_uses_waveshare_build_header_name():
    assert "ESP_PLANTS_WAVESHARE_DISTRIBUTION_MARKER" in SOURCE
    assert "ESP_PLANTS_DISTRIBUTION_MARKER" not in SOURCE

def test_wifi_ip_uses_arduino_wifi_api_spelling():
    assert "WiFi.localIP()" in SOURCE
    assert "WiFi.LocalIP()" not in SOURCE
