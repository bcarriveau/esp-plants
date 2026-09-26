from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_alpha29_replaces_temporary_topology_dump_with_last_hop_resolution():
    main = read("firmware/m5-h2-zigbee/src/main.cpp")
    route = read("firmware/m5-h2-zigbee/include/sensor_route.h")

    # Alpha.28's temporary table dumper is gone from the active source path.
    for old in (
        "dumpTopologyTables",
        "topologyDiagDue",
        "kTopologyDiagSensorIntervalMs",
        "[topology][neighbor]",
        "[topology][route]",
        "[topology][source-route]",
        "t = dump Zigbee topology tables",
        "esp_zb_nwk_get_next_route",
        "esp_zb_nwk_get_next_route_record",
    ):
        assert old not in main

    # Production logic now asks one question only: did this packet arrive through
    # one unique live router neighbor? Direct children intentionally produce none.
    assert "sensor_route::resolveRepeater(" in main
    assert "neighbor.lqi != packetLqi" in main
    assert "directSensor" in main
    assert "routerForPacket" in main
    assert "RouteState::ROUTED" in main
    assert "RouteState::DIRECT" not in main
    assert "Two routers with the same current LQI are ambiguous. Never guess." in route


def test_route_state_is_transient_and_direct_is_silent():
    main = read("firmware/m5-h2-zigbee/src/main.cpp")
    view = read("firmware/waveshare-hub/include/sensor_route_view.h")

    assert "uint8_t repeaterIeee[8]{};" in main
    assert "DIRECT TO HUB" not in view
    assert "route.state != plantlink::RouteState::ROUTED" in view
    assert "learnedMs" not in view
    assert "FlagRouteOnly" not in main
    assert "sendSensorReport(sensor, true)" not in main


def test_firmware_versions_advance_independently():
    h2 = read("firmware/m5-h2-zigbee/include/build_version.h")
    ws = read("firmware/waveshare-hub/include/build_version.h")
    root_version = read("VERSION").strip()
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.29"' in h2
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.42"' in ws
    assert root_version == "0.2.0-alpha.42"
