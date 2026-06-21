# Configuration Layout

Root config entry file:

- `config.json`

The root file points to smaller domain-specific files to keep each file focused and maintainable:

- `project/project.json`
- `io/io.json`
- `placer/placer.json`
- `runtime/runtime.json`
- `placer/grid.json`
- `placer/cost.json`
- `runtime/communication.json`
- `runtime/modules.json`
- `runtime/hardware-drivers.json`
- `runtime/device-drivers.json`
- `runtime/actors.json`

Suggested loading strategy:

1. Load `config.json` first.
2. Resolve each entry from `includes` relative to the `config/` directory.
3. Recursively load child files that also contain `includes`.

`runtime/modules.json` defines module startup order. The first module should be the communication monitor.

`runtime/hardware-drivers.json` defines physical hardware interfaces (serial and CAN placeholder).

Serial hardware driver fields:

- `portName`: configured device name or absolute device path. If it is a name like `marlinXY`, runtime resolves it to `/dev/marlinXY`.
- `baudRate`, `dataBits`, `parity`, `stopBits`: serial line settings (Marlin defaults here are 115200, 8, N, 1).
- `serialProtocol`: protocol adapter id (currently `marlin-gcode`).
- `defaultRotateSpeed`: fallback feedrate for `rotate` when no speed is provided.
- `serialResponseTimeoutMs`: timeout waiting for terminal Marlin response (`ok` or `Error:`).

`runtime/device-drivers.json` maps logical device driver names to hardware IDs using human-readable names.

`runtime/actors.json` maps actor IDs to hardware driver/device driver/channel and value rules:

- `id`: actor identifier used by `appActor`
- `driver`: hardware id (for documentation and validation)
- `deviceDriver`: destination module used on the message bus
- `index`: actor channel index sent as `P<index>`
- `minValue`, `maxValue`: accepted numeric range
- `offValue`, `onValue`: canonical values for `off` and `on`
- `allowedValues`: optional strict whitelist (for binary actors like `[0, 255]`)

All module messages use a common frame with:

- `timestampEpochMs`
- `destinationModule` or `*` for broadcast
- `sourceModule`
- `payloadType`
- `payloadJson` (module-specific payload)

Device command/reply payload types:

- `device-command`
- `device-reply`

Marlin protocol actions currently translated by the serial adapter:

- `homeXYAB` -> `G28 X Y A B`
- `disableAllSteppers` -> `M18 ...` by motion driver group
- `homeZ` -> `G28 <axis-letter>` using mapping below
- `moveXY` -> `G0 X<position-mm> Y<position-mm> F<speed-int>`
- `moveZ` -> `G0 <axis-letter><position-mm> F<speed-int>` using mapping below
- `rotate` -> `G0 A<rotation-degree> F<speed-int>`
- `operateActorAnalog` -> `M114 P<channel> S<value-int>`
- `operateActorDigital` -> `M114 P<channel> S<0|1>`

Z-axis mapping for drivers:

- `moveZ1` -> axis `X` on hardware `marlinAB`
- `moveZ2` -> axis `Y` on hardware `marlinAB`
- `moveZ3` -> axis `X` on hardware `marlinCD`
- `moveZ4` -> axis `Y` on hardware `marlinCD`

Disable-all mapping:

- `moveXY` -> `M18 X Y` to `marlinXY`
- `moveZ1`/`moveZ2`/`rotR1`/`rotR2` -> `M18 X Y A B` to `marlinAB`
- `moveZ3`/`moveZ4`/`rotR3`/`rotR4` -> `M18 X Y A B` to `marlinCD`
