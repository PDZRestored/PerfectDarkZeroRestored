// Restored sounds, round 3 (Wed 18 Nov 2026, job sound3). TU3 addresses. Needs pdz_restored_sounds; each sound has
// its own switch. Every sound here starts and stops through restored_voice_Wed18Nov (nothing keeps a sound object).
//
// TORCH ON / OFF (pdz_snd_torch). A weapon's torch (the P9P's and GunType 3's third function) is bit 0x20 of the
//   owner's flag word (+64), flipped by the gun state machine's vtable slot +256 (sub_82259428 at 822594B8,
//   sub_8224F230 at 8224F2B0) with no sound. The light itself is made and removed by the character update
//   sub_8254C5A0 (r3 = character): the light pointer is character +5676 (made at 8254C7B0, removed at 8254C868).
//   GUNS_TORCH_ON / GUNS_TORCH_OFF are in the guns bank and nothing asks for them. Here: when that pointer goes
//   from 0 to a light in that update, GUNS_TORCH_ON; from a light to 0, GUNS_TORCH_OFF (every way the light goes
//   off: the button, the weapon put away). As Rare starts its neighbouring gun toggles (sub_822556F0): not placed
//   for the local player (owner +64 bit 2), at the character for anyone else. Sound only.
// FLUORESCENT BUZZ (pdz_snd_fluor_buzz). The prop templates light\general, light\general_shootable and
//   light\general_shootable2 (Subway's garage lights, the Laboratory) list STOCK_FLUOR_SMASH / STOCK_FLUOR_FALL:
//   Rare's own data says they are fluorescent fittings. STOCK_FLUOR_BUZZ is asked for by nothing. Here: the world's
//   objects (the list at [82D6BFF0] +43212, as Rare's contact routine walks it) are named once each by their
//   template (vfunc +72, the path before the record); a fitting of those three templates buzzes while its light is
//   on: its linked scene light (prop +812; state words at light +0x590 / +0x594: 1 fading in, 2 on) or, with none,
//   its own light (prop +800, dropped by a bullet). Shot out, switched off (the Laboratory's blackout uses the same
//   state) or the prop gone: the buzz stops. A fitting with neither light is logged and never buzzes (its state
//   cannot be known). Only the pdz_snd_fluor_buzz_max fittings nearest a player (within the buzz's range, the range
//   picker hook's 5 m, Rare's substation hum) hum at once; each is a kept sound, started 1 m inside the range and
//   stopped 2 m past it. The cue is a set of short takes, not a loop: when a take has ended the next is started.
//   Quiet (pdz_snd_fluor_buzz_volume).
//   Fri 20 Nov night (log 032): only tube MODELS buzz (pdz_snd_fluor_buzz_tubes_only: the bulbs and pillar lamps share
//   the template), the next take starts the moment one ends (pdz_snd_fluor_buzz_hold: one steady buzz while near), and
//   range and loudness are the camera's (pdz_snd_fluor_buzz_listener: height above the camera + 3 m, 0.45 reaching the
//   camera straight under it). Every start, stop and refusal is logged with the listener's distance and the range.
// F12 POINTER (pdz_pointer_log, log only). The tester's ambience tour: F12 (the mark key) also logs what is under the
//   crosshair. A ray as Rare's hitscan casts it (sub_82259B80, mask 1 = what bullets hit, Rare's closest-hit
//   collector 82090738: hit place at +68, surface id at +104) from the player's eye (player +0x1FA0) to his
//   crosshair point (+0x1FAC), then the placed objects nearest the hit place (the world list), each with its prop
//   type (template), model name, class and place. The ray itself names no object (Rare adds objects in a second
//   pass), so the line says "nearest object" with its distance from the hit; with none within 1.5 m it is level
//   scenery, named by its surface (number, impact and ricochet cue): the engine gives no mesh or material name
//   there. A pointer, never a placement.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/func.h>

#include "guest_function.h"
#include "restored_voice_Wed18Nov.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DECLARE(bool, pdz_restored_sounds);
REXCVAR_DECLARE(double, pdz_snd_fluor_buzz_range_m);
REXCVAR_DEFINE_BOOL(pdz_snd_torch, true, "Gameplay",
                    "Restored sound: switching a weapon's torch on or off makes the game's own torch click. Needs "
                    "pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_fluor_buzz, true, "Gameplay",
                    "Restored sound: fluorescent light fittings buzz quietly while they are lit (the game's own "
                    "buzz, heard only close by; the few nearest only). It stops when the light is shot out or "
                    "switched off. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_fluor_buzz_volume, 0.5, "Gameplay",
                      "Restored sound: the fluorescent buzz's loudness (0 to 1). Its range is the fitting's height above "
                      "the player's floor + 2 m, so it is quietly heard standing under one (Thu 19 Nov)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_snd_fluor_buzz_max, 3, "Gameplay",
                     "Restored sound: how many fluorescent fittings may buzz at once (the nearest ones)")
    .range(0, 6)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Fri 20 Nov night (log 032, the Laboratory 22:12-22:18): see BuzzTick.
REXCVAR_DEFINE_BOOL(pdz_snd_fluor_buzz_flicker_only, true, "Gameplay",
                    "Restored sound (Sun 22 Nov): the strip-light buzz (the game's STOCK_FLUOR_BUZZ, a flickering tube) "
                    "plays only at lights that flicker: the light's own animation is the game's blink or random "
                    "flicker. Steady lights, big or small, scenery strip lights or fittings, are silent. Off = every "
                    "tube light as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_fluor_buzz_tubes_only, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): only real tube fittings buzz (strip lights, train strip lights, "
                    "warehouse lights, tunnel lights), not the bulbs and pillar lamps that share their template. Off = "
                    "every fitting of the light templates. Needs pdz_snd_fluor_buzz")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_fluor_buzz_hold, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): a tube's buzz is held as one steady sound while the player is "
                    "near: the game's buzz is a set of short takes, and the next take now starts the moment one ends "
                    "(before: a gap of up to 3 s after each). Also the scenery strip lights. Off = as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_fluor_buzz_listener, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): a tube's buzz is clearly heard standing under it: its range is "
                    "its height above the camera + pdz_snd_fluor_buzz_extra_m and its loudness is set so that "
                    "pdz_snd_fluor_buzz_at_ear of it reaches the camera right under it (the game measures and fades a "
                    "sound from the camera). Also the scenery strip lights. Off = height + 2 m from the body, loudness "
                    "pdz_snd_fluor_buzz_volume")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_fluor_buzz_at_ear, 0.45, "Gameplay",
                      "pdz_snd_fluor_buzz_listener: the buzz's loudness at the camera right under the tube (0 to 1; at "
                      "most the game's full loudness for a high tube)")
    .range(0.05, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_fluor_buzz_extra_m, 3.0, "Gameplay",
                      "pdz_snd_fluor_buzz_listener: a tube's buzz is heard to its height above the camera + this many metres")
    .range(1.0, 10.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_pigeon_prop_coo, true, "Gameplay",
                    "Restored sound: a single pigeon sitting somewhere (a lamp post on Nightclub) coos now and then, "
                    "quietly and only close by, the game's own coo; never once it is shot or has flown. Needs "
                    "pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(double, pdz_snd_pigeon_coo_range_max_m); // restored_sounds_r2_Tue17Nov.cpp (Sat 21 Nov evening)
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_prop_coo_m, 8.0, "Gameplay",
                      "pdz_snd_pigeon_prop_coo: it coos when a player is within the bird's height above her floor + this "
                      "many metres (Thu 19 Nov night: the bird sits about 8 m up a lamp post)")
    .range(2.0, 30.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_prop_coo_range_m, 12.0, "Gameplay",
                      "pdz_snd_pigeon_prop_coo: the coo's sound range is the bird's height above the player's floor + this "
                      "many metres (the game's sound fades in a straight line to nothing at its range: about 0.6 loud "
                      "standing under a bird 8 m up)")
    .range(2.0, 40.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_prop_coo_volume, 1.0, "Gameplay",
                      "pdz_snd_pigeon_prop_coo: its loudness (0 to 1; 1 = the game's own; Thu 19 Nov night: 0.3 gave about "
                      "a tenth under the bird, unheard)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Fri 20 Nov night (log 032: the lamp-post pigeon cooed 4 times 6.3-9.4 m away, "about 0.5-0.7 loud", and was not
// heard). Rare's stock bank plays STOCK_PIGEON_COO at -15.1 dB (its XSB sound volume 0x4C) where a footstep
// (STOCK_FSTEP_CONC_WALK) has -3.9 dB (0x8F); both are in categories at the same volume (effect / footsteps, 0xAA in
// pdz.xgs); the recording itself is loud (peaks at 0 dB). So at the same distance a coo is 11 dB under a footstep.
// pdz_snd_pigeon_coo_fix (both coos, the single pigeons here and the flocks of restored_sounds_r2): the distance is
// measured from the game's listener (the camera, what its sound manager measures), the range is made long enough that
// the listener gets pdz_snd_pigeon_coo_ear of the loudness, and the loudness is pdz_snd_pigeon_coo_gain times the
// coo's own (x2 = +6 dB). Off: the rules before.
REXCVAR_DEFINE_BOOL(pdz_snd_pigeon_coo_fix, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): the pigeons' coo is clearly heard near the bird: measured from "
                    "the camera, a range long enough for most of its loudness to reach it, and louder than the game's "
                    "bank plays it (the bank has it 11 dB under a footstep). Off = the quiet coo before. Needs "
                    "pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// (Later the same night, the meter helper's reading: a placed sound's loudness is applied twice, so a gain g is heard
// as g * g: 1.4 = about +6 dB. Net bank + recording: the coo -16.0 dB / -10.9 dBFS, a concrete footstep -4.9 / -19.5:
// at 1.0 the coo is about 2.5 dB under a footstep; at 1.4 about 3 dB over it. And the cue LOOPS for ever in the bank:
// with the fix it is a kept sound stopped after pdz_snd_pigeon_coo_take_s, one coo.)
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_coo_gain, 1.4, "Gameplay",
                      "pdz_snd_pigeon_coo_fix: the coo's loudness times this (heard squared: 1.4 = about +6 dB; 1 = the "
                      "game's own)")
    .range(1.0, 3.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_coo_take_s, 1.0, "Gameplay",
                      "pdz_snd_pigeon_coo_fix: the coo cue loops for ever in the game's bank: it is stopped after this "
                      "many seconds (its recording is 0.97 s: one coo)")
    .range(0.3, 5.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_coo_ear, 0.8, "Gameplay",
                      "pdz_snd_pigeon_coo_fix: the share of the coo's loudness that reaches the camera of the nearest "
                      "player when it starts (the game's sound fades in a straight line to nothing at its range)")
    .range(0.1, 0.95)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_pointer_log, true, "Debug",
                    "Log only: F12 (the mark key) also writes 'pdz_pointer:' lines naming what is under the "
                    "crosshair: the nearest placed objects (prop type, model, place) or the level scenery's surface")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_8254C5A0); // the character update that makes and removes the torch light (r3 = character)
REX_EXTERN(sub_82259B80);        // physics ray (r3 mask, r4 from, r5 to, r6 collector, r7 1): r3 = hit
REX_EXTERN(sub_824F4E68);        // the surface record of a surface id (r3) -> r3

namespace pdz::prop_contacts
{
  std::string TemplateOf(uint8_t *base, uint32_t rec); // restored_sounds_r2_Tue17Nov.cpp
}
namespace pdz::r3
{
  void Tick(PPCContext &ctx, uint8_t *base);
  void PointerPress(); // F12 (any thread)
}

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;                     // +0 -> the session (+25 level), +24 -> the world
  constexpr uint32_t kWorldFirst = 43212, kWorldEnd = 43216; // world: object nodes (+4 object, +12 next)
  constexpr uint32_t kPlayerOne = 0x82E3C3E4;
  constexpr uint32_t kTorchLight = 5676;
  constexpr char kBuzz[] = "XACT_SOUNDBANK_STOCK_FLUOR_BUZZ";

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    uint8_t *p = base + a;
    p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && (a & 3u) == 0; }
  bool Readable(uint8_t *base, uint32_t address, uint32_t size)
  {
    // Sat 21 Nov (pdz_fix_readable_cache): the cached check (guest_readable_Sat21Nov.cpp); below = off.
    if (pdz::guestmem::CacheOn())
    {
      return pdz::guestmem::Readable(const_cast<uint8_t *>(base), address, size);
    }
    if (address < 0x10000u || uint64_t(address) + size > 0xFFFF0000ull)
    {
      return false;
    }
    for (uint32_t at = address; at < address + size; at = (at & ~0xFFFu) + 0x1000u)
    {
      size_t length = 1;
      rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
      if (!rex::memory::QueryProtect(base + at, length, access) ||
          (uint32_t(access) & uint32_t(rex::memory::PageAccess::kReadOnly)) == 0)
      {
        return false;
      }
    }
    return true;
  }

  // An object's template record (its vfunc +72), or 0.
  uint32_t RecordOf(PPCContext &ctx, uint8_t *base, uint32_t object)
  {
    // Sun 22 Nov night (audit): the object mapped, its vtable in the game image (82000000-83000000), and the slot a
    // code address there, not the pure-virtual handler (GuestFunction refuses that and non-code too).
    if (!pdz::guestmem::Ok(base, object, 4))
    {
      return 0;
    }
    const uint32_t vtable = Load32(base, object);
    if (vtable < 0x82000000u || vtable >= 0x83000000u || (vtable & 3u))
    {
      return 0;
    }
    const uint32_t slot = Load32(base, vtable + 72);
    if (slot < 0x82000000u || slot >= 0x83000000u || slot == 0x82AA2BC8u)
    {
      return 0;
    }
    PPCFunc *fn = pdz::GuestFunction(slot);
    if (!fn)
    {
      return 0;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = object;
    fn(frame.ctx, base);
    return Guest(frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
  }
  // An object's model name (setup +140), or "".
  std::string ModelOf(uint8_t *base, uint32_t object)
  {
    const uint32_t setup = Readable(base, object, 96) ? Load32(base, object + 84) : 0;
    const uint32_t name = Readable(base, setup, 144) ? Load32(base, setup + 140) : 0;
    if (!Readable(base, name, 96))
    {
      return {};
    }
    const char *c = reinterpret_cast<const char *>(base + name);
    std::string s;
    for (size_t n = 0; n < 95 && c[n] >= 32 && c[n] < 127; ++n)
    {
      s.push_back(c[n]);
    }
    return s;
  }

  // --- the fluorescent fittings ---
  struct Fitting
  {
    uint32_t prop = 0, vtable = 0, setup = 0;
    uint32_t sound = 0;
    float emit_y = 0.0f; // the height its buzz is made at (a fitting high above the player: lowered)
    float range_cm = 0.0f; // (Thu 19 Nov: its own range: its height above the player's floor + 2 m)
    bool no_light_logged = false;
    // (check run 4: at the edge of the range a buzz was started, cut by the sound manager and started again, twice a
    // second.) Not asked again before this time: 3 s after a refusal or after a buzz that died within a second.
    std::chrono::steady_clock::time_point retry{}, started{};
    // Fri 20 Nov night: a tube model (pdz_snd_fluor_buzz_tubes_only), its loudness for this hold, the takes played in
    // this hold (pdz_snd_fluor_buzz_hold), when the hold began, the next "why not" line.
    bool tube = false, not_tube_logged = false;
    float volume = 0.0f;
    uint32_t takes = 0;
    std::chrono::steady_clock::time_point held_since{}, why_at{};
  };
  std::vector<Fitting> g_fittings;                 // the game thread only
  std::unordered_map<uint32_t, uint32_t> g_known;  // object -> its vtable when it was named (any kind)
  uint32_t g_world = 0, g_level = 0xFFFFFFFFu, g_identity_lines = 0;
  std::chrono::steady_clock::time_point g_next_walk{}, g_next_buzz{};
  uint64_t g_buzz_starts = 0;

  // Thu 19 Nov: single pigeons (model aid_darkcharacter_xenon_props_animal_pigeon), e.g. on Nightclub's lamp posts.
  struct Pigeon
  {
    uint32_t prop = 0, vtable = 0, setup = 0;
    float pos[3] = {};
    std::chrono::steady_clock::time_point next{};
    bool gone = false;
    bool pool = false; // (Thu 19 Nov night: one of many birds parked at one spot: a flock's own birds, not a sitter)
  };
  std::vector<Pigeon> g_pigeons; // the game thread only
  std::atomic<bool> g_level_reset{false}; // Sun 22 Nov night (audit): set by pdz::r3::LevelReset (any thread)
  uint64_t g_pigeon_coos = 0;

  // A prop's health through its own virtual (vtable +332, r4 = 0), as lab_light_shot_log.cpp reads it; -1 = unknown.
  float PropHealth(PPCContext &ctx, uint8_t *base, uint32_t prop)
  {
    // Sun 22 Nov night (audit): the object mapped, its vtable in the game image, and the slot a code address there
    // (GuestFunction also refuses the pure-virtual handler 82AA2BC8).
    if (!pdz::guestmem::Ok(base, prop, 4))
    {
      return -1.0f;
    }
    const uint32_t vtable = Load32(base, prop);
    const uint32_t slot = vtable >= 0x82000000u && vtable < 0x83000000u && (vtable & 3u) == 0 ? Load32(base, vtable + 332) : 0;
    PPCFunc *fn = slot >= 0x82000000u && slot < 0x83000000u && slot != 0x82AA2BC8u ? pdz::GuestFunction(slot, "a prop's health read")
                                                                                  : nullptr;
    if (!fn)
    {
      return -1.0f;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = prop;
    frame.ctx.r4.u64 = 0;
    fn(frame.ctx, base);
    const float h = float(frame.ctx.f1.f64);
    return h == h ? h : -1.0f;
  }

  bool IsFitting(std::string_view tpl)
  {
    return tpl == "light\\general" || tpl == "light\\general_shootable" || tpl == "light\\general_shootable2";
  }
  // Fri 20 Nov night (log 032: the only buzzes were on light_bulbyell and light_pillarlight, a bulb and a pillar lamp:
  // the template light\general is also every other lamp's). The tube models of those templates (LIGHTS.md, Thu 19 Nov,
  // and the models in the play logs): striplight, striplightyell, striplight_green, trainstriplight,
  // electrical_warehouse_light, tunnellight; and any model named "fluor" or "tube".
  bool IsTubeModel(std::string model)
  {
    for (char &c : model)
    {
      c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return model.find("light_striplight") != std::string::npos || model.find("light_trainstriplight") != std::string::npos ||
           model.find("warehouse_light") != std::string::npos || model.find("light_tunnellight") != std::string::npos ||
           model.find("fluor") != std::string::npos || model.find("tube") != std::string::npos;
  }

  // Sun 22 Nov (pdz_snd_fluor_buzz_flicker_only): the animation mode of the fitting's light (its linked scene light
  // +812, else its own +800; mode at light +804: 0 steady, 1 blink, 2 pulse, 3 random flicker, 4 held), or -1.
  int LightMode(const uint8_t *base, const Fitting &f)
  {
    if (!pdz::guestmem::Ok(base, f.prop, 816)) // (Sun 22 Nov night (audit): a remembered fitting only while mapped)
    {
      return -1;
    }
    const uint32_t linked = Load32(base, f.prop + 812), own = Load32(base, f.prop + 800);
    const uint32_t light = Guest(linked) ? linked : Guest(own) ? own : 0;
    return light && pdz::guestmem::Ok(base, light, 808) ? int(Load32(base, light + 804)) : -1;
  }
  bool FlickerMode(int mode) { return mode == 1 || mode == 3; }
  const char *ModeName(int mode)
  {
    return mode == 0 ? "steady" : mode == 1 ? "blink" : mode == 2 ? "pulse" : mode == 3 ? "random flicker" : mode == 4 ? "held (steady)" : "unknown";
  }
  // May this fitting buzz at all (tube model, and a flickering light while pdz_snd_fluor_buzz_flicker_only)?
  bool BuzzAllowed(const uint8_t *base, const Fitting &f)
  {
    if (REXCVAR_GET(pdz_snd_fluor_buzz_tubes_only) && !f.tube)
    {
      return false;
    }
    return !REXCVAR_GET(pdz_snd_fluor_buzz_flicker_only) || FlickerMode(LightMode(base, f));
  }
  uint32_t g_flicker_kept = 0, g_flicker_dropped = 0;

  // 1 lit, 0 not lit, -1 the fitting has no light we can read.
  int Lit(const uint8_t *base, const Fitting &f)
  {
    if (!pdz::guestmem::Ok(base, f.prop, 816)) // (Sun 22 Nov night (audit): a remembered fitting only while mapped)
    {
      return -1;
    }
    const uint32_t linked = Load32(base, f.prop + 812), own = Load32(base, f.prop + 800);
    const uint32_t light = Guest(linked) ? linked : Guest(own) ? own : 0;
    if (!light || !pdz::guestmem::Ok(base, light, 0x598)) // (Sun 22 Nov night (audit): the light only where mapped)
    {
      return -1;
    }
    const uint32_t a = Load32(base, light + 0x590), b = Load32(base, light + 0x594);
    return (a == 1 || a == 2 || b == 1 || b == 2) ? 1 : 0;
  }

  void StopAllBuzz(PPCContext &ctx, uint8_t *base)
  {
    for (Fitting &f : g_fittings)
    {
      pdz::snd::Stop(ctx, base, f.sound);
    }
  }

  void WalkWorld(PPCContext &ctx, uint8_t *base, uint32_t world)
  {
    const uint32_t end = Load32(base, world + kWorldEnd);
    uint32_t node = Load32(base, world + kWorldFirst);
    int named = 0;
    for (int i = 0; Guest(node) && node != end && i < 16384 && named < 400; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      if (!Guest(o) || !pdz::guestmem::Ok(base, o, 4)) // (Sun 22 Nov night (audit): only where mapped)
      {
        continue;
      }
      const uint32_t vtable = Load32(base, o);
      const auto it = g_known.find(o);
      if (it != g_known.end() && it->second == vtable)
      {
        continue;
      }
      g_known[o] = vtable;
      ++named; // (a few syscalls each: at most 400 new objects a walk, so a level start is spread over seconds)
      // (Only a placed prop is asked for its template: its setup names a "props_" model. Characters and the other
      // classes of the world list are never asked.)
      const std::string model = ModelOf(base, o);
      if (model.find("props_") == std::string::npos)
      {
        continue;
      }
      // Sat 21 Nov evening (the owner: a placed pigeon on Subway was never found): Rare's model names differ in case
      // (Nightclub "..._props_animal_pigeon", Subway "..._props_Animal_pigeon"): matched without case now.
      std::string lower = model;
      for (char &ch : lower)
      {
        ch = char(std::tolower(static_cast<unsigned char>(ch)));
      }
      if (lower.find("animal_pigeon") != std::string::npos && g_pigeons.size() < 192)
      {
        // Thu 19 Nov: a single pigeon (an animating prop, not a flock spawner), found by its own model.
        Pigeon p;
        p.prop = o;
        p.vtable = vtable;
        p.setup = Load32(base, o + 84);
        for (int k = 0; k < 3; ++k)
        {
          p.pos[k] = LoadF(base, o + 48 + 4 * k);
        }
        p.next = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000 + int(std::rand() % 6000));
        // (Thu 19 Nov night, log 025: 60 of the 64 "single pigeons" sat at one spot, (-12 26 -8), near the level's
        // origin: the flocks' own birds, parked there until a flock takes off, and ten of them cooed at once when Jo
        // passed 10 m from it at 21:21:23. Birds that share a spot are a flock's, never a sitter: no coo.)
        for (Pigeon &q : g_pigeons)
        {
          const float dx = q.pos[0] - p.pos[0], dy = q.pos[1] - p.pos[1], dz = q.pos[2] - p.pos[2];
          if (dx * dx + dy * dy + dz * dz < 30.0f * 30.0f)
          {
            if (!q.pool)
            {
              REXLOG_INFO("pdz_snd_pigeon_prop: more than one pigeon at ({:.0f} {:.0f} {:.0f}): a flock's own birds parked there, "
                          "not a sitting pigeon: no coo from them",
                          q.pos[0], q.pos[1], q.pos[2]);
            }
            q.pool = true;
            p.pool = true;
          }
        }
        g_pigeons.push_back(p);
        REXLOG_INFO("pdz_snd_pigeon_prop: a single pigeon (model {}; class {:08X}) sits at ({:.0f} {:.0f} {:.0f}) ({} this level)",
                    model, vtable, p.pos[0], p.pos[1], p.pos[2], g_pigeons.size());
        continue;
      }
      const uint32_t rec = RecordOf(ctx, base, o);
      if (!rec)
      {
        continue;
      }
      const std::string tpl = pdz::prop_contacts::TemplateOf(base, rec);
      if (!IsFitting(tpl) || g_fittings.size() >= 256 || !pdz::guestmem::Ok(base, o, 816)) // (Sun 22 Nov night (audit): +812)
      {
        continue;
      }
      Fitting f;
      f.prop = o;
      f.vtable = vtable;
      f.setup = Load32(base, o + 84);
      f.tube = IsTubeModel(model);
      g_fittings.push_back(f);
      // Sun 22 Nov (pdz_snd_fluor_buzz_flicker_only): one line per tube fitting that will buzz, a count of the dropped.
      if (f.tube)
      {
        const int mode = LightMode(base, f);
        if (FlickerMode(mode) || !REXCVAR_GET(pdz_snd_fluor_buzz_flicker_only))
        {
          ++g_flicker_kept;
          REXLOG_INFO("pdz_snd_fluor_buzz_flicker: BUZZES: fitting at ({:.0f} {:.0f} {:.0f}), model {}, its light's mode {} ({}) | this "
                      "level: {} fittings buzz, {} steady ones dropped",
                      LoadF(base, o + 48), LoadF(base, o + 52), LoadF(base, o + 56), ModelOf(base, o), mode, ModeName(mode),
                      g_flicker_kept, g_flicker_dropped);
        }
        else
        {
          const uint32_t n = ++g_flicker_dropped;
          if ((n & (n - 1)) == 0)
          {
            REXLOG_INFO("pdz_snd_fluor_buzz_flicker: no buzz: fitting at ({:.0f} {:.0f} {:.0f}), model {}, its light's mode {} ({}) | "
                        "this level: {} fittings buzz, {} steady ones dropped",
                        LoadF(base, o + 48), LoadF(base, o + 52), LoadF(base, o + 56), ModelOf(base, o), mode, ModeName(mode),
                        g_flicker_kept, n);
          }
        }
      }
      if (g_identity_lines < 16)
      {
        ++g_identity_lines;
        REXLOG_INFO("pdz_snd_fluor_buzz: a light fitting ({}; model {}: {}; class {:08X}) at ({:.0f} {:.0f} {:.0f}): linked light "
                    "{:08X}, own light {:08X}, now {} ({} fittings found so far this level)",
                    tpl, ModelOf(base, o),
                    f.tube ? "a tube model" : "NOT a tube model: no buzz while pdz_snd_fluor_buzz_tubes_only is on", vtable,
                    LoadF(base, o + 48), LoadF(base, o + 52), LoadF(base, o + 56),
                    Load32(base, o + 812), Load32(base, o + 800),
                    Lit(base, f) == 1 ? "lit" : Lit(base, f) == 0 ? "not lit" : "no light of its own to read: it will not buzz",
                    g_fittings.size());
      }
    }
  }

  // Thu 19 Nov: a single pigeon coos now and then while it sits (the game's own coo, quiet, within
  // pdz_snd_pigeon_prop_coo_m of a player). Never once it has been shot (health 0), has moved (it flew or fell) or
  // is gone.
  void PigeonTick(PPCContext &ctx, uint8_t *base)
  {
    if (!REXCVAR_GET(pdz_restored_sounds) || !REXCVAR_GET(pdz_snd_pigeon_prop_coo) || g_pigeons.empty())
    {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    const float start_extra = float(REXCVAR_GET(pdz_snd_pigeon_prop_coo_m)) * 100.0f;
    const float range_extra = float(REXCVAR_GET(pdz_snd_pigeon_prop_coo_range_m)) * 100.0f;
    const float volume = float(REXCVAR_GET(pdz_snd_pigeon_prop_coo_volume));
    for (Pigeon &p : g_pigeons)
    {
      if (p.gone || p.pool || now < p.next)
      {
        continue;
      }
      p.next = now + std::chrono::milliseconds(2000);
      // (Sun 22 Nov night (audit): a remembered pigeon only while its memory is mapped; else it is gone.)
      const bool mapped = pdz::guestmem::Ok(base, p.prop, 88);
      const bool zeroed = mapped && Load32(base, p.prop + 48) == 0 && Load32(base, p.prop + 52) == 0 && Load32(base, p.prop + 56) == 0;
      if (!mapped || Load32(base, p.prop) != p.vtable || Load32(base, p.prop + 84) != p.setup || zeroed)
      {
        p.gone = true;
        REXLOG_INFO("pdz_snd_pigeon_prop: the pigeon at ({:.0f} {:.0f} {:.0f}) is gone: no more coo", p.pos[0], p.pos[1], p.pos[2]);
        continue;
      }
      const float at[3] = {LoadF(base, p.prop + 48), LoadF(base, p.prop + 52), LoadF(base, p.prop + 56)};
      const float dx = at[0] - p.pos[0], dy = at[1] - p.pos[1], dz = at[2] - p.pos[2];
      if (dx * dx + dy * dy + dz * dz > 50.0f * 50.0f)
      {
        p.gone = true;
        REXLOG_INFO("pdz_snd_pigeon_prop: the pigeon from ({:.0f} {:.0f} {:.0f}) has moved (it flew off or fell): no more coo",
                    p.pos[0], p.pos[1], p.pos[2]);
        continue;
      }
      // (Thu 19 Nov night, log 025 21:16:23-21:17:19: five coos at 6.8-9.9 m from the lamp-post pigeon, loudness 0.30,
      // range 12 m: about a tenth loud, unheard. The bird sits about 8 m up and the game's 3D sound fades in a straight
      // line to nothing at its range, so its height above the player's floor now counts, as for the buzz: the sound's
      // range is that height + pdz_snd_pigeon_prop_coo_range_m, it coos when a player is within that height +
      // pdz_snd_pigeon_prop_coo_m, loudness 1.0: about 0.6 standing under a bird 8 m up.)
      float ears[3] = {};
      const float d = pdz::snd::NearestPlayerCm(base, at, ears);
      const float floor_y = ears[1] - 80.0f; // (her body point is about 80 cm above her feet)
      const float height = std::clamp(at[1] - floor_y, 0.0f, 3000.0f);
      if (d < 0.0f || d > height + start_extra)
      {
        continue; // (nobody near: asked again in 2 s)
      }
      const float health = PropHealth(ctx, base, p.prop);
      if (health == 0.0f)
      {
        p.gone = true;
        REXLOG_INFO("pdz_snd_pigeon_prop: the pigeon at ({:.0f} {:.0f} {:.0f}) was shot (health 0): no more coo", at[0], at[1], at[2]);
        continue;
      }
      p.next = now + std::chrono::milliseconds(8000 + int(std::rand() % 12000));
      float range_m = (height + range_extra) / 100.0f;
      // (Fri 20 Nov night, pdz_snd_pigeon_coo_fix: measured from the listener, a range that gives it most of the
      // loudness, and the gain over the bank's quiet coo.)
      const bool fix = REXCVAR_GET(pdz_snd_pigeon_coo_fix);
      const float lis_cm = pdz::snd::ListenerCm(base, at);
      float loud = volume;
      if (fix && lis_cm >= 0.0f)
      {
        const float ear = float(REXCVAR_GET(pdz_snd_pigeon_coo_ear));
        range_m = std::min(60.0f, std::max(range_m, lis_cm / 100.0f / (1.0f - ear)));
        loud = std::min(4.0f, volume * float(REXCVAR_GET(pdz_snd_pigeon_coo_gain)));
      }
      // Sat 21 Nov evening (pdz_snd_pigeon_coo_range_max_m, as the flocks: a Subway pigeon 25 m up cooed with a 60 m
      // range, 30 m away): at most that many metres, or the bird's height + 5 m when that is more.
      if (const float cap = float(REXCVAR_GET(pdz_snd_pigeon_coo_range_max_m)); cap > 0.0f)
      {
        range_m = std::min(range_m, std::max(cap, (height + 500.0f) / 100.0f));
      }
      pdz::snd::CooRangeM(range_m);
      const pdz::snd::Result r =
          fix ? pdz::snd::Timed(ctx, base, "XACT_SOUNDBANK_STOCK_PIGEON_COO", at, loud, float(REXCVAR_GET(pdz_snd_pigeon_coo_take_s)))
              : pdz::snd::Once(ctx, base, "XACT_SOUNDBANK_STOCK_PIGEON_COO", at, loud);
      pdz::snd::CooRangeM(0.0f);
      const int state = pdz::snd::LastOnceState();
      const float from = fix && lis_cm >= 0.0f ? lis_cm : d;
      const float at_jo = loud * loud * std::max(0.0f, 1.0f - from / (range_m * 100.0f)); // (heard squared)
      const uint64_t n = ++g_pigeon_coos;
      REXLOG_INFO("pdz_snd_pigeon_prop: the pigeon at ({:.0f} {:.0f} {:.0f}), {:.1f} m above the nearest player's floor, coos; that "
                  "player {:.1f} m away, the listener (camera) {:.1f} m (loudness {:.2f}{}, range {:.1f} m: about {:.2f} loud at the "
                  "listener): STOCK_PIGEON_COO {}{} [{}] ({} so far)",
                  at[0], at[1], at[2], height / 100.0f, d / 100.0f, lis_cm / 100.0f, loud,
                  fix ? " = the game's x the coo gain" : "", range_m, at_jo, pdz::snd::Text(r),
                  state == 6 || state == 8 ? fmt::format(" BUT CUT AT ONCE by the game's sound manager (state {})", state) : std::string(),
                  pdz::snd::Load(base, 10), n);
    }
  }

  void BuzzTick(PPCContext &ctx, uint8_t *base)
  {
    const uint32_t session = Load32(base, kGame);
    const uint32_t world = Load32(base, kGame + 24);
    const uint32_t level = Guest(session) ? base[session + 25] : 0xFFFFFFFFu;
    // (Sun 22 Nov night (audit): a level start / restart (pdz::r3::LevelReset) also forgets the fittings and pigeons.)
    if (world != g_world || level != g_level || g_level_reset.exchange(false, std::memory_order_relaxed))
    {
      StopAllBuzz(ctx, base);
      g_fittings.clear();
      g_known.clear();
      g_world = world;
      g_level = level;
      g_identity_lines = 0;
      g_flicker_kept = 0, g_flicker_dropped = 0;
      g_pigeons.clear(); // (Thu 19 Nov)
    }
    const bool on = REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_fluor_buzz);
    if (!on || !Guest(world))
    {
      StopAllBuzz(ctx, base);
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now >= g_next_walk)
    {
      g_next_walk = now + std::chrono::milliseconds(500);
      if (g_known.size() > 60000)
      {
        g_known.clear();
      }
      WalkWorld(ctx, base, world);
    }
    // Fri 20 Nov night (log 032, the Laboratory 22:12:29-32: the same fitting "started" four times in 3.6 s).
    // pdz_snd_fluor_buzz_hold: the cue is a set of 8 short takes (0.4 to 1.6 s, Rare's variation "random, no repeat"
    // in stock.xsb); a take that ended was asked again only at the next 250 ms pick, and one shorter than 1 s was taken
    // for a refusal and kept silent 3 s. Now, every update, a held buzz whose take has ended gets the next take at once
    // while its tube is lit and the listener is within its range: one steady sound until the player leaves.
    const bool hold = REXCVAR_GET(pdz_snd_fluor_buzz_hold);
    const bool from_listener = REXCVAR_GET(pdz_snd_fluor_buzz_listener);
    const bool tubes_only = REXCVAR_GET(pdz_snd_fluor_buzz_tubes_only);
    auto stop_buzz = [&](Fitting &f, const char *reason, float d) {
      if (!f.sound)
      {
        return;
      }
      pdz::snd::Stop(ctx, base, f.sound);
      if (now >= f.why_at && pdz::guestmem::Ok(base, f.prop, 60)) // (Sun 22 Nov night (audit): its place read only where mapped)
      {
        f.why_at = now + std::chrono::seconds(5);
        REXLOG_INFO("pdz_snd_fluor_buzz: the fitting at ({:.0f} {:.0f} {:.0f}): STOCK_FLUOR_BUZZ stopped ({}; the {} {:.1f} m from it, "
                    "range {:.1f} m; held {:.1f} s, {} take(s))",
                    LoadF(base, f.prop + 48), f.emit_y, LoadF(base, f.prop + 56), reason, from_listener ? "listener" : "nearest player",
                    d / 100.0f, f.range_cm / 100.0f, std::chrono::duration<double>(now - f.held_since).count(), f.takes);
      }
    };
    if (hold)
    {
      for (Fitting &f : g_fittings)
      {
        if (!f.sound || pdz::snd::Alive(base, f.sound))
        {
          continue;
        }
        pdz::snd::Stop(ctx, base, f.sound); // (its take has ended: freed)
        if (!pdz::guestmem::Ok(base, f.prop, 88))
        {
          continue; // (Sun 22 Nov night (audit): a remembered fitting only while mapped; the pick below drops it)
        }
        const float at[3] = {LoadF(base, f.prop + 48), f.emit_y, LoadF(base, f.prop + 56)};
        const float d = from_listener ? pdz::snd::ListenerCm(base, at) : pdz::snd::NearestPlayerCm(base, at);
        if (Lit(base, f) != 1 || d < 0.0f || d > f.range_cm || !BuzzAllowed(base, f))
        {
          continue; // (dark, or the listener past the range: the pick below decides)
        }
        pdz::snd::Result why;
        pdz::snd::BuzzRangeM(f.range_cm / 100.0f);
        f.sound = pdz::snd::Start(ctx, base, kBuzz, at, from_listener ? f.volume : float(REXCVAR_GET(pdz_snd_fluor_buzz_volume)), why, 24);
        pdz::snd::BuzzRangeM(0.0f);
        if (f.sound)
        {
          ++f.takes;
          f.started = now;
        }
        else
        {
          f.retry = now + std::chrono::seconds(1);
          if (now >= f.why_at)
          {
            f.why_at = now + std::chrono::seconds(5);
            REXLOG_INFO("pdz_snd_fluor_buzz: the fitting at ({:.0f} {:.0f} {:.0f}): its next take {} (the {} {:.1f} m from it, range "
                        "{:.1f} m; {})",
                        at[0], at[1], at[2], pdz::snd::Text(why), from_listener ? "listener" : "nearest player", d / 100.0f,
                        f.range_cm / 100.0f, pdz::snd::Load(base, 10));
          }
        }
      }
    }
    if (now < g_next_buzz || g_fittings.empty())
    {
      return;
    }
    g_next_buzz = now + std::chrono::milliseconds(250);
    const float range_default = float(REXCVAR_GET(pdz_snd_fluor_buzz_range_m)) * 100.0f;
    const int max = REXCVAR_GET(pdz_snd_fluor_buzz_max);
    struct Near
    {
      float d;
      size_t i;
    };
    std::array<Near, 6> best{};
    int n_best = 0;
    std::vector<float> dist(g_fittings.size(), -1.0f); // (this pick's distance of each fitting, for the lines below)
    for (size_t i = 0; i < g_fittings.size();)
    {
      Fitting &f = g_fittings[i];
      // (Sun 22 Nov night (audit): a remembered fitting only while its memory is mapped; else it is gone.)
      const bool mapped = pdz::guestmem::Ok(base, f.prop, 88);
      const bool zeroed = mapped && Load32(base, f.prop + 48) == 0 && Load32(base, f.prop + 52) == 0 && Load32(base, f.prop + 56) == 0;
      if (!mapped || Load32(base, f.prop) != f.vtable || Load32(base, f.prop + 84) != f.setup || zeroed)
      {
        pdz::snd::Stop(ctx, base, f.sound); // the fitting is gone (smashed)
        g_known.erase(f.prop);
        f = g_fittings.back();
        g_fittings.pop_back();
        dist.pop_back();
        continue;
      }
      const int lit = Lit(base, f);
      const float at[3] = {LoadF(base, f.prop + 48), LoadF(base, f.prop + 52), LoadF(base, f.prop + 56)};
      // (Wed 18 Nov night: log 021, the Subway garage: no buzz was ever asked for there, because the fittings hang
      // more than the start distance above the floor and the distance was measured in 3D. A fitting above the
      // player (up to 12 m) is now measured across the floor; its buzz is made under it, at most 3 m above the
      // player, so that the cue's own short range lets it be heard.)
      // (Thu 19 Nov, log 022 06:58:13: started at 4.5 m with a 5 m range and loudness 0.35 under fittings about 5 m up:
      // Rare's 3D gain falls in a straight line to nothing at the range, so it was inaudible. Now the buzz is made at
      // the fitting itself and its range is the fitting's height above the player's floor (her body point is about
      // 80 cm above her feet) + 2 m; the distance is the real one.)
      // (Fri 20 Nov night, pdz_snd_fluor_buzz_listener: the distance is the listener's (the camera: what the game's
      // sound manager measures and fades from), the range its height above the camera + pdz_snd_fluor_buzz_extra_m, the
      // loudness such that pdz_snd_fluor_buzz_at_ear reaches the camera straight under it.)
      float where[3] = {};
      const float d = from_listener ? pdz::snd::ListenerCm(base, at, where) : pdz::snd::NearestPlayerCm(base, at, where);
      dist[i] = d;
      f.emit_y = at[1];
      if (!f.sound && d >= 0.0f)
      {
        if (from_listener)
        {
          pdz::snd::UnderPlan(at[1], where[1], float(REXCVAR_GET(pdz_snd_fluor_buzz_extra_m)) * 100.0f, 300.0f,
                              float(REXCVAR_GET(pdz_snd_fluor_buzz_at_ear)), 1.0f, f.range_cm, f.volume);
        }
        else
        {
          const float floor_y = where[1] - 80.0f;
          const float height = std::clamp(at[1] - floor_y, 0.0f, 1200.0f);
          f.range_cm = std::max(300.0f, height + 200.0f); // (the range picker gets it at the start: BuzzRangeM)
          f.volume = float(REXCVAR_GET(pdz_snd_fluor_buzz_volume));
        }
      }
      const float range = f.range_cm > 0.0f ? f.range_cm : range_default;
      const bool playing = pdz::snd::Alive(base, f.sound);
      if (!playing && f.sound)
      {
        pdz::snd::Stop(ctx, base, f.sound); // (it has ended, or was freed with the level)
        if (!hold && now - f.started < std::chrono::seconds(1))
        {
          f.retry = now + std::chrono::seconds(3); // (cut at once: the listener is past the range)
        }
      }
      if (!BuzzAllowed(base, f))
      {
        stop_buzz(f, (tubes_only && !f.tube) ? "not a tube model (pdz_snd_fluor_buzz_tubes_only)"
                                             : "its light does not flicker (pdz_snd_fluor_buzz_flicker_only)", d);
        if (!f.not_tube_logged && d >= 0.0f && d < 400.0f)
        {
          f.not_tube_logged = true;
          REXLOG_INFO("pdz_snd_fluor_buzz: the player is {:.1f} m from the fitting at ({:.0f} {:.0f} {:.0f}) (model {}): no buzz, it is "
                      "not a tube model (a bulb or lamp of the same template)",
                      d / 100.0f, at[0], at[1], at[2], ModelOf(base, f.prop));
        }
        ++i;
        continue;
      }
      if (lit != 1 && d >= 0.0f && d < 300.0f && !f.no_light_logged)
      {
        f.no_light_logged = true; // (once per fitting: why a fitting the player stands at stays silent)
        REXLOG_INFO("pdz_snd_fluor_buzz: the player is {:.1f} m from the fitting at ({:.0f} {:.0f} {:.0f}) (model {}) and it does "
                    "NOT buzz: {}",
                    d / 100.0f, at[0], at[1], at[2], ModelOf(base, f.prop),
                    lit == 0 ? "its light is not lit (shot out or switched off)" : "it has no light of its own that can be read");
      }
      // Started only well inside the range (1 m in), kept to 2 m past it.
      if (lit != 1 || d < 0.0f || d > (playing ? range + 200.0f : range - 100.0f) || (!playing && now < f.retry))
      {
        stop_buzz(f, lit != 1 ? "its light is out" : "the player has gone", d); // shot out, switched off, or the players have gone
        ++i;
        continue;
      }
      // One of the nearest `max`?
      int at_rank = n_best;
      while (at_rank > 0 && best[at_rank - 1].d > d)
      {
        --at_rank;
      }
      if (at_rank < max)
      {
        for (int k = std::min(n_best, max - 1); k > at_rank; --k)
        {
          best[k] = best[k - 1];
        }
        best[at_rank] = {d, i};
        n_best = std::min(n_best + 1, max);
      }
      ++i;
    }
    // Those not among the nearest stop; the nearest start.
    for (size_t i = 0; i < g_fittings.size(); ++i)
    {
      bool chosen = false;
      for (int k = 0; k < n_best; ++k)
      {
        chosen = chosen || best[k].i == i;
      }
      Fitting &f = g_fittings[i];
      if (!chosen)
      {
        stop_buzz(f, "others are nearer (pdz_snd_fluor_buzz_max)", dist[i]);
        continue;
      }
      if (f.sound)
      {
        continue;
      }
      const float at[3] = {LoadF(base, f.prop + 48), f.emit_y, LoadF(base, f.prop + 56)};
      const float volume = from_listener ? f.volume : float(REXCVAR_GET(pdz_snd_fluor_buzz_volume));
      pdz::snd::Result why;
      pdz::snd::BuzzRangeM(f.range_cm > 0.0f ? f.range_cm / 100.0f : 0.0f); // (Thu 19 Nov: its own range)
      f.sound = pdz::snd::Start(ctx, base, kBuzz, at, volume, why, 24);
      pdz::snd::BuzzRangeM(0.0f);
      f.started = now;
      f.held_since = now;
      f.takes = f.sound ? 1 : 0;
      if (!f.sound)
      {
        f.retry = now + std::chrono::seconds(3);
      }
      const uint64_t n = ++g_buzz_starts;
      // (Wed 18 Nov night: every start or refusal is logged for the first 40, then each power of two: the log must
      // show whether a buzz was asked for. Fri 20 Nov night: with pdz_snd_fluor_buzz_hold a start is a new hold; the
      // takes that follow are counted in its stop line.)
      if (n <= 40 || (n & (n - 1)) == 0)
      {
        const float d = dist[i];
        REXLOG_INFO("pdz_snd_fluor_buzz: the lit tube fitting at ({:.0f} {:.0f} {:.0f}) (model {}), the {} {:.1f} m from it: "
                    "STOCK_FLUOR_BUZZ {} (loudness {:.2f}, range {:.1f} m = {}: about {:.2f} at the listener now; at most {} at "
                    "once; {}{} starts asked so far)",
                    at[0], at[1], at[2], ModelOf(base, f.prop), from_listener ? "listener" : "nearest player", d / 100.0f,
                    pdz::snd::Text(why), volume, f.range_cm / 100.0f,
                    from_listener ? "its height above the camera + pdz_snd_fluor_buzz_extra_m" : "its height above the player's floor + 2 m",
                    d >= 0.0f && f.range_cm > 0.0f ? volume * volume * std::max(0.0f, 1.0f - d / f.range_cm) : 0.0f, max,
                    f.sound ? (hold ? "held: the next take starts as one ends; " : "") : pdz::snd::Load(base, 10) + "; ", n);
      }
    }
  }

  // --- the F12 pointer ---
  std::atomic<uint32_t> g_presses{0};

  void Pointer(PPCContext &ctx, uint8_t *base)
  {
    const uint32_t jo = Load32(base, kPlayerOne);
    const uint32_t world = Load32(base, kGame + 24);
    constexpr uint32_t kPhysicsWorld = 0x82E3C2F4; // sub_82259B80 reads [82E3C2F4] and [82E3C2F0]
    if (!Guest(jo) || !Guest(world) || !Guest(Load32(base, kPhysicsWorld)) || !Guest(Load32(base, kPhysicsWorld - 4)))
    {
      REXLOG_INFO("pdz_pointer: F12: no player in a level: nothing to point at");
      return;
    }
    float from[3], to[3];
    for (int k = 0; k < 3; ++k)
    {
      from[k] = LoadF(base, jo + 0x1FA0 + 4 * k);
      to[k] = LoadF(base, jo + 0x1FAC + 4 * k);
    }
    // Guest stack scratch below the caller's frame (as the stealth layer's ray): from +0, to +16, the collector +32.
    const uint32_t buf = (ctx.r1.u32 - 0x600) & ~0xFu;
    std::memset(base + buf, 0, 0x180);
    for (uint32_t k = 0; k < 3; ++k)
    {
      Store32(base, buf + 4 * k, std::bit_cast<uint32_t>(from[k]));
      Store32(base, buf + 16 + 4 * k, std::bit_cast<uint32_t>(to[k]));
    }
    const uint32_t coll = buf + 32;
    Store32(base, coll, 0x82090738); // Rare's closest-hit collector (the hitscan sub_8223FC00 builds it)
    bool hit = false;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r1.u64 = buf - 0x800;
      frame.ctx.r3.u64 = 1; // what bullets hit
      frame.ctx.r4.u64 = buf;
      frame.ctx.r5.u64 = buf + 16;
      frame.ctx.r6.u64 = coll;
      frame.ctx.r7.u64 = 1;
      sub_82259B80(frame.ctx, base);
      hit = (frame.ctx.r3.u32 & 0xFF) != 0;
    }
    float at[3] = {to[0], to[1], to[2]};
    uint32_t surface = 0, record = 0;
    if (hit)
    {
      for (int k = 0; k < 3; ++k)
      {
        at[k] = LoadF(base, coll + 68 + 4 * k);
      }
      surface = (uint32_t(base[coll + 104]) << 8) | base[coll + 105];
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = surface;
      sub_824F4E68(frame.ctx, base);
      record = Guest(frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
    }
    auto name_at = [&](uint32_t off) -> std::string {
      const uint32_t p = record ? Load32(base, record + off) : 0;
      if (p < 0x10000u || p >= 0xF0000000u || !Readable(base, p, 64)) // (text: any alignment)
      {
        return "(none)";
      }
      const char *t = reinterpret_cast<const char *>(base + p);
      const std::string s(t, strnlen(t, 64));
      return s.empty() ? std::string("(empty)") : s;
    };
    const float aim = std::sqrt((at[0] - from[0]) * (at[0] - from[0]) + (at[1] - from[1]) * (at[1] - from[1]) +
                                (at[2] - from[2]) * (at[2] - from[2]));
    // The placed objects nearest the hit place.
    struct Cand
    {
      float d;
      uint32_t o;
    };
    std::vector<Cand> near;
    const uint32_t end = Load32(base, world + kWorldEnd);
    uint32_t node = Load32(base, world + kWorldFirst);
    for (int i = 0; Guest(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      if (!Guest(o) || o == jo || !pdz::guestmem::Ok(base, o, 60)) // (Sun 22 Nov night (audit): only where mapped)
      {
        continue;
      }
      const float dx = LoadF(base, o + 48) - at[0], dy = LoadF(base, o + 52) - at[1], dz = LoadF(base, o + 56) - at[2];
      const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
      if (d == d && d < 500.0f)
      {
        near.push_back({d, o});
      }
    }
    std::sort(near.begin(), near.end(), [](const Cand &a, const Cand &b) { return a.d < b.d; });
    // Only things with a model of their own count (the list also holds pads, triggers and the like): the nearest
    // 24 are asked for their model name.
    {
      std::vector<Cand> named;
      for (size_t i = 0; i < near.size() && i < 24 && named.size() < 4; ++i)
      {
        if (!ModelOf(base, near[i].o).empty())
        {
          named.push_back(near[i]);
        }
      }
      near.swap(named);
    }
    const bool object = hit && !near.empty() && near[0].d <= 150.0f;
    if (!hit)
    {
      REXLOG_INFO("pdz_pointer: F12: the crosshair's line hit nothing within its 250 m (the sky, or beyond): Jo at ({:.0f} {:.0f} "
                  "{:.0f})",
                  LoadF(base, jo + 48), LoadF(base, jo + 52), LoadF(base, jo + 56));
      return;
    }
    REXLOG_INFO("pdz_pointer: F12: under the crosshair at ({:.0f} {:.0f} {:.0f}), {:.1f} m from the eye: surface {} (kind {}; "
                "impact {}, ricochet {}) | {} | Jo at ({:.0f} {:.0f} {:.0f})",
                at[0], at[1], at[2], aim / 100.0f, surface, record ? int32_t(Load32(base, record)) : -1, name_at(140), name_at(144),
                object ? fmt::format("most likely the placed object {:.0f} cm from that place (next line)", near[0].d)
                       : std::string("LEVEL SCENERY: no placed object within 1.5 m of that place (the engine gives no mesh or "
                                     "material name for scenery; the surface above is all it knows)"),
                LoadF(base, jo + 48), LoadF(base, jo + 52), LoadF(base, jo + 56));
    for (size_t i = 0; i < near.size() && i < 4; ++i)
    {
      const uint32_t o = near[i].o;
      const std::string model = ModelOf(base, o);
      const uint32_t rec = model.find("props_") != std::string::npos ? RecordOf(ctx, base, o) : 0; // (placed props only)
      const std::string tpl = rec ? pdz::prop_contacts::TemplateOf(base, rec) : std::string();
      REXLOG_INFO("pdz_pointer:   object {}: {:.0f} cm from that place: prop type {}, model {}, class {:08X}, kind number {}, at "
                  "({:.0f} {:.0f} {:.0f})",
                  i + 1, near[i].d, tpl.empty() ? "(no template path)" : tpl, model.empty() ? "(no model name)" : model,
                  Load32(base, o), Load32(base, o + 12), LoadF(base, o + 48), LoadF(base, o + 52), LoadF(base, o + 56));
    }
  }
} // namespace

namespace pdz::r3
{
  void PointerPress() { g_presses.fetch_add(1, std::memory_order_relaxed); }

  void Tick(PPCContext &ctx, uint8_t *base)
  {
    BuzzTick(ctx, base);
    PigeonTick(ctx, base); // (Thu 19 Nov)
    if (g_presses.load(std::memory_order_relaxed))
    {
      g_presses.store(0, std::memory_order_relaxed);
      if (REXCVAR_GET(pdz_pointer_log))
      {
        Pointer(ctx, base);
      }
      void SoundHeardF12(PPCContext &, uint8_t *); SoundHeardF12(ctx, base); // pdz_snd_heard_log: what can I hear here (sound_heard_Fri20Nov.cpp)
    }
  }

  // Sun 22 Nov night (audit): from the level music setup (music_restore.cpp): every level start, a restart included.
  void LevelReset() { g_level_reset.store(true, std::memory_order_relaxed); }
} // namespace pdz::r3

// The character update that makes and removes the torch's light.
namespace
{
  std::chrono::steady_clock::time_point g_torch_last{};
  uint64_t g_torch_n = 0;
}
REX_HOOK_RAW(sub_8254C5A0)
{
  const uint32_t c = ctx.r3.u32;
  const bool ok = Guest(c);
  const bool before = ok && Load32(base, c + kTorchLight) != 0;
  __imp__sub_8254C5A0(ctx, base);
  if (!ok)
  {
    return;
  }
  const bool after = Load32(base, c + kTorchLight) != 0;
  if (after == before || !REXCVAR_GET(pdz_restored_sounds) || !REXCVAR_GET(pdz_snd_torch))
  {
    return;
  }
  const auto now = std::chrono::steady_clock::now();
  if (now - g_torch_last < std::chrono::milliseconds(80))
  {
    return; // (two characters in one frame, or a flicker: one click)
  }
  g_torch_last = now;
  const bool local = (Load32(base, c + 64) & 2u) != 0; // Rare's own "the local player" test
  const float at[3] = {LoadF(base, c + 48), LoadF(base, c + 52) + 120.0f, LoadF(base, c + 56)};
  const char *cue = after ? "XACT_SOUNDBANK_GUNS_TORCH_ON" : "XACT_SOUNDBANK_GUNS_TORCH_OFF";
  const pdz::snd::Result r = pdz::snd::Once(ctx, base, cue, local ? nullptr : at);
  const uint64_t n = ++g_torch_n;
  if ((n & (n - 1)) == 0 || r != pdz::snd::Result::kStarted)
  {
    REXLOG_INFO("pdz_snd_torch: the torch of {} went {}: {} {} ({} so far)", local ? "the local player" : "a character",
                after ? "on" : "off", after ? "GUNS_TORCH_ON" : "GUNS_TORCH_OFF", pdz::snd::Text(r), n);
  }
}
