# Doorlight | Shortcuts library and next-level ideas

The **Smart Bulbs** tab is the home page. **Shortcuts** is its own workspace, with a card library and per-shortcut editor. These examples split **implemented presets** from **future optional integrations** so you know what actually works in this version.

## Implemented and editable defaults

| Shortcut | When | Then |
|---|---|---|
| Weekday Sunbeam | Monday–Friday, 7:00 AM | Two Linkind bulbs rise from warm 10% to a cooler 65%, taking 15 minutes |
| Slow Saturday/Sunday | Weekends, 9:00 AM | Gentler 45% sunrise |
| Unexpected visitor | Existing door sensor says loitering | Violet gentle pulse, restore bulb state afterward |
| Quiet hallway | Motion while night profile is active | Subtle warm amber pulse, with global night dimming |
| Someone else enters | Door entry when your *home/away tracker* says away | Warm bulbs on |
| Someone else exits | Door exit when tracker says away | Bulbs off; use carefully, since this cannot prove the room is empty |
| Door still open | Requires a door-open event exposed by the original lock (not currently present in its WebSocket; leave disabled) | Low-intensity red pulse |
| Movie time | Manual click or external REST call | Indigo 12%, auto-restore after 2 hours |
| Study focus | Manual click or external REST call | Cool bright lighting for 45 minutes, then restore |
| Evening amber | 6:00 PM daily | A warm fade; **disabled by default** to avoid surprise changes |

## Extra ideas for a future revision

| Concept | How it could work | Additional requirement |
|---|---|---|
| **True room-aware “last person leaves”** | Turn off only when the room is truly empty | Room occupancy sensor or reliable multi-source occupancy inference; existing PIR + person presence is insufficient |
| **Bedroom wake-up with weather color** | Sunrise in blue on rainy mornings, gold on sunny ones | Home Assistant weather entity condition and rule variables |
| **Audio-reactive light scenes** | Pulse the strip or bulbs in time with music | Local audio feed/analyzer; Matter cloud effects may not be controllable |
| **Pomodoro conductor** | Focus color for 25 min, short warm break, repeat | Wait/delay steps and repeat/branch support inside the Shortcuts engine |
| **Gaming “do not interrupt”** | When a game is fullscreen, suppress door alerts on bedside bulb but leave local LED active | Companion PC agent or a Home Assistant sensor |
| **Soft landing** | After 10 PM, opening the door turns on just one lamp at 8% for two minutes | Rule trigger + night condition (mostly available now) |
| **Sunset-following schedule** | Shift night time based on local sunset instead of fixed 6 PM | Home Assistant `sun.sun` schedule integration with timezone-aware offset |
| **Priority automation stacking** | Visitor alerts interrupt movie mode, then resume even after nested/overlapping alerts | Stack-based transaction and automation priority engine |
| **Manual scene takeover** | Detect wall switch or Apple Home change and cancel/override all temporary effects | Persistent HA WebSocket subscription and multi-controller ownership tracking |
| **Reliable door identity** | Identify you specifically as you walk in or out | Opt-in proximity sensors or fingerprint-to-entry correlation; home/away alone does not establish identity |

## Engineering direction

This v3 ships an **event → conditions → 1–3 sequential lighting actions** engine. A full Apple Shortcuts-style workflow builder would also need conditional branches, waits, variables, parallel groups, trigger history, undo, a visual execution trace, and multiple priority layers. Those are sensible v4 requirements, but don't claim they are all finished here.

A particularly useful future architecture is a distinct lightweight rule evaluator with a durable execution queue and transaction snapshots. It would preserve authoring/editing separately from execution, avoid blocking the ESP32 main loop on several Home Assistant requests, and support complete automated tests with a simulated Matter backend. This is the clean next step once the current hardware integration is proven.
