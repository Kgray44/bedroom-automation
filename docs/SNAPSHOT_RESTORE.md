# Snapshot & Restore Engine: Endpoint and Recovery Guide

## Existing bedroom-light firmware API

The unchanged `Kgray44/Bedroom_Lights` ESP32-S3 development firmware already exposes these HTTP GET routes:

- `/api/state` reports mode, hex color, Kelvin mode, master brightness, animation strength, animation speed parameters, palette state, transitions, timer/test status.
- `/set?mode=rainGlass&color=7E7BFE&masterBrightness=50` changes the displayed mode/settings. It may schedule a persistent flash write in that original firmware.
- `/api/palettes/select?id=warm-linen&enabled=1` restores an active palette.
- `/api/transitions/set?enabled=1&durationMs=1500&style=fade` restores transition configuration.

Only the **new ESP32** receives the firmware changes in Doorlight 05.

## External control (authenticated Doorlight API)

```bash
# Check last effect, recovery and verification status
curl -H 'X-API-Key: YOUR_KEY' http://doorlight.local/api/bedroom/status

# Fetch actual current strip state
curl -H 'X-API-Key: YOUR_KEY' http://doorlight.local/api/bedroom/live

# Pulse purple for 6 seconds, then automatically restore + read back
curl -X POST http://doorlight.local/api/bedroom/test \
  -H 'X-API-Key: YOUR_KEY' -H 'Content-Type: application/json' \
  -d '{"mode":"strobe","color":"B78FFF","brightnessPct":25,"durationSec":6,"halfMs":600}'

# Or preview a built-in animation, no custom per-pixel driving needed
curl -X POST http://doorlight.local/api/bedroom/test \
  -H 'X-API-Key: YOUR_KEY' -H 'Content-Type: application/json' \
  -d '{"mode":"rainGlass","color":"628CCB","brightnessPct":30,"durationSec":10}'

# End the active effect and safely request restoration
curl -X POST http://doorlight.local/api/bedroom/cancel -H 'X-API-Key: YOUR_KEY'

# Recover an unresolved, still-owned effect
curl -X POST http://doorlight.local/api/bedroom/recover \
  -H 'X-API-Key: YOUR_KEY' -H 'Content-Type: application/json' -d '{"force":false}'

# ONLY after inspecting the situation, explicitly override manual-edit protection
curl -X POST http://doorlight.local/api/bedroom/recover \
  -H 'X-API-Key: YOUR_KEY' -H 'Content-Type: application/json' -d '{"force":true}'

# Discard a stale snapshot without touching the strip
curl -X POST http://doorlight.local/api/bedroom/forget \
  -H 'X-API-Key: YOUR_KEY' -H 'Content-Type: application/json' -d '{"confirm":"FORGET"}'
```

`POST /api/bedroom/test` is **asynchronous**: HTTP `202` means queued, not that restoration succeeded. Poll `/api/bedroom/status` until `stage=="idle"` and `result` explicitly says `Restored and verified` or until `stage=="failed"`. In failure mode the persisted snapshot remains on Doorlight for user review.

## Shortcuts output

Set `target="bedroom"`, `restore=true`, `durationSec` within `1..600` and optional `stripMode` such as `strobe`, `rainGlass`, `rainbow`, `candleHall`, `slowPulse` or `quietNorthernLights`. The `stripHalfMs` field (100–5000 ms) sets strobe half-period; color, Kelvin and brightness follow the original strip API. Other rules keep existing behavior. The new controller refuses permanent bedroom-strip output actions to avoid unintentionally stealing control of ambient lighting.

## Recovery state machine

`idle → capturing → applying → confirming-effect → active → checking-ownership → restoring-state → restoring-palette → restoring-transitions → verifying → idle`

- A second alert joins the bounded priority queue and does **not** replace the original snapshot.
- A manual change during a displayed alert blocks automatic restoration. Status becomes `failed`, with the original still saved.
- After reboot during an alert, Doorlight reads its NVS snapshot and checks whether the strip still matches the planned or last verified effect.
- Up to **3** restore cycles are performed after configuration mismatches. Network outages defer the attempt; they do not silently discard the snapshot.
- `force:true` is only for a conscious operator choice after inspecting `/api/bedroom/live`. It can overwrite later changes, so it is never automatic.
- Settings are compared using Kelvin for white-temperature mode; RGB hex is a separate source only when RGB is selected.

## Unavoidable no-firmware-change limitations

You cannot restore the *exact* animation phase/frame, active timer elapsed time, or scene-tracking metadata over these routes. Opening an animation can reset its phase. Shortcuts with an active original timer/test/transition are intentionally skipped. Frequent remote `/set` calls can trigger original controller flash saves. Use local animations and conservative event cooldowns instead of sending a new LED frame over HTTP 30 times per second.
