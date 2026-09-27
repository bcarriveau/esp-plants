from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_alpha30_keeps_last_hop_resolution_and_debounces_repeater_offline_state():
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



def test_repeater_offline_requires_a_sustained_neighbor_table_absence():
    main = read("firmware/m5-h2-zigbee/src/main.cpp")
    assert "constexpr uint32_t kInfrastructureOfflineTimeoutMs = 45000;" in main

    service = main.split("void serviceInfrastructureRegistry()", 1)[1].split(
        "void handlePlantFrame", 1
    )[0]
    pre_scan = service.split("esp_zb_nwk_info_iterator_t iterator", 1)[0]
    assert "seenThisScan" in service
    assert "infrastructure[i].online = false" not in pre_scan
    assert "now - node.lastSeenMs >= kInfrastructureOfflineTimeoutMs" in service
    assert "node.online = false;" in service
    assert "sendInfrastructureReport(node);" in service


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
    assert '#define ESP_PLANTS_H2_VERSION "0.2.0-alpha.30"' in h2
    assert '#define ESP_PLANTS_WAVESHARE_VERSION "0.2.0-alpha.44"' in ws
    assert root_version == "0.2.0-alpha.44"
