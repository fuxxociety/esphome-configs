# Vendored: ssieb/esphome_components `pedestal`

Source: https://github.com/ssieb/esphome_components at commit 3ff50bd1fd6a69ac41a18ca198b389c13e328eaa
(the commit the live fan-mbrpedestal firmware was built from; the component has since been removed upstream).

Changes for ESPHome 2026, nothing else: `fan/__init__.py` uses `fan.fan_schema(PedestalFan)` and `fan.new_fan()`
(the old `fan.FAN_SCHEMA` / `CONF_OUTPUT_ID` API is gone). The C++ is unchanged.

The previous local copy (modified, different logic) is kept in `reference/components-pedestal-local/`.

## Local change 2026-11-01: speed calibration
`duty_to_speed` recalibrated from the fan's measured speed-line duty per speed (see the comment in `fan/pedestal.cpp`).
The upstream table reported this fan one speed too high at almost every step, so speed 11 showed as 12 (100 %) and
IR speed changes, which are computed from the detected speed, landed one step off.
