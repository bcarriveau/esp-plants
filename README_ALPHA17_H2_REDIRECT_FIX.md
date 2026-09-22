ESP PLANTS alpha.17 - H2 GitHub redirect fix
=============================================

Base branch: waveshare-zigbee
Base commit: ba9efab5c24c2c076a652883db3151c9de52357d

Why alpha.16 existed
--------------------
The Waveshare on alpha.15 needed a newer published version in order to enter the
OTA install path. Publishing alpha.16 was therefore the correct test target.

That test proved:
- release discovery now selects the newest version correctly;
- Update All reaches the H2-first installer;
- the H2 release download fails at GitHub redirect handling.

Fix
---
The H2 asset downloader previously tried to read the Location header with
esp_http_client_get_header() after fetch_headers(). The existing, proven metadata
downloader captures Location through HTTP_EVENT_ON_HEADER instead.

alpha.17 changes the H2 downloader to the same bounded header-event method:
- Location captured during HTTP_EVENT_ON_HEADER;
- total response header bytes remain bounded;
- redirect URL length remains bounded;
- HTTPS certificate validation remains enabled;
- only the existing approved GitHub/release-assets hosts are accepted;
- redirect count remains limited.

No broad redirect bypass or host wildcard was added.

Test sequence
-------------
1. Overlay this ZIP on the current waveshare-zigbee checkout.
2. Build/flash the Waveshare alpha.17 locally once. The redirect fix must be
   running on the Waveshare before it can fix H2 asset downloads.
3. Your log currently reports:
       [plantlink] H2 hello: m5-h2-zigbee/0.1-dev
   That is pre-Phase-2 H2 firmware. Flash the H2 release firmware over USB once
   so it has the PlantLink OTA receiver.
4. Build/publish the next release (alpha.18) with:
       tools\make-waveshare-release.cmd
5. On the alpha.17 Waveshare, Check Now -> Update All.
   alpha.18 is the first clean end-to-end test of Waveshare downloading the H2
   asset, transferring it over PlantLink, confirming the H2 reboot/build, and
   then updating its own inactive S3 slot.

No UI geometry, Zigbee behavior, sensor logic, persistence schema, or Wi-Fi
behavior changed.

Static source validation was performed here. PlatformIO compilation and physical
hardware testing were not available in this environment.
