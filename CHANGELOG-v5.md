# Doorlight 05 | Changes

- Added `BedroomSnapshotEngine.h` and native `BedroomRestorePolicy.h` on the **new** ESP32 only.
- Use the original bedroom controller's existing `GET /api/state`, `/set`, `/api/palettes/select`, `/api/transitions/set` routes.
- Persist original state and last expected temporary effect in new ESP32 NVS before modifying the strip.
- Serialize overlapping events/shortcuts through a bounded, priority-ordered queue with **one** original snapshot.
- Add readback of the effect, ownership check before restore, full-configuration verification afterward, and up to three restore cycles on mismatches.
- Automatic reboot recovery when the strip still matches Doorlight's expected effect; manual changes block automatic overwrite.
- Add `GET /api/bedroom/status`, `GET /api/bedroom/live`, `POST /api/bedroom/test`, `/cancel`, `/recover`, `/forget`, authenticated like the rest of Doorlight.
- Integrate temporary strip actions into Shortcuts and legacy motion / walk-by / loitering signatures.
- New Smart Bulbs Snapshot & Restore Studio: real state preview, animation/color/brightness/duration controls, restore and readback.
- Bedroom effect actions require bounded duration + restore to prevent unintended permanent changes.
- Respect the existing strip timer, diagnostic test and in-flight transitions by refusing interruption.
- No patches or updates to the existing fingerprint lock or bedroom controller.
- Source-level tests + portable C++ restore safety tests passed; full ESP32 toolchain compilation and physical testing remain outstanding.

# Doorlight 06 changes
- Smooth, configurable native strip fade-in and fade-out with saved transition preference restoration.
- Windows DPAPI-protected Gaming Mode watcher with a source-owned lease watchdog.
- Updated dashboard, API and Codex instructions.
- Originals unchanged; hardware compilation unverified in sandbox.
