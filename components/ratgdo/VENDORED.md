# Vendored: ratgdo/esphome-ratgdo `ratgdo`

Source: https://github.com/ratgdo/esphome-ratgdo at commit c11e9347b980d493421905613da4c7e502f886e2, directory
`components/ratgdo`, plus the repo's `LICENSE` (GPL-2.0).

One change: on ESP8266, `__init__.py` takes its two PlatformIO libraries from the local copies instead of GitHub:
`components/secplus` and `components/espsoftwareserial` (each has its own VENDORED.md).

Not vendored, because this fleet's ESP8266 board never uses them: the ESP32 (ESP-IDF) path still fetches
`ratgdo/secplus` branch `add-esp-idf-support` from GitHub, and `sensor/__init__.py` fetches `stm32duino/VL53L4CX` for the
v3.2 Disco distance sensor.

To update: replace this directory with `components/ratgdo` of the new commit, keep `LICENSE`, reapply the change above,
update the commit above, and compare upstream's `base.yaml` with `templates/ratgdo/v25i.yaml`.
