# Shift Sound Reset (in development)

Product id (proposed): `shift_sound_reset`. Status: **owner test passed**
(2026-10-26), not yet released. 0.1.0 failed on the player car (rule 17
disproven). 0.1.1 in `dll/` replaces the ownership rule (rule 18) and passed
three in-game runs: alone, with 4 AI cars, and a full race with the teammate
(rules 19-20). 0.1.2 re-reads the `.sfx` files at each session start (rules
21-22) and passed the owner's test on the 1990 C&D install; 0.1.3 turns the log
off by default. Remaining before release: see "Open items".

## Objective

When the car heard as the main car changes gear (up or down), restart its
engine sound samples from their beginning, with a configurable fade-in in
milliseconds. Cockpit and outside sounds. The main car is the one the camera
is on: the player's own car, or another car the player switches to with +/-
in game (owner clarification 2026-10-26: a viewed AI car's cockpit and
outside engine sounds play exactly like the player's). All other cars are
left untouched.

Problem it solves: modern sound packs use long engine recordings (several
seconds) as the engine layers. The game loops each layer continuously and
only changes its pitch with RPM, so a recording reaches its end in the middle
of a gear and jumps back to its start.

Non-goals: sounds of any car other than the one being watched, changing pitch
or volume curves, editing sound files.

Acceptance:
- every gear change of the watched car restarts its voiced engine layers:
  **met** (player 84/84 and 190/190 with restarts in 189; viewed AI cars
  59/62 and 181/182, misses explained in rules 19-20);
- no other car's sound is restarted, even with the same sound files and
  alongside: **met** (owner test, full race, rule 20);
- no audible click at the configured fade: **met** at `FadeInMs` 30 (owner);
- with `Enabled=0` the game sounds exactly as without the mod: by design
  (nothing is installed), not tested in game;
- survives 3 race loads in one process: not tested on purpose.

## Evidence so far (2026-10-26)

| # | Claim | Tier | Source |
|---|---|---|---|
| 1 | In the owner's sound pack (`Audio\Vehicle\int\Ford`), engine layers are 4.7-7.5 s stereo 44.1 kHz recordings (`l1` 4.67 s, `m1` 6.59 s, `h1` 7.45 s). | FACT (file measurement) | WAV headers on the owner's install |
| 2 | A car's `f1sounds.sfx` declares five engine layers per view, `VS_INSIDE_ENGINE_1..5` and `VS_OUTSIDE_ENGINE_1..5`, plus `VS_*_SHIFT_UP/DOWN_1..3`. | FACT (file content) | `SeasonData\Vehicles\Arrows\2002_A23\f1sounds.sfx` |
| 3 | The executable imports from Mss32.dll: `AIL_set_sample_position`, `AIL_set_sample_playback_rate`, `AIL_set_sample_loop_count`, `AIL_set_sample_file`, `AIL_set_sample_volume_levels`, `AIL_file_read` and others; no 3D-sample API. Mss32.dll exports `AIL_sample_ms_position` and `AIL_set_sample_ms_position`. | FACT (import/export tables) | objdump of `F1 Challenge Online.exe` (co-op variant `DE151845...`) and `Mss32.dll` |
| 4 | All sample calls sit in one module at 0x404000-0x408000. `AIL_set_sample_playback_rate` has exactly one call site (0x405CEE). | DERIVED (static) | disassembly |
| 5 | 0x405B90 is the per-sound-object update (thiscall, esi = object). It computes listener distance, sets volume/pan (0x405CC1), sets pitch = `[[obj+0x54]+0x8C] * [obj+0x48] * [obj+0x44]` (0x405CEE), and when flag `[obj+0x18] & 0x10` is set it starts the sample at a random position (`[obj+0x4C]` x random table at 0x8AAE74, `AIL_set_sample_position` at 0x405D32) before `AIL_resume_sample`. | DERIVED (static) | disassembly |
| 6 | The vehicle sound parser at about 0x4F7500 compares each line with `VS_INSIDE_` / `VS_OUTSIDE_` and parses the remainder (`ENGINE_1=%s` ... ) into one of two sound sets. Nearby keys: `EngineRPMSoundInside`, `EngineRPMSoundOutside`, `playerEngineVolume*`, `non-playerEngineVolume*`. | DERIVED (static) | disassembly + strings 0x5D6A00-0x5D6D10 |
| 6a | The owner's Lightloader (Sep 2026 `dinput8.dll` with a separate `f1c_game_update_broker.dll`, broker ABI 15) refused probe 0.1.0, whose host check demanded Hub 2 (`LLP_CAP_HUB_2`, generation 2): LightLoader.log "FAILED protected plug-in initialization", no probe log written. The installed Pit Stop Failures 1.0.5, which loads, requires only magic, ABI >= 0x20000, pid, loader module, product id, `EXCLUSIVE_HOST` and generation >= 1. Probe 0.1.1 uses that contract. Inferred cause: this loader does not offer Hub 2. | FACT (refusal) / DERIVED (cause) | LightLoader.log 2026-10-26 18:45:30; disassembly of installed pitstop_failures.dll 1.0.5 |
| 7 | Probe 0.1.1 loaded under Lightloader v2.4.2 (other mods disabled): 11 imports hooked, both 0x405B90 call sites verified, broker ABI 15 player slot 0. | FACT (live) | LightLoader.log 2026-10-26 18:54:29; probe log |
| 8 | Miles sample handles are a small shared voice pool: one handle plays skid, TC, engine layers and shift sounds in turn (`AIL_set_sample_file` from 0x405991). The stable identity of a sound is the game's sound object (`esi` in 0x405B90): one object per sound file, e.g. `INT\FERRARI\MAX4.WAV` always object 0x25FC30D8 in that process. | FACT (live) | probe log F/H lines |
| 9 | Every sample file loads through `AIL_file_read`, so the full path (`AUDIO\VEHICLE\INT\FERRARI\MAX4.WAV`) is known per object. | FACT (live) | probe log F lines |
| 10 | The player's engine layers are the objects whose playback rate tracks engine speed: r = 0.94 (`INT MAX4`), 0.89 (`EXT MAX1`), 0.87 (`EXT L1`), 0.83 (`EXT H2`); skid/TC sounds are near 0 or negative; wind rush 0.77 (tracks speed). Layers seen: INT `IDLE4 L3 M4 H6 MAX4`, EXT `IDLE4 L1 M2 H2 MAX1`, matching five per view. | FACT (live) | probe H lines, 2 s windows |
| 11 | `[obj+0x4C]` holds the sample's data size in bytes (`MAX1` 4219024), i.e. the scale for the random-start position; `[obj+0x18]` flags were 0 for all vehicle sounds and the game never set a position on them (pos n=0). A layer starts from 0 whenever it is (re)bound to a voice. | FACT (live) / DERIVED (start-from-0: no position set after `AIL_set_sample_file`) | probe H/O lines |
| 12 | Engine samples play straight through gear changes: at 141 shifts the layers' positions were spread across the whole recording; for the same sample in consecutive shifts the position advanced 242x and jumped back 25x. Confirms the owner's report. | FACT (live) | probe G lines |
| 13 | Every shift is two gear changes in the same frame (0-16 ms apart): old gear -> 0 -> new gear. A shift must be detected as "landed in a non-neutral gear different from the last non-neutral gear". | FACT (live) | probe G lines (282 changes = 141 shifts, 71 up / 70 down) |
| 14 | `EngineRPM` (0x39A0) reads about 1450-1900 at shifts, consistent with rad/s (1850 rad/s = 17,700 rpm), not rpm. | DERIVED (live values + plausibility) | probe G lines |
| 15 | AI cars DO have engine sounds, outside layers only (`EXT\BMW`, `EXT\FORD`, `EXT\HONDA`, ...); cockpit (`INT`) layers exist only for the player (in that run no AI car was
viewed; a viewed AI car does get cockpit layers, rule 20). Every car has its own sound objects: 108 engine-layer objects in a full-grid session, and 37 files were played by more than one object (`EXT\FERRARI\MAX1` by 4: player, teammate and other cars using Ferrari sounds). A file name therefore cannot identify the player's sounds, as the owner warned (an `.sfx` can be copied to any car). | FACT (live) | probe run 2026-10-26 19:02, full grid |
| 16 | Each car's sound objects carry that car's world position at `+0x28/+0x2C/+0x30`: all ten player objects (INT and EXT) read the same (13.62, -7.50, 3.83) in one snapshot, other cars' objects are elsewhere. `+0x04` points into a per-car block (player 0x1AFBF2B8-0x1AFBF3D0, a teammate 0x240CEAF0-0x240CED20); `+0x5C` is shared sample data (all `EXT MAX1` objects 0x1AF3CAA8); `+0x54` the global listener 0x5EA440. | FACT (live snapshot) / DERIVED (meaning of +0x04/+0x5C) | probe O lines |
| 17 | ~~Sound position equals the car's `WorldPosition`, so `OwnerMeters` 1.0 identifies the car's sounds.~~ **DISPROVEN** by mod 0.1.0 in game: the player's engine sounds sat 1.2-1.3 m from the player's `WorldPosition` at all 91 shifts (none restarted); two AI cars' sat at 0.98-1.0 m (only 1-2 of 5 layers restarted). The sound point is offset from `WorldPosition` (probably the engine), by a car-dependent amount. Owner report: worked for AI cars, not for the player car. | DISPROVEN (live) | `shift_sound_reset_log.txt` 2026-10-26 19:24 |
| 18 | Replacement rule (0.1.1): all of a car's sound objects share one position (rule 16). The focus car's sound point is the closest engine-layer sound within `SearchMeters` (2.0) of its `WorldPosition`, tracked as an offset relative to the car and refreshed every 50 ms; a sound belongs to the focus car when it is within 0.25 m of that point. Independent of the per-car offset. | CONFIRMED (rows 19-20) | - |
| 19 | 0.1.1 in game (player + 4 AI, no teammate): restarts on 189/190 player shifts and 181/182 shifts of three viewed AI cars; owner reports it works on the player car and the AI cars. 1-3 layers restart per shift (only voiced layers; rule 11). The car-to-sound distance is not constant: player 1.39-1.52 m this run (1.2-1.3 m in the 0.1.0 run), AI 0.36-0.97 m. Non-matched engine sounds as close as 0.3-0.4 m to a viewed AI car were seen, which can only be that car's own layers: the 0.25 m match misses some layers. Inferred cause: sound positions and `WorldPosition` are updated at different moments, so the gap varies with speed. | FACT (results, distances) / DERIVED (cause) | `shift_sound_reset_log.txt` 2026-10-26, 0.1.1 |
| 20 | 0.1.1 in a full race with the teammate (same sound files possible): player 84/84 shifts restarted, viewed teammate (slot 2) 59/62. The 3 misses came together right after a view switch, with no engine sound of that car within 10 m (nearest 10-260 m), so nothing was playing to restart. Every shift restarted cockpit layers or outside layers, never both (all 434 shifts across both 0.1.1 runs): only the current view's set is voiced. Owner test: shifting while alongside the teammate never reset the other car's sound, in either direction, and no clicks at `FadeInMs` 30. Player sound point held 1.39-1.47 m from `WorldPosition`; nearest other engine sound 1.3 m. Rule 18 holds in practice. | FACT (results, owner report) / DERIVED (view-dependent voicing, cause of the 3 misses) | `shift_sound_reset_log.txt` 2026-10-26 19:56, 0.1.1 |
| 21 | Second install (1990 C&D, `F1C_CMT.exe` 3141632 bytes, Lightloader 2.4.4, Lamborghini engine sounds): the probe showed nothing unusual (all 8 engine files named in the car's `.sfx` and recognised by the mod's matcher, gear pattern n -> 0 -> m, rate tracks RPM at r 0.96-1.00, one shared sound position per car). With the mod 0.1.1 the first sessions logged no shifts and the owner heard no effect; after a restart it restarted 34 of 34 shifts (16 cockpit, 18 outside, sound point steady at 1.30-1.32 m). Cause, confirmed by the owner: the car's `.sfx` was being edited while the game was open, and the mod reads the `.sfx` files only once at game start (engine files found: 103 in the earlier runs, 105 after the edit), so layers named in the edited file were unknown to it. The earlier absence of any shift line is not explained by this and is assumed to be no gear changes in those sessions (unconfirmed). | CONFIRMED (cause: owner report + file timestamps + counts) / FACT (results) | probe log 2026-10-29 21:22; `shift_sound_reset_log.txt` 2026-10-29 21:35 |
| 22 | When the game parses a car's `.sfx` (static, `F1C_CMT.exe`, same layout as the SDK target): the parser 0x4F7460 (refs `VS_INSIDE_` 0x5D6770, `EngineRPMSoundInside`) is called only from 0x4F70C0 (which also refs `F1Sounds.sfx` 0x5D6788), called only from 0x4F8370, called only from `VehgenScnFileLoader` 0x4C6C40. That loader has three callers: `DynMan_Init` 0x42C1A0 (from `Game_Init` 0x42D6B0, site 0x42C30A), `NetCommPlayerSlotSync` 0x45FC50 (multiplayer car join, 0x45FE45) and 0x4B8DA0 (site 0x4B8F6B, purpose OPEN). `GameMainEntry` 0x45B200 loops: menus 0x42D5D0 -> `Game_Init` -> [session start 0x42D7A0 (calls `DynMan_Restart` 0x42C3B0, which does not reach the loader) -> frame loop 0x42D8A0]... -> teardown 0x42DA30 -> menus -> `Game_Init`. So the `.sfx` is parsed each time the player goes from the menus into a session, and not on a restart inside it. Matches the owner's experience (edits apply after leaving to the menus and loading again). Whether a session change within one event (e.g. practice to qualifying) goes through `Game_Init` is not checked. | DERIVED (static call graph) / CONFIRMED (owner experience) | objdump of `F1C_CMT.exe` 2026-10-29 |
| 23 | 0.1.2 in game (1990 C&D, owner): 107 shifts, 106 with restarts; the `.sfx` re-read ran at each of 3 session starts (13-14 ms, 0 new files in that run). Owner reports it works. | FACT (log) / CONFIRMED (owner) | `shift_sound_reset_log.txt` 2026-10-29 21:54, 0.1.2 |

All static addresses are for the co-op executable variant, which shares its
layout with the SDK target (same timestamp, image size and probe bytes; see
`../sdk_sdk_sandbox/multiplayer_ai_host/TARGET_COMPATIBILITY_EVIDENCE_2026-08-23.md`).

## Probe (`probe/`)

`shift_sound_probe.dll` observes only. It redirects 11 Mss32 import-table
entries to wrappers that call the original first, then record the call. No
game code bytes are patched. It writes `shift_sound_probe_log.txt` beside the
DLL (bounded to 8 MB; the previous run is kept as `_prev`).

- Player slot: broker vehicle snapshot `controller_role == 0`. Gear 0x39FC
  (FACT) and RPM 0x39A0 (CONFIRMED) are read from that slot.
- Per handle, every 2 s: file, WAV format, loop count, rate calls (min/max/last),
  correlation of rate with player RPM (`r_rpm`; player engine layers should be
  near 1.0), position sets with caller, start/stop counts, volume, and
  sound-object fields +0x14 +0x18 +0x44 +0x48 +0x4C.
- At every player gear change: each recently pitched sample's position and
  length in ms.
- Gates: image base, timestamp and image size; the two 0x405B90 call sites are
  byte-checked before esi is ever read as an object; import slots must still
  point at the real Mss32 exports (all-or-nothing).

Build: `probe\build.bat` (output `%TEMP%\shift_sound_probe`). Headless test:
`probe\thunk_test.c` checks the esi-capturing thunks keep the stack balanced,
preserve esi, pass arguments unchanged and read object fields through esi
(PASS, 1006 checks, 2026-10-26).

Ran twice in game 2026-10-26 (alone, then full grid); results in rules 7-16.
The probe and the mod redirect the same imports: install only one.

## Mod 0.1.3 (`dll/`)

Files for players: `shift_sound_reset.dll` and, optionally,
`shift_sound_reset.ini` (missing keys use the defaults), both in
`plugins\lightloader`. Needs Lightloader with a broker reporting vehicle
snapshots (ABI >= 11); without it only slot 0 is followed.

History: 0.1.0 used `OwnerMeters` 1.0 around the car's `WorldPosition`; in
game the player's sounds sat 1.2-1.3 m away and never matched (rule 17).
0.1.1 finds the car's sound point from the sounds themselves (rule 18).
0.1.2 re-reads the `.sfx` files at each session start (rules 21-22). 0.1.3
writes no log unless `Log=1` (owner request: logs off by default).

- Engine layers: at start-up, and again each time the player's car appears in
  a new session (the game re-parses the `.sfx` on every load from the menus,
  rule 22), the worker reads every `.sfx` under the game folder and collects
  the `VS_INSIDE_ENGINE_n` / `VS_OUTSIDE_ENGINE_n` file names. Names are only
  added, never dropped, and after an addition every voice re-checks its file. A voice plays an engine layer when the file loaded into it (from
  `AIL_file_read` + `AIL_set_sample_file`) ends with one of those names. On
  the owner's install: 46 `.sfx`, 460 engine lines, 189 distinct files;
  Ferrari INT/EXT and BMW layers recognised, skid/TC not (checked with the
  mod's own scanner, read-only, 2026-10-26).
- Focus car: broker session snapshot `spectated_vehicle_slot` (the camera
  target; the game's 0x7C54DC, documented as the spectated slot in
  `../sdk_f1cui_web_remote/README.md` #35 and already used by Pit Stop
  Failures as its audio listener car) when valid, else the player's slot
  (`controller_role == 0`). Polled every 100 ms.
- Focus car only (rule 18, `ssr_anchor_classify`): the focus car's sound
  point is the closest engine-layer sound within `SearchMeters` (2.0) of its
  `WorldPosition`, kept as an offset relative to the car and refreshed from
  the previous 50 ms window (never from a half-finished one, so a car
  alongside processed first cannot be adopted); a sound belongs to the focus
  car within `MatchMeters` (0.25) of it. The point is dropped after 500 ms
  without candidates and re-learned after a change of focus car. Limit: in a
  very fast spin (yaw > about 2 rad/s) the car's own sounds can drift out of
  `MatchMeters` for a moment and miss a restart.
- Shift: rule 13, on the focus car's gear. `Upshifts` / `Downshifts`
  switches. A change of focus car re-arms detection, so switching from a car
  in 5th to one in 3rd is not a shift.
- Restart and fade: everything happens in the redirected
  `AIL_set_sample_volume_levels` call from 0x405B90, which runs just before
  the pitch call for every audible sound each frame. The first time each of
  the focus car's engine-layer objects passes it within 200 ms of a shift, the
  mod calls `AIL_set_sample_ms_position(h, 0)` on its voice and scales the
  game's left/right levels from 0 to 1 over `FadeInMs` (linear). Layers
  without a voice need nothing (rule 11).
- Logging: off by default; no file is written or touched. With `Log=1` the
  mod writes `shift_sound_reset_log.txt` (1 MB cap, previous run kept as
  `_prev`) with start-up, session and focus-car lines and one line per shift:
  the number of cockpit/outside layers restarted, the largest distance of an
  owned sound from the car and the nearest other car's engine sound.
- Fails closed (nothing installed) on: `Enabled=0`, unknown executable
  layout, unexpected bytes at 0x405CC1, an import already redirected by
  another module (including the probe; do not install both), or a missing
  `AIL_set_sample_ms_position` export.
- Test: `dll\ssr_test.c` (run by `build.bat`): shift detection on the live
  gear pattern, fade, `.sfx` parsing, path matching, the sound-point rule
  (first window untrusted, adoption of the closest, 10 cm drift kept, a car
  alongside rejected, point dropped when sounds stop), and an end-to-end run
  through the real thunk at the distances measured in game (own sounds
  1.25 m from the car, a teammate alongside with the same file 1.88 m away)
  proving only the focus car's layers restart (not the teammate, not
  non-engine sounds), the fade ramps 0 -> 0.5 -> 1, switching the camera to
  the teammate is not a shift but its own shift then restarts its layers and
  leaves the player's alone, the stack stays balanced and esi survives; a
  car's `.sfx` edited on disk between sessions (new layer unknown before the
  re-read, recognised after it on a voice bound earlier, old names kept); and
  the `Log` setting (off without the key, with an old `LogShifts=1` ini, with
  an invalid value or without an ini) (218 checks).

Address inventory (MOD_STANDARD R6), all for the shared co-op/SDK layout:

| Address | Use | Check |
|---|---|---|
| 0x405CC1 / 0x405CC7 | `call [AIL_set_sample_volume_levels]` in 0x405B90 / its return address | bytes `FF 15 B8 E3 59 00` verified before install |
| Mss32 import slots | `AIL_file_read`, `AIL_set_sample_file`, `AIL_release_sample_handle`, `AIL_set_sample_volume_levels` | must equal the real Mss32 exports |
| 0x5EC6D8 + slot*0x48C0 | vehicle | slot from broker `spectated_vehicle_slot`, else `controller_role == 0` |
| veh+0x34E0 | WorldPosition x/y/z (read) | FACT |
| veh+0x39FC | CurrentGear (read) | FACT |
| obj+0x28 | sound object world position (read) | live probe; `VirtualQuery` before read |

## Open items (before release)

Against `sdk/docs/MOD_STANDARD.md`:
- R6: a test that the source's addresses match the inventory above.
- R5: the import redirections are not undone on unload.
- R7: no explicit multiplayer/replay decision (effect is local sound only).
- R9: 3 race loads in one process not tested on purpose.
- Without the broker the `.sfx` files are only read at game start (the
  session-start re-read needs the broker's player slot).
- Usage and crash reports (`f1c_usage.h`): not in this checkout; ask the
  maintainer.
- Meta `lightloader` is `>=2.0.0`; should be the oldest loader with broker
  ABI 11 vehicle snapshots (not verified).
- Promotion to `mods/shift_sound_reset/` (`f1c_mod.json`, `install.txt`),
  author field and where findings go: to agree with the maintainer.

## Lines for `findings.jsonl`

`sdk/findings.jsonl` comes from the corpus and is not linked in this checkout
(creating it here would block `setup_corpus.py`). Append these when it is:

```json
{"id":"engine-audio-sample-update-405b90","tier":"DERIVED","claim":"0x405B90 is the per-sound-object update (thiscall, esi=object): volume/pan at 0x405CC1, pitch = [[obj+0x54]+0x8C]*[obj+0x48]*[obj+0x44] via the only AIL_set_sample_playback_rate call (0x405CEE), random start position when [obj+0x18]&0x10 via AIL_set_sample_position at 0x405D32.","evidence":"static disassembly of the co-op exe variant","source":"sdk/research/dev_mods/shift_sound_reset/README.md"}
{"id":"engine-audio-sfx-parser-4f7500","tier":"DERIVED","claim":"The vehicle .sfx parser near 0x4F7500 routes VS_INSIDE_ and VS_OUTSIDE_ lines into two sound sets and parses ENGINE_1..5, SHIFT_UP/DOWN_1..3 and related keys.","evidence":"static disassembly + strings 0x5D6A00-0x5D6D10","source":"sdk/research/dev_mods/shift_sound_reset/README.md"}
{"id":"engine-audio-voice-pool-sound-objects","tier":"FACT","claim":"Miles sample handles are a shared voice pool reassigned between sounds; the stable identity is the sound object (esi in 0x405B90), one per sound file. All vehicle samples load through AIL_file_read. Player engine layers' playback rate correlates 0.83-0.94 with engine speed.","evidence":"shift_sound_probe 0.1.1 live run 2026-10-26, Lightloader 2.4.2, co-op exe variant","source":"sdk/research/dev_mods/shift_sound_reset/README.md"}
{"id":"engine-audio-layers-play-through-shifts","tier":"FACT","claim":"Engine layer samples keep playing through gear changes (141 shifts: positions spread over the whole recording, advancing 242x vs 25x back for consecutive shifts). Each shift is gear n -> 0 -> m within one frame.","evidence":"shift_sound_probe 0.1.1 live run 2026-10-26","source":"sdk/research/dev_mods/shift_sound_reset/README.md"}
{"id":"engine-audio-miles-imports","tier":"FACT","claim":"F1C imports AIL_set_sample_position/playback_rate/loop_count/file/volume_levels and AIL_file_read from Mss32.dll; no 3D sample API. The shipped Mss32.dll exports AIL_sample_ms_position and AIL_set_sample_ms_position.","evidence":"PE import/export tables","source":"sdk/research/dev_mods/shift_sound_reset/README.md"}
{"id":"engine-audio-sound-point-offset","tier":"FACT","claim":"All of a car's sound objects share one world position (obj+0x28/2C/30), but it is not the car's WorldPosition (veh+0x34E0): the player's sat 1.2-1.52 m away, AI cars' 0.36-1.0 m, varying between runs and with motion. Matching a car's sounds by distance from WorldPosition (1 m) fails for the player car.","evidence":"shift_sound_reset 0.1.0 and 0.1.1 live runs 2026-10-26","source":"sdk/research/dev_mods/shift_sound_reset/README.md"}
{"id":"engine-audio-view-dependent-layers","tier":"DERIVED","claim":"Only the current view's engine layer set is voiced for the watched car: in 434 shifts, cockpit or outside layers were playing, never both. A viewed AI car gets cockpit layers like the player.","evidence":"shift_sound_reset 0.1.1 live runs 2026-10-26","source":"sdk/research/dev_mods/shift_sound_reset/README.md"}
```
