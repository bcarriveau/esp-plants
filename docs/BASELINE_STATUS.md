# Baseline status

Repository version: `0.1.0-alpha.1`  
Established: `2026-08-09`

This document separates what is present in source from what has been physically
verified.

## Imported firmware baselines

### T5 hub

Imported from the Phase 3B home-Wi-Fi / UDP setup/receiver project.

Included source contains:

- compile fixes for the duplicate constant problem,
- live-plant IP field fix,
- setup portal,
- central plant naming,
- ESP-NOW provisioning,
- home-Wi-Fi normal mode,
- UDP receiver/ACK,
- e-paper/power handling.

Previous development testing established that the T5 could join the configured
home network. This repository packaging step did not recompile the T5 in the
current runtime.

### XIAO sensor

Imported from the Phase 3E factory-style calibration project.

Its transport foundation was developed from the already working home-Wi-Fi/UDP
sender path.

Phase 3E additionally contains:

- adaptive scheduling,
- watering follow-up,
- service-mode automatic sampling,
- factory-style calibration storage/workflow.

The Phase 3E project was structurally sanity-checked when generated, but the
current environment does not contain the user's pioarduino toolchain and this
repository packaging step does not claim a fresh compile or final physical
verification.

## Protocol sync

At repository creation, both `plant_protocol.h` copies were byte-identical and
used protocol v3.

Run the sync tool after any protocol edit.

## Source preservation

The core imported files are copied without intentional behavioral edits during
GitHub packaging.

See `BASELINE_MANIFEST.md` for hashes.

## Stable-release gate

Do not relabel this alpha baseline as stable until:

- T5 builds,
- XIAO builds,
- provisioning passes,
- normal UDP + ACK passes,
- service mode passes,
- calibration passes,
- watering follow-up passes,
- deep-sleep timer/button wake passes,
- T5 naming persistence passes,
- power/shutdown behavior passes.
