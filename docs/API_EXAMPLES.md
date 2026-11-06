# Doorlight REST API examples

All mutating API requests require `X-API-Key` (or browser HTTP Basic Auth), and a local trusted network. Set `TOKEN` to your configured bridge API key and replace addresses with your actual LAN addresses. These are examples, not commands executed against real hardware.

## Read bulb states and the shortcuts library

```bash
curl -H 'X-API-Key: TOKEN' http://doorlight.local/api/bulbs
curl -H 'X-API-Key: TOKEN' http://doorlight.local/api/shortcuts
```

## Change the existing door mode schedule

```bash
curl -X PUT http://doorlight.local/api/schedule \
  -H 'X-API-Key: TOKEN' -H 'Content-Type: application/json' \
  -d '{"enabled":true,"everyone":"06:15","roommates":"08:00"}'
```

A mode switch may take multiple attempts. `/api/mode` returns *queued*, not verified. Poll `/api/status` until `lastModeResult` contains `CONFIRMED ...` or `FAILED after 3 attempts ...`.

## Create a scheduled shortcut with two independent actions

The days bit mask is `Mon=1, Tue=2, Wed=4, Thu=8, Fri=16, Sat=32, Sun=64` (127 = every day). Each rule has one trigger, one condition, and 1–3 actions.

```bash
curl -X PUT http://doorlight.local/api/shortcuts \
 -H 'X-API-Key: TOKEN' -H 'Content-Type: application/json' \
 -d '{
   "id":"late-night-dim",
   "name":"Late night comfort",
   "enabled":true,
   "trigger":"time",
   "time":"22:00",
   "days":127,
   "everyMinutes":0,
   "cooldownSec":15,
   "condition":"any",
   "actions":[
     {"target":"bulb1","operation":"fade","color":"FFAE6B","brightness":12,"kelvin":2200,"durationSec":0,"transitionSec":8,"restore":false},
     {"target":"bulb2","operation":"off","color":"000000","brightness":0,"kelvin":0,"durationSec":0,"transitionSec":2,"restore":false}
   ]
 }'
```

## Trigger a rule from another device

```bash
curl -X POST http://doorlight.local/api/shortcuts/run \
  -H 'X-API-Key: TOKEN' -H 'Content-Type: application/json' \
  -d '{"id":"movie"}'
```

## Start a five-minute countdown for a saved shortcut

```bash
curl -X POST http://doorlight.local/api/shortcuts/countdown \
  -H 'X-API-Key: TOKEN' -H 'Content-Type: application/json' \
  -d '{"id":"movie","delaySeconds":300}'
```

**Caveat:** Countdown is volatile RAM, reset by reboot. Weekly scheduled events use local NTP time, persistent rule storage and at-most-once per local calendar day tracking.

## Change Night Mode

```bash
curl -X PUT http://doorlight.local/api/night-mode \
  -H 'X-API-Key: TOKEN' -H 'Content-Type: application/json' \
  -d '{"enabled":true,"start":"18:00","end":"07:00","dimPercent":50}'
```

## Control one Matter bulb through Home Assistant

```bash
curl -X POST http://doorlight.local/api/bulbs/control \
  -H 'X-API-Key: TOKEN' -H 'Content-Type: application/json' \
  -d '{"target":"bulb2","operation":"on","color":"96C9FF","brightness":30,"kelvin":0,"transitionSec":2,"durationSec":0,"restore":false}'
```

The bridge requires Home Assistant's Matter integration, two real `light.*` entity IDs and a private long-lived Home Assistant token in `BridgeController/Secrets.h`. It does not commission Matter devices itself.


## Reversible bedroom strip control

See [Snapshot & Restore API and recovery guide](SNAPSHOT_RESTORE.md). No firmware changes are required on the existing strip.

## Doorlight 06 · Native fades and PC gaming lease

These endpoints are on the **new Doorlight controller**, not the existing bedroom strip or lock. Requests require admin Basic auth or `X-API-Key` on trusted LAN.

```bash
curl -H "X-API-Key: $DOORLIGHT_API_KEY" http://doorlight.local/api/bedroom/fades
curl -X PUT -H "X-API-Key: $DOORLIGHT_API_KEY" \
  -H 'Content-Type: application/json' \
  -d '{"enabled":true,"fadeInMs":1500,"fadeOutMs":3000}' \
  http://doorlight.local/api/bedroom/fades

curl -X POST -H "X-API-Key: $DOORLIGHT_API_KEY" \
  -H 'Content-Type: application/json' \
  -d '{"active":true,"source":"pc-watcher","leaseSeconds":90}' \
  http://doorlight.local/api/gaming
# Refresh before 90s expires. Stop with {"active":false,"source":"pc-watcher"}.
curl -H "X-API-Key: $DOORLIGHT_API_KEY" http://doorlight.local/api/routines
```

The built-in Web UI uses `{"active":true}` without `source`, so manual gaming stays under `source=manual`. A PC watcher cannot steal or cancel a manually started session. On watcher crash the 90-second renewable lease expires automatically.
