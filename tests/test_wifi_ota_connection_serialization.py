from pathlib import Path

SRC = (Path(__file__).resolve().parents[1] / "firmware/waveshare-hub/src/update_service.cpp").read_text()


def section(start, end):
    return SRC[SRC.index(start): SRC.index(end, SRC.index(start))]


def test_all_station_begins_are_claimed_and_arduino_auto_retry_disabled():
    s = section("void beginStationConnection(", "struct OtaMemorySnapshot")
    assert "stationAttemptPending()" in s
    assert "claimStationAttempt()" in s
    assert "WiFi.setAutoReconnect(false)" in s
    assert "WiFi.setAutoReconnect(true)" not in SRC
    assert s.count("WiFi.begin(") == 1


def test_setup_begin_is_claimed_and_saves_only_after_success():
    s = section('portalServer.on("/save"', 'portalServer.on("/generate_204"')
    assert "stationAttemptPending()" in s
    assert "claimStationAttempt()" in s
    assert 'WiFi.begin(pendingSsid.c_str(), pendingPassword.c_str())' in s
    assert 'networkPreferences.putString("ssid"' not in s
    assert 'WiFi.SSID() == pendingSsid' in SRC


def test_manual_offline_intent_disables_retry_and_retains_credentials():
    s = section("void disconnectWifi()", "void reconnectWifi()")
    assert "reconnectSuppressed = true" in s
    assert "WiFi.setAutoReconnect(false)" in s
    assert 'networkPreferences.remove("ssid")' not in s
    f = section("void forgetWifi()", "void requestCheck()")
    assert "reconnectSuppressed = true" in f
    assert 'networkPreferences.remove("ssid")' in f
    assert 'networkPreferences.remove("pass")' in f


def test_ota_metadata_retry_keeps_working_dhcp_connection():
    s = section("void runUpdateCheckOnNetworkWorker()", "void runInstallOnNetworkWorker()")
    assert 'if (!validStationAddress() && !recoverOtaNetwork(' in s
    assert "for (uint8_t attempt = 0; attempt < 2; ++attempt)" in s
    assert 'before HTTPS retry' in s
    assert 'beginStationConnection(true)' not in section('bool recoverOtaNetwork(', 'bool otaMemoryPreflight(')


def test_http_200_partial_response_fails_closed_and_logs_diagnostics():
    s = section("bool httpsGetText(", "String normalizedVersion(")
    assert 'return ok && complete && lengthMatches;' in s
    assert 'HTTPS response incomplete' in s
    assert 'esp_http_client_is_chunked_response' in s
    assert 'lastHttpsTransportFailure = true' in s
    assert 'config.crt_bundle_attach = esp_crt_bundle_attach;' in s
    assert 'config.skip_cert_common_name_check = false;' in s


def test_failed_candidate_restores_previous_wifi_without_overwriting_it():
    failure = section("void failPendingWifi(", "void commitPendingWifi(")
    service = section("void service()", "void startWifiSetup()")
    assert "WiFi.disconnect(false, false);" in failure
    assert "releaseStationAttempt();" in failure
    assert "PortalAttemptState::Failed" in service
    assert "beginStationConnection();  // WIFI_AP_STA" in service
    assert 'networkPreferences.putString("ssid"' not in failure
