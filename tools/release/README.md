# Perfect Dark Zero Restored

Perfect Dark Zero Restored (PDZ Restored) is a native PC version of Rare's Xbox 360 game Perfect Dark Zero. The
game's own code has been recompiled into a Windows program, so it runs directly on your PC. There is no emulator.

As well as the PC version itself, it restores two multiplayer modes Rare left unfinished, Plant the Flag and Protection,
gets bots playing Sabotage and allows matches with up to 31 bots. It adds Counter-Op, a two-player campaign mode,
optional stealth on four missions, and sounds Rare recorded but the game never played.

No game files come with it. You need your own copy of the game.

## What you get

- **Counter-Op.** A new two-player campaign mode, after the N64 original's Counter-Operative: player 1 is Jo,
  player 2 takes over the level's enemy guards one at a time, in split screen. Built on Rare's own co-op and
  multiplayer code, with special parts on most levels: a spiderbot on Datacore, Mai Hem on Mansion, one of the twins
  on Trinity Infiltration, hovercraft drivers on River, Mai Hem's hovercraft gun on Outpost, and flying jetpack
  riders on Datacore, Trinity Escape and Jungle. Two controllers needed.
- **Stealth, optional.** On the Laboratory, the Nightclub docks, the Subway and the Mansion grounds, calm guards
  notice you more slowly in the dark at a distance, can be crept up on from behind, investigate noises, shot-out
  lamps and bodies, and hear gunfire more faintly through walls. What they do and say is Rare's own; the rules for
  when a calm guard notices you are new. The moment a guard sees you in plain view or you go loud, he is Rare's
  own guard again. One switch turns it off.
- **Sounds Rare recorded, now heard.** Body falls, footsteps, pigeons, flies, hums, the buzz of flickering
  fluorescent tubes, sparks, crate and cabinet knocks, bullets into water, the audioscope zoom, the torch click and
  more: only Rare's own recordings, only where Rare's game has the thing and the moment, each on its own switch.
- **Two cut multiplayer modes restored.** Rare left Plant the Flag and Protection unfinished. Both are now
  finished and playable, with bots.
- **Bots in Sabotage.** Sabotage shipped without bot support. Now bots play it too.
- **Up to 31 bots** in a match, and 32-player Infection. Bots can use turrets. Facility and Ruin stay 8-player maps,
  as Rare made them.
- Both official map packs, from your own copies. A known fault in some copies of Map Pack One is repaired when you
  install it. The US Platinum Hits disc already has them.
- A native Direct3D 12 renderer that matches the Xbox 360 picture, up to 4K.
- **Far lighting.** Distant lights and lit windows stay lit instead of switching on as you get close. A Quality
  preset adds longer sun shadows and full streetlight beams at a steady 60 fps.
- 120 fps in the campaign, if your PC and screen can manage it. Multiplayer runs at 60 fps.
- Built on Title Update 3, with all of Rare's fixes.
- Dozens of bug fixes, including aiming, weapons, enemy AI, cover, missing speech and sounds, and menus.
  Cutscenes stay in sync with their sound.
- A music choice for multiplayer matches, which can also play your own MP3s (see below).
- A settings window with display, graphics, frame rate, gameplay, map pack and control options, including a field
  of view slider, a Performance / Quality visual preset and a Restored / Original gameplay choice.
- Safe mode, for when something goes wrong.
- Designed for a controller. Mouse and keyboard work too, and every key, mouse button and the wheel can be rebound.

## What you need

- Windows 10 or 11, 64-bit.
- A processor with AVX2: Intel 4th generation ("Haswell", 2013) or newer, or AMD Ryzen or newer. Older processors
  can't run this version.
- A graphics card that supports DirectX 12.
- The Microsoft Visual C++ Redistributable (x64): https://aka.ms/vs/17/release/vc_redist.x64.exe
- Your own Perfect Dark Zero disc, as a disc image (.iso) or its extracted files. Any of the five original retail
  discs works, and so does the US Platinum Hits disc (see below).
- Title Update 3 for Perfect Dark Zero, copied from your own Xbox 360. Any disc's Title Update 3 works. The US
  Platinum Hits disc doesn't need it.
- Optional: the two map packs, copied from your own Xbox 360. On the console they're in
  `Content\0000000000000000\4D5307D3\00000002`.

## Setting it up

1. Unzip PDZ Restored into its own folder, for example `C:\Games\PDZRestored`.
2. Put your disc image (or the extracted disc) and the Title Update 3 file into the `game` folder.
3. Start `PDZRestored.exe`.
4. The first start checks your game files, prepares them in the `data` folder and makes the graphics shaders. In
   our tests the menus appeared about 20 seconds after the first start, and about 10 seconds after later starts.
   The very first start can take a little longer while Windows checks the new program. For about 5 of those
   seconds the game window is already open and black. That is the renderer getting ready, not a hang. The same
   happens once after each update.
5. The settings window opens. Add your map packs on the Map packs tab if you have them, then press Play.

After the first start you can delete the disc image from the `game` folder if you want the space back.

## US Platinum Hits

The US Platinum Hits disc works as it is:

1. Put its disc image (.iso), or the extracted disc, into the `game` folder.
2. You don't need Title Update 3. If one is in the `game` folder, it isn't used for this disc.
3. Both map packs are already on this disc. The Map packs tab shows them as included; there's nothing to add there.

## Counter-Op (two players)

One player is Jo. The other plays the enemy guards: he takes over one guard at a time, and when he dies he takes
another. Jo wins by completing the mission; player 2 wins by stopping her.

1. Connect two controllers. The first one is Jo. (Mouse and keyboard can't be player 1 here: the first controller
   would drive player 1 too.)
2. Choose Campaign, Mission Mode, COUNTER OPERATIVE, then Local.
3. Player 2 joins on the mission select screen, as in co-op. Pick a mission and a difficulty.

While player 2 waits for his next guard, his half of the screen follows Jo. He can't pick anything up, use Jo's
gadgets, drive vehicles or hurt Jo's allies. If Jo leaves him behind (a door she locks behind her, a new part of
the level), he is moved to a guard ahead by himself. Restart mission keeps Counter-Op. On Bridge the jetpack riders
stay the game's own.

## Settings worth knowing

- **VSync** (Display tab):
  - **On** (the default) locks the game to your screen's refresh. It's smooth at 60 and 120 Hz. On a 144 or
    165 Hz screen it runs at 72 fps.
  - **VRR** is for G-Sync or FreeSync. Only choose it if G-Sync or FreeSync is switched on in your graphics driver
    for this screen. It runs the campaign at up to 120 fps on any VRR screen.
  - **Off** can tear.
- **Visual preset** (Graphics tab): Performance (the default) is Rare's look with distant lights fixed, at up to
  120 fps. Quality adds longer shadows and full streetlight beams at a steady 60 fps. Custom lets you choose.
- **Restored or Original** (Gameplay tab): Restored is the game as PDZ Restored intends it: guard cover fixes,
  stealth, aim enhancements and restored sounds. Original turns those four off. Custom lets you mix. Bug fixes
  with no switch stay on. Changes take effect when a level starts.
- **Stealth** (Gameplay tab, part of Restored): on the Laboratory, the Nightclub docks, the Subway and the Mansion
  grounds, calm guards notice you more slowly in the dark and can be crept up on. Seen or loud, they are Rare's own
  guards. Off: guards as on the Xbox 360.
- **Guard voices** (Gameplay tab): how loud characters speak. The default is +3 dB; 0 dB is Rare's level.
- **Resolution** (Display tab): Desktop resolution is your screen's own size. If you updated from an older
  version and Fullscreen looks soft, the row will show 1920 x 1080: pick Desktop resolution once.
- **Mouse and keyboard:** Esc pauses the game. In menus, Esc goes back and **P** is Start (you need it to start a
  multiplayer match). Enter or left click selects. The Controls tab shows every key and lets you change them.
- **The settings window** opens at every start until you tick "Don't show this again". To bring it back, hold Shift
  while starting the game, or use `PDZ Restored Settings.bat`.

## Your own music in multiplayer

You can add your own MP3 files to the multiplayer Music choice:

1. Put the MP3s in a folder of their own.
2. Open `settings\hardware.toml` (next to `PDZRestored.exe`) in a text editor and add this line with your folder's
   path. Write each `\` twice:

   ```
   pdz_mp_music_custom_dir = "C:\\path\\to\\your\\mp3s"
   ```

   To make them louder or quieter, also add this line. The number is in dB, for example `-3.0` or `2.0`:

   ```
   pdz_mp_music_custom_volume_db = 0.0
   ```

3. Start the game. Your tracks appear in the Music row of the multiplayer setup screen as `REMIX: <title>`, after
   the game's own tracks. Random picks from them too.

## Performance

Most of the testing has been on one PC, with an NVIDIA graphics card. There it runs the campaign at 120 fps (the
Quality preset at 60), and multiplayer, co-op and Counter-Op at 60.

Slower PCs should hold 60 fps. How far above 60 you get depends mostly on your processor. AMD and Intel graphics
cards haven't been tested yet, so reports are very welcome.

## If something goes wrong

- **Safe mode:** start `PDZRestored (Safe mode).bat`, or press Safe mode in the settings window. It uses a slower
  compatibility renderer, locked at 60 fps.
- **Bug reports:** please include the newest log from the `logs` folder next to `PDZRestored.exe`. The settings
  window's Advanced tab has a button that opens it. Say which processor and graphics card you have.

## Known issues

- A diagonal shadow flicker at the end of the Bridge mission. It comes from the game's own shadows and is very likely
  in the original game too.
- Menus and on-screen prompts show Xbox controller buttons, also when you play with mouse and keyboard.
- The first start, and the first start after an update: the game window stays black for about 5 seconds while the
  renderer prepares. It is not a hang.
- A body can occasionally hop slightly after it lands.
- Scenes with many bodies on the ground (about 20 or more) can stutter.
- Counter-Op: player 2's view can show Jo's position for a moment when he dies.
- Counter-Op: player 2 does not wear the armour pieces of the guard he takes over: they fall to the floor, and his
  armour bar shows only the guard's armour value.
- Counter-Op: the security girl (Laboratory, River) has Jo's hands in first person.
- Counter-Op: on Rooftops, a parked 4x4's rear tyres can glow white-purple in split screen.
- Subway: three red screens make no sound when shot.
- Subway: a blue neon sign near the gate keeps glowing after it is shot (Rare's own bug).
- Subway: stepping into the last train can take a second try at 120 fps.
- Getting off the top of a ladder, the view can swing to one side before it straightens.
- Spiderbots make no sound when they fall: Rare recorded none.
- The release page lists every known issue.

## How it was made

PDZ Restored was made with a lot of help from Claude (Anthropic's AI assistant), which did much of the research,
programming and testing.

## Credits

- **Rare**, for Perfect Dark Zero.
- **nikolaygorb**, for PerfectDarkZeroRecomp, the recompilation this project grew from (MIT licence).
- **The ReXGlue SDK** and its authors, the recompilation toolkit and runtime it is built on.
- **Xenia**, the Xbox 360 research project the SDK draws on.
- **XenosRecomp** by hedge-dev, the shader translator built into PDZ Restored.
- **PDZ Reinstated / PerfectDark023**, for the idea behind the physics timing fix.
- **2023PerfectDark**, for their contribution to the recompilation.
- The authors of the libraries listed in `THIRD_PARTY_NOTICES.md`.

PDZ Restored is an unofficial fan project. Perfect Dark Zero belongs to Rare and Microsoft, and this project is not
made, approved or supported by them.

Made by a fan, out of love for Perfect Dark Zero and respect for the people at Rare who made it.

## Licences

The licences are in `LICENSE`, `THIRD_PARTY_NOTICES.md` and the `licenses` folder. PDZ Restored includes Microsoft's
`dxil.dll` and `dxcompiler.dll`. By using or sharing PDZ Restored you agree to their terms (`licenses\dxc`,
summarised in `THIRD_PARTY_NOTICES.md`).
