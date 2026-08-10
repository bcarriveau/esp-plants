# Security

## Current security model

The project is local-first and does not require a cloud service, Home Assistant,
or MQTT broker.

Home-Wi-Fi credentials are stored in device NVS.

The T5 setup hotspot uses a generated password that is stored on the T5.

## Known limitation: provisioning

The current ESP-NOW provisioning exchange sends the home-Wi-Fi provisioning
payload without application-level authenticated encryption.

Provisioning is intended to be:

- local,
- short-lived,
- user-initiated,
- performed with the sensor physically nearby.

That is not the same as a production-secure pairing design.

Before describing the project as production/family release quality, add an
authenticated provisioning design and threat-model it.

## Logs and public issues

Before posting logs publicly, remove:

- home-Wi-Fi SSIDs if desired,
- passwords,
- private network details that you do not want public,
- device MAC addresses if you do not want them published.

No password should ever be intentionally printed by firmware diagnostics.

## Reporting security problems

Until a formal contact method is added, do not publish an exploitable security
issue containing real credentials. Open a minimal issue that a security contact
path is needed instead.
