from pathlib import Path
import json

ROOT = Path(__file__).resolve().parents[1]
UPDATE = ROOT / "firmware" / "waveshare-hub" / "src" / "update_service.cpp"
SOURCE = UPDATE.read_text(encoding="utf-8")


def _https_reader_body():
    start = SOURCE.index("bool httpsGetText(")
    end = SOURCE.index("String normalizedVersion", start)
    return SOURCE[start:end]


def test_empty_release_array_is_valid_json_and_success_path_is_preserved():
    assert json.loads("[]") == []
    assert 'deserializeJson(releases, releasesBody.data(), releasesBody.length())' in SOURCE
    assert 'if (!releases.is<JsonArray>())' in SOURCE
    assert '"No Waveshare update release is published yet"' in SOURCE
    no_release = SOURCE.index('"No Waveshare update release is published yet"')
    remember = SOURCE.index("rememberCheckTime();", no_release)
    assert remember > no_release


def test_reader_attempts_body_read_before_complete_data_can_terminate_it():
    body = _https_reader_body()
    loop = body.index("for (;;) {")
    first_read = body.index("esp_http_client_read(", loop)
    first_complete = body.index("esp_http_client_is_complete_data_received(client)", loop)
    assert first_read < first_complete
    assert "while (!esp_http_client_is_complete_data_received(client))" not in body


def test_malformed_or_empty_json_logs_bounded_diagnostics_and_fails_safely():
    assert "if (releasesError)" in SOURCE
    assert "releasesError.c_str()" in SOURCE
    assert "http=%d body_bytes=%u" in SOURCE
    assert 'setStatus("GitHub returned an invalid release list")' in SOURCE


def test_oversized_and_incomplete_known_length_metadata_are_rejected():
    body = _https_reader_body()
    assert "static_cast<uint64_t>(length) > maximumBytes" in body
    assert "body.length() > static_cast<size_t>(length)" in body
    assert "length <= 0 || body.length() == static_cast<size_t>(length)" in body
    assert "return ok && complete && lengthMatches;" in body


def test_ota_security_and_worker_invariants_remain_present():
    required = [
        "esp_crt_bundle_attach",
        "skip_cert_common_name_check = false",
        "metadataHostAllowed",
        "kMaxMetadataRedirects",
        '"Accept-Encoding", "identity"',
        "kOtaMinInternalFreeBytes",
        "kOtaMinLargestInternalBlockBytes",
        "kOtaNetworkWorkerStackBytes",
        "MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT",
    ]
    for token in required:
        assert token in SOURCE
