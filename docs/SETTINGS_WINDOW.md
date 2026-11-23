# Settings window, map packs and the release settings file (2026-11-04)

## The window (`src/setup/settings_window.cpp`)

A standard Windows window: native controls with the system's look (comctl32 v6 activated around this window only,
`src/pdz_comctl6.manifest`, resource 3).

**When it shows:**
- after the first-run setup, on the Map packs tab (the packs are setup's optional last step);
- on every start until "Don't show this again" is ticked (`pdz_settings_show`);
- when Shift is held at launch;
- with `--pdz_settings_open` (`PDZ Restored Settings.bat`).

Never in a developer's build or with `pdz_setup_quiet`. Play and Safe mode save and start. The close box (or Alt+F4) saves the same way and quits without starting the game (0.9.2).

**Tabs:**

| Tab | Settings | Writes |
|---|---|---|
| Display | Monitor, Borderless / Fullscreen, Resolution (Fullscreen only; Desktop by default), Render scale 100 / 75 / 50 %, VSync / VRR On / VRR (G-Sync / FreeSync) / Off (each with its own help line; Off warns: tearing and judder), Field of view 73-103 degrees across 16:9 with Default (Rare's) | `monitor`, `fullscreen`, `fullscreen_exclusive`, `resolution` (Fullscreen with a chosen size only), `pdz_native_scale` (75 / 50 % of the desktop's auto scale, nearest integer, 50 % one lower where both would be the same; 100 % = no line, and a value the file had is dropped at once; the saved value is shown again when the window reopens, 0.9.2), `pdz_present_pacing` (fixed / vrr; a hand-set present / latch is kept by On), `vsync` (read from the settings file: the GPU plugin that owns it isn't loaded when the window opens, 0.9.2), `pdz_fov_scale` |
| Frame rate | Campaign Auto / 120 / 60 (single player only; since 2026-11-17 Auto = `pdz_sp_auto`: 120 fps, and the paced 60 for a level whose first 10 s of play do not keep 120; 120 and 60 are fixed; the default is 120; the note under the row says what this screen gets, in a line, then that co-op shares the screen and so uses the Split screen choice: co-op levels keep the 60 divider in `game_pace.cpp`); Multiplayer 60, greyed out; Split screen Auto / 60 / 40 / 30 (2026-11-08: the rate while 2 or more local players share the screen in a co-op or multiplayer level; Auto = 60 with 2 players, 40 with 3 or 4 on 120 Hz pacing; see below) | `pdz_sp_120`, `pdz_sp_auto`, `pdz_splitscreen_cap` (0 = Auto) |
| Map packs | each pack, its maps and status; Add / Remove; Add pack files...; Check installed packs | the packs (below) |
| Controls | mouse sensitivity, invert, Step zoom (off), one row per action with two keys each (rebind by pressing), Reset controls to defaults; an orange warning line under the buttons while Start has only keys the menus use (Enter, Esc, left / right click, the wheel) or none: there is then no Start in menus, add a plain key (P is Start in menus by default); it follows every change of the rows (2026-11-08); a Locktopus group (0.9.4): Steer with Mouse / Keys / Both, Mouse sensitivity Low .. High (800 .. 80 counts, 400 in the middle), the keys as text from the current binds, and a practice dial on Rare's own locks with the lock's name under it, New lock and Agent / Secret Agent / Perfect Agent (dimmed under "Click to test" until a click or Space makes it active; docs/MOUSE_KEYBOARD.md) | `pdz_mouse_sensitivity`, `pdz_mouse_invert`, `pdz_zoom_steps`, `pdz_minigame_inputs`, `pdz_minigame_mouse_counts` (each only when changed; a Locktopus change also sets `pdz_fix_minigame_keyboard = true`) and the changed `keybind_*` lines in `mapping.toml` (comments kept) |
| Advanced | Open settings file, Open logs folder, Rebuild shaders (removes `generated.json`), Reset to defaults, Network play | `net_lan` |

- **Help:** the line at the bottom explains whatever control has the mouse over it or the keyboard focus (polled
  every 100 ms; no hover tooltips). It is owner-drawn, at most two lines, centred beside its info icon (0.9.4).
- **Layout check (0.9.4):** `pdz_settings_test_capture` also checks every tab (text that doesn't fit its control,
  overlaps, controls running into the help strip, help lines over two lines) and writes its lines and a problem count
  to `settings_test.txt` in the capture folder; use `pdz_settings_test_dpi` 96 / 120 / 144 for 100 / 125 / 150 %.
- **Its lines:** the window writes only the lines it owns: in place where they are, else under the release file's
  "Settings window" marker. It sets them for the current start too.
- **Windowed:** back in the Screen mode choice (2026-11-04: `--fullscreen=false` ran 150 s through the front end into Datacore with no crash, log PDZ_Job8 008; the NVIDIA crash noted in docs/MAP_PACKS.md was an older build's). It writes `fullscreen = false`.

**Test:** `pdz_settings_test_capture=<folder>` shows each tab, writes `tab<N>.bmp` and closes. All five tabs were
captured in the test folder.

## Graphics and Gameplay tabs (0.9.5, 2026-11-16)

Seven tabs, in this order (also the capture files' numbers and the side bar buttons' ids 180-186): Display 0,
Graphics 1, Frame rate 2, Gameplay 3, Map packs 4, Controls 5, Advanced 6. Rule: what changes the look is on
Graphics, what changes play is on Gameplay; plain bug fixes have no switch; fine-tuning stays in the settings file.

| Tab | Row | Choices -> lines | Release default |
|---|---|---|---|
| Graphics | Render scale | 200% / 150% / 100% / 75% / 50%. 75% / 50% -> `pdz_native_scale` (as before). 150% / 200% -> `pdz_supersample` 150 / 200 (supersampling: the native renderer draws at a higher integer scale, at most 6, and shrinks the finished frame with an area filter; checked against the graphics card's memory at the start, lowered with a log warning if it would not fit); `pdz_native_scale` then has no line. The segment order changed: 100% is now the third segment (index 2), so a test's `click=` id for a Render scale segment moved by two | 100% (no lines) |
| Graphics | Upscaling | Smooth / Sharpen / FSR -> `present_effect = "bilinear" / "cas" / "fsr"` | FSR |
| Graphics | Anti-aliasing | None / FXAA / SMAA -> `swap_post_effect = "none" / "fxaa" / "smaa"` | SMAA |
| Graphics | Anisotropic filtering | Rare's / 2x / 4x / 8x / 16x -> `anisotropic_override = -1 / 2 / 3 / 4 / 5` | 16x (5) |
| Graphics | Visual preset | Performance / Quality / Custom: sets the four rows below, the Frame rate tab's Campaign row and `pdz_far_lighting_beams` (see "Visual preset") | Performance |
| Graphics | Far lighting (Custom only) | switch -> `pdz_far_lighting` | on |
| Graphics | Long shadows (Custom only) | Off / 40 m / 60 m / 80 m -> `pdz_long_shadows`, `pdz_long_shadows_distance = 2.0 / 3.0 / 4.0` | Off |
| Graphics | Full streetlight beams (Custom only) | switch -> `pdz_far_lighting_beams_no_fade` | off |
| Graphics | Single-player frame rate (Custom only) | Auto / 120 fps / 60 fps -> `pdz_sp_auto`, `pdz_sp_120` (the same setting as Campaign on the Frame rate tab; each follows the other). Auto: 120 fps, and the paced 60 on a level that keeps missing it (see "Auto frame rate") | 120 fps |
| Gameplay | Game | Restored / Original: sets the three switches below, all on / all off; neither is lit when they are mixed. It reads and changes no graphics setting | Restored |
| Gameplay | Guard cover fixes | switch -> `pdz_ai_cover_fixes` | on |
| Gameplay | Stealth | switch -> `pdz_lab_guard_stealth` (the stealth layer's master switch: an optional layer for the quiet part; a guard who sees the player or hears her go loud is the game's own guard again; off = every part of the layer off on Nightclub, Subway, Mansion and the Laboratory) | on |
| Gameplay | Guard voices | slider -12 to +9 dB -> `pdz_guard_voice_db`, with a Default (+3 dB) button; under Stealth since the 0.9.5 rc (2026-11-16), before that on Display; control ids 600-602 unchanged | +3 dB |
| Gameplay | Aim enhancements | switch -> `pdz_aim_still_lock`, `pdz_aim_respect_headshots`, `pdz_aim_assist_zoomed` together | on |

- **Auto frame rate (2026-11-17, `pdz_sp_auto`; a Custom choice only).** With `pdz_sp_120` on, every single
  player level starts at the paced rate (120 fps on a 120 Hz display) and is measured to its end in windows of 10 s
  of play (`pdz_sp_auto_seconds`; frames of a cutscene are left out). Fixed-refresh display: each frame's time is
  counted in whole display refreshes; a window whose 95th percentile is over 8.6 ms (`pdz_sp_auto_p95_ms`: more than
  one frame in twenty missed its refresh) is a missed window, and two missed windows in a row put the level on the
  paced 60 until it ends (never back up), exactly as a level on `pdz_sp_60_levels`. VRR display
  (`pdz_present_pacing = "vrr"`): the rate floats and is never switched; a window under 110 fps only gets a line.
  The next level tries 120 again. `pdz_pace: pdz_sp_auto: level ...` lines: the first window, each missed or low
  window, the switch. Nothing is measured where 60 is not a different rate (a 60 Hz display). Test:
  `--pdz_sp_auto_p95_ms=1` switches down after two windows.
- **Visual preset (2026-11-16).** Performance: far lighting on with the hidden-room beams (`pdz_far_lighting`,
  `pdz_far_lighting_beams`), long shadows off, full streetlight beams off, Campaign 120 fps.
  Quality: the same plus long shadows to 60 m on every level (`pdz_long_shadows_skip_levels = ""`: Outpost
  included), full streetlight beams, Campaign 60 fps: a fixed, paced 60 on every level (2026-11-17, second change
  that day; Auto is a Custom choice). Saved values that are the earlier Quality (Auto with those rows) and carry no
  `pdz_visual_custom = true` line run and open as Quality at 60; Save writes `pdz_visual_custom` when Custom is picked. Custom: the four rows, which are
  hidden for the other two. A preset click sets the rows; Save then writes them as if set by hand. The preset shown
  on opening is what the saved values say (anything that fits neither is Custom); changing Campaign on the Frame
  rate tab moves the preset to Custom if the values then fit neither. Long shadows at a fixed 120 fps remove the skip list
  line, so Outpost keeps Rare's shadows. `pdz_far_lighting_beams` has no row of its own any more (id 645 reserved);
  a hand-set `false` reads as Custom and is kept until a preset is picked. Bug fixes (the water reflection) have
  no row. Help texts per choice, as for VSync.
- **Only what changed is written.** Each row remembers its value as loaded; Play, Safe mode and the close box write
  a row's lines only when the row differs, in place with their comments (`WriteKeys`). A value set by hand that is
  none of a row's choices (`swap_post_effect = "fxaa_extreme"`, `anisotropic_override = 0`) shows no choice lit and
  is kept unless a choice is picked. The window logs `pdz_settings: graphics / gameplay rows changed: ...`.
- **Read from the file:** `swap_post_effect` and `anisotropic_override` belong to the GPU plugin, which isn't loaded
  while the window is open (as `vsync`). Everything applies from the start that follows.
- **No 2x MSAA row** (removed 2026-11-16): `native_2x_msaa` changes nothing in this game (it draws no multisampled
  surfaces and the native renderer has none). The line stays in the settings file; the window never writes it. Its
  control id 642 stays reserved, so the other ids did not move.
- **Help strip:** on these two tabs the strip is taller (`kHelpTallH`, up to six lines) because their agreed help
  texts are longer than two lines; the other tabs keep two. The layout check knows both limits.
- **Reset to defaults** (Advanced) puts both tabs back to the release defaults above.
- **Test items:** `click=<id>` in `pdz_settings_test_rebind` clicks any control: a tab button 180-186, a segment
  2000 + group x 8 + index (groups: Upscaling 640, Anti-aliasing 641, Anisotropic 643, Game 660), a switch by its own
  id (Far lighting 644, Full streetlight beams 648, cover 662, stealth 663, aim 664); more groups: Long shadows 646,
  Visual preset 647, Single-player frame rate 649. `close=1` ends the script with the
  close box (saves, the game doesn't start) instead of Play.

## Split-screen frame cap (`pdz_splitscreen_cap`, 2026-11-08; untested in game)

The Frame rate tab's Split screen choice. 3-player split screen drops frames at 60; a steady lower rate paces better.
- **Where it acts:** `game_pace.cpp` `PdzFrontendPace` (the present step, game thread), the same lever as
  `pdz_sp_120`'s 60 for co-op / multiplayer and `pdz_sp_60_levels`: the SDK's guest vblank divider
  (`null_gpu::SetGuestVblankDivider`). With the guest paced at 120 Hz, 60 = divider 2, 40 = 3, 30 = 4; the divider is
  `round(paced Hz / cap)`, never below the scene's own (so a 60 Hz display gives 60 / 30 / 30, a 144 Hz display's 72 Hz
  pacing 72 / 36 / 36). `pdz::pace::EffectiveHz` reports the capped rate, so the Havok step (`pdz_physics_frame_step`)
  and the prop fixes follow it as they do at 60 and 120.
- **When:** in a level (state 21) with 2 or more local players: the participants (list `82E34224`, +4 next) of the
  player kind (+944 = 2) whose local user (+1680) is 0-3, as distinct users; counted at the level start and every 60
  frames (a list walk of at most 64 nodes, no guest calls, no allocation). Menus, loading, briefings and one-player
  games are never capped; the cap lifts when the level ends or the count drops below 2.
- **Auto (0, the default):** 2 players keep today's 60; 3 or 4 players run at 40 when the paced rate is 90 Hz or
  more (120 / 3), else 60. 60 / 40 / 30 apply to any split screen.
- **Log:** `pdz_splitscreen_cap: <n> local players, cap <x> Hz (setting <v>)` when it engages or changes,
  `pdz_splitscreen_cap: <n> local players, cap off (setting <v>)` when it releases. The existing
  `pdz_pace: pdz_sp_120: level (game mode N): X fps` line shows the rate too. Hot-reload: a test script's
  `cvar <t> pdz_splitscreen_cap 30` changes it mid-level.
- **Needs** pdz_sp_120's pacing (null backend, vsync on, pdz_null_refresh_hz unset); without it nothing is capped.

## Controls tab (2026-11-04, evening)

- **Rows:** one per action, the game's name with the pad button in brackets (Fire (RT), Aim / zoom (LT), Reload (X),
  Use / open / take cover (A), Crouch / stand (L3), Dodge roll (LB), Melee (B), Select weapon (Y), Secondary fire
  mode (RB), Pause (Start), Objectives / scores (Back), the four moves, the four D-pad gadget slots, the four keyboard
  look directions, Right stick click (R3)). PDZ has no jump and one weapon button (no next / previous).
- **Rebind:** click a row's Key 1 or Key 2 cell (or press Enter on the row: Key 1; or the Change key 1 / 2 buttons),
  then press anything: every key (by its scan code, as SDL will see it: `rex::ui::Win32KeyMessageToScancode`), mouse
  buttons 1-5, buttons 6-32 (Raw Input's raw button state, registered only while waiting), the wheel up / down and
  tilt left / right. Esc cancels; so does switching away. Delete or Clear empties the row.
- **Names:** `rex::ui::InputCodeDisplayName` ("Left Ctrl", "Mouse 4 (back)", "Wheel up"); the file gets the bind token
  (`InputCodeToken`: "LControl", "Mouse4", "WheelUp").
- **Clashes:** an input another action has asks: Yes swap (the other action gets the old key), No use it for both,
  Cancel.
- **Two keys:** a value with more than two (the default Select weapon `Q,WheelUp,WheelDown`) shows the rest in Key 2;
  setting Key 2 replaces them with one.
- **Saved** with Play / Safe mode: only the keybind lines that changed, in place with their comments; set for this
  start too. Reset controls to defaults restores job 2's keys, sensitivity 2.0 and invert off (saved with Play); the
  Advanced tab's Reset also resets them, at once.
- **Test:** `pdz_settings_test_rebind="keybind_a:2=XButton2;keybind_x=Key:11:1D:ext;keybind_back:2=Raw:6;..."` runs
  the rebinds through the capture path (posted Windows messages; Raw:<n> feeds the raw button state), answers clashes
  with `pdz_settings_test_answer` (swap / both / cancel), logs every step and the final rows, then presses Play. It
  shows the window even with `pdz_setup_quiet`.

## Frame rate on the display (`game_pace.cpp`, `pdz_display_switch_120`)

**VSync / VRR (2026-11-06): no Auto.** Auto chose VRR whenever the display's EDID advertised it (HDMI VRR,
FreeSync, Adaptive-Sync), but the EDID can't tell whether VRR is switched on in the graphics driver: in the
new-player test a FreeSync EDID with VRR off in the NVIDIA driver gave 117 fps Present(0) on a fixed 120 Hz display,
which juddered; `fixed` was smooth. So:
- **On** = `pdz_present_pacing = "fixed"` (the default and the release file's value): locked to the display's refresh.
- **VRR (G-Sync / FreeSync)** = `"vrr"`: only if VRR is on in the driver for that display.
- **Off** = `vsync = false` (the pacing stays fixed); its help line is the tearing warning.
- An older settings file's `"auto"` is read as `"fixed"`, with one `pdz_pace:` line saying so.

With pdz_sp_120:

| Display | On (fixed) | VRR |
|---|---|---|
| 60 Hz | 60 | (needs 119 Hz or more) |
| 120 Hz | 120 | 117 (capped under the refresh) |
| 144 / 165 Hz | the even division at most 120: 72 / 82.5 | 120 (capped) |
| 240 Hz | 120 (240 / 2) | 120 (capped) |

The switch of a 144 / 165 Hz display to 120 Hz (`pdz_display_switch_120`) only ran under Auto, so it no longer
happens; set the display to 120 Hz in Windows for 120 fps with On.

The choice is logged (`pdz_pace: display N Hz: ...`), and so is a start-up summary:
`pdz: this PC: GPU ... | display ... Hz, VRR ... | pacing ... | CPU N cores, M threads | second record thread ...`.

## Map packs in the exe (`src/setup/map_packs.cpp`)

**The port:** `tools/install_map_packs.py` + `tools/stfs.py`.
- **Steps:** STFS read-only; the known Map Pack One repairs (Plaza 1 byte, Trench 2 bytes); every file against the
  package's SHA-1 block hashes; every `.pkg` inflated with its Adler-32 trailer checked.
- **Where:** a pack that verifies completely goes to `<user data>\0000000000000000\4D5307D3\00000002\<package file
  name>\`, where TU3 lists it (`XamContentCreateEnumerator` includes shared content); `pdz_install.txt` holds each
  file's SHA-1 for "Check installed packs".
- **A failure:** a pack that fails is left as `<name>.failed` with the reason.
- **From a script:** `pdz_map_packs_install = "<file>;<file>"` installs at start-up.

**Tests:**
- **Install from your copies (read only):** Map Pack One from `<your Map Pack One folder>`, installed
  repaired. Map Pack Two from its own folder, every file verified. Your files were unchanged.
- **Against the old script:** all 78 + 58 installed files are byte-identical to `install_map_packs.py`'s install.
- **Front end:** with the packs, 0 shader misses. Without them, the original maps; every other test ran without
  packs.

## The release settings file (`settings/release/hardware.toml`)

Shipped as `settings\hardware.toml` by `stage_release.py`. It is the committed template plus:
- `log_level = "info"`;
- frame timing off;
- `pdz_cutscene_smooth = false`;
- `net_lan = false`;
- the developer's multiplayer play set: Plant the Flag, Protection and Sabotage with bots and the Sabotage stages;
- `pdz_infection_32`;
- `pdz_mp_announcer_extras` (now a fix, on in code too);
- the window's section.

The aim enhancements are unchanged (on). `net_lan` (SDK) off binds every socket to 127.0.0.1.

`stage_release.py` refuses:
- launchers other than `PDZ Restored Settings.bat` and `PDZRestored (Safe mode).bat`;
- any audio file;
- anything from `MyMusic`.

## Field of view (2026-11-04, `src/mod/fov_tuning.cpp`)

- **What it scales:** `pdz_fov_scale` multiplies the zoom factor the gameplay camera asks for (`sub_825AC8E0`, its
  call returning to `822156D4`), so the camera's FOV is 58 degrees (Rare's 4:3 horizontal) x zoom factor x scale.
  Across a 16:9 screen that is 2 atan(4/3 tan(29 deg x scale)): **72.9 degrees** at 1.0 (Rare's), 103.4 at 1.5.
- **The slider:** whole degrees from 73 (Rare's, written as 1.000) to 103 (scale 1.494); "Default (Rare's)" moves it
  back. Saved as `pdz_fov_scale` in the settings file; the Advanced tab's Reset puts 1.0 back.
- **Zoom:** a scope's or step zoom level's FOV is widened by the same factor, so every zoom keeps its magnification
  relative to the unzoomed view (not Rare's absolute zoomed FOV). The mouse's zoomed turn rate (`mouse_look.cpp`)
  uses the same widened angles, so it stays in proportion.
- **Not scaled:** every other caller of the zoom factor and every other camera (vehicles, turrets, cutscenes,
  menus): `pdz_fov_log` lists the callers with counts every 10 s.
- **Test hook:** `pdz_settings_test_rebind="pdz_fov_degrees=90"` moves the slider through its own notification.

## The look (2026-11-04, `src/setup/theme.cpp`)

- **Palette:** navy window #050B1E, panels #0E1C44, side bar / help strip / footer #081230, title bar #030714 (DWM,
  Windows 11), borders #23407F, text #E4EAF4 / #A2B1CF, selected choices and switches #1F5FE0, the selected tab's bar
  and info notes #5FD3FF, warnings #FF8A50.
- **Buttons:** Play (and a question's main answer) orange gradient with navy upper-case Rajdhani; Safe mode and Cancel
  brushed silver; Remove / Reset orange outline; Add blue; the rest panel buttons. All owner-drawn real buttons, so Tab,
  Space and Enter work as before; a cyan ring shows the focus.
- **Controls:** side bar tabs, segmented choices (Screen mode, Render scale, VSync, Campaign, Multiplayer), switches
  instead of check boxes, a themed slider.
- **Drop-down lists** (Monitor, Resolution, 2026-11-04 polish): drawn by us, a panel with a cyan chevron; the open list
  (`OpenDrop`) is a drawn popup in the same colours, the item under the mouse / the keyboard in blue. Mouse, wheel,
  arrows / Page Up / Down / Home / End, Enter or Space to choose, Esc / Tab / a click outside to close; a controller's
  up / down / A / B too.
- **The key list:** still a Windows list view (rebinding, hit tests), drawn in the theme: the header in the side bar's
  colour with the column names in silver Rajdhani capitals (15:1 contrast), a line under each row and between the
  columns, every other row slightly lighter, the selected row blue with white text, and our own scroll bar (the list's
  own is clipped off by its window region; ours scrolls it: drag, click above / below the thumb, the wheel).
- **Map packs:** the status keeps "Installed (repaired)" across starts: the install record (`pdz_install.txt`, first
  line `repaired 1`) says so. Packs installed by the old `tools/install_map_packs.py` have no record and show
  "Installed"; Remove and Add again writes one. Colours: installed cyan, not installed dimmer, didn't verify orange.
- **Field of view in tests:** `pdz_settings_test_capture` shows Rare's 73 degrees unless its rebind script sets
  `pdz_fov_degrees`; a fresh settings file has `pdz_fov_scale = 1.0`.
- **Ids:** the side bar tabs use 180-184 (in the restyle they were 120-124 and pushed Monitor to 121, so the Monitor /
  Resolution lists drew as empty tabs and Screen mode / Render scale choices switched tabs; fixed). `click=<id>` in
  `pdz_settings_test_rebind` clicks a control (a segment's id: 2000 + group x 8 + index).
- **Fonts:** Segoe UI for text, Rajdhani Bold (SIL OFL, `licenses/rajdhani`) for the title, headings and Play, embedded
  in the exe (resource 10).
- **Our message box** (`theme::ShowMessage`) replaces every Windows message box of the settings window and setup, same
  text and buttons; the setup progress window is redrawn the same way. The Windows file picker stays Windows'.
- **Controller:** up / down move the focus, A presses, B cancels a rebind or a question, LB / RB change tabs, left /
  right change a segmented choice.
- **Screenshots:** `docs/settings_window/` (`pdz_settings_test_capture`, `pdz_setup_test_capture`; `_100pc` /
  `_150pc` with `pdz_settings_test_dpi` 96 / 144: the window's own layout at that scale, but Windows still draws
  title bars and scroll bars at the screen's scale, so the key list's scroll bar there is too wide).
