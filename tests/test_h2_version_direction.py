from pathlib import Path
import shutil
import subprocess
import textwrap

import pytest

ROOT = Path(__file__).resolve().parents[1]
H2_HEADER = ROOT / "firmware/waveshare-hub/include/h2_ota_client.h"
H2_CLIENT = ROOT / "firmware/waveshare-hub/src/h2_ota_client.cpp"
UPDATE = ROOT / "firmware/waveshare-hub/src/update_service.cpp"


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    brace = source.index("{", start)
    depth = 0
    for pos in range(brace, len(source)):
        if source[pos] == "{":
            depth += 1
        elif source[pos] == "}":
            depth -= 1
            if depth == 0:
                return source[start : pos + 1]
    raise AssertionError(f"unterminated function: {signature}")


def test_h2_target_states_are_directional_not_match_different():
    source = H2_HEADER.read_text(encoding="utf-8")
    enum = source[source.index("enum class TargetState") : source.index("};", source.index("enum class TargetState"))]
    for state in ("UNKNOWN", "MATCH", "OLDER_THAN_RELEASE", "NEWER_THAN_RELEASE"):
        assert state in enum
    assert "DIFFERENT" not in enum


def test_production_identity_classifier_handles_older_equal_newer_and_invalid(tmp_path: Path):
    compiler = shutil.which("g++")
    if not compiler:
        pytest.skip("g++ is not available for the production header host test")

    source = tmp_path / "h2_direction.cpp"
    executable = tmp_path / "h2_direction"
    source.write_text(
        textwrap.dedent(
            r'''
            #include <cassert>
            #include "h2_ota_client.h"

            int main() {
              using espplants_h2_ota::TargetState;
              using espplants_h2_ota::targetStateForIdentity;
              const char *releaseVersion = "1.4.2-alpha.11";
              const char *releaseBuild = "ESPPLANTS-H2-1.4.2-alpha.11";

              assert(targetStateForIdentity("ESPPLANTS-H2-1.4.2-alpha.10",
                                            releaseVersion, releaseBuild) ==
                     TargetState::OLDER_THAN_RELEASE);
              assert(targetStateForIdentity("ESPPLANTS-H2-1.4.2-alpha.11",
                                            releaseVersion, releaseBuild) ==
                     TargetState::MATCH);
              assert(targetStateForIdentity("ESPPLANTS-H2-1.4.2-alpha.12",
                                            releaseVersion, releaseBuild) ==
                     TargetState::NEWER_THAN_RELEASE);
              assert(targetStateForIdentity("ESPPLANTS-H2-not-a-version",
                                            releaseVersion, releaseBuild) ==
                     TargetState::UNKNOWN);
              assert(targetStateForIdentity("not-an-h2-build",
                                            releaseVersion, releaseBuild) ==
                     TargetState::UNKNOWN);
              assert(targetStateForIdentity("ESPPLANTS-H2-v1.4.2-alpha.11",
                                            releaseVersion, releaseBuild) ==
                     TargetState::UNKNOWN);
              return 0;
            }
            '''
        ),
        encoding="utf-8",
    )
    include = ROOT / "firmware/waveshare-hub/include"
    subprocess.run(
        [compiler, "-std=c++17", f"-I{include}", str(source), "-o", str(executable)],
        check=True,
    )
    subprocess.run([str(executable)], check=True)


def test_h2_update_never_downloads_for_newer_or_unknown_identity():
    source = H2_CLIENT.read_text(encoding="utf-8")
    body = function_body(source, "Result updateForRelease(")
    newer = body.index("TargetState::NEWER_THAN_RELEASE")
    unknown = body.index("TargetState::UNKNOWN")
    download = body.index("getAsset(")
    assert newer < download
    assert unknown < download
    assert "downgrade skipped" in body[newer:download]
    assert "automatic H2 update blocked" in body[unknown:download]


def test_same_waveshare_h2_older_is_h2_only_and_newer_is_not_downgraded():
    source = UPDATE.read_text(encoding="utf-8")
    check = function_body(source, "bool checkGithubRelease()")
    assert "TargetState::OLDER_THAN_RELEASE" in check
    assert "h2OnlyUpdate = true;" in check
    assert 'setStatus("H2 update available: v%s", bestRelease.h2Version);' in check
    assert "TargetState::NEWER_THAN_RELEASE" in check
    assert "downgrade skipped" in check
    assert "TargetState::UNKNOWN" in check
    assert "automatic H2 update is blocked" in check


def test_waveshare_older_h2_newer_can_continue_without_h2_downgrade():
    update = UPDATE.read_text(encoding="utf-8")
    check = function_body(update, "bool checkGithubRelease()")
    install = function_body(update, "void runInstallOnNetworkWorker()")
    client = function_body(H2_CLIENT.read_text(encoding="utf-8"), "Result updateForRelease(")

    # A newer Waveshare release is offered without requiring an H2 downgrade decision
    # during the check. Install then asks H2 first; NEWER returns OK before any H2 asset
    # download, allowing the existing Waveshare installer stage to continue.
    assert "if (comparison < 0)" in check
    assert "hasUpdate = true;" in check[check.index("if (comparison < 0)") :]
    assert client.index("TargetState::NEWER_THAN_RELEASE") < client.index("getAsset(")
    assert "return Result::OK;" in client[
        client.index("TargetState::NEWER_THAN_RELEASE") : client.index("getAsset(")
    ]
    h2 = install.index("espplants_h2_ota::updateForRelease")
    waveshare = install.index("espplants_ota_installer::install", h2)
    assert h2 < waveshare


def test_unknown_h2_identity_fails_safe_before_waveshare_install():
    client = function_body(H2_CLIENT.read_text(encoding="utf-8"), "Result updateForRelease(")
    unknown = client.index("TargetState::UNKNOWN")
    assert "return Result::FAILED;" in client[unknown : client.index("getAsset(")]


def test_same_waveshare_and_matching_h2_reports_up_to_date():
    source = UPDATE.read_text(encoding="utf-8")
    check = function_body(source, "bool checkGithubRelease()")
    same = check[check.index("} else if (comparison == 0) {") : check.index("} else {", check.index("} else if (comparison == 0) {") + 1)]
    assert "TargetState::OLDER_THAN_RELEASE" in same
    assert "TargetState::NEWER_THAN_RELEASE" in same
    assert "TargetState::UNKNOWN" in same
    assert 'setStatus("Up to date: v%s", ESP_PLANTS_WAVESHARE_VERSION);' in check


def test_waveshare_older_with_h2_match_or_older_keeps_h2_first_order():
    update = UPDATE.read_text(encoding="utf-8")
    check = function_body(update, "bool checkGithubRelease()")
    install = function_body(update, "void runInstallOnNetworkWorker()")
    client = function_body(H2_CLIENT.read_text(encoding="utf-8"), "Result updateForRelease(")

    older_ws = check[check.index("if (comparison < 0)") : check.index("} else if (comparison == 0)")]
    assert "hasUpdate = true;" in older_ws
    assert "h2OnlyUpdate = false;" in older_ws

    match = client.index("TargetState::MATCH")
    download = client.index("getAsset(")
    assert "return Result::OK;" in client[match:download]
    # OLDER_THAN_RELEASE intentionally has no skip branch in updateForRelease;
    # it falls through to the existing verified H2 download/transfer path.
    assert "TargetState::OLDER_THAN_RELEASE" not in client[:download]

    h2 = install.index("espplants_h2_ota::updateForRelease")
    waveshare = install.index("espplants_ota_installer::install", h2)
    assert h2 < waveshare
