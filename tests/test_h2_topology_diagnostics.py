from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_h2_topology_diagnostics_are_dev_only_and_non_authoritative():
    main = read("firmware/m5-h2-zigbee/src/main.cpp")

    assert "kTopologyDiagSensorIntervalMs = 30000" in main
    assert "[topology][neighbor]" in main
    assert "[topology][route]" in main
    assert "[topology][source-route]" in main
    assert "esp_zb_nwk_get_next_neighbor" in main
    assert "esp_zb_nwk_get_next_route" in main
    assert "esp_zb_nwk_get_next_route_record" in main
    assert "if (topologyDiagDue(*sensor)) dumpTopologyTables(sensor, &event);" in main

    # Diagnostics observe the same tables but do not replace the production
    # route resolver or infer a route from LQI.
    assert "sensor_route::resolve(tables, sensor.ieee, sensor.shortAddress, report);" in main
    assert "packet_lqi" in main
    assert 'Serial.printf("[topology] channel=%u link_status_period=%us\\n",' in main
    assert "same lqi" not in main.lower()

    # Both the automatic dumps and manual console command are development-only.
    assert "#if defined(ESP_PLANTS_H2_DEV_DIAGNOSTICS)\n    if (topologyDiagDue(*sensor))" in main
    assert "#if defined(ESP_PLANTS_H2_DEV_DIAGNOSTICS)\n    case 't':" in main


def test_h2_diagnostic_build_identity_is_alpha28():
    version = read("firmware/m5-h2-zigbee/include/build_version.h")
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.28"' in version
