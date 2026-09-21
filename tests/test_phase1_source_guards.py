import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class Phase1SourceGuardTests(unittest.TestCase):
    def text(self, relative):
        return (ROOT / relative).read_text(encoding="utf-8")

    def test_no_insecure_tls(self):
        combined = self.text("firmware/waveshare-hub/src/update_service.cpp") + self.text(
            "firmware/waveshare-hub/src/plants_ota_installer.cpp"
        )
        self.assertNotIn("setInsecure", combined)
        self.assertIn("esp_crt_bundle_attach", combined)
        self.assertIn("skip_cert_common_name_check = false", combined)

    def test_inactive_ota_partition_and_final_activation(self):
        source = self.text("firmware/waveshare-hub/src/plants_ota_installer.cpp")
        self.assertIn("esp_ota_get_next_update_partition", source)
        self.assertIn("esp_ota_end", source)
        self.assertIn("esp_ota_set_boot_partition", source)
        self.assertLess(source.index("esp_ota_end"), source.index("esp_ota_set_boot_partition"))

    def test_package_identity_guards_present(self):
        source = self.text("firmware/waveshare-hub/src/plants_ota_installer.cpp")
        for required in (
            "ESP-PLANTS-OTA",
            "WAVESHARE-ESP32-S3-LCD-7",
            "ESP-PLANTS-WAVESHARE",
            "ESP-PLANTS-DISTRIBUTION-BUILD",
            "packageSha256",
            "firmwareSha256",
        ):
            self.assertIn(required, source)

    def test_user_data_uses_plantdata(self):
        main = self.text("firmware/waveshare-hub/src/main.cpp")
        self.assertIn('preferences.begin("espplants", false, "plantdata")', main)
        self.assertIn("migrateUserPreferences", main)

    def test_release_environment_is_separate(self):
        pio = self.text("firmware/waveshare-hub/platformio.ini")
        self.assertIn("[env:waveshare_s3_touch_lcd_7_release]", pio)
        self.assertIn("-DESP_PLANTS_DISTRIBUTION_BUILD=1", pio)
        self.assertIn("post:scripts/build_plants_ota.py", pio)

    def test_wifi_portal_reports_failed_connection(self):
        source = self.text("firmware/waveshare-hub/src/update_service.cpp")
        self.assertIn("kPortalConnectTimeoutMs", source)
        self.assertIn("PortalAttemptState::Failed", source)
        self.assertIn('portalServer.on("/status"', source)
        self.assertIn("failPendingWifi", source)
        self.assertIn("Check the Wi-Fi password and try again", source)
        self.assertIn("meta http-equiv='refresh'", source)

    def test_setup_qr_matches_radar_style_encoder(self):
        source = self.text("firmware/waveshare-hub/src/main.cpp")
        self.assertIn("<src/extra/libs/qrcode/qrcodegen.h>", source)
        self.assertIn("qrcodegen_encodeText", source)
        self.assertIn("lv_canvas_set_buffer", source)
        self.assertIn("WIFI:T:WPA;S:%s;P:%s;;", source)
        self.assertIn("SCAN TO CONNECT", source)

    def test_release_cmd_is_double_click_safe(self):
        source = self.text("tools/make-waveshare-release.cmd")
        self.assertIn(r"%USERPROFILE%\.platformio\penv\Scripts\platformio.exe", source)
        self.assertIn("where pio", source)
        self.assertIn("RELEASE BUILD FAILED", source)
        self.assertGreaterEqual(source.lower().count("pause"), 3)


if __name__ == "__main__":
    unittest.main()
