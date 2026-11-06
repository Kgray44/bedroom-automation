# Doorlight 06 · Smooth transitions, without patches to existing devices

## Desired behavior

- Fade **from the current ambient mode into each alert** and **back into the original mode**.
- Default: **1.5 seconds in, 1.5 seconds out**, editable in the Smart Bulbs > Snapshot & Restore Studio. `GET/PUT /api/bedroom/fades` allows 0, 500, 1500, 3000, or 5000 ms (the exact native durations supported by the existing strip controller), plus an enable switch.
- Separate indicator LED uses PWM with smoothstep attack/release instead of abrupt rectangular digital edges. For native animated strobe/flash modes, the **fade in/out of the alert** is smooth, but the *internal flashing* remains intentionally discontinuous.
- Smart bulbs already use Home Assistant `transition` values. Gaming Mode starts with a 3-second transition, Pomodoro starts with a 4-second transition, and saved Home Assistant scenes are restored by the current integration.

## No changes to the existing strip firmware

The unmodified `Kgray44/Bedroom_Lights` branch `codex/mode-overhaul-esp32s3-target` already implements `/api/transitions/set?enabled=1&durationMs=1500&style=fade` and transitions on `GET /set?mode=...`.

Doorlight 06 captures the original strip state (including existing transition settings) into its own NVS, sets a *temporary* fade on the strip, calls `/set` once to begin the alert, waits, then sets the restore fade, calls `/set` for the original state, restores palette settings, waits for the fade to finish, restores the user's original transition settings and reads back `/api/state` for verification. Retry/failure and manual override safety remain.

**Constraints:** the strip's existing `/set` changes are persisted on the original device after a debounce. Avoid high-volume notifications and keep cooldowns. Original animation frame position and running timers are not saved by the public API, so Doorlight refuses to take over the strip while a timer, test or existing transition is active. Smoothness depends on the native firmware's implementation and the Wi-Fi response time, not on sending 30–60 HTTP requests per second. These are *configuration* read-backs, not optical output verification.

## Control and test

```bash
curl -H "X-API-Key: YOUR_PRIVATE_KEY" http://doorlight.local/api/bedroom/fades

curl -X PUT -H "X-API-Key: YOUR_PRIVATE_KEY" -H "Content-Type: application/json" \
  -d '{"enabled":true,"fadeInMs":1500,"fadeOutMs":3000}' \
  http://doorlight.local/api/bedroom/fades
```

Perform a conservative five-second solid-color test while the strip is idle. Monitor `/api/bedroom/status`, confirm visual fade in/out, inspect original color/animation/brightness/palette and original fade settings before/after. Then simulate Wi-Fi loss after an applied alert, reboot recovery, manual overrides, queued effects and a temporary HTTP error. Do not perform unattended door-access tests on first deployment.
