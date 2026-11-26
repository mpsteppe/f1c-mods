# LCD Display Modes, Car Status Damage Rendering, and Mid-Race Setup Parameters

- **Date**: 2026-11-23
- **Target**: `F1 Challenge Online.exe` (`ImageBase = 0x00400000`, 3,137,536 bytes)
- **Evidence Sources**:
  - Direct Capstone 32-bit x86 disassembly of `F1 Challenge Online.exe`
  - Ghidra decompilation (`F1 Challenge Online.exe.c`)

All addresses, offsets, constants, and calling conventions below are **VERIFIED** against the binary unless explicitly marked **INFERRED**.

---

## 1. LCD Mode State Machine & Input Handling (`onscreen.cpp`)

### 1.1 Per-Frame Update
- **`FUN_0049f070` (`0x0049F070`)**: Per-frame HUD/LCD update dispatcher.
  - Calls `FUN_0049f790` (`0x0049F790`, `this = 0x007CE048`) to process LCD input latches.
  - Calls `FUN_004d8450` (`0x004D8450`, `this = 0x008AD298`) to update 2D HUD overlays.
  - Calls `FUN_0049d070` (`0x0049D070`, `this = 0x007CE058`) to render the steering wheel LCD.

### 1.2 State Globals
- `0x007CE048` (`int32`): Active LCD Mode:
  - `0`: Lap Times Display
  - `1`: Pit Stop Menu
  - `2`: Car Status
  - `3`: Driver Aids
  - `4`: Engine / Brake Temps
- `0x007CE04C` (`int32`): Pit Stop Menu selected row (`0..4`).
- `0x007CF7AC` (`uint8`): `.PLR` `[GRAPHIC OPTIONS]` `LCD Display Modes` bitmask (`0..7`, default `3`):
  - Bit 0 (`0x01`): Mode 2 (Car Status)
  - Bit 1 (`0x02`): Mode 3 (Driver Aids)
  - Bit 2 (`0x04`): Mode 4 (Engine / Brake Temps)
- `0x008AD384` (`uint8`): Set by `FUN_0049f150` (`0x0049F150`) to `(mode == 1)`. Enables the 2D HUD Pit Menu overlay (`*(0x008AD298 + 0xD8)`) when outside cockpit camera (`*(int *)(*(int *)0x005EA528 + 0x34) != 1`).
- `0x007C54E0` (`F1CVehicle*`): Currently viewed vehicle pointer.
- `0x007C54E8` (`F1CVehicle*`): Player vehicle pointer.
- `0x007C54DC` (`int32`) / `0x007C54E4` (`int32`) / `0x007C54F0` (`F1CVehicle*[]`): Viewed slot, player slot, and vehicle pointer array. Viewed-is-player check compares `*(int *)(VehiclePtrArray[viewedSlot] + 0x4894) == *(int *)(VehiclePtrArray[playerSlot] + 0x4894)`.

### 1.3 Input Edge Latches (`FUN_0049f790` at `0x0049F790`)
- Prologue: `A1 34 C2 7C 00 85 C0 74 68`
- `0x007CC234`: LCD Mode cycle. In cockpit/TV-cockpit (`*(int *)(0x005EA518) == *(int *)(0x005EA528)` and camera mode `1` or `2`), increments mode `0..4` skipping disabled bits in `0x007CF7AC` and wrapping `>= 5` back to `0`. Outside cockpit, toggles between `0` and `1`. Calls `FUN_0049f150` (`0x0049F150`).
- When `mode == 1` (`0x007CE048 == 1`):
  - `0x007CC28C`: LCD Up -> `FUN_0049f170(1)` (`0x0049F170`)
  - `0x007CC2B8`: LCD Down -> `FUN_0049f170(0)` (`0x0049F170`)
  - `0x007CC2E4`: LCD Increase -> `FUN_004a5750(row)` (`0x004A5750`, `this = playerVeh + 0x32CC`)
  - `0x007CC310`: LCD Decrease -> `FUN_004a57a0(row)` (`0x004A57A0`, `this = playerVeh + 0x32CC`)

### 1.4 Pit Service Hold (`FUN_004a6200` at `0x004A6200`)
- While `*(int *)0x007CE048 == 1`, sets `*(uint8_t *)(pit + 0x190) = 0` so pit service waits until the player exits the Pit Stop Menu. Other modes (including a future Mode `5`) do not block pit service.

---

## 2. Steering Wheel Display (`0x007CE058`) & Drawing Primitives

### 2.1 Object Layout (`this = 0x007CE058`)
- `+0x04` (`0x007CE05C`): `FONTREAD.BMP`
- `+0x08` (`0x007CE060`): `FONTLCD.BMP`
- `+0x0C` (`0x007CE064`): `FONTAIDS.BMP`
- `+0x14` (`0x007CE06C`): `LCD00.BMP` material pointer; render target surface is `*(void **)(*(uintptr_t *)(this + 0x14) + 0x54)`
- `+0x18` (`0x007CE070`): `LCD00.BMP` background bitmap (Modes 0, 1, 3, 4)
- `+0x1C` (`0x007CE074`): `LCD01.BMP` background bitmap (Mode 2: Car Status)

### 2.2 Screen Dispatcher (`FUN_0049d5d0` at `0x0049D5D0`)
- Dispatches via jump table at `0x0049D6D4` (index table `0x0049D6AC`), calling:
  - Mode 0: `0x0049D8B0` (Lap Times)
  - Mode 1: `0x0049DA60` (Pit Stop Menu, calling `0x0049F1C0`)
  - Mode 2: `0x0049DE40` (Car Status)
  - Mode 3: `0x0049DB80` (Driver Aids)
  - Mode 4: `0x0049E3E0` (Engine / Brake Temps)

### 2.3 Verified Drawing Primitives
- **`FUN_005432f0` (`0x005432F0`)**: Blits background bitmap (`+0x18` or `+0x1C`) onto target surface.
- **`FUN_00543440` (`0x00543440`)**: Filled rectangle.
  - Prologue (`7 bytes`): `56 8B F1 8D 44 24 14` (`push esi; mov esi, ecx; lea eax, [esp + 0x14]`)
  - Signature: `void __thiscall FillRect(void *surface, int x, int y, int w, int h, uint32_t argbColor)` (`ret 0x14`).
- **`FUN_0049f6a0` (`0x0049F6A0`)**: Text draw (`__thiscall` on `0x007CE048`: `(const char *str, int x, int y, void *font, void *surface, int align)` where `align` is `0` = left, `1` = right, `2` = center).
- **`FUN_00553a20` (`0x00553A20`)**: 2D line segment (used in `0x0049F1C0` for Pit Menu row highlight).

---

## 3. Car Status Screen (`FUN_0049de40` at `0x0049DE40`) & Damage Mechanics

### 3.1 Vanilla Car Status Behavior (`0x0049DE40`)
- Prologue (`8 bytes`): `83 EC 10 53 55 56 8B F1` (`sub esp, 0x10; push ebx; push ebp; push esi; mov esi, ecx`)
- Signature: `void __thiscall RenderCarStatus(void *this_0x7CE058, int display_off)` (`ret 4`).
  - **Important**: `FUN_0049de40` reads `display_off` as an 8-bit `bool` (`0x0049DE84: mov al, byte ptr [esp + 0x24]; test al, al`), and callers push 32-bit registers whose upper 24 bits contain leftover data (e.g. `0x007CE000` observed live in `F1C_CMT.exe`). Any hook must mask `(display_off & 0xFF)`.
- When `display_off == 0`:
  1. Blits `LCD01.BMP` (`*(this + 0x1C)`) onto `surface`.
  2. **Front Wing** `(x=195, y=26, w=18, h=53)` (`0x0049DEC2..0x0049DF20`):
     - Checks only `*(int *)(veh + 0xCD4) != 0` and `*(uint8_t *)(veh + 0xD14) != 0`.
     - Intact -> solid Green (`0xFF00FF00`). Detached -> 1 Hz blinking Red (`0xFFFF0000` when `((int)SessionTime & 1) == 0`).
     - **Bug**: Never reads aerodynamic front wing damage.
  3. **Rear Wing** `(x=42, y=35, w=14, h=35)` (`0x0049DF21..0x0049DF7C`):
     - Checks only `*(int *)(veh + 0xD18) != 0` and `*(uint8_t *)(veh + 0xD58) != 0`.
     - Intact -> solid Green (`0xFF00FF00`). Detached -> 1 Hz blinking Red (`0xFFFF0000`).
     - **Bug**: Never reads aerodynamic rear wing damage.
  4. **Tires & Wear** (`0x0049E570`, `0x0049E7B0`): Renders 4 tire temps and 4 wear bars.
  5. **Undertray Plank** `(x=59, y=49, w=46, h=6)` (`0x0049E011..0x0049E06D`):
     - If viewed vehicle is player, checks and clears `*(uint8_t *)0x007C8B40` (1-frame bottoming latch). Draws Yellow (`0xFFFFFF00`) on bottoming, else Green (`0xFF00FF00`).
  6. **Center Chassis / Body** `(x=87, y=45, w=16, h=13)` (`0x0049E06E..0x0049E085`):
     - Pushes hardcoded `0x0000FF00` (`push 0xff00`) — **never checks any damage variable**.
  7. **Fuel & Pit Lap** (`y=105`): Renders fuel and pit lap text.

### 3.2 Exact Aero & Chassis Damage Accumulators and Max Caps
Verified from `FUN_004ec1a0` (`0x004EC1A0`), `FUN_0043e890..0x0043ee00`, `FUN_004f1860` -> `0x0043B930` (pit front wing adjustment), `FUN_004ed0f0` (`0x004ED0F0`), and live Frida inspection of `F1C_CMT.exe`.
Note ISI's longitudinal Z-axis sign convention: `Z < -1.0` is the **front** of the car (`0x0043EBD0` -> `0x0043E890` Front Wing + `0x0043E980` Diffuser), while `Z >= 1.0` is the **rear** of the car (`0x0043EC50` -> `0x0043EAE0` Rear Wing + `0x0043EA70` Body).

| Component | Physics Base | Current Value | Clean Baseline | Max Delta Cap | Pit Repair Behavior |
|---|---|---|---|---|---|
| **Front Wing Drag** (`0x0043E890`) | `0x007C7428` | `0x007C8BD0` (`+0x17A8`, `double`) | `0x007C9030` (`+0x1C08`, `double`) | `+0.25` (`0x0059EB50`) | Reset to baseline (`0x0043EC80`) |
| **Front Wing Lift** (`0x0043E890`) | `0x007C7428` | `0x007C8BE8` (`+0x17C0`, `double`) | `0x007C9038` (`+0x1C10`, `double`) | `+0.20` (`0x0059EB28`) | Reset to baseline (`0x0043EC80`) |
| **Rear Wing Drag** (`0x0043EAE0`) | `0x007C7428` | `0x007C8C00` (`+0x17D8`, `double`) | `0x007C9068` (`+0x1C40`, `double`) | `+0.25` (`0x0059EB50`) | Reset to baseline (`0x0043EC80`) |
| **Rear Wing Lift** (`0x0043EAE0`) | `0x007C7428` | `0x007C8C18` (`+0x17F0`, `double`) | `0x007C9070` (`+0x1C48`, `double`) | `+0.20` (`0x0059EB28`) | Reset to baseline (`0x0043EC80`) |
| **Diffuser Drag** (`0x0043E980`) | `0x007C7428` | `0x007C8C30` (`+0x1808`, `double`) | `0x007C9048` (`+0x1C20`, `double`) | `+0.25` (`0x0059EB50`) | Clamped to `<= baseline + 0.15` (`0x0059EC60`) |
| **Diffuser Lift** (`0x0043E980`) | `0x007C7428` | `0x007C8C40` (`+0x1818`, `double`) | `0x007C9050` (`+0x1C28`, `double`) | `+0.20` (`0x0059EB28`) | Clamped to `<= baseline + 0.12` (`0x0059EC90`) |
| **Body Drag** (`0x0043EA70`) | `0x007C7428` | `0x007C8FE8` (`+0x1BC0`, `double`) | `0x007C9058` (`+0x1C30`, `double`) | `+0.40` (`0x0059EC80`) | Clamped to `<= baseline + 0.25` (`0x0059EB50`) |
| **General Vehicle Aero** (`0x004ED0F0`, all cars) | `veh` | `veh + 0x3A78` (`float`) | `veh + 0x3A84` (`float`) | `+0.45f` (`0x0059E8FC`) | Halved toward baseline (`0x004ED030`) |

### 3.3 Suspension / Wheel Bend & Detachment (`FUN_00442060`, `FUN_00439210`, `FUN_0049e570`)
- **Wheel Attachment (`0x0049E570`, all cars)**:
  - For corner `i = 0..3` (`0 = FL`, `1 = FR`, `2 = RL`, `3 = RR`), the vehicle struct holds a `0x128`-byte block at `veh + 0x834 + i * 0x128`:
    - `+0x00` (`uint32_t`): wheel record pointer (non-zero if wheel exists)
    - `+0x40` (`uint8_t`, `veh + 0x874 + i * 0x128`): intact flag (`0` = detached)
- **Player Suspension / Wheel Bend (`0x00442060`, `this = 0x007C7428`)**:
  - For corner `i = 0..3`, wheel physics block is `0x007C7428 + 0x28C + i * 0x464`:
    - `+0x1C4` (`uint8_t`): detached flag
    - `+0x1C5` (`uint8_t`): bent flag (`1` on first bend impact)
    - `+0x1D0` / `+0x1D8` (`double`): toe/camber deflection (radians), clamped to `±0.1745329252` rad (`±10°`)
    - `+0x1E0` (`double`, `0x007C7894 + i * 0x464`): cumulative bend magnitude in radians, initialized to `0.0` in `FUN_00439210` (`0x00439210`), incremented by `norm_severity * 0.0872664626` rad (`0°..5°` per impact), flagging suspension damage (`veh + 0x4068 = 4`) above `0.015` rad.
- **`LCD01.BMP` Car Silhouette Pixel Map (`256 x 128`, extracted from `cmaps.mas`)**:
  - Sidepods: `x = 78..128`, top black outline `y = 25`, bottom black outline `y = 78`, center plank/diffuser `y = 44..59`.
  - Front tires: `x = 164..188`, `y = 15..31` (FL) and `y = 73..89` (FR); nose outline `y = 46..57` at `x = 173..179`.
  - Rear tires: `x = 48..72`, `y = 15..31` (RL) and `y = 73..89` (RR); rear body outline `y = 45..58` at `x = 59..65`.

---

## 4. Target Mid-Race Adjustable Physics Parameters (`FUN_0043c250` at `0x0043C250`)

Verified from `FUN_0043c250(this = 0x007C7428, setupStruct, hdvEngineStruct)`:

1. **Engine Rev Limit**: Setup block `+0x1570` (`double base, double step, int count, int setting` at `+0x1584`, scaled by `2π/60`). Uses `RevLimitLogic` at `*(double *)(hdv + 0x6BC)`. Writes `0x007C9104` (`+0x1CDC`), `0x007C910C` (`+0x1CE4`), `0x007C9114` (`+0x1CEC`), `0x007C90BC` (`+0x1C94`), and float mirror `veh + 0x39A4`.
2. **Engine Brake Map**: Setup block `+0x15F0` (`setting` at `+0x1604`). Writes `0x007C9078` (`+0x1C50`, `((setting * step) + base) * 0.09549296585513721`).
3. **Differential Pump**: Setup block `+0x1470` (`setting` at `+0x1484`). Uses `DiffPumpTorque` at `*(double *)(hdv + 0x13C8)`. Writes `0x007C9240` (`+0x1E18`).
4. **Differential Power (Accel)**: Setup block `+0x14B0` (`setting` at `+0x14C4`). Writes `0x007C9248` (`+0x1E20`).
5. **Differential Coast (Decel)**: Setup block `+0x14F0` (`setting` at `+0x1504`). Writes `0x007C9250` (`+0x1E28`).
6. **Differential Preload**: Setup block `+0x1530` (`setting` at `+0x1544`). Writes `0x007C9258` (`+0x1E30`).

### 4.1 Stack Allocation Lifecycle & Hook Prologues (Phase 2)
- **`FUN_0043e050` (`0x0043E050`)**: Allocates temporary stack buffers `stack_hdv` (`[esp + 0x14]`) and `stack_setup` (`[esp + 0x1A7C]`), parses `.HDV` and active setup, calls `FUN_0043c250(0x007C7428, stack_setup, stack_hdv)` at `0x0043E168`, then destroys both stack buffers on exit (`0x0043E20C`). Meanwhile, `0x007C56C0` is only populated when entering the Garage UI (`FUN_00433ed0`). Therefore, hooking `FUN_0043c250` (`0x0043C250`, 6-byte prologue `55 8B EC 83 E4 F8`) captures the authoritative `setupStruct` and `hdvStruct` on every track entry.
- **`FUN_0042f470` (`0x0042F470`)**: Formats garage/setup display strings:
  - Case `0x27` (Rev Limit): `round(raw * 9.549296585513721)` -> `"%d,%03d"`
  - Case `0x29` (Engine Brake): `"%d"` (`setting`)
  - Case `0x26` (Diff Preload): `"%d"` (`setting + 1`)
  - Cases `0x23..0x25` (Diff Pump/Power/Coast): `round(raw * 100.0)` -> `"%d%%"`
- **LCD Hooks**:
  - `FUN_0049f790` (`0x0049F790`, 7-byte prologue `A1 34 C2 7C 00 85 C0`)
  - `FUN_0049d5d0` (`0x0049D5D0`, 8-byte prologue `A1 48 E0 7C 00 83 F8 04`)
  - `FUN_005531f0` (`0x005531F0`, `__thiscall` on font: `(const char *str, int *out_w, int *out_h, int use_scale)`)
  - `FUN_005432f0` (`0x005432F0`, `__thiscall` on bitmap `*(this_lcd + 0x18)`: `(0, 0, surface, 0, 0, 1.0f, 1.0f, 0, 0)`)

---

## 5. Additional `.HDV` / `ENGINE.INI` Parameters & `EngineMap` Architecture

Verified from `FUN_004f4270` (`0x004F4270`), `FUN_004f3c10` (`0x004F3C10`), `FUN_0043c250` (`0x0043C250`), `FUN_0043ef20` (`0x0043EF20`), and `FUN_00443b70` (`0x00443B70`):

- **Cockpit-Adjustable `.HDV` Controls on `0x007C7428`**:
  - **Brake Balance (Rear Bias)**: `RearBrakeRange` -> `*(double *)(0x007C7428 + 0x155C)` (used in `0x00446A89` every physics tick).
  - **Brake Pressure**: `BrakePressureRange` -> `*(double *)(0x007C7428 + 0x1554)` (used in `0x00446C1F` every physics tick).
  - **Traction Control (`TractionControlGrip`, `TractionControlLevel`)**: Live doubles at `+0x150..+0x170` (`0x007C7578..0x007C7598`) used in `0x0044691A` when TC aid is active.
  - **ABS (`ABS4Wheel`, `ABSGrip`, `ABSLevel`)**: Live bool + doubles at `+0x178..+0x198` (`0x007C75A0..0x007C75C0`) used in `0x00446C20+` when ABS aid is active.
  - **Steering Lock**: `SteerLockRange` -> `*(double *)(0x007C7428 + 0x1438)` (FL) and `+0x1440` (FR).
  - **Front/Rear Anti-Roll Bars (`FrontAntiSwayRange`, `RearAntiSwayRange`)**: Converted during `FUN_0043c250` via `FUN_004f6850`/`FUN_004f6880` into wheel/suspension stiffness; **INFERRED** mid-race ARB adjustment would require updating suspension stiffness/sway terms directly.
- **Dead `EngineMap` Parameter & Fuel Map Design**:
  - `EngineMapRange` / `EngineMapSetting` is parsed into `setupStruct + 0x15B0` (`setting` at `+0x15C4`, `FUN_0042f470` case `0x28`), but `FUN_0043c250` never reads `+0x15B0..+0x15EC`.
  - **Torque Table**: `FUN_0043c250` builds a `32 rad/s`-spaced lookup table at `*(float **)(0x007C7428 + 0x1CFC)` with `*(int *)(0x007C7428 + 0x1D08)` entries, each 8 bytes `(float base_torque, float delta_torque_throttle_minus_coast)`.
  - **Fuel Burn (`FUN_0043ef20`)**: `fuel_burn = *(double *)(0x007C7428 + 0x1AC) * throttle * fabs(eng_rpm)`.
  - **Heat Generation (`FUN_0043ef20`)**: `CombustionHeat` at `0x007C9184` (`0x007C7428 + 0x1D5C`) and `EngineSpeedHeat` at `0x007C918C` (`0x007C7428 + 0x1D64`) drive oil temperature rise each tick (which transfers to water via `OilWaterHeatTransfer`).
  - Scaling `*(double *)(0x007C7428 + 0x1AC)` (`FuelConsumption`), `0x007C9184`/`0x007C918C` (heat), and the positive-torque entries in `*(float **)(0x007C7428 + 0x1CFC)` relative to the middle setting (`Map 3` of `1..5`) implements a Fuel Mixture `EngineMap` modifier.

---

## 6. AI Dynamic Engine Map, Extra Race Fueling & Lifetime Preservation

Verified from `FUN_004e5f60` (`0x004E5F60`), `FUN_00429960` (`0x00429960`), and `FUN_004ed960` (`0x004ED960`):

- **AI Vehicle Engine State (`veh` in `0x007C54F0[]`, `*(int *)(veh + 0x32C8) == 1`)**:
  - `*(float *)(veh + 0x3990)`: AI fuel consumption rate (used in `0x004ED960` to burn `*(float *)(veh + 0x3984)`).
  - `*(float **)(veh + 0x39B0)` / `*(int *)(veh + 0x39BC)`: AI `32 rad/s` torque lookup table (`[coast, power_delta]` pairs).
  - `*(float *)(veh + 0x3D70)` / `*(float *)(veh + 0x3D54)`: AI engine lifetime countdown timer (seconds) and smoking latch (`-789.0f`).
- **AI Pit Strategy (`0x00429960`)**:
  - `*(int *)(veh + 0x4588)`: Total planned pit stops; `*(float *)(veh + 0x45E8)`: estimated fuel per lap; `*(float *)(veh + 0x45F0)`: initial fuel load.
  - Scaling `fuel_per_lap`, `initial_fuel`, and planned stint fuel loads in `0x00429960` automatically carries into `0x0042A3D0` mid-race pit refueling.

### 6.1 AI Yielding Rule (Team Orders & Blue Flags)

Verified in `FUN_00424800`, `FUN_00425140`, `FUN_0042a7d0`, and `FUN_0042af70`:
- `veh + 0x45F8`: Teammate `F1CVehicle*`.
- `veh + 0x45FC`: Transient per-frame yielding target pointer (cleared at the end of `FUN_00425140`).
- `veh + 0x48A8`: Driver order / team priority rank (`teammate + 0x48A8 < veh + 0x48A8` triggers team orders when `completed_laps > 3`).
- `other + 0x403C > veh + 0x403C` within `25-30m` triggers blue-flag lapping yield.

When any of these yielding conditions is active in `esh_ai_compute_gaps`, `EshAiContext.is_yielding` is set to `1` and `esh_ai_decide_engine_map()` drops the AI car to `0` (`Map 1`, lowest setting) so the passing car gets by cleanly.

## 7. SVM `EngineBrakeMapSetting` Loader Fix

Verified in `F1 Challenge Online.exe` / `F1C_CMT.exe`:
- `0x004302AF` writes Engine Brake Map to `.svm` files using `"EngineBrakeMapSetting=%d\n"` (`0x005C3A50`) from `*(int *)(setup_struct + 0x1604)`.
- `FUN_004f3c10` (`0x004F3C10`, called from `0x004F421B` and `0x004F5D8C`) calls `FUN_0042e1f0` (`0x0042E1F0`) on `setup_struct + 0x15F0` looking only for `"EngineBrakingMapRange"` (`0x005D54B4`) and `"EngineBrakingMapSetting"` (`0x005D54CC`).
- Because `.svm` files use `"EngineBrakeMapSetting"` (without `"ing"`), `.svm` loads previously ignored the saved Engine Brake Map and kept the `.hdv` / `ENGINE.INI` default.
- Hooking `0x004F3C10` (`esh_parse_engine_settings_hook`) to additionally call `FUN_0042e1f0(setup_struct + 0x15F0, parser, NULL, "EngineBrakeMapSetting", 0, 0x3FF00000)` after the original function loads and clamps the `.svm` setting into `[0, count - 1]`.
