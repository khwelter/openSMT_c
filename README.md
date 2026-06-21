# openSMT_c

Simple C++ starter project for an SMT placing unit with a communication-first module runtime.

## Structure

- `include/`: public headers for the library
- `src/`: library implementation and app entrypoint
- `config/`: hierarchical JSON configuration files

Key runtime components:

- `openSMT::comm::MessageBus`: local and UDP-capable module communication
- `openSMT::modules::IModule`: stand-alone module interface
- `openSMT::modules::CommunicationMonitorModule`: first module in startup order
- `openSMT::modules::DeviceDriverRuntimeModule`: loads hardware and real device drivers from config
- `openSMT::App`: central orchestrator

## Build

```bash
cmake -S . -B build
cmake --build build
```

## Run

```bash
./build/openSMT_c_app
```

`openSMT_c_app` stays alive until it receives a stop message addressed to module `appmain`.

Send a stop message from the separate terminal executable:

```bash
./build/appterm
```

Send XY homing command from a standalone executable:

```bash
./build/appHomeXY
```

Optional arguments for `appHomeXY`:

```bash
./build/appHomeXY config/config.json moveXY
```

Send Z1..Z4 homing commands from a standalone executable:

```bash
./build/appHomeZ
```

Move XY from command line:

```bash
./build/appMoveXY [-x <x-position>] [-y <y-position>] [-f <speed>]
```

- default speed: `25000`
- at least one of `-x` or `-y` is required

Move one Z axis from command line:

```bash
./build/appMoveZ -a <axisno> -z <z-position> [-f <speed>]
```

- `axisno` in `1..4`
- default speed: `5000`

Disable all steppers on all motion boards:

```bash
./build/appDisableAll
```

Rotate one R axis from command line:

```bash
./build/appRotZ -a <axisno> -d <angle-0..360> [-f <speed>]
```

- `axisno` in `1..4`
- default speed: `5000`

Move axes relatively using appmain global position store:

```bash
./build/appMoveR [-x <dx>] [-y <dy>] [-z1 <dz>] [-z2 <dz>] [-z3 <dz>] [-z4 <dz>] [-r1 <dr>] [-r2 <dr>] [-r3 <dr>] [-r4 <dr>] [-f <speed>]
```

- default speed: `5000`
- at least one axis delta is required
- queries `appmain` for current X, Y, Z1..Z4, R1..R4 and converts relative deltas to absolute motion commands

Query current global positions from appmain:

```bash
./build/appGetPos [config/config.json]
```

- prints current global axis store (`X`, `Y`, `Z1..Z4`, `R1..R4`)
- prints configured reference positions (`posCalib`, `posCalibSec`, `posPark`, `posCamBot`, `posDiscard`, `posChange`)
- prints current global actor values from appmain unique store

Query current global actor states from appmain:

```bash
./build/appGetActors [config/config.json]
```

- always prints numeric value (`0..255`)
- for binary actors also prints interpreted state (`on`/`off`)

Operate configured actor value:

```bash
./build/appActor -a <actorId> -v <value|on|off>
```

or shorthand:

```bash
./build/appActor <actorId> <value|on|off>
```

- actor translation table is loaded from `config/runtime/actors.json`
- actor command is translated to Marlin `M114 P<index> S<value>`

Move to configured special XY position:

```bash
./build/appGoto <positionName> [-f <speed>]
```

- known position names: `posCalib`, `posCalibSec`, `posPark`, `posCamBot`, `posDiscard`, `posChange`
- sends one absolute `moveXY` command to the configured X/Y of the selected special position

Optional arguments for `appterm`:

```bash
./build/appterm config/config.json "maintenance-stop"
```

Optionally provide a custom config root file:

```bash
./build/openSMT_c_app config/config.json
```

## Configuration

- Root file: `config/config.json`
- The root file references smaller child files in `project/`, `io/`, `placer/`, and `runtime/`.
- During build, CMake copies `config/` beside the executable as `build/config/`.

Runtime hierarchy:

- `runtime/communication.json`: communication listener and remote routes
- `runtime/modules.json`: modules in startup order
- `runtime/hardware-drivers.json`: hardware driver definitions
- `runtime/device-drivers.json`: human-readable device-to-hardware mapping
- `runtime/actors.json`: actor id to driver/channel/value mapping table

The first entry in `runtime/modules.json` is the communication monitor module.

Stop control message frame:

- `destinationModule`: `appmain`
- `sourceModule`: `appterm`
- `payloadType`: `app-stop-request`
- `payloadJson`: `{ "command": "stop", "reason": "..." }`

Position query message frame:

- `destinationModule`: `appmain`
- `payloadType`: `app-position-query`
- `payloadJson`: `{ "command": "get-positions", "reason": "..." }`

Position reply payload type:

- `app-position-reply`
- includes current axis store (`x`, `y`, `z1..z4`, `r1..r4`), configured references (`posCalib`, `posCalibSec`, `posPark`, `posCamBot`, `posDiscard`, `posChange`), and current actor values

The app control message schema and constants are centralized in `include/openSMT/control/AppControlMessage.hpp`.

## Device Driver Framework

Base virtual class:

- `include/openSMT/drivers/DeviceDriver.hpp`

Implemented concrete driver:

- `include/openSMT/drivers/GenericDeviceDriver.hpp`

Hardware driver layer:

- `include/openSMT/hw/IHardwareDriver.hpp`
- `include/openSMT/hw/SerialHardwareDriver.hpp`
- `include/openSMT/hw/CanHardwareDriver.hpp` (placeholder)
- `include/openSMT/hw/MarlinSerialProtocolAdapter.hpp`

All real device drivers subscribe to their own module destination and only process `device-command` frames targeted to them.
Reply routing supports:

- source-only reply (`replyMode = "source"`)
- broadcast reply (`replyMode = "broadcast"`)

Supported driver actions:

- `homeXYAB`
- `disableAllSteppers`
- `moveXY` (`xPos`, `yPos`, `speed`)
- `moveZ` (`zPos`, `speed`)
- `rotate` (`relativeRotation`)
- `operateActorDigital` (`channel`, `state`)
- `operateActorAnalog` (`channel`, `value`)
- `readSensorDigital` (`channel`)
- `readSensorAnalog` (`channel`)
- `readVersionNumber`

Marlin serial protocol mappings:

- `homeXYAB` -> `G28 X Y A B`
- `disableAllSteppers` -> `M18 ...` depending on driver group
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

Rotation mapping for drivers:

- `rotR1` -> axis `A` on hardware `marlinAB`
- `rotR2` -> axis `B` on hardware `marlinAB`
- `rotR3` -> axis `A` on hardware `marlinCD`
- `rotR4` -> axis `B` on hardware `marlinCD`

Disable-all mapping:

- `moveXY` -> `M18 X Y` to `marlinXY`
- `moveZ1`/`moveZ2`/`rotR1`/`rotR2` -> `M18 X Y A B` to `marlinAB`
- `moveZ3`/`moveZ4`/`rotR3`/`rotR4` -> `M18 X Y A B` to `marlinCD`

`rotate` uses `defaultRotateSpeed` from `runtime/hardware-drivers.json` unless a speed is provided in command payload.

Serial transport behavior (Marlin-focused):

- opens configured port in 8N1 mode (`baudRate`, `dataBits`, `parity`, `stopBits`)
- writes each G-code command as one line with `\n`
- reads response lines until terminal status
- terminal success: line starts with `ok`
- terminal failure: line starts with `Error:` or `error:`
- informational lines like `busy:`, `echo:`, and `start` are accepted as non-terminal
- timeout controlled via `serialResponseTimeoutMs` in `runtime/hardware-drivers.json`
