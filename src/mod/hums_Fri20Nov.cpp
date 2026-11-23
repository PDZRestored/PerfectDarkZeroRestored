// Restored hums by object (Fri 20 Nov 2026; every candidate of the sound radius audit, as the tester chose).
// TU3 addresses. Needs pdz_restored_sounds; each family has its own switch (on). Only Rare's own recordings, only on
// objects that really are in the level, found by their own MODEL name (setup +140), never by a hand-picked place.
//
//   pdz_snd_computer_hum   STOCK_COMPUTER_LARGE_LP  models electrical_mainframe, electrical_computer_midsize_lab
//                          (Datacore, Laboratory, Mansion, Trinity 1 and 2)
//   pdz_snd_vending_hum    STOCK_VEND_HUM           model container_drinks_dispenser (Trinity 1 and 2)
//   pdz_snd_bin_flies      STOCK_FLIES_BUZZING      models container_dumpster, container_wheelie_bin,
//                          structure_litterbin_conc (and _cover) (Nightclub, Subway); Rare's own 2 m flies rule
//                          (the range picker's "_FLIES_BUZZING"), made over the bin's lid (1 m above its place)
//   pdz_snd_tank_bubbles   STOCK_AQUA_BUBBLE_LP     at the jellyfish tanks (model Animal_jellyfish): ONE sound per
//                          tank: jellyfish within 4 m of each other are one tank, the sound at their middle
//   pdz_snd_pipe_flow      STOCK_PIPE_FLOW          model structure_pipe (Rooftops). Rare's steady flow loop only: no
//                          drips or leaks, since neither the model name nor its template ("example") says the pipe
//                          leaks
//   pdz_snd_neon_sign_hum  STOCK_NEONSIGN_HUM       models light_chinese_sign1 to 4 (Subway, Rooftops); silent while
//                          the sign's own light (prop +812 / +800, as the fluorescent fittings) is out
//   pdz_snd_scenery_tubes  STOCK_FLUOR_BUZZ         on Rare's SCENE lights that are strip lights by the rule the
//                          tester approved (LIGHTS.md, Thu 19 Nov): a point light (type 2), radius 5 m or more, no prop
//                          owns it (no prop's +812 / +800 points to it), lamp glass (surface 38, "GUNS_BULLETHIT_GLASS")
//                          straight above it within 3 m, and a white / cool colour (green at least 0.85 of red: drops
//                          the Mansion's warm orange). Measured on the audit's light lists: Subway 26, Laboratory 17,
//                          Bridge 46, River 9, Datacore 3, Trinity 1 and 2 3 each, Mansion 0. The buzz is made at the
//                          glass; it follows its light: silent while the light is switched off or shot out (state
//                          words +0x590 / +0x594: 1 fading in, 2 on) and, for a blinking / flickering light (animation
//                          mode +804: 1 blink, 2 pulse, 3 random flicker), while its current colour (+828..+836, written
//                          by Rare's light animation sub_826060B0) is dark.
// THE RULE FOR EVERY HUM (as the fluorescent fittings' buzz, Thu 19 Nov): the sound's range is the object's height
//   above the nearest player's floor + pdz_snd_hum_range_extra_m (3 m; at least 3 m), handed to Rare's range picker
//   through NextRangeM; only the pdz_snd_hum_max nearest of each family play (the scenery tubes: pdz_snd_fluor_buzz_max,
//   at pdz_snd_fluor_buzz_volume, height + 2 m, exactly as the fittings); each is a kept sound (restored_voice_Wed18Nov:
//   started 1 m inside its range, stopped 2 m past it, all of them at a level end); a cue that is a set of takes is
//   started again when a take ends. An object is DESTROYED when it goes from the world list, its class or setup
//   changes, its place is zeroed, or its health (vtable +332) reads 0: its sound stops and never starts again.
// FRI 20 NOV NIGHT (log 032: the flies never started, the tubes restarted every second, yellowish tubes passed):
//   pdz_snd_hum_listener   distances and ranges from the game's listener (the camera, Rare's listener slots: what its
//                          sound manager measures a start against), range = height above / below the camera + 3 m
//   pdz_snd_bin_flies_reach  the flies' range = Rare's 2 m + the bin's half size (dumpster 1 m, wheelie bin 0.4 m,
//                          litter bin 0.3 m) + the lid's height to the camera, handed to the range picker
//   pdz_snd_scenery_tubes_cool  LIGHTS.md rule B colour (blue >= 0.6 of max(red, green))
//   pdz_snd_fluor_buzz_hold / _listener (restored_sounds_r3)  the tubes' takes chained into one held buzz; range and
//                          loudness from the camera (0.45 reaching it straight under the glass)
//   Every start, refusal, stop and "does not play" is logged with the listener's distance and the range (rate-limited).
//   Rare's animation mode 4 (light +804, sub_826060B0) is "held": no animation, a fixed mix of its two colours; such a
//   light is treated as steady (its state words decide), as before.
// Cost: a walk of the world list every 500 ms (at most 400 new objects named a walk: a few syscalls each, as the
//   fittings' walk), the nearest-few test every 250 ms, the scenery tubes' light test every update for the few that
//   play (loads only). The tube rule's rays (one each, Rare's hitscan ray) once per level, 16 a frame.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
REXCVAR_DEFINE_DOUBLE(pdz_snd_tank_bubbles_reach_m, 8.0, "Gameplay",
                      "Restored sound (Sat 21 Nov evening): the jellyfish tanks' bubbling is heard within this many "
                      "metres of the jellyfish (it starts about 1 m inside that; the animal floats in the middle of the "
                      "tank, so 3.5 m was only heard pressed against the glass). 0 = the Fri 20 Nov reach (height + 3 m)")
    .range(0.0, 20.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(bool, pdz_snd_fluor_buzz_flicker_only); // (restored_sounds_r3_Wed18Nov.cpp, Sun 22 Nov)
REXCVAR_DEFINE_BOOL(pdz_snd_scenery_tubes_reach, true, "Gameplay",
                    "Restored sound (Sat 21 Nov evening): a scenery strip light buzzes only where a player can stand "
                    "near it: the floor under it (Rare's bullet ray, 8 m down at most) must have one of Rare's AI "
                    "walking pads (one that characters reach, on the same floor within 20 m and in clear sight of the floor "
                    "under the light). A light in a void or under a floor nobody reaches stays silent "
                    "(logged). Off = every strip light found buzzes (before)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(double, pdz_snd_fluor_buzz_volume);
REXCVAR_DECLARE(int32_t, pdz_snd_fluor_buzz_max);

REXCVAR_DEFINE_BOOL(pdz_snd_computer_hum, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): the large computers (mainframe banks) hum quietly close by, the "
                    "game's own computer loop; it stops when one is destroyed. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_vending_hum, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): drinks machines hum quietly close by (the game's own vending hum); "
                    "it stops when one is destroyed. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_bin_flies, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): flies buzz over dumpsters, wheelie bins and litter bins (the game's "
                    "own flies, heard within its own 2 m). Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_tank_bubbles, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): the jellyfish tanks bubble quietly close by (the game's own aquarium "
                    "loop, one per tank). Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_pipe_flow, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): the Rooftops pipes carry the game's own water-flow loop, quietly "
                    "close by. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_neon_sign_hum, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): the Chinese signs hum quietly close by (the game's own neon hum) "
                    "while their light is on. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_scenery_tubes, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): the strip lights that are part of the level's scenery (Subway's "
                    "garage, the Laboratory and others: lamp glass right above a white or cool ceiling light) buzz "
                    "like the fluorescent fittings, only while lit; a flickering one buzzes with its light. Needs "
                    "pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_hum_volume, 0.5, "Gameplay",
                      "Restored hums (Fri 20 Nov): their loudness, 0 to 1 (the flies: always the game's own 1.0, since "
                      "the game's own range for them is only 2 m)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_hum_range_extra_m, 3.0, "Gameplay",
                      "Restored hums (Fri 20 Nov): a hum's range is the object's height above the player's floor + this "
                      "many metres (the flies keep the game's own 2 m)")
    .range(1.0, 20.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_snd_hum_max, 2, "Gameplay", "Restored hums (Fri 20 Nov): how many of each kind may play at once (the nearest)")
    .range(0, 4)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Fri 20 Nov night (log 032): see the Tick.
REXCVAR_DEFINE_BOOL(pdz_snd_hum_listener, true, "Gameplay",
                    "Restored hums (Fri 20 Nov night): every hum is started, ranged and stopped by its distance from the "
                    "camera, which is what the game's sound manager measures (a start past the range from the camera is "
                    "refused): its range is its height above or below the camera + pdz_snd_hum_range_extra_m. Off = from "
                    "the player's body, as before")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_bin_flies_reach, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): the flies are heard standing at a bin or dumpster: the game's 2 m "
                    "flies range counts from the bin's side (its half size) and allows for the height between the lid and "
                    "the camera. Off = 2 m from the point over the lid (only a player pressed against a small bin was near "
                    "enough). Needs pdz_snd_bin_flies and pdz_snd_hum_listener")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_scenery_tubes_cool, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): only white or cool scenery lights count as strip lights (blue at "
                    "least 0.6 of the larger of red and green); yellowish ones are left out. Off = green at least 0.85 of "
                    "red, as before. Read once per level")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(bool, pdz_snd_fluor_buzz_hold);       // (restored_sounds_r3_Wed18Nov.cpp, Fri 20 Nov night)
REXCVAR_DECLARE(bool, pdz_snd_fluor_buzz_listener);
REXCVAR_DECLARE(double, pdz_snd_fluor_buzz_at_ear);
REXCVAR_DECLARE(double, pdz_snd_fluor_buzz_extra_m);

REX_EXTERN(sub_82259B80); // physics ray (r3 mask, r4 from, r5 to, r6 collector, r7 1): r3 = hit
namespace lab_stealth
{
  bool SegmentBlocked(PPCContext &ctx, uint8_t *base, const float a[3], const float b[3], uint32_t mask); // lab_stealth.cpp
}

namespace pdz::hums
{
  void Tick(PPCContext &ctx, uint8_t *base);
}

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;                     // +0 -> the session (+8 == 21 running, +25 level), +24 world
  constexpr uint32_t kWorldFirst = 43212, kWorldEnd = 43216; // world: object nodes (+4 object, +12 next)
  constexpr uint32_t kLights = 0x82E19AE0, kLightCount = 0x82E3C5F4;
  constexpr uint32_t kPhysicsWorld = 0x82E3C2F4;

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
  bool Finite(float f) { return f == f && f > -1.0e7f && f < 1.0e7f; }
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
      s.push_back(char(std::tolower(static_cast<unsigned char>(c[n]))));
    }
    return s;
  }
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
    PPCFunc *fn = slot >= 0x82000000u && slot < 0x83000000u && slot != 0x82AA2BC8u
                      ? pdz::GuestFunction(slot, "a hum object's health read")
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

  enum Kind
  {
    kComputer,
    kVending,
    kFlies,
    kTank,
    kPipe,
    kNeon,
    kTube, // the scenery strip lights (scene lights, not props)
    kKinds
  };
  struct KindInfo
  {
    const char *name;
    const char *cue;
    const char *log;
  };
  constexpr KindInfo kInfo[kKinds] = {
      {"computer", "XACT_SOUNDBANK_STOCK_COMPUTER_LARGE_LP", "pdz_snd_computer_hum"},
      {"vending machine", "XACT_SOUNDBANK_STOCK_VEND_HUM", "pdz_snd_vending_hum"},
      {"bin", "XACT_SOUNDBANK_STOCK_FLIES_BUZZING", "pdz_snd_bin_flies"},
      {"jellyfish tank", "XACT_SOUNDBANK_STOCK_AQUA_BUBBLE_LP", "pdz_snd_tank_bubbles"},
      {"pipe", "XACT_SOUNDBANK_STOCK_PIPE_FLOW", "pdz_snd_pipe_flow"},
      {"neon sign", "XACT_SOUNDBANK_STOCK_NEONSIGN_HUM", "pdz_snd_neon_sign_hum"},
      {"scenery strip light", "XACT_SOUNDBANK_STOCK_FLUOR_BUZZ", "pdz_snd_scenery_tubes"},
  };
  bool KindOn(int k)
  {
    switch (k)
    {
    case kComputer: return REXCVAR_GET(pdz_snd_computer_hum);
    case kVending: return REXCVAR_GET(pdz_snd_vending_hum);
    case kFlies: return REXCVAR_GET(pdz_snd_bin_flies);
    case kTank: return REXCVAR_GET(pdz_snd_tank_bubbles);
    case kPipe: return REXCVAR_GET(pdz_snd_pipe_flow);
    case kNeon: return REXCVAR_GET(pdz_snd_neon_sign_hum);
    case kTube: return REXCVAR_GET(pdz_snd_scenery_tubes);
    default: return false;
    }
  }
  int KindOfModel(const std::string &m)
  {
    if (m.find("props_") == std::string::npos)
    {
      return -1;
    }
    if (m.find("electrical_mainframe") != std::string::npos || m.find("electrical_computer_midsize_lab") != std::string::npos)
    {
      return kComputer;
    }
    if (m.find("container_drinks_dispenser") != std::string::npos)
    {
      return kVending;
    }
    if (m.find("container_dumpster") != std::string::npos || m.find("container_wheelie_bin") != std::string::npos ||
        m.find("structure_litterbin_conc") != std::string::npos)
    {
      return kFlies;
    }
    if (m.find("animal_jellyfish") != std::string::npos)
    {
      return kTank;
    }
    if (m.find("structure_pipe") != std::string::npos)
    {
      return kPipe;
    }
    if (m.find("light_chinese_sign") != std::string::npos)
    {
      return kNeon;
    }
    return -1;
  }

  struct Member
  {
    uint32_t prop = 0, vtable = 0, setup = 0;
    bool gone = false;
  };
  // One sound source: a prop (most kinds), a jellyfish tank (its members), or a scene light (the tubes).
  struct Source
  {
    int kind = 0;
    std::vector<Member> members;
    uint32_t light = 0;       // kTube: the scene light
    float at[3] = {};         // where the sound is made
    uint32_t sound = 0;
    float range_cm = 0.0f;
    bool gone = false, start_logged = false;
    int health_checks = 0;
    std::chrono::steady_clock::time_point retry{}, started{}, dark_since{}, health_at{};
    bool dark = false;
    // Fri 20 Nov night: its loudness for this start (-1: the switch's), a bin's half size (cm), the takes of this hold,
    // when the hold began, the next refusal / stop / "why not" line.
    float volume = -1.0f, half_cm = 0.0f;
    uint32_t takes = 0;
    std::chrono::steady_clock::time_point held_since{}, why_at{};
  };
  std::vector<Source> g_sources; // the game thread only
  std::unordered_map<uint32_t, uint32_t> g_known; // object -> its vtable when it was named
  std::unordered_set<uint32_t> g_owned_lights;    // scene lights some prop points to (+812 / +800)
  uint32_t g_world = 0, g_level = 0xFFFFFFFFu;
  std::chrono::steady_clock::time_point g_next_walk{}, g_next_pick{}, g_running_since{};
  bool g_running = false;
  int g_walk_new_last = -1;
  // The tube rule: 0 not yet, 1 testing (rays), 2 done.
  int g_tube_stage = 0;
  std::vector<uint32_t> g_tube_queue;
  size_t g_tube_next = 0;
  int g_tube_found = 0;
  int g_tube_steady = 0; // (Sun 22 Nov: strip lights dropped by pdz_snd_fluor_buzz_flicker_only)
  std::array<uint64_t, kKinds> g_starts{};

  void StopAll(PPCContext &ctx, uint8_t *base)
  {
    for (Source &s : g_sources)
    {
      pdz::snd::Stop(ctx, base, s.sound);
    }
  }

  // Rare's closest-hit ray (mask 1, what bullets hit), as the F12 pointer casts it: true with the hit place and surface.
  bool Ray(PPCContext &ctx, uint8_t *base, const float from[3], const float to[3], float at[3], uint32_t &surface)
  {
    const uint32_t buf = (ctx.r1.u32 - 0x600) & ~0xFu;
    std::memset(base + buf, 0, 0x180);
    for (uint32_t k = 0; k < 3; ++k)
    {
      Store32(base, buf + 4 * k, std::bit_cast<uint32_t>(from[k]));
      Store32(base, buf + 16 + 4 * k, std::bit_cast<uint32_t>(to[k]));
    }
    const uint32_t coll = buf + 32;
    Store32(base, coll, 0x82090738); // Rare's closest-hit collector (the hitscan builds it)
    bool hit = false;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r1.u64 = buf - 0x800;
      frame.ctx.r3.u64 = 1;
      frame.ctx.r4.u64 = buf;
      frame.ctx.r5.u64 = buf + 16;
      frame.ctx.r6.u64 = coll;
      frame.ctx.r7.u64 = 1;
      sub_82259B80(frame.ctx, base);
      hit = (frame.ctx.r3.u32 & 0xFF) != 0;
    }
    if (hit)
    {
      for (int k = 0; k < 3; ++k)
      {
        at[k] = LoadF(base, coll + 68 + 4 * k);
      }
      surface = (uint32_t(base[coll + 104]) << 8) | base[coll + 105];
    }
    return hit;
  }

  // A prop light (the signs) or a scene light: 1 on, 0 off, -1 no light to read.
  int LightOn(const uint8_t *base, uint32_t light)
  {
    if (!Guest(light) || !pdz::guestmem::Ok(base, light, 0x598)) // (Sun 22 Nov night (audit): only where mapped)
    {
      return -1;
    }
    const uint32_t a = Load32(base, light + 0x590), b = Load32(base, light + 0x594);
    return (a == 1 || a == 2 || b == 1 || b == 2) ? 1 : 0;
  }
  float Bright(const uint8_t *base, uint32_t at)
  {
    const float r = LoadF(base, at), g = LoadF(base, at + 4), b = LoadF(base, at + 8);
    return Finite(r) && Finite(g) && Finite(b) ? std::max(0.0f, r) + std::max(0.0f, g) + std::max(0.0f, b) : 0.0f;
  }
  // A scene light lit right now: switched on, and for an animated one (blink / pulse / flicker) not in a dark phase.
  bool TubeLitNow(const uint8_t *base, uint32_t light)
  {
    if (LightOn(base, light) != 1)
    {
      return false;
    }
    const uint32_t mode = Load32(base, light + 804);
    if (mode < 1 || mode > 3)
    {
      return true; // (steady / held: its state words decide)
    }
    const float full = std::max(Bright(base, light + 704), Bright(base, light + 760));
    return full <= 0.0f || Bright(base, light + 828) >= 0.35f * full;
  }

  void Walk(PPCContext &ctx, uint8_t *base, uint32_t world)
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
      ++named;
      const std::string model = ModelOf(base, o);
      if (model.find("props_") == std::string::npos || !pdz::guestmem::Ok(base, o, 816)) // (+812 read below)
      {
        continue;
      }
      // (Owned scene lights, for the tube rule: what a prop's +812 / +800 points to; only pointers that are scene
      // lights matter, checked when the rule runs.)
      for (const uint32_t off : {812u, 800u})
      {
        const uint32_t l = Load32(base, o + off);
        if (Guest(l))
        {
          g_owned_lights.insert(l);
        }
      }
      const int kind = KindOfModel(model);
      if (kind < 0 || g_sources.size() >= 512)
      {
        continue;
      }
      Member m;
      m.prop = o;
      m.vtable = vtable;
      m.setup = Load32(base, o + 84);
      const float p[3] = {LoadF(base, o + 48), LoadF(base, o + 52), LoadF(base, o + 56)};
      if (!Finite(p[0]) || !Finite(p[1]) || !Finite(p[2]))
      {
        continue;
      }
      if (kind == kTank)
      {
        // One sound per tank: a jellyfish within 4 m of a tank's first jellyfish belongs to that tank.
        Source *tank = nullptr;
        for (Source &s : g_sources)
        {
          const float dx = s.at[0] - p[0], dy = s.at[1] - p[1], dz = s.at[2] - p[2];
          if (s.kind == kTank && dx * dx + dy * dy + dz * dz < 400.0f * 400.0f)
          {
            tank = &s;
            break;
          }
        }
        if (tank)
        {
          const float n = float(tank->members.size());
          for (int k = 0; k < 3; ++k)
          {
            tank->at[k] = (tank->at[k] * n + p[k]) / (n + 1.0f); // (the middle of its jellyfish)
          }
          // (Fri 20 Nov night: how far its jellyfish spread from the middle, added to its range: the tank's glass, not
          // its middle, is where a player stands.)
          const float sx = tank->at[0] - p[0], sz = tank->at[2] - p[2];
          tank->half_cm = std::min(400.0f, std::max(tank->half_cm, std::sqrt(sx * sx + sz * sz)));
          tank->members.push_back(m);
          continue;
        }
      }
      Source s;
      s.kind = kind;
      s.members.push_back(m);
      for (int k = 0; k < 3; ++k)
      {
        s.at[k] = p[k];
      }
      if (kind == kFlies)
      {
        s.at[1] += 100.0f; // (over the lid)
        // (Fri 20 Nov night, pdz_snd_bin_flies_reach: how far the bin's side is from its middle.)
        s.half_cm = model.find("container_dumpster") != std::string::npos ? 100.0f
                    : model.find("container_wheelie_bin") != std::string::npos ? 40.0f
                                                                                : 30.0f;
      }
      if (kind == kNeon)
      {
        const uint32_t linked = Load32(base, o + 812), own = Load32(base, o + 800);
        s.light = Guest(linked) ? linked : Guest(own) ? own : 0;
      }
      g_sources.push_back(s);
      size_t of_kind = 0;
      for (const Source &q : g_sources)
      {
        of_kind += q.kind == kind ? 1 : 0;
      }
      if (of_kind <= 8)
      {
        REXLOG_INFO("{}: a {} (model {}; class {:08X}) at ({:.0f} {:.0f} {:.0f}){} ({} this level)", kInfo[kind].log,
                    kInfo[kind].name, model, vtable, p[0], p[1], p[2],
                    kind == kNeon ? (s.light ? " with a light of its own" : " with no light of its own to read") : "", of_kind);
      }
    }
    g_walk_new_last = named;
  }

  // Sat 21 Nov evening (pdz_snd_scenery_tubes_reach): the places of Rare's AI walking pads a character can reach:
  // from every character's current pad (+0xB4; Rare's list 0x82E454A0 +0x710 .. +0x714, node +0 / +8) over the pad
  // links (pad +8 the first link; link +0 the pad it goes to, +4 the next link, +15 0 = a sight-only link, skipped;
  // links an obstacle box closes now (+14: a door) are walked: the door opens). Pad place +16. Once per level.
  std::vector<std::array<float, 3>> g_pads;
  int g_pad_seeds = 0;
  void BuildPads(uint8_t *base)
  {
    g_pads.clear();
    g_pad_seeds = 0;
    std::unordered_set<uint32_t> seen;
    std::vector<uint32_t> q;
    auto add = [&](uint32_t pad) {
      if (Guest(pad) && Readable(base, pad, 28) && seen.insert(pad).second)
      {
        q.push_back(pad);
      }
    };
    uint32_t node = Load32(base, 0x82E454A0u + 0x710);
    const uint32_t end = Load32(base, 0x82E454A0u + 0x714);
    for (int i = 0; i < 512 && node != end && Guest(node) && Readable(base, node, 12); ++i)
    {
      const uint32_t c = Load32(base, node);
      node = Load32(base, node + 8);
      if (Guest(c) && Readable(base, c, 0xB8))
      {
        const size_t before = q.size();
        add(Load32(base, c + 0xB4));
        g_pad_seeds += q.size() > before ? 1 : 0;
      }
    }
    for (size_t i = 0; i < q.size() && q.size() < 30000; ++i)
    {
      uint32_t link = Load32(base, q[i] + 8);
      for (int n = 0; n < 32 && Guest(link) && Readable(base, link, 16); ++n, link = Load32(base, link + 4))
      {
        if (base[link + 15] != 0)
        {
          add(Load32(base, link));
        }
      }
    }
    for (const uint32_t pad : q)
    {
      const std::array<float, 3> at = {LoadF(base, pad + 16), LoadF(base, pad + 20), LoadF(base, pad + 24)};
      if (Finite(at[0]) && Finite(at[1]) && Finite(at[2]))
      {
        g_pads.push_back(at);
      }
    }
  }
  // Can a player stand near this light? The floor under it, then the nearest reachable pad on that floor (within
  // 1.5 m of its height). Returns true (and says why) when he can; the text is for the log either way.
  bool TubeReachable(PPCContext &ctx, uint8_t *base, const float p[3], std::string &why)
  {
    const float down[3] = {p[0], p[1] - 800.0f, p[2]};
    float floor[3] = {};
    uint32_t surface = 0;
    if (!Ray(ctx, base, p, down, floor, surface))
    {
      why = "no floor within 8 m under it (a void)";
      return false;
    }
    // (Rare's AI pads are sparse, often 10 m apart: a pad within 3 m failed every Laboratory room. Now: a reachable pad on
    // that floor (within 1.5 m of its height) within 20 m, in clear sight of the floor under the light (Rare's sight
    // blockers, 1 m above both): the floor is part of where people walk. The 8 nearest are tried.)
    std::vector<std::pair<float, size_t>> near;
    for (size_t i = 0; i < g_pads.size(); ++i)
    {
      const auto &pad = g_pads[i];
      if (std::fabs(pad[1] - floor[1]) <= 150.0f)
      {
        const float h = std::hypot(pad[0] - floor[0], pad[2] - floor[2]);
        if (h <= 2000.0f)
        {
          near.push_back({h, i});
        }
      }
    }
    std::sort(near.begin(), near.end());
    const float eye[3] = {floor[0], floor[1] + 100.0f, floor[2]};
    for (size_t k = 0; k < near.size() && k < 8; ++k)
    {
      const auto &pad = g_pads[near[k].second];
      const float to[3] = {pad[0], pad[1] + 100.0f, pad[2]};
      if (!lab_stealth::SegmentBlocked(ctx, base, eye, to, 0x20))
      {
        why = fmt::format("floor {:.0f} cm under it; a walking pad characters reach is {:.0f} cm away on that floor, in clear sight",
                          p[1] - floor[1], near[k].first);
        return true;
      }
    }
    why = fmt::format("floor {:.0f} cm under it; {}", p[1] - floor[1],
                      near.empty() ? std::string("no walking pad characters reach on that floor within 20 m")
                                   : fmt::format("the {} nearest walking pads on that floor (from {:.0f} cm) are all behind walls", std::min<size_t>(near.size(), 8), near[0].first));
    return false;
  }

  // The tube rule over Rare's scene lights (once per level, when the world walk has named every object).
  void TubeRule(PPCContext &ctx, uint8_t *base)
  {
    if (g_tube_stage == 0)
    {
      const uint32_t count = std::min<uint32_t>(Load32(base, kLightCount), 2000u);
      int owned = 0, other = 0;
      for (uint32_t i = 0; i < count; ++i)
      {
        const uint32_t L = Load32(base, kLights + 4 * i);
        if (!Guest(L) || !pdz::guestmem::Ok(base, L, 0x598)) // (Sun 22 Nov night (audit): only where mapped)
        {
          continue;
        }
        if (Load32(base, L + 680) != 2 || !(LoadF(base, L + 684) >= 500.0f))
        {
          continue; // (a point light, radius 5 m or more)
        }
        const float r = LoadF(base, L + 704), g = LoadF(base, L + 708), bl = LoadF(base, L + 712);
        // (Fri 20 Nov night, pdz_snd_scenery_tubes_cool: LIGHTS.md's rule B, white / cool only: blue at least 0.6 of the
        // larger of red and green. Log 032: the old test (green at least 0.85 of red) let the Laboratory's yellowish
        // lights through, colour 2.00 1.82 0.70.)
        const bool cool = REXCVAR_GET(pdz_snd_scenery_tubes_cool)
                              ? (Finite(r) && Finite(g) && Finite(bl) && std::max(r, g) > 0.0f && bl >= 0.6f * std::max(r, g))
                              : (Finite(r) && Finite(g) && r > 0.0f && g >= 0.85f * r);
        if (!cool)
        {
          ++other; // (warm: orange, red or yellow)
          continue;
        }
        if (g_owned_lights.count(L))
        {
          ++owned; // (a fitting prop's own light: the fittings buzz on their own)
          continue;
        }
        g_tube_queue.push_back(L);
      }
      g_tube_stage = 1;
      g_tube_next = 0;
      BuildPads(base); // (Sat 21 Nov evening: pdz_snd_scenery_tubes_reach)
      REXLOG_INFO("pdz_snd_scenery_tubes: reach rule: {} walking pads reached from {} characters' pads", g_pads.size(), g_pad_seeds);
      REXLOG_INFO("pdz_snd_scenery_tubes: the level's {} scene lights: {} are white / cool point lights of radius 5 m or more "
                  "that no prop owns (to be tested for lamp glass above), {} such lights belong to a prop, {} are warm",
                  count, g_tube_queue.size(), owned, other);
      return;
    }
    for (int n = 0; n < 16 && g_tube_next < g_tube_queue.size(); ++n, ++g_tube_next)
    {
      const uint32_t L = g_tube_queue[g_tube_next];
      if (!pdz::guestmem::Ok(base, L, 0x598)) // (Sun 22 Nov night (audit): queued in an earlier frame)
      {
        continue;
      }
      const float p[3] = {LoadF(base, L + 720), LoadF(base, L + 724), LoadF(base, L + 728)};
      if (!Finite(p[0]) || !Finite(p[1]) || !Finite(p[2]))
      {
        continue;
      }
      const float up[3] = {p[0], p[1] + 300.0f, p[2]};
      float at[3] = {};
      uint32_t surface = 0;
      if (!Ray(ctx, base, p, up, at, surface) || surface != 38)
      {
        continue; // (no lamp glass straight above within 3 m)
      }
      // Sat 21 Nov evening (pdz_snd_scenery_tubes_reach; log 034: a Laboratory buzz heard ~10 m above it, under the
      // prison): a strip light no player can stand near gets no buzz.
      {
        std::string why;
        const bool reach = TubeReachable(ctx, base, p, why);
        const bool use = reach || !REXCVAR_GET(pdz_snd_scenery_tubes_reach) || g_pads.empty();
        REXLOG_INFO("pdz_snd_scenery_tubes: reach: strip light {:08X} at ({:.0f} {:.0f} {:.0f}): {}: {}", L, p[0], p[1], p[2], why,
                    reach ? "a player can stand under it: BUZZ"
                    : use ? (g_pads.empty() ? "no walking pads known on this level: BUZZ (rule not applied)" : "nobody can stand near it, but the switch is off: BUZZ")
                          : "NO BUZZ: no player can stand near it");
        if (!use)
        {
          continue;
        }
      }
      // Sun 22 Nov (pdz_snd_fluor_buzz_flicker_only): only a flickering strip light buzzes (Rare's animation mode +804:
      // 1 blink, 3 random flicker; 0 steady, 2 pulse, 4 held).
      const uint32_t mode = Load32(base, L + 804);
      const char *mode_name = mode == 0 ? "steady" : mode == 1 ? "blink" : mode == 2 ? "pulse" : mode == 3 ? "random flicker"
                              : mode == 4 ? "held (steady)" : "unknown";
      if (REXCVAR_GET(pdz_snd_fluor_buzz_flicker_only) && mode != 1 && mode != 3)
      {
        ++g_tube_steady;
        REXLOG_INFO("pdz_snd_fluor_buzz_flicker: no buzz: scenery strip light at ({:.0f} {:.0f} {:.0f}), mode {} ({})", p[0], p[1], p[2],
                    mode, mode_name);
        continue;
      }
      REXLOG_INFO("pdz_snd_fluor_buzz_flicker: BUZZES: scenery strip light at ({:.0f} {:.0f} {:.0f}), mode {} ({}), rate {:.1f}", p[0],
                  p[1], p[2], mode, mode_name, LoadF(base, L + 808));
      Source s;
      s.kind = kTube;
      s.light = L;
      s.at[0] = at[0], s.at[1] = at[1] - 2.0f, s.at[2] = at[2]; // (on the glass, just below it)
      g_sources.push_back(s);
      ++g_tube_found;
      if (g_tube_found <= 40)
      {
        REXLOG_INFO("pdz_snd_scenery_tubes: scene light {:08X} at ({:.0f} {:.0f} {:.0f}) (radius {:.0f} cm, colour {:.2f} {:.2f} {:.2f}, "
                    "animation mode {}) has lamp glass {:.0f} cm above it: a strip light, its buzz made at the glass",
                    L, p[0], p[1], p[2], LoadF(base, L + 684), LoadF(base, L + 704), LoadF(base, L + 708), LoadF(base, L + 712),
                    Load32(base, L + 804), at[1] - p[1]);
      }
    }
    if (g_tube_next >= g_tube_queue.size())
    {
      g_tube_stage = 2;
      REXLOG_INFO("pdz_snd_scenery_tubes: {} scenery strip lights on this level (lamp glass straight above within 3 m)", g_tube_found);
      REXLOG_INFO("pdz_snd_fluor_buzz_flicker: this level: {} scenery strip lights buzz (they flicker), {} steady ones dropped{}",
                  g_tube_found, g_tube_steady, REXCVAR_GET(pdz_snd_fluor_buzz_flicker_only) ? "" : " (switch off: all buzz)");
    }
  }

  // Destroyed? (members gone, class / setup changed, place zeroed). Health is read separately (it calls the game).
  bool MembersGone(const uint8_t *base, Source &s)
  {
    if (s.kind == kTube)
    {
      return false;
    }
    bool any = false;
    for (Member &m : s.members)
    {
      if (m.gone)
      {
        continue;
      }
      if (!pdz::guestmem::Ok(base, m.prop, 88)) // (Sun 22 Nov night (audit): a remembered prop only while mapped)
      {
        m.gone = true;
        continue;
      }
      const bool zeroed = Load32(base, m.prop + 48) == 0 && Load32(base, m.prop + 52) == 0 && Load32(base, m.prop + 56) == 0;
      if (Load32(base, m.prop) != m.vtable || Load32(base, m.prop + 84) != m.setup || zeroed)
      {
        m.gone = true;
        continue;
      }
      any = true;
    }
    return !any;
  }
  std::atomic<bool> g_level_reset{false}; // Sun 22 Nov night (audit): set by pdz::hums::LevelReset (any thread)
} // namespace

namespace pdz::hums
{
  // Sun 22 Nov night (audit): from the level music setup (music_restore.cpp): every level start, a restart included.
  void LevelReset() { g_level_reset.store(true, std::memory_order_relaxed); }

  void Tick(PPCContext &ctx, uint8_t *base)
  {
    const uint32_t session = Load32(base, kGame);
    const uint32_t world = Load32(base, kGame + 24);
    const uint32_t level = Guest(session) ? base[session + 25] : 0xFFFFFFFFu;
    // (Sun 22 Nov night (audit): a level start / restart (LevelReset) also forgets the remembered props and lights.)
    if (world != g_world || level != g_level || g_level_reset.exchange(false, std::memory_order_relaxed))
    {
      StopAll(ctx, base);
      g_sources.clear();
      g_known.clear();
      g_owned_lights.clear();
      g_world = world;
      g_level = level;
      g_tube_stage = 0;
      g_tube_queue.clear();
      g_tube_found = 0;
      g_tube_steady = 0;
      g_running = false;
      g_walk_new_last = -1;
    }
    bool any = false;
    for (int k = 0; k < kKinds; ++k)
    {
      any = any || KindOn(k);
    }
    if (!REXCVAR_GET(pdz_restored_sounds) || !any || !Guest(world) || !Guest(session))
    {
      StopAll(ctx, base);
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool running = Load32(base, session + 8) == 21;
    if (running && !g_running)
    {
      g_running_since = now;
    }
    g_running = running;
    if (now >= g_next_walk)
    {
      g_next_walk = now + std::chrono::milliseconds(500);
      if (g_known.size() > 60000)
      {
        g_known.clear();
      }
      Walk(ctx, base, world);
    }
    // The tube rule: the level has run 10 s, the walk has named everything (a walk with nothing new), and the physics
    // world is there for the rays.
    if (REXCVAR_GET(pdz_snd_scenery_tubes) && g_tube_stage < 2 && running && now - g_running_since >= std::chrono::seconds(10) &&
        g_walk_new_last == 0 && Guest(Load32(base, kPhysicsWorld)) && Guest(Load32(base, kPhysicsWorld - 4)))
    {
      TubeRule(ctx, base);
    }
    // Fri 20 Nov night: every start, refusal and stop is said with the distance from the game's listener (the camera:
    // the game's sound manager refuses a start past the range measured from it, sub_82203328 -> sub_821FEE48) and the
    // range used (pdz_snd_hum_listener: both are the listener's now).
    const bool from_listener = REXCVAR_GET(pdz_snd_hum_listener);
    const bool hold = REXCVAR_GET(pdz_snd_fluor_buzz_hold);
    auto stop_src = [&](Source &s, const char *reason, float d) {
      if (!s.sound)
      {
        return;
      }
      pdz::snd::Stop(ctx, base, s.sound);
      if (now >= s.why_at)
      {
        s.why_at = now + std::chrono::seconds(5);
        REXLOG_INFO("{}: the {} at ({:.0f} {:.0f} {:.0f}): {} stopped ({}; the {} {:.1f} m from it, range {:.1f} m; held {:.1f} s, "
                    "{} take(s))",
                    kInfo[s.kind].log, kInfo[s.kind].name, s.at[0], s.at[1], s.at[2], kInfo[s.kind].cue + 15, reason,
                    from_listener ? "listener" : "nearest player", d / 100.0f, s.range_cm / 100.0f,
                    std::chrono::duration<double>(now - s.held_since).count(), s.takes);
      }
    };
    auto volume_of = [&](const Source &s) {
      return s.volume >= 0.0f ? s.volume
             : s.kind == kFlies ? 1.0f
             : s.kind == kTube  ? float(REXCVAR_GET(pdz_snd_fluor_buzz_volume))
                                : float(REXCVAR_GET(pdz_snd_hum_volume));
    };
    auto distance_of = [&](const Source &s, float *where) {
      return from_listener ? pdz::snd::ListenerCm(base, s.at, where) : pdz::snd::NearestPlayerCm(base, s.at, where);
    };
    // The tubes that play follow their light every update (a flickering light).
    for (Source &s : g_sources)
    {
      if (s.kind != kTube || !s.sound)
      {
        continue;
      }
      if (!TubeLitNow(base, s.light))
      {
        if (!s.dark)
        {
          s.dark = true;
          s.dark_since = now;
        }
        else if (now - s.dark_since >= std::chrono::milliseconds(120))
        {
          stop_src(s, "its light went dark", distance_of(s, nullptr)); // (dark for a moment: silent until it is lit again)
        }
      }
      else
      {
        s.dark = false;
      }
    }
    // (Fri 20 Nov night, pdz_snd_fluor_buzz_hold: a held sound whose take has ended gets the next take at once while
    // the listener is within its range: the buzz is 8 short takes; the loops never end, so they are not touched.)
    if (hold)
    {
      for (Source &s : g_sources)
      {
        if (!s.sound || s.gone || pdz::snd::Alive(base, s.sound))
        {
          continue;
        }
        pdz::snd::Stop(ctx, base, s.sound); // (its take has ended: freed)
        const float d = distance_of(s, nullptr);
        const bool lit = s.kind == kTube ? TubeLitNow(base, s.light) : s.kind == kNeon && s.light ? LightOn(base, s.light) == 1 : true;
        if (!KindOn(s.kind) || !lit || d < 0.0f || d > s.range_cm)
        {
          continue; // (the pick below decides)
        }
        pdz::snd::Result why;
        pdz::snd::NextRangeM(s.range_cm / 100.0f);
        s.sound = pdz::snd::Start(ctx, base, kInfo[s.kind].cue, s.at, volume_of(s), why, 24);
        pdz::snd::NextRangeM(0.0f);
        if (s.sound)
        {
          ++s.takes;
          s.started = now;
        }
        else
        {
          s.retry = now + std::chrono::seconds(1);
          if (now >= s.why_at)
          {
            s.why_at = now + std::chrono::seconds(5);
            REXLOG_INFO("{}: the {} at ({:.0f} {:.0f} {:.0f}): its next take {} (the {} {:.1f} m from it, range {:.1f} m; {})",
                        kInfo[s.kind].log, kInfo[s.kind].name, s.at[0], s.at[1], s.at[2], pdz::snd::Text(why),
                        from_listener ? "listener" : "nearest player", d / 100.0f, s.range_cm / 100.0f,
                        pdz::snd::Load(base, s.kind == kTube || s.kind == kComputer || s.kind == kVending || s.kind == kTank ? 10 : 12));
          }
        }
      }
    }
    if (now < g_next_pick || g_sources.empty())
    {
      return;
    }
    g_next_pick = now + std::chrono::milliseconds(250);
    const double extra = REXCVAR_GET(pdz_snd_hum_range_extra_m) * 100.0;
    struct Near
    {
      float d;
      size_t i;
    };
    std::array<std::array<Near, 6>, kKinds> best{};
    std::array<int, kKinds> n_best{};
    std::vector<float> dist(g_sources.size(), -1.0f); // (this pick's distance of each source)
    for (size_t i = 0; i < g_sources.size(); ++i)
    {
      Source &s = g_sources[i];
      if (s.gone)
      {
        continue;
      }
      if (MembersGone(base, s))
      {
        s.gone = true;
        pdz::snd::Stop(ctx, base, s.sound);
        REXLOG_INFO("{}: the {} at ({:.0f} {:.0f} {:.0f}) is gone (destroyed): its sound stops for good", kInfo[s.kind].log,
                    kInfo[s.kind].name, s.at[0], s.at[1], s.at[2]);
        continue;
      }
      if (!KindOn(s.kind))
      {
        pdz::snd::Stop(ctx, base, s.sound);
        continue;
      }
      float where[3] = {};
      const float d = distance_of(s, where);
      dist[i] = d;
      if (d < 0.0f)
      {
        pdz::snd::Stop(ctx, base, s.sound);
        continue;
      }
      if (!s.sound)
      {
        if (from_listener)
        {
          // (Fri 20 Nov night: from the listener. The tubes: pdz_snd_fluor_buzz_listener's rule, as the fittings. The
          // bins: pdz_snd_bin_flies_reach: Rare's 2 m flies rule + the bin's half size + the height between the lid and
          // the camera, so the flies are heard standing at the bin (log 032: the 8 Nightclub bins never started: Rare's
          // 2 m from the lid point was asked within 1.5 m of it, so only a player pressed against a small bin was ever
          // near enough). The other hums: their height above / below the camera + pdz_snd_hum_range_extra_m.)
          const float dy = std::min(std::fabs(s.at[1] - where[1]), 1200.0f);
          if (s.kind == kTube && REXCVAR_GET(pdz_snd_fluor_buzz_listener))
          {
            pdz::snd::UnderPlan(s.at[1], where[1], float(REXCVAR_GET(pdz_snd_fluor_buzz_extra_m)) * 100.0f, 300.0f,
                                float(REXCVAR_GET(pdz_snd_fluor_buzz_at_ear)), 1.0f, s.range_cm, s.volume);
          }
          else if (s.kind == kFlies)
          {
            s.range_cm = REXCVAR_GET(pdz_snd_bin_flies_reach) ? 200.0f + s.half_cm + std::min(dy, 600.0f) : 200.0f;
            s.volume = 1.0f;
          }
          else
          {
            s.range_cm = std::max(300.0f, dy + float(extra)) + s.half_cm; // (a tank: + its spread)
            if (s.kind == kTank && REXCVAR_GET(pdz_snd_tank_bubbles_reach_m) > 0.0)
            {
              // (Sat 21 Nov evening, log 034: the owner heard the tanks only pressed against the glass: 3.5 m reach.)
              s.range_cm = std::max(s.range_cm, float(REXCVAR_GET(pdz_snd_tank_bubbles_reach_m)) * 100.0f);
            }
            s.volume = -1.0f;
          }
        }
        else
        {
          const float floor_y = where[1] - 80.0f; // (her body point is about 80 cm above her feet)
          const float height = std::clamp(s.at[1] - floor_y, 0.0f, 1200.0f);
          s.range_cm = s.kind == kFlies ? 200.0f                                             // Rare's own flies rule
                       : s.kind == kTube ? std::max(300.0f, height + 200.0f)                 // as the fittings' buzz
                                         : std::max(300.0f, height + float(extra));
          if (s.kind == kTank && REXCVAR_GET(pdz_snd_tank_bubbles_reach_m) > 0.0)
          {
            s.range_cm = std::max(s.range_cm, float(REXCVAR_GET(pdz_snd_tank_bubbles_reach_m)) * 100.0f); // (Sat 21 Nov evening)
          }
          s.volume = -1.0f;
        }
      }
      const float range = s.range_cm;
      const bool playing = pdz::snd::Alive(base, s.sound);
      if (!playing && s.sound)
      {
        pdz::snd::Stop(ctx, base, s.sound); // (a take ended, or it was freed with the level)
        if (!hold && now - s.started < std::chrono::seconds(1))
        {
          s.retry = now + std::chrono::seconds(3); // (cut at once: the listener is past the range)
        }
      }
      // Destroyed by gunfire (health 0)? Asked once a second while a player is near (it calls the game).
      if (s.kind != kTube && s.kind != kTank && d < range + 500.0f && now >= s.health_at)
      {
        s.health_at = now + std::chrono::seconds(1);
        if (PropHealth(ctx, base, s.members[0].prop) == 0.0f)
        {
          s.gone = true;
          pdz::snd::Stop(ctx, base, s.sound);
          REXLOG_INFO("{}: the {} at ({:.0f} {:.0f} {:.0f}) was destroyed (health 0): its sound stops for good", kInfo[s.kind].log,
                      kInfo[s.kind].name, s.at[0], s.at[1], s.at[2]);
          continue;
        }
      }
      const bool lit = s.kind == kTube ? TubeLitNow(base, s.light) : s.kind == kNeon && s.light ? LightOn(base, s.light) == 1 : true;
      const float inside = std::min(100.0f, range * 0.25f);
      if (!lit || d > (playing ? range + 200.0f : range - inside) || (!playing && now < s.retry))
      {
        // (Fri 20 Nov night: why a source the listener is near does not play, at most every 10 s each.)
        if (!s.sound && d < range + 300.0f && now >= s.why_at)
        {
          s.why_at = now + std::chrono::seconds(10);
          const std::string why =
              !lit ? (s.kind == kTube && pdz::guestmem::Ok(base, s.light, 0x598) ? fmt::format("its light is not lit now (state words {} / {}, animation mode {}: 0 steady, 1 blink, "
                                                    "2 pulse, 3 random flicker, 4 held)",
                                                    Load32(base, s.light + 0x590), Load32(base, s.light + 0x594), Load32(base, s.light + 804))
                                      : std::string("its light is out"))
              : now < s.retry ? std::string("waiting to ask again after a refusal or a cut")
                              : fmt::format("the listener is not yet within {:.1f} m (it starts 1 m or a quarter inside its range)",
                                            (range - inside) / 100.0f);
          REXLOG_INFO("{}: the {} at ({:.0f} {:.0f} {:.0f}) does not play: {} (the {} {:.1f} m from it, range {:.1f} m)", kInfo[s.kind].log,
                      kInfo[s.kind].name, s.at[0], s.at[1], s.at[2], why, from_listener ? "listener" : "nearest player", d / 100.0f,
                      range / 100.0f);
        }
        stop_src(s, !lit ? "its light is out" : "the player has gone", d); // destroyed light / switched off / dark phase, or the players have gone
        continue;
      }
      const int max = s.kind == kTube ? std::min(REXCVAR_GET(pdz_snd_fluor_buzz_max), 6) : REXCVAR_GET(pdz_snd_hum_max);
      int &nb = n_best[s.kind];
      auto &b = best[s.kind];
      int at_rank = nb;
      while (at_rank > 0 && b[at_rank - 1].d > d)
      {
        --at_rank;
      }
      if (at_rank < max)
      {
        for (int k = std::min(nb, max - 1); k > at_rank; --k)
        {
          b[k] = b[k - 1];
        }
        b[at_rank] = {d, i};
        nb = std::min(nb + 1, max);
      }
    }
    for (size_t i = 0; i < g_sources.size(); ++i)
    {
      Source &s = g_sources[i];
      if (s.gone)
      {
        continue;
      }
      bool chosen = false;
      for (int k = 0; k < n_best[s.kind]; ++k)
      {
        chosen = chosen || best[s.kind][k].i == i;
      }
      if (!chosen)
      {
        stop_src(s, "others of its kind are nearer (pdz_snd_hum_max / pdz_snd_fluor_buzz_max)", dist[i]);
        continue;
      }
      if (s.sound)
      {
        continue;
      }
      const float volume = volume_of(s);
      pdz::snd::Result why;
      const bool own_range = s.kind != kFlies || (from_listener && REXCVAR_GET(pdz_snd_bin_flies_reach));
      if (own_range)
      {
        pdz::snd::NextRangeM(s.range_cm / 100.0f); // (its own range; the flies kept Rare's 2 m before Fri 20 Nov night)
      }
      s.sound = pdz::snd::Start(ctx, base, kInfo[s.kind].cue, s.at, volume, why, 24);
      pdz::snd::NextRangeM(0.0f);
      s.started = now;
      s.held_since = now;
      s.takes = s.sound ? 1 : 0;
      if (!s.sound)
      {
        s.retry = now + std::chrono::seconds(3);
      }
      const uint64_t n = ++g_starts[s.kind];
      if (n <= 12 || (n & (n - 1)) == 0 || (!s.sound && now >= s.why_at))
      {
        if (!s.sound)
        {
          s.why_at = now + std::chrono::seconds(5);
        }
        const float d = dist[i];
        REXLOG_INFO("{}: the {} at ({:.0f} {:.0f} {:.0f}), the {} {:.1f} m from it: {} {} (loudness {:.2f}, range {:.1f} m{}: about "
                    "{:.2f} at the listener now; {}{} starts so far)",
                    kInfo[s.kind].log, kInfo[s.kind].name, s.at[0], s.at[1], s.at[2], from_listener ? "listener" : "nearest player",
                    d / 100.0f, kInfo[s.kind].cue + 15, pdz::snd::Text(why), volume, s.range_cm / 100.0f,
                    s.kind == kFlies ? (own_range ? ": the game's 2 m flies rule + the bin's half size + its height to the camera"
                                                  : ": the game's own flies rule")
                    : from_listener  ? ": its height above / below the camera + a few metres"
                                     : ": its height above the player's floor + a few metres",
                    d >= 0.0f && s.range_cm > 0.0f ? volume * volume * std::max(0.0f, 1.0f - d / s.range_cm) : 0.0f,
                    s.sound ? std::string(hold && s.kind == kTube ? "held: the next take starts as one ends; " : "")
                            : pdz::snd::Load(base, s.kind == kNeon || s.kind == kPipe || s.kind == kFlies ? 12 : 10) + "; ",
                    n);
      }
    }
  }
} // namespace pdz::hums
