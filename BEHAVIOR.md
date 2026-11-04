# Fleet behaviour spec (Phase 1: review before any YAML is written)

Sources: the current tree, the live HA registry (`../logs/ha-refs-20261031-101047.{md,json}`), the core HA automations and
scripts (`../logs/ha-autos-core.yaml`, `../logs/ha-scripts-core.yaml`), and the 2026-10-30 migration check.

`[decided]` marks your answers from planning. `[observed]` is what the configs or HA actually do today. `[CONFIRM]` marks
the open questions below.

## 0. Operation pipeline: room control loop (takes priority over any single device)

### 0.1 Intended (as you described it)
Smart mode (Relay Config = Always On; the relay stays energised and the bulbs are the load):
1. The physical button is pressed on the switch.
2. The switch changes no load. It sends the event to HA.
3. HA reacts. For a single press it moves the room's bulb group through the state machine bright → medium → low → night → off.
4. The bulbs change, and report their new state.
5. HA relays that state back to the switch, whose status-LED brightness then shows the room's current brightness.

Side effect, and a requirement: **if the bulbs are changed any other way (HA UI, a scene, voice), the switch indicator
still shows their current state.**

Switched mode: the switch toggles its own relay locally (decided) and still sends the event. The LED shows on/off.

### 0.2 Observed today (automation traces from 2026-10-31, `../logs/ha-autos-core.yaml`)
| Step | What actually happens | Status |
|---|---|---|
| 1→2 | The switch sends `esphome.single {devicename, name_friendly, action, mode, brightness, device_id}`. **The switch computes the next step itself** (`select_brightness` + an 8 s window) and sends it as `brightness`. ESPHome **adds `device_id` = HA's internal device-registry id** (e.g. `f0b11d36…`) automatically | works |
| 3 | `Wall Switch Single-Press…` → `script.single_press_global` builds `light.group_<devicename>` / `light.<devicename>_group`. The switches send **`masterbed-light-switch`, `kitchen`, …**, but the groups that exist are `light.group_masterbed` and `light.diningroom_group`, and most rooms have none. **The for-each loop gets zero groups and nothing changes.** Only rooms with their own `script.single_press_<x>` (e.g. masterbath_closet at 14:10) do anything | **broken for most rooms** |
| 3 (double) | `Wall Switch Double-Press…` builds `light.group_<device_id>`. Because device_id is the registry id, it never matches | **broken** |
| 4 | Bulbs fire `esphome.light {device_id: <bulb system_name>, action: on/off}`. **No brightness** is included | partial |
| 5 | `Bulbs trigger Switch indicators` → `switch.<id>` and `light.<id>_indicator_light`. Neither exists for the current switches | **broken** |
| — | **No switch light is a member of its room's group.** The only groups containing switch entities are `all_masterbedroom_lights` (contains `masterbath_closet_switch` and the masterbed ceiling-fan dimmer) and `masterbath_shower_group` | differs from intent |

So the loop exists only in pieces. The new firmware and the v2 automations have to rebuild it end to end; renaming alone
won't fix it.

### 0.3 Proposed design (v2)
**One key ties everything together: `room_key`** (e.g. `masterbed`, `diningroom`, `upbath_vent`). It's a substitution on the
switch and on every bulb in the room. HA owns one group per room: **`light.group_<room_key>`**, containing the bulbs only.

Feedback (steps 4–5): the two methods you described compared, with a recommendation.

| Method | How | Catches external changes? | Drawbacks |
|---|---|---|---|
| A. Bulb events (yours) | each bulb fires `esphome.light` with brightness → HA → switch | only from ESPHome bulbs | N bulbs mean N events per change, so the indicator flickers through intermediate values. Every bulb has to know its room. Any non-ESPHome bulb is invisible |
| B. Switch light inside the group (current intent) | HA sets the group, and the switch light gets the same values | only when the **group** is commanded. A single bulb or a scene that targets bulbs directly isn't reflected | the switch's light skews the group's average brightness and "any on" state. Toggling the switch light in the UI changes only the switch |
| **C. Group-state mirror (recommended)** | one generic v2 automation: **state change of `light.group_<room_key>` → set the switch's light** to the group's on/off, brightness and colour | **yes, from any source and any brand of bulb.** It's HA's aggregated state, so one update per change | needs the group to exist (it does once Phase 5 creates them) |

With C, the switch's light is also a **remote** for the room. A second generic automation forwards a change that a *user*
makes to the switch's light (`context.user_id` set, not from an automation) to the group. Context ids prevent loops: a
change the mirror makes is never forwarded back.

On the switch, in smart mode, the **status LED shows the virtual light's brightness** (scaled into the LED's visible range, so
"night" still glows), and goes to Off Brightness when the light is off. This happens in firmware (`copy_vals`), so
`light.<x>_indicator_light` and the HA-side LED automations aren't needed.

State machine (step 3). Two placements:
- **Switch-side (recommended):** as today, the switch works out the next step, but from its **mirrored light brightness**
  instead of a private select, so it continues from wherever the bulbs actually are. The event keeps `brightness`, and
  `single_press_global` keeps working unchanged once the groups exist.
- HA-side: the switch sends only `single_press`. HA works out the next step from the group's brightness and the time since the
  last press. That's one more piece of state in HA, and the existing scripts have to change.

Feedback method C and the switch-side state machine are [decided]. Behaviour when HA is offline is defined in §0.5.

### 0.5 Utility power loss and HA outages [decided, from your rules; existing behaviour noted]
Goal: after any outage, a switch and its bulbs work as a plain switch and plain bulbs **with no HA involved**.

**Restart cause decides the boot behaviour.** Every device reads the chip's reset reason at boot, through a shared helper in
`common/boot/reset_reason.h` that covers ESP8266 `ESP.getResetInfoPtr()`, ESP32/C3 `esp_reset_reason()` and BK72xx
`lt_get_reboot_reason()`.
- **Power-on reset** (utility power returned, or the relay just energised the bulb): apply the power-loss rules below.
- **Software / watchdog / OTA reboot**: restore exactly the state from before the reboot (relay, light state, mode). A
  self-reboot never changes what's lit.

| Device | After a power-on reset | After a software reboot | Today |
|---|---|---|---|
| Switch relay | **de-energised, always** | restored | energised at boot in Always On |
| Switch mode | Relay Config (the *preferred* mode) is kept. The *effective* mode is **Switched until HA connects**, then returns to the preferred mode. The relay stays off throughout | restored | preferred mode restored, relay on |
| Bulbs (every template, including Kauf through its power-on select) | **on at 100 %, neutral white** (`ALWAYS_ON` + initial state; ~300 mireds, matching `single_press_global` "bright") | previous state restored | Treatlife: inverted toggle; LightingInside: restore, default on |
| DS01C ceiling fan | **off** [decided] | restored | MCU default |

So after an outage: everything is dark, the relays are open, and the first press energises the relay, which brings the bulbs up at
full brightness. That's the same result whether or not HA is up.

**Relay energising rule (smart mode):** the relay is only energised by
1. a physical press, which also counts as the first step (off → bright), or
2. HA turning on the switch's light: the `[v2] Switch → room` mirror, a dashboard, or `[v2] Room power-up` (HA turns the
   switch's light on when the room group is commanded on while its bulbs are unavailable).

It never energises by itself when the mode changes back to Always On.

**HA outage while running (power fine):**
- Switches get `api: reboot_timeout: 0`, like today's "No HASS" default, so they never reboot just because HA is away.
- Smart mode with the API disconnected for **30 min** (`ha_offline_revert: 30min`): the effective mode becomes **Switched**. The
  relay keeps its current state, so whatever is lit stays lit, and the button now toggles the relay. Bulbs power-cycled by the
  relay come up at 100 %. The status LED shows the error blink, as in 2025.x `blink_status_led`.
- When the API reconnects, the effective mode returns to the preferred mode. The relay isn't changed (it stays as the user left it).
- Bulbs keep their `reboot_timeout`, because a software reboot now restores state and doesn't jump to 100 %.

### 0.4 What v2 needs in HA (Phase 5, each write confirmed with you first)
- Create the missing groups as `light.group_<room_key>`, bulbs only. Leave the old groups (`diningroom_group`, `all_*`) as they
  are; they're used by dashboards.
- `[v2] Room single press`: the same logic as today, but keyed on `room_key`. The event sends `devicename: <room_key>` plus
  `room_key`. ESPHome's `device_id` is ignored.
- `[v2] Room double press`: keyed on `room_key`.
- `[v2] Room mirror → switch` and `[v2] Switch → room` (method C).
- The old automations are disabled per room as each room moves to v2.

## 1. The contract between switches and Home Assistant [observed]

HA doesn't bind to entity ids much. It builds names from the **`devicename` key in the event data**
(`system_name`, e.g. `craftroom`, `masterbath_vanity`, `upbath_vent`), slugified:

| HA builds | From | Used by |
|---|---|---|
| `light.group_<key>`, `light.<key>_group` | `devicename` | `script.single_press_global` (applies the brightness preset to every light in the group) |
| `script.single_press_<key>` | `devicename` | per-room overrides (craftroom, fireplace, laundryroom, masterbath_*, masterbed, officeceiling, upbath_*) |
| `timer.<key>` | `ventname` / `devicename` | `script.single_press_vent`, `Timer Expired (multiple)` |
| `select.<key>_relay_config`, `switch.<key>` | timer entity name | `Timer Expired (multiple)` (Always On → fire `esphome.single` with `effect: off`; Switched → `switch.turn_off`) |
| `switch.<device_id>`, `light.<device_id>_indicator_light` | `esphome.light` from bulbs | `Bulbs trigger Switch indicators` |
| `light.<devicename>_light_switch` | `esphome.single_press` | `Switch Events` (mirrors the virtual light's brightness/colour to the matching bulbs) |

Events HA listens for:

| Event | Data | Sent by |
|---|---|---|
| `esphome.switch` | `action: switch_on/switch_off, devicename, mode` | SS01S |
| `esphome.single` | `action: single_press, devicename, mode, brightness` | SS01S (from `select_brightness.on_value`), and **HA itself** (bulb reconnect, timers) |
| `esphome.single` | `action: hold` | (`Wall Switch Hold…`, which has no actions) |
| `esphome.double` | `action: double_press, device_id` | DS01C (`device_id`) and SS01S (`devicename`). The automation reads `device_id` and targets `light.group_<id>` |
| `esphome.single_press` | `devicename` | front-entry-2 style |
| `esphome.light` | `device_id, action` | bulbs |

**Consequence for naming:** the room key in events is the stable interface, not hostnames or entity ids. The new
firmware keeps a `room_key` substitution, sent as `devicename` (and as `device_id` on the double event, for
compatibility), so the existing scripts, groups and timers keep resolving. Hostnames and display names can be
normalised freely. Only about 40 dashboard cards and 5 automations reference concrete ESPHome entity ids; ha-refs lists them.

## 2. SS01S light switch (≈30 devices) [decided + observed]

Live today, in two generations:
- **2024.x SS01S** (`switch-*`, e.g. Switch-Craftroom): `switch.<key>`, `select.<key>_relay_config`,
  `select.<key>_brightness`, Single/Quad Click buttons, Status/Error LEDs (disabled), uptime, wifi.
- **2025.x Kauf-style** (Craftroom Closet, Garage Sconce, Front Porch, Masterbath Closet). Source recovered at
  `../backups/haos_device_backup_06Jul2025/includes/device_configs/treatlife-ss01s-mono.yaml` plus `binary_sensors/gpio_button*`:
  - **"Light"**: a *binary* `ui_binary_light`. Its output drives a hidden `relay_control_switch`, which honours Relay Config
    (Always On blocks turning off, Always Off blocks turning on). This is already "local relay, always".
  - Status LED On/Off Brightness: config lights on a bit-bucket output, copied to the real LED by `copy_vals`. Also
    Restore Lights, No HASS (API `reboot_timeout` 0 or 15 min), Button Press Duration, and Single Press.
  - Gestures:
    - single → `script_cycle_brightness`: the same toggle / 8 s bright→…→off loop as 2024.x.
    - 3 s hold → `script_cycle_switch_mode` **on reaching 3 s**, plus a blinking LED while held, plus the event `esphome.switchmode`.
    - 30 s hold → Wi-Fi credential reset.
    - double/hold/10 s → `hass_entity_*` toggles (all `no.ne`).
  - Event: `esphome.single` with `name_friendly` but **no `devicename`**. That means `Wall Switch Single-Press…` can't
    resolve groups or scripts for these four devices today; `Switch Events` with `light.<x>_light_switch` covers them.
- Prototype `switch-lights-template.yaml` (same backup): a virtual **rgbww** light on a bit-bucket, with 4 preset lights and an
  `esphome.switch_light` event. It was never deployed.

Target (one template, built from the 2025.x design: `ui_binary_light` → `relay_control_switch`, Status LED On/Off
Brightness, `copy_vals`, No HASS):
- **The switch is the representative entity for what it controls.** [decided] A light switch exposes a light, and a fan/vent
  switch exposes a fan. Other load types are decided when we get to them. The separate `switch.<key>` goes away.
- **Light switches: a virtual rgbww light** (`name: None`, so it takes the device name), on bit-bucket outputs, as in
  `reference/includes-2025-07/device_configs/switch-lights-template.yaml`. It replaces the 2025.x binary "Light". [decided]
  It exposes every capability any bulb has (RGB, colour temperature through cold/warm white, brightness), so HA can mirror it to
  `light.group_<key>`. The relay follows on/off only. The physical **status/error LEDs stay monochromatic**.
- **Fan/vent switches** (`*-vent`, office-fan, thomas-ceiling-fan): a template **fan** (on/off) whose state drives the relay.
  Same gestures and events; the vent timers keep working from the `brightness` field (`off` vs. not off).
  - Switched mode: light on/off drives the relay **locally**, always. [decided] Brightness is kept but has no electrical effect.
  - Always On (smart-bulb) mode: the relay stays energised and the light's brightness steps through
    bright → medium → low → night → off. HA mirrors it to `light.group_<key>`.
- Entities kept: Relay Mode select (Switched / Always On / Always Off), Brightness select (the preset name, sent as `brightness`), Status LED and
  Error LED (disabled by default), Restart, Uptime, Wi-Fi diagnostics.
- Dropped: the Single/Quad Click test buttons and `switch.<key>`. [decided] The v2 copies of `Timer Expired (multiple)` and
  `Bulbs trigger Switch indicators` target `light.<key>` / `fan.<key>` instead.
- Status LED: on (dim) when the light is on, off when it's off. Hold-progress flashing overrides it (§5).
- HA offline and power loss: see §0.5. Switched mode always works locally. Smart mode falls back to Switched after 30 min without
  HA, and right after a power-on reset.

## 3. DS01C dimmer-switch module → ceiling-fan controller (8 devices) [decided]
- **Every DS01C drives the fan motor of a ceiling fan; none drives a light.** So by the representative-entity rule it
  exposes a **fan**, not a light. The tuya MCU keeps doing the "dimming", which here is speed control.
- Entity: a template `fan` with `speed_count: 100`. `on_turn_on/off` → tuya dp1; `on_speed_set` → dp2, scaled from 1–100 onto
  dp3-min…1000 (as with the tuya light today, `max_value: 1000`). Tuya sensors on dp1/dp2 feed state back, so wall-touch
  changes reach HA.
- Single press toggles dp1 locally. The other gestures fire events (§5).
- **Status LED**: the module should have a directly controllable LED (user). Find it in Phase 4 on the test device, either as a
  GPIO or as a tuya datapoint (tuya logs every dp at boot), and wire it to the shared hold-flash feedback. Until it's found,
  the template has an optional `status_led_pin` substitution.
- The dashboards using `light.*_ceiling_fan` get `fan.*` cards in v2.

## 4. 3-way (Downstairs / Upstairs 3-Way, ESP32-C3)
- Live firmware (2025.6.2) exposes only `switch.relay_2`, GPIO probes `sense5/18/19` and a power sensor. It looks like a
  bring-up firmware.
- `devices/treatlife-3way.yaml` is a Kauf PLF10 adaptation with the wrong pins.
- **Pinout resolved** from the backup's `treatlife-ss01-3way.yaml` (ESP32-C3-12F): relay IO04, button IO05 (inverted),
  white LED IO18 (inverted), red LED IO19 (inverted), power/usage sensor IO03 (inverted, 50 ms delayed_off). This
  matches the live `switch.relay_2` on GPIO4 and the `sense5` input.
- Target: the SS01S template on the ESP32-C3 platform with those pins. The power sensor is the actual load state,
  because the other leg of the 3-way can flip it, so **the light's state follows the power sensor**, not the relay. A single
  press toggles the relay, which flips the load whichever way the other leg is set.
- **Migrated 2026-11-01** (`switch-upstairs-stairwell`, `switch-downstairs-stairwell`). IO18/IO19 confirmed as the LEDs by
  pad translation from the original ESP8266 board (relay, button and sensor translate the same way and work). Load
  polarity checked live: both Load sensors read on while the bulb had power.
- **Both legs feed ONE bulb.** Anything outside the firmware (dashboards, automations) takes the stairwell's state from the
  two Load sensors, `binary_sensor.upstairs_stairwell_load` OR `binary_sensor.downstairs_stairwell_load`, never from a
  relay or one leg's light alone. Commands go to ONE leg (`light.upstairs_stairwell`): its firmware flips the relay only
  when the Load sensor disagrees. Never command both legs at once: both can see "off", both flip, and the bulb stays off.
- **Planned: smart-bulb mode** (when the stairwell gets a smart bulb). A 3-way can't just hold its relay closed, because
  the other leg can open the circuit. Smart mode keeps the circuit powered instead: when Load drops, the leg flips its
  relay back. Presses become events and HA steps the bulb (bright → medium → low → night → off, as on the SS01S). Dashboards
  then control the bulb, with the Load sensors as its "has power" indicator.

## 5. Gestures, shared by all switches [decided]

| Gesture | Action |
|---|---|
| single | SS01S: Switched → toggle; Always On → next brightness step. DS01C/3-way → toggle |
| double, quad | event only (`esphome.double`, `esphome.quad`) |
| hold (≥750 ms, released before 3 s) | event only (`esphome.single` with `action: hold`, as HA expects today) |
| held > 3 s | status LED flashes 400 ms on / 400 ms off. Released between 3 and 10 s → cycle Relay Mode Switched ↔ Always On |
| held > 10 s | LED fast-flashes at 100/100 ms (proposed). Release → reboot |

Timing stays at hold 750 ms, gap 333 ms; the single-press `invalid_cooldown` is 1500 ms, as in 2025.x. Triple is removed.

Changes from 2025.x: the mode change moves from "on reaching 3 s" to "on release between 3 and 10 s", and there's a new
reboot after 10 s. **The 30 s Wi-Fi credential reset is removed entirely.** [decided] Mode changes also fire `esphome.switchmode`.

Event data on every event: `devicename: <room_key>`, `device_id: <room_key>`, `name_friendly`, `action`, `mode`, `brightness`.
With both keys present, the existing `Wall Switch …` automations resolve groups and scripts for every switch, including the four
2025.x devices that can't today.

## 6. Bulbs and lights [decided + observed]

| Template | Devices | Colour modes | Effects set |
|---|---|---|---|
| Treatlife SL10 (BK7231T WB8P / BK7231N CB8P) | bulb-diningroom-1…5, -craftroom-1, -frontporch-1, -frontentry-1, -masterbed-1/2, -officeceiling-2 | RGB + W (interlock) | rgbw |
| LightingInside sm2335 (A19 / B11) | bulb-masterbath-toilet, bulb-livingroom-4/5/6 | RGB + CWWW (interlock) | rgbww |
| Kauf (remote package, extended) | kauf-bulb-30xxxx, masterbed-shelf, masterbath-tub, livingroom-shelf, masterbed-bedside | RGBWW | rgbww |
| RGB + CWW fixture | blnan-ceiling (30 px, not installed), masterbath-shower-fixture (60 px WS2812, `reference/`) | addressable RGB strip + separate CWWW pair | see below |
| Growlights | livingroom-growlights (5 mono channels) | brightness | mono |

- **RGB + CWW fixtures: one template, `templates/fixture/rgb-cww.yaml`.** Both use the same hardware: ESP8266, CW/WW on
  `esp8266_pwm`, a WS2812/WS2811 strip through `neopixelbus` on GPIO2. Pins, LED count, colour order and CT range are
  substitutions. Physically, each fixture is a **CWW panel in the centre** (the main light) and an **addressable RGB ring** around
  it (the accent). [decided] There are two entities, because that's the only way to run both at once with independent
  settings (panel colour temperature + ring colour or effect). HA's rgbww colour modes make a combined entity zero the RGB part
  whenever a colour temperature is set.
  - **Main: the panel** as a `cwww` light (`name: None`, so it's the fixture's name). It gets the shared cwww (brightness) effects.
  - **Accent: the ring** as an addressable `neopixelbus` light named "Ring". It gets the shared rgb effects plus the addressable
    set (Rainbow, Spinner, Pulse Wave, Symmetrical Glow, Concert Sequence, from the shower config, which are moved into
    `common/effects/addressable.yaml`).
  - Both entities fire `esphome.light` and are independent. Neither one turns the other off.
- **No brightness-preset effects and no `variable_brightness` service.** [decided] `script.single_press_global` already
  applies them HA-side using the `supported_color_modes` of each light.
- Every bulb keeps firing `esphome.light {device_id, action}` on turn on/off, because `Bulbs trigger Switch indicators` uses it.
- Restore modes are **replaced** by the §0.5 rule: 100 % neutral white after a power-on reset, restore after a software reboot.
  The Treatlife `RESTORE_INVERTED_DEFAULT_OFF` toggle trick goes away. A deliberate power cycle still turns a bulb on, but no
  longer toggles it.
- Keep `default_transition_length: 250ms`, or the Default Fade number where it exists today.

## 7. Everything else
- **Minoston MP22WP appliance plugs** (dishwasher, dryer, washing machine) are unified into the switch scheme [decided]:
  - their button behaves like a wall switch (`common/switch/core.yaml`)
  - **after a power cut the relay comes back as it was** (`power_loss_open_relay: 'false'`), unlike wall switches
  - they keep the **Minoston LED modes** (`common/switch/led-modes.yaml`) and the **Scale** trims
  - the main entity is a **switch** (`switch.dryer`)
  - the power-monitoring block is the hand-tuned original, line for line (verified against its generated code)
  - settings are still saved 3 s after a change, despite the 5 min flash interval
  - dropped: Monitoring Update Interval (fixed 10 s), Boot State, Button Config, Debounce Time, the 30 s force-AP hold
  - `binary_sensor.<x>_device_in_use`, `sensor.<x>_power` and `_total_daily_energy` keep their ids exactly.
- `fan.fan_mbrpedestal_pedestal_fan` is used by scripts and dashboards, so its id is kept.
- **Garage door** (`cover-garage`, ratgdo v2.5i, Security+ 2.0, added 2026-10-31): the door is the device's main entity
  (`cover.garage_door`); light, lock remotes, motion, obstruction and the dry contacts as upstream ratgdo. Vendored
  component and libraries in `components/` (ratgdo, secplus, espsoftwareserial).
- Motion sensors, btproxy, aquarium plug, Kauf plug: move them onto `common/` and keep their entities.

## 8. Duplicates and missing configs: one config per hostname

| Live device | Keep | Retire |
|---|---|---|
| switch-backporch, -breakfastnook, -downbath, -garage, -masterbath-*, -upbath-vent, … | `switch-<key>.yaml` | `<room>-light-switch.yaml`, `<room>-fan.yaml` duplicates |
| dimmer-craftroom-ceiling | `craftroom-ceiling-fan-switch.yaml` → renamed `dimmer-craftroom-ceiling.yaml` | |
| dimmer-* | `dimmer-<room>-ceiling.yaml` | `<room>-ceiling-fan.yaml`, `spare-ceiling-fan.yaml` |
| bulb-masterbath-toilet | `bulb-masterbath-toilet.yaml` | `masterbath-toilet-bulb.yaml` |
| office ceiling | `dimmer-office-ceiling.yaml`, `office-ceiling-light.yaml` (Office-Light-Switch, live) | `switch-officeceiling.yaml`, `office-ceiling-fan.yaml` (points at the dimmer's address) |
| front porch | `front-porch.yaml` (hostname `frontporch`, which is what the HA device is) | `switch-frontporch.yaml`, `frontporch-minimal.yaml`. **Not responding right now**, so it's not a test device and it's flashed last, once it's back |
| bulb-frontentry-1, front-entry, garage-sconce, kitchen, mason-ceiling-light, masterbath-shower-fixture, masterbed-ceiling-fan, upstairs-stairwell | restored from git HEAD into `reference/` (together with the July 2025 `includes/` as `reference/includes-2025-07/`) | they were deleted from the add-on dir before the copy. `reference/` is outside the dashboard, CI, yamllint and check_includes |

## 9. Proposed test device per type (Phase 4)

| Type | Device | IP | Why |
|---|---|---|---|
| SS01S | switch-garage | .60 | reachable, low impact |
| DS01C (ceiling fan) | dimmer-craftroom-ceiling | .53 | reachable; also used to find the status LED |
| 3-way | Upstairs 3-Way | ? | bring-up firmware anyway |
| Treatlife BK7231T | bulb-diningroom-5 | .55 | one of 5 in a group |
| Treatlife BK7231N | bulb-diningroom-2 | .50 | |
| LightingInside | bulb-masterbath-toilet | .80 | |
| Kauf bulb | kauf-bulb-30288a | ? | |
| RGB + CWW fixture | masterbath-shower-fixture | ? | the installed one (blnan isn't installed) |
| Minoston plug | plug-dryer | .72 | |
| Kauf plug | kauf-plug-44ed55 | .67 | |
| Enbrighten plug | aquarium-light-outlet | .68 | |
| others | one each: motion-*, btproxy, fan-mbrpedestal, growlights | | |

## Open questions
1. ~~Virtual light type~~: rgbww; the indicator LEDs stay mono.
2. ~~Drop `switch.<key>`~~: yes. The switch exposes the entity type of its load (light or fan).
3. DS01C: no LED feedback for the 3 s and 10 s holds. Is that acceptable?
4. ~~3-way pinout~~: resolved from the backup. A bench check is still part of the Phase 4 test on that device.
5. Shower fixture: is the white channel mono (one PWM) or CWWW?
6. Front Porch: which config is live?
7. ~~Restore deleted configs~~: done, in `reference/`.
8. ~~30 s Wi-Fi credential reset~~: removed.
3. ~~DS01C LED~~: the module has a controllable LED; find it on the test device.
5. ~~Fixture white~~: a CWW centre panel (main) plus an RGB ring (accent). One template with two entities.
6. ~~Front Porch~~: `front-porch.yaml`, deferred until the device responds.
9. ~~DS01C load~~: all of them drive ceiling-fan motors, so they expose a fan. Fan type goes in the name (see §Naming).
10. ~~Fixture entity shape~~: panel = main cwww light, ring = "Ring" addressable light, so both can run at once.
11. ~~§0.3 pipeline~~: method C, switch-side state machine; outages handled as in §0.5.
12. ~~DS01C after power loss~~: off.

## Naming [decided 2026-10-31]
Source of truth: `naming/naming.yaml` (generated by `../tools/rename_map.py draft`), with hand decisions in `naming/overrides.yaml`.
The resulting old→new map is `naming/rename-map.csv`, and its review is `../logs/rename-map.md`.

- **Shortest descriptive friendly name, with no load word**, because the domain already says it: `light.kitchen`,
  `light.master_bath_vanity`, `fan.upstairs_bath_vent`, `fan.master_bed_ceiling`, `fan.master_bed_pedestal`,
  `light.dining_room_1`, `sensor.dryer_power`. Rooms are written short: Master Bath, Master Bed, Upstairs Bath, Downstairs Bath.
- Bulbs in a room that has a switch are always numbered (`Dining Room 1…5`, `Master Bath Toilet 1`), so they never collide with
  the switch's light. Standalone bulbs aren't numbered (`Living Room Shelf`, `Master Bed Shelf`).
- Auxiliary entities have short names under the device ("Relay Config", "Uptime", "Power"), and HA prefixes the device name.
- **ID conflicts are not a concern. Each device is removed from HA and re-added under its new name** at its flash (Phase 5).
- Rooms and loads from your answers:
  - Dining Room: 5 ceiling bulbs. Bedrooms: 2 ceiling bulbs each, where they have smart bulbs.
  - **Master bath shower switch powers both the shower fixture and the tub bulb**, so they share `room_key: masterbath_shower`.
  - Master Bed Shelf and Living Room Shelf: standalone, with no wall switch.
  - **Craft Room Wash 1 / 2**: `plug-craftroom-wash-1` is an **Enbrighten WFD4105** (BK7231N), the former aquarium plug;
    `plug-craftroom-wash-2` is a **Sengled B1C-NB7 with its chip swapped to an ESP8266** (BL0937 power monitoring), the former
    "Kauf Plug 44ed55". [decided] Their buttons behave **exactly like the wall switches**: the same gesture engine, events,
    Relay Config modes, power-loss and HA-outage rules and LED feedback (`common/switch/core.yaml`). That replaces the Kauf
    Button Config / debounce / LED-mode / Boot State settings and the 30 s AP hold. Debounce stays 100 ms, as their old
    configs had. The two plugs power the craft room's bright wash lights, so each exposes a *light*. Both sit in
    `light.group_craftroom_wash`, outside the craft room switch's smart-mode loop. [decided] The craft room switch's **double press
    toggles `light.group_craftroom_wash`** through `[v2] Room double press`, which maps the press's `room_key` (`craftroom`)
    to that group.
  - The 4 Kauf bulbs (302342, 30288a, 302e9a, 304cc4) are unreachable, so they're **retired**. Their HA entries are left alone.
  - `light.kitchen`'s leftover wrapper was removed by you (2026-10-31), so there are no real id collisions left. Two overlaps
    (`dining_room_*`, `office_ceiling_*`) only need the device that holds them today migrated first.
  - Kitchen (Switch-Kitchen): the load isn't a smart bulb, so it stays in **Switched** mode. Like every switch, it keeps the 3 s
    hold to change mode from the switch itself. There's no lock-out. [decided]
  - The dead tub bulb (MAC a4:e5:7c:bc:34:d8) was removed from HA on 2026-10-31. Its UniFi client record remains.
- The 60 configs on the retire list are stale and won't be used. The list is regenerated after the refactor.

### Earlier rules (still apply)
- Hostname: `<class>-<room>[-<type>][-<n>]`.
- **Fans: `fan-<room>-<type>`**, with type ∈ `ceiling`, `pedestal`, `desk`, `vent` (bath exhaust), `box`, … The type is also a
  substitution (`fan_type`), so it ends up in the friendly name ("Master Bedroom Ceiling Fan") and in the device's `comment`/area.
  Examples:
  - `dimmer-masterbed-ceiling` → `fan-masterbed-ceiling`
  - `fan-mbrpedestal` → `fan-masterbed-pedestal`
  - `switch-upbath-vent` → `fan-upbath-vent`
  - `switch-office-fan` → `fan-office-ceiling` (SS01S switching a ceiling fan)
  - `thomas-ceiling-fan-switch` → `fan-thomas-ceiling`
- The **hardware** (DS01C, SS01S, pedestal board) is in the template name, not the hostname, so a new device type doesn't
  need a new naming rule.
- The `room_key` sent in events stays as it is today (`upbath_vent`, `masterbed`, …) whatever the hostname, so HA
  timers, groups and scripts keep resolving.
