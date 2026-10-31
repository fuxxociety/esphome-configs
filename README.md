# ESPHome fleet configuration

Modular ESPHome configs for the house (ESPHome 2026.10). This directory **is** the ESPHome dashboard's config
directory: on Home Assistant it is a git clone (`/config/esphome`), updated with `git pull`.

How every device is meant to behave (switch/bulb pipeline, power-loss and HA-outage rules, gestures, naming) is
specified in [BEHAVIOR.md](BEHAVIOR.md). Read that before changing a template.

## Layout

```
<hostname>.yaml            one thin file per device: substitutions + one template (the dashboard lists these)
templates/<brand>/*.yaml   hardware + behaviour for one kind of device
common/                    shared building blocks
  platforms/<chip>.yaml      esp8266 · esp8285 · esp32 · esp32c3 · bk7231n · bk7231t  (each includes base.yaml)
  base.yaml                  name, API, OTA, Wi-Fi, logger, time, diagnostics, captive portal
  boot/power.h               power-on reset vs software reboot (all chips)
  button/multiclick.yaml     the one gesture engine for every physical button
  switch/                    wall-switch behaviour: core + loads (smart light, lamp, fan, appliance) + LED policies
  light/                     shared bulb behaviour, PWM channel, …
  effects/                   one file per light effect; color / white / addressable lists
  bluetooth/proxy.yaml       BLE tracker + Bluetooth proxy (ESP32)
components/                local external components (pedestal fan, ratgdo + its libraries; vendored)
naming/                    naming.yaml (generated), overrides.yaml (hand decisions), rename-map.csv
diagnostics/               wifi-diag firmware for the Wi-Fi debugging tools (USB test board, not a fleet device)
legacy/                    the pre-refactor configs and their includes, out of the dashboard. They do NOT compile on
                           ESPHome 2026; rollback images are built from them with each device's old ESPHome version
reference/                 configs and includes recovered from backups, reference only (not built)
```

| Template | Devices |
|---|---|
| `treatlife/ss01s-light.yaml` (`-1m` for 1 MB units) | SS01S light switches |
| `treatlife/ss01s-fan.yaml` | SS01S bath vent switches |
| `treatlife/ds01c-fan.yaml` | DS01C ceiling-fan controllers |
| `treatlife/ss01-3way.yaml` | SS01 3-way switches (ESP32-C3) |
| `treatlife/sl10-wb8p.yaml`, `sl10-cb8p.yaml` | Treatlife SL10 RGBW bulbs (BK7231T / BK7231N) |
| `lightinginside/a19.yaml`, `b11.yaml` | LightingInside RGBWW bulbs |
| `kauf/rgbww-bulb.yaml` | Kauf RGBWW bulbs |
| `fixture/rgb-cww.yaml` | CWW panel + addressable ring fixtures |
| `minoston/mp22wp.yaml` | appliance plugs with power monitoring |
| `enbrighten/wfd4105.yaml`, `sengled/b1c-nb7.yaml` | lamp plugs |
| `diy/esp32-pwm-5ch.yaml` | grow lights |
| `diy/esp32-ir-pedestal-fan.yaml` | IR pedestal fan bridge |
| `ratgdo/v25i.yaml` | ratgdo v2.5i garage door controller (Security+ 2.0) |
| `ratgdo/v25i.yaml` | ratgdo v2.5i/v2.52i/v2.53i garage door controller (ESP8266, Security+ 2.0) |

## Conventions

- **Hostname** `<class>-<room>[-<fixture>][-<n>]`: `switch-kitchen`, `fan-upbath-vent`, `bulb-diningroom-3`.
- **friendly_name**: the shortest descriptive name, no "Light"/"Fan": HA prefixes every entity with it and the
  domain says the rest (`light.kitchen`, `fan.master_bed_ceiling`). Bulbs behind a switch are numbered.
- **room_key**: the key HA automations use to find a device's group, scripts and timers (`light.group_<room_key>`).
  Every event a device sends carries it as `devicename` and `room`.
- A device file sets `name`, `friendly_name`, `room_key` and includes **one** template. Anything else in it (pins,
  calibration, `use_address`) is a per-unit fact and gets a comment.
- Secrets only via `!secret` (keys in [secrets.yaml.example](secrets.yaml.example)); never inline.

## Adding or renaming a device

1. Add or correct the device in `naming/overrides.yaml` (keyed by its HA device name).
2. `python ../tools/rename_map.py draft --force && python ../tools/rename_map.py build` (needs a fresh
   `../tools/ha_refs.py` scan) and review `../logs/rename-map.md`.
3. `python ../tools/gen_devices.py` to see the plan, `--write` to write the device files. Files it generated start
   with a marker line and are regenerated; hand-written device files are never touched.
4. `esphome config <file>`; CI runs yamllint and `esphome config` on every top-level file.

Build ESP32-C3 and other ESP-IDF devices from PowerShell or cmd, not Git Bash (ESP-IDF refuses MSYS).
