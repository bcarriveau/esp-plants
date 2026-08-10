# Roadmap

This is a direction document, not a promise that every item belongs in the next
release.

## Near term

### Verify Phase 3E baseline

- compile current XIAO project locally,
- physically exercise calibration,
- confirm saved endpoints across restart,
- verify watering-watch behavior after calibration,
- confirm adaptive sleep timing in serial logs.

### Strengthen T5 display product behavior

- build the multi-plant e-paper dashboard around the proven receiver/setup
  foundation,
- keep refresh frequency appropriate for e-paper,
- make last-seen/battery/state clearly readable,
- avoid animations and unnecessary redraws.

### Improve setup diagnostics

Potential setup-page additions:

- last sensor IP,
- last seen,
- last moisture,
- battery,
- provisioning status,
- calibration status/version if exposed later.

## Configuration evolution

Potential centrally managed settings:

- dry/almost-dry/normal/wet thresholds,
- meaningful-change percent,
- local check intervals,
- heartbeat interval,
- watering rise threshold.

Preferred architecture:

- edit from T5,
- store the user-facing configuration centrally,
- send machine configuration explicitly to each XIAO,
- keep plant name T5-only.

If wire configuration is added, design an explicit config packet rather than
overloading the current ACK field.

## Security

Before production/family release:

- authenticate provisioning,
- encrypt credential transfer,
- define re-pair/reset behavior,
- document trust model.

## Reliability

- long-duration battery test,
- router reboot recovery,
- T5 reboot recovery,
- sensor reboot recovery,
- network DHCP/subnet change behavior,
- multiple AP/mesh-node behavior,
- packet-loss statistics,
- stale-sensor indication on T5.

## Calibration / sensing

- gather several real watering/drying cycles,
- compare raw millivolts and percentages,
- decide whether linear calibration is sufficient,
- consider optional per-sensor state thresholds,
- avoid trying to force readings to match another manufacturer's arbitrary
  percentage scale.

## Future convenience

Possible additions after the local core is solid:

- T5-hosted firmware upload/OTA workflow,
- export/import T5 plant configuration,
- per-sensor firmware/version display,
- optional local API.

No cloud dependency should become mandatory.
