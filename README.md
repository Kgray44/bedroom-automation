# Doorlight 06 | Bedroom Automation

**A standalone ESP32-S3 orchestrator for the existing fingerprint door lock, bedroom LED strip, and two Matter smart bulbs.** No firmware changes to the original door lock or bedroom lighting controllers. No additional motion sensors.

## Highlights

- Modern responsive dashboard with two live-state smart-bulb previews and a separate editable **Shortcuts** rule library.
- Existing fingerprint lock `/ws` motion, walk-by, loiter, entry/exit event feed; mode scheduling with three-attempt read-back verification and failure reporting.
- Roommates Mode at 08:00 and Everyone Mode at 06:15, with editable local schedules; Night Mode 18:00–07:00 with 50% dimming of temporary alerts.
- Home Assistant local Matter bulb control: independent colors, brightness and transitions, temporary scene snapshots and restoration.
- **Smooth 0.5–5 second strip fades** into alerts and back out, using the existing ESP32-S3 bedroom controller's native transition API. Original fade settings restored and verified; configurable via Smart Bulbs > Snapshot & Restore Studio or `GET/PUT /api/bedroom/fades`.
- **Snapshot & Restore Engine:** saves original animation, brightness, color/Kelvin, palette and transition settings into the new ESP32's NVS; bounds overlapping alert queues, retries safe restore, checks actual state, refuses to override manual changes.
- Windows **Gaming Mode companion**: process-name watcher, encrypted API key (Windows DPAPI), debounced start/stop, lease heartbeats, automatic return to normal bulbs on crash/disconnect.
- Optional fingerprint-assisted correlation from the lock's existing wireless access log (not proof of identity), Pomodoro, weather-aware weekday/weekend alarm routines, OTA browser and Arduino updates, authenticated REST API.

## Installation

1. Install Arduino ESP32 core, `ArduinoJson` and `WebSockets` (Markus Sattler). For ESP32-S3 use FQBN `esp32:esp32:esp32s3:PartitionScheme=min_spiffs` (OTA-capable application slots larger than 1.4 MB). Verify your board’s actual flash size and select the matching flash-size option; do not select `huge_app`, because that removes standard dual-slot OTA. **GPIO18 is the default indicator output for ESP32-S3; GPIO23 for classic ESP32.** For a small LED, use a resistor; use a properly powered MOSFET driver for higher-power lights.
2. Copy `BridgeController/Secrets.example.h` to `BridgeController/Secrets.h`, configure private Wi-Fi/admin/API/OTA secrets and optional Home Assistant long-lived token, then flash the **new** ESP32 only. NEVER commit Secrets.h.
3. Browse `http://doorlight.local/` on your trusted LAN. Configure lock hostname `fingerprintlock.local`, strip hostname `bedroom-leds.local`, Home Assistant endpoint/real light entities, and your editable schedule. Test `/api/status` and `/api/bedroom/live`.
4. Open Smart Bulbs > Snapshot & Restore Studio, enable strip control, save your 1500 ms default fades, test one short effect and watch the restore read-back report. Test disconnected/restarted device recovery before unattended use.
5. Install the optional Windows watcher from [`pc-gaming/README.md`](pc-gaming/README.md). It contacts only the new ESP32's `/api/gaming`; manually started gaming sessions are protected from the watcher.

## Repository map

- [`BridgeController/`](BridgeController): single Arduino sketch plus dashboard, automation and restore headers.
- [`pc-gaming/`](pc-gaming): Windows PowerShell process watcher and safe game-list example.
- [`docs/SMOOTH_TRANSITIONS.md`](docs/SMOOTH_TRANSITIONS.md): fades, restoration and physical limitations.
- [`docs/CODEX_IMPLEMENTATION.md`](docs/CODEX_IMPLEMENTATION.md): precise Codex implementation and test instructions.
- [`docs/API_EXAMPLES.md`](docs/API_EXAMPLES.md): authenticated REST endpoints and request examples.
- [`tests/`](tests): source-contract and C++ policy tests.
- [`preview/`](preview): offline, simulated HTML dashboard preview; **not live hardware data**.

## Security and limitations

**Public GitHub repository:** never commit user-specific Wi-Fi, OTA credentials, API keys or Home Assistant token. Original fingerprint lock WebSocket has no robust authentication; place the original lock/strip and Doorlight on a trusted isolated LAN, never port-forward. The existing strip's HTTP API also lacks strong authentication. The new ESP32 does not change that.

The unmodified strip firmware can fade via its native transition engine, but its internal strobe flashes remain discontinuous. It also persists some `/set` changes to flash; use cooldowns to limit writes. Animation frame position, running timers and active scene links cannot be restored losslessly. Doorlight refuses to interrupt ongoing strip timers/tests/transitions, and read-back verifies reported settings rather than photons.

**Build/testing status:** GitHub Actions runs the actual ESP32-S3 Arduino compile with an OTA-capable larger-app partition. The first toolchain run passed parsing and reached a default flash partition overflow (1.42 MB versus 1.31 MB), prompting the partition correction. Consult the latest workflow result before treating the build as verified. Physical hardware integration tests remain required; see Codex handoff. The Wi-Fi/HA connections have not been verified against live hardware in this environment. The two other device repositories remain untouched.
