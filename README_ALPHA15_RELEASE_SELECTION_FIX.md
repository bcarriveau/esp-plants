ESP PLANTS alpha.15 - release ordering fix
==========================================

Base commit:
  0ecfd5965d37620e3e8a6ab6496a3399f5511577

Observed live GitHub release API order at packaging time:
  v0.2.0-alpha.4
  v0.2.0-alpha.2
  v0.2.0-alpha.14

The updater previously returned immediately after the first valid compatible
manifest. That made alpha.4 appear as "LATEST" even though alpha.14 was already
published.

This package changes release discovery to scan the full returned release list and
choose the highest compatible semantic version before deciding whether an update
is available.

It preserves the existing Phase 2 H2-first Update All injection.

Apply:
  Copy this ZIP over the repository root, build normally, then create the combined
  release with tools\make-waveshare-release.cmd.

Expected after publishing alpha.15:
  A device on alpha.13/alpha.14 should report alpha.15 as Latest even if GitHub's
  API returns older releases first.

No UI geometry, Zigbee behavior, persistence schema, or sensor logic changed.
