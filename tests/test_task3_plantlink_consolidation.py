from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_authoritative_protocol_definition_is_single_source():
    authoritative = read("shared/plantlink/plantlink.h")
    legacy = read("shared/plantlink_protocol.h")

    assert "constexpr size_t kMaxPayloadBytes=256;" in authoritative
    assert "InfrastructureReport=0x15" in authoritative
    assert "H2OtaBegin=0x50" in authoritative
    assert "H2OtaChunk=0x51" in authoritative
    assert "H2OtaEnd=0x52" in authoritative
    assert "H2OtaStatus=0x53" in authoritative
    assert "H2OtaAbort=0x54" in authoritative
    assert "CapabilityH2Ota=1u<<4" in authoritative

    assert '#include "plantlink/plantlink.h"' in legacy
    assert "enum class MessageType" not in legacy
    assert "MAX_PAYLOAD_BYTES" not in legacy
    assert "PROTOCOL_VERSION" not in legacy


def test_ota_extension_does_not_redeclare_core_wire_ids():
    ota = read("shared/plantlink/plantlink_ota.h")
    assert "kChunkDataBytes=248" in ota
    assert "kBeginBytes=133" in ota
    assert "kStatusBytes=6" in ota
    assert "kBuildIdBytes=96" in ota
    assert "kCapabilityH2Ota" not in ota
    assert "kBegin=0x50" not in ota
    assert "kChunk=0x51" not in ota
    assert "kEnd=0x52" not in ota
    assert "kStatus=0x53" not in ota
    assert "kAbort=0x54" not in ota


def test_h2_has_one_capability_source_for_hello_and_periodic_heartbeat():
    main = read("firmware/m5-h2-zigbee/src/main.cpp")
    assert "constexpr uint32_t kH2Capabilities" in main
    assert "plantlink::CapabilityH2Ota" in main
    assert "plantlink::putU32LE(payload + 4, kH2Capabilities);" in main

    hello_case = main.split("case plantlink::MessageType::Hello:", 1)[1].split("break;", 1)[0]
    assert "sendHelloAck();" in hello_case
    assert "sendHeartbeat();" in hello_case


def test_h2_uses_single_explicit_frame_dispatch_path():
    main = read("firmware/m5-h2-zigbee/src/main.cpp")
    receiver = read("firmware/m5-h2-zigbee/src/h2_ota_receiver.cpp")

    assert "espplants_h2_ota::handlePlantLinkFrame(frame)" in main
    assert "bool handlePlantLinkFrame(const plantlink::Frame&f)" in receiver
    for msg in ("H2OtaBegin", "H2OtaChunk", "H2OtaEnd", "H2OtaAbort"):
        assert f"plantlink::MessageType::{msg}" in receiver

    assert "setFrameObserver" not in receiver
    assert "frameObserver" not in receiver
    assert "sendPhase2Hello" not in receiver
    assert "case plantlink::MessageType::Hello:" not in receiver


def test_waveshare_upload_port_stays_com11():
    platformio = read("firmware/waveshare-hub/platformio.ini")
    assert "upload_port = COM11" in platformio
