// Sounds chosen by a prop's MODEL (Wed 18 Nov 2026, job bymodel). TU3 addresses. Needs pdz_restored_sounds; each
// family has its own switch (on). Only Rare's own recordings, only at moments the game already has.
//
// The model of a prop is its setup's model name (prop +84 -> setup, +140 -> "aid_darkcharacter_..."), as
// lab_grate_sound.cpp reads it; a door's setup is laid out differently, so a door's model name is the first
// "aid_darkcharacter_" string its setup (or its model record, door +72) points to (DoorModelKey).
//
// METAL DOORS (pdz_snd_metal_doors). Rare's door sound player sub_82563788 (r3 = door, kept in r31; r4 = slot) looks
//   up one of the door template's five door sounds by name (sub_82202D20) and plays it at the door (+48). The door
//   update sub_82561240 asks for: slot 1 (+184) when a closed door starts to open, slot 2 (+188) when it is fully
//   open, slot 3 (+192) when an open door starts to close, slot 4 (+196) when it is shut (slot 0, +200, is "locked":
//   restored_sounds_r2_Tue17Nov.cpp). The Nightclub's red / metal doors (door_level1_door_normal) and its metal
//   double doors (door_club_door_Double) name no template of their own and play Rare's wooden set
//   (STOCK_DOOR_WOOD_OPEN / _THUD / _SQUEAK / _CLOSE). For the models below, and only while the slot asks for that
//   wooden name, the lookup asks for Rare's metal door set instead, by name:
//     opening starts  STOCK_DOOR_WOOD_OPEN   -> STOCK_METAL_DOOR_OPEN
//     fully open      STOCK_DOOR_WOOD_THUD   -> STOCK_METAL_SWING_OPEN
//     closing starts  STOCK_DOOR_WOOD_SQUEAK -> STOCK_METAL_DOOR_SQUEAK
//     shut            STOCK_DOOR_WOOD_CLOSE  -> STOCK_METAL_SWING_CLOSE
//   (An empty name at that slot, a moment that is silent today, gets the metal one too.)
//   Same moment, same call, same place: one sound replaced by one sound (never two, never none: if the metal name is
//   not in the loaded banks, Rare's wooden one stays). Models: the two Nightclub models (Wed 18 Nov), and the
//   two other swinging doors that Rare's own model-to-template list (in the level data) files under metal:
//   door_blue2 ("door\metaldoorswing", a template that never shipped; Rooftops) and Rusty_door ("door\metal";
//   Rooftops, Subway). Both swing with the Nightclub red door's animation (club_reddoor_anim). A door whose
//   template has sounds of its own is never touched (its names are not the wooden ones).
//   Wed 18 Nov, later (job sound3, the tester's listening): METAL_DOOR_OPEN is a handle being turned and the door
//   opening, METAL_SWING_OPEN another opening, METAL_DOOR_SQUEAK (8 takes) the hinges. The mapping is now:
//     opening starts  -> the door's own opening sound: METAL_DOOR_OPEN or METAL_SWING_OPEN, chosen ONCE PER DOOR from
//                        the door's place (a hash of its place in 50 cm steps: the same door makes the same sound
//                        all level and after a reload; different doors can differ)
//     while it swings -> METAL_DOOR_SQUEAK once, pdz_snd_metal_door_squeak_delay_s after the opening began and not
//                        before the opening sound has ended: asked through Rare's own door sound player
//                        (sub_82563788, the closing-starts slot), which never starts a door sound while another of
//                        that door still plays (given up 2.5 s after the opening began, or at the door's next moment)
//     fully open      -> nothing (an empty name: the "two sounds on opening")
//     closing starts  -> METAL_DOOR_SQUEAK
//     shut            -> METAL_SWING_CLOSE
//   Loudness and range are Rare's own for these cues (the door player; "_DOOR_" cues are heard to 10 m).
// SWIVEL CHAIRS (pdz_snd_swivel_chairs; model furniture_chair_swivel, every level). They use the chair template
//   (furniture\chair), whose bounce slot borrows STOCK_DOOR_WOOD_THUD and whose slide slot borrows
//   GUNS_PISTOLWHIP_IMPACT. Rare's contact routine sub_82511A90 plays the bounce slot for a contact faster than the
//   template's lowest speed (name lookup returning to 82512050; r30 -> the list entry, +0 the prop), at most 4 at a
//   time, and the slide slot while a prop slides faster than its slide speed (lookup returning to 825121C0; the
//   prop at r28 + r25). For a swivel chair only (wooden chairs share the template and keep their sounds):
//     a contact when the chair has tipped over (its up axis more than 60 degrees from upright) and had not fallen
//       before: STOCK_SWIVEL_CHAIR_FALL (once a fall; it is standing again when its up axis is within 25 degrees);
//     any other contact: STOCK_SWIVEL_CHAIR_BOUNCE, at most one each pdz_snd_swivel_chair_gap_s per chair (a
//       contact within that time, or within 0.6 s of the fall sound, is silent: the sound before covers it);
//     a slide while it stands on its wheels: STOCK_SWIVEL_CHAIR_ROLL (lying down it keeps Rare's slide sound).
//   The up axis is read from the chair's Havok body (rotation columns at +48 / +64 / +80, position at +96 in metres),
//   found once per chair among the prop's own pointers, as prop_motion_rate.cpp's grate log finds it. If it is not
//   found, every contact is a bounce.
// FUSE BOX HUM (pdz_snd_fusebox_hum; model electrical_fusebox_test: Nightclub, Mansion, Rooftops, Subway). The
//   cue STOCK_FUSEBOX_HUM_LP (Rare's loop) is asked for by nothing. Each placed fuse box of that model gets it at its
//   place (prop +48), at pdz_snd_fusebox_hum_volume, faded by distance with Rare's own range for its kind of sound (the
//   range picker sub_82206888 has no rule of its own for this cue); it stops when the fuse box is destroyed (the
//   object goes, or its position is zeroed, as the vent grates' does) and when the switch is turned off.
#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

#include "bymodel_sounds_Wed18Nov.h"
#include "restored_voice_Wed18Nov.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DECLARE(bool, pdz_restored_sounds);
REXCVAR_DECLARE(double, pdz_snd_fusebox_hum_range_m);
REXCVAR_DEFINE_BOOL(pdz_snd_metal_doors, true, "Gameplay",
                    "Restored sound: metal swinging doors (the Nightclub's red and double doors, the blue "
                    "doors of Rooftops and Subway) open and close with the game's own metal door "
                    "sounds instead of the wooden ones. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_fix_round_door_wood, true, "Gameplay",
                    "Sun 22 Nov: the round doors (Datacore, Trinity Infiltration / Escape) play the game's metal door "
                    "unlock sound AND, from the game's own data, a wooden door creak / thud on top. On (default) = drop "
                    "the wooden layer from these doors only. Off = as the game's data has it")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_metal_door_squeak_fits_s, 1.2, "Gameplay",
                      "Restored sound: a metal door squeaks as it starts to close only when its swing takes at least "
                      "this long (seconds); a quicker door plays only the slam when it shuts")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_metal_door_squeak_delay_s, 0.4, "Gameplay",
                      "Restored sound: how long after a metal door starts to open its hinges squeak (seconds; never "
                      "before the opening sound has ended)")
    .range(0.0, 2.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_swivel_chairs, true, "Gameplay",
                    "Restored sound: office swivel chairs fall over, knock and roll with the game's own swivel chair "
                    "sounds instead of a wooden door thud. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_swivel_chair_gap_s, 0.4, "Gameplay",
                      "Restored sound: the shortest time between two knock sounds of the same swivel chair (seconds)")
    .range(0.0, 5.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_fusebox_hum, true, "Gameplay",
                    "Restored sound: fuse boxes hum quietly (the game's own hum, fading with distance) until they are "
                    "destroyed. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_fusebox_hum_volume, 0.35, "Gameplay",
                      "Restored sound: the fuse box hum's loudness (0 to 1)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Fri 20 Nov night (log 032 22:24:36: "not started yet ... nearest player 1.0 m away", all session). The game's sound
// manager refuses a start farther from its LISTENER (the camera, about 0.9 m above the player's body point) than the
// sound's range (RequestToPlay sub_82203328 -> sub_821FEE48), and the range was the 1 m of
// pdz_snd_fusebox_hum_range_m: standing at the box the camera is already more than 1 m from it. Now the range is the
// box's height above the player's floor + pdz_snd_fusebox_hum_reach_m, and the distance is the listener's.
REXCVAR_DEFINE_BOOL(pdz_snd_fusebox_hum_reach, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): the fuse box hum really starts when the player stands at the box: "
                    "its range is the box's height above her floor + pdz_snd_fusebox_hum_reach_m, measured from the "
                    "camera as the game measures it (the 1 m range was always refused). Off = the 1 m before. Needs "
                    "pdz_snd_fusebox_hum")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_fusebox_hum_reach_m, 1.5, "Gameplay",
                      "pdz_snd_fusebox_hum_reach: the fuse box hum's range is its height above the player's floor + this "
                      "many metres (heard at the box, gone a step or two away)")
    .range(0.5, 10.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_fusebox_hum_reach_volume, 0.7, "Gameplay",
                      "pdz_snd_fusebox_hum_reach: the hum's loudness while that rule is on (0 to 1; a placed sound's "
                      "loudness is applied twice by the game, so 0.7 is heard as about 0.5; the 0.35 of "
                      "pdz_snd_fusebox_hum_volume was heard as about 0.12)")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_82202C78); // the name resolver (r3 = name): the cue, or -1
REX_EXTERN(sub_82202D20); // cue reference (r3 = ref {char name[256]; cue}, r4 = name)
REX_EXTERN(sub_821FF428); // sound from a reference (r3 = ref, r4 = position or 0, r5, r6): the sound, or 0
REX_EXTERN(sub_822061C8); // play (r3 = sound, f1 = delay)
REX_EXTERN(sub_822055C0); // stop (r3 = sound)
REX_EXTERN(sub_82205BD0); // release (r3 = sound)
REX_EXTERN(sub_82563788); // Rare's door sound player (r3 = door, r4 = slot: 3 = closing starts)

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8; // -> the game state / session block (+25 mission)
  constexpr uint32_t kSoundVolume = 332; // a sound's loudness (0 to 1)

  // The door sound player's lookups (return addresses in sub_82563788) and the wooden names they replace.
  struct DoorSlot
  {
    uint32_t lr;
    const char *moment, *wood, *metal;
  };
  constexpr DoorSlot kDoorSlots[] = {
      {0x825638B8, "opening starts", "XACT_SOUNDBANK_STOCK_DOOR_WOOD_OPEN", "XACT_SOUNDBANK_STOCK_METAL_DOOR_OPEN"},
      {0x8256388C, "fully open", "XACT_SOUNDBANK_STOCK_DOOR_WOOD_THUD", "XACT_SOUNDBANK_STOCK_METAL_SWING_OPEN"},
      {0x82563860, "closing starts", "XACT_SOUNDBANK_STOCK_DOOR_WOOD_SQUEAK", "XACT_SOUNDBANK_STOCK_METAL_DOOR_SQUEAK"},
      {0x82563834, "shut", "XACT_SOUNDBANK_STOCK_DOOR_WOOD_CLOSE", "XACT_SOUNDBANK_STOCK_METAL_SWING_CLOSE"},
  };
  // The metal swinging door models (the part after "props_").
  // (Wed 18 Nov night: door_rusty_door (Subway, Rooftops) is out of the set: it LOOKS wooden in play; it has Rare's
  // wooden sounds again.)
  constexpr std::string_view kMetalDoors[] = {"door_level1_door_normal", "door_club_door_double", "door_door_blue2"};
  constexpr uint32_t kBounceLookup = 0x82512050, kSlideLookup = 0x825121C0; // in sub_82511A90
  constexpr char kChairModel[] = "furniture_chair_swivel";
  constexpr char kFuseboxModel[] = "electrical_fusebox_test";
  constexpr char kChairFall[] = "XACT_SOUNDBANK_STOCK_SWIVEL_CHAIR_FALL";
  constexpr char kChairBounce[] = "XACT_SOUNDBANK_STOCK_SWIVEL_CHAIR_BOUNCE";
  constexpr char kChairRoll[] = "XACT_SOUNDBANK_STOCK_SWIVEL_CHAIR_ROLL";
  constexpr char kHum[] = "XACT_SOUNDBANK_STOCK_FUSEBOX_HUM_LP";

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  void StoreF(uint8_t *base, uint32_t a, float f)
  {
    const uint32_t v = std::bit_cast<uint32_t>(f);
    uint8_t *p = base + a;
    p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
  }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
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
  bool On() { return REXCVAR_GET(pdz_restored_sounds); }

  // The prop's model name after "props_" (lower case), or "" (setup +84 -> +140). Every step is checked readable:
  // +84 is not a setup for every kind of object that has a body or a contact (smoke run, Wed 18 Nov: a read of
  // text taken for a pointer at a level start). Not a hot path: prop bodies at a level start, door and contact
  // sound lookups.
  std::string ModelKey(uint8_t *base, uint32_t prop)
  {
    const uint32_t setup = Readable(base, prop, 96) ? Load32(base, prop + 84) : 0;
    const uint32_t name = Readable(base, setup, 144) ? Load32(base, setup + 140) : 0;
    if (!Readable(base, name, 96))
    {
      return {};
    }
    const char *c = reinterpret_cast<const char *>(base + name);
    std::string s;
    for (size_t n = 0; n < 95 && c[n] >= 32 && c[n] < 127; ++n)
    {
      s.push_back(char(c[n] >= 'A' && c[n] <= 'Z' ? c[n] + 32 : c[n]));
    }
    const size_t at = s.rfind("props_");
    return at == std::string::npos ? s : s.substr(at + 6);
  }

  // A door's model name (identity pass, Wed 18 Nov: a door's setup +140 is not its model name, "?" or nothing):
  // the first pointer to an "aid_darkcharacter_" string among the door setup's words (+0 to +0x300), else among
  // the words of the door's model record (door +72). Once per door setup a level (cached): door sounds are rare.
  struct DoorModel
  {
    uint32_t setup = 0;
    std::string key;
  };
  std::array<DoorModel, 96> g_door_models{};
  uint32_t g_door_models_n = 0;
  std::string KeyOf(const uint8_t *base, uint32_t name)
  {
    const char *c = reinterpret_cast<const char *>(base + name);
    std::string s;
    for (size_t n = 0; n < 95 && c[n] >= 32 && c[n] < 127; ++n)
    {
      s.push_back(char(c[n] >= 'A' && c[n] <= 'Z' ? c[n] + 32 : c[n]));
    }
    const size_t at = s.rfind("props_");
    return at == std::string::npos ? s : s.substr(at + 6);
  }
  std::string DoorModelKey(uint8_t *base, uint32_t door)
  {
    const uint32_t setup = Readable(base, door, 96) ? Load32(base, door + 84) : 0;
    for (uint32_t i = 0; i < g_door_models_n; ++i)
    {
      if (g_door_models[i].setup == setup)
      {
        return g_door_models[i].key;
      }
    }
    std::string key;
    constexpr char kPrefix[] = "aid_darkcharacter_";
    const uint32_t where[2] = {setup, Readable(base, door, 96) ? Load32(base, door + 72) : 0};
    const uint32_t span[2] = {0x300, 0x100};
    for (int w = 0; w < 2 && key.empty(); ++w)
    {
      if (!Readable(base, where[w], span[w]))
      {
        continue;
      }
      for (uint32_t off = 0; off < span[w] && key.empty(); off += 4)
      {
        const uint32_t p = Load32(base, where[w] + off);
        if (p >= 0x10000u && p < 0xF0000000u && Readable(base, p, sizeof(kPrefix)) &&
            std::memcmp(base + p, kPrefix, sizeof(kPrefix) - 1) == 0)
        {
          key = KeyOf(base, p);
        }
      }
    }
    if (setup && g_door_models_n < g_door_models.size())
    {
      g_door_models[g_door_models_n++] = {setup, key};
    }
    return key;
  }

  // A guest copy of a name (made once, kept for the session).
  struct GuestName
  {
    const char *text;
    uint32_t at = 0;
  };
  GuestName g_names[] = {{kDoorSlots[0].metal}, {kDoorSlots[1].metal}, {kDoorSlots[2].metal}, {kDoorSlots[3].metal},
                         {kChairFall},          {kChairBounce},        {kChairRoll},          {kHum},
                         {""}};
  // (sound3) A metal door that has begun to open: its hinge squeak is still to come.
  struct DoorSqueak
  {
    uint32_t door = 0, vtable = 0, playing = 0; // playing: the door's sound field (+1196) when the opening began
    std::chrono::steady_clock::time_point began{};
  };
  std::array<DoorSqueak, 8> g_squeaks{};
  constexpr uint32_t kDoorPlaying = 1196; // the door's playing sound (restored_sounds_r2_Tue17Nov.cpp)
  bool g_squeak_call = false; // our own call of the door player for the hinges
  void SqueakForget(uint32_t door)
  {
    for (DoorSqueak &q : g_squeaks)
    {
      if (q.door == door)
      {
        q.door = 0;
      }
    }
  }
  enum Name
  {
    kNameDoor0 = 0,
    kNameFall = 4,
    kNameBounce = 5,
    kNameRoll = 6,
    kNameHum = 7,
    kNameSilent = 8,
  };
  uint32_t Text(uint8_t *base, int which)
  {
    GuestName &g = g_names[which];
    if (!g.at)
    {
      const uint32_t n = uint32_t(std::strlen(g.text) + 1);
      g.at = REX_KERNEL_MEMORY()->SystemHeapAlloc(n < 64 ? 64 : n);
      if (g.at)
      {
        std::memset(base + g.at, 0, n < 64 ? 64 : n);
        std::memcpy(base + g.at, g.text, n);
      }
    }
    return g.at;
  }
  // The name as a guest string if a loaded bank has it, else 0.
  uint32_t Loaded(PPCContext &ctx, uint8_t *base, int which)
  {
    const uint32_t at = Text(base, which);
    if (!at)
    {
      return 0;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = at;
    sub_82202C78(frame.ctx, base);
    return int32_t(frame.ctx.r3.u32) == -1 ? 0 : at;
  }

  // The level the remembered things belong to.
  uint32_t g_session = 0, g_level = 0xFFFFFFFFu;
  bool SameLevel(const uint8_t *base)
  {
    const uint32_t session = Load32(base, kGame);
    return Guest(session) && session == g_session && base[session + 25] == g_level;
  }
  void NewLevelIfSo(const uint8_t *base);

  // --- doors ---
  struct DoorSeen
  {
    std::string model;
    int slot;
  };
  std::array<DoorSeen, 24> g_door_seen{};
  uint32_t g_door_seen_n = 0;

  // A cue name for the log, without "XACT_SOUNDBANK_" ("(empty)" for an empty one).
  std::string_view Short(const uint8_t *base, uint32_t name)
  {
    const char *c = reinterpret_cast<const char *>(base + name);
    std::string_view s(c, strnlen(c, 64));
    constexpr std::string_view kPrefix = "XACT_SOUNDBANK_";
    if (s.empty())
    {
      return "(empty)";
    }
    return s.size() > kPrefix.size() && s.substr(0, kPrefix.size()) == kPrefix ? s.substr(kPrefix.size()) : s;
  }

  // Wed 18 Nov night: how long each metal door takes to swing (opening starts -> fully open), measured as it opens.
  // Its closing takes as long: when that is too short for the hinge squeak and the slam, only the slam plays.
  struct DoorTime
  {
    uint32_t door = 0, vtable = 0;
    std::chrono::steady_clock::time_point began{};
    float swing_s = -1.0f; // -1: not measured yet
    bool said = false;
  };
  std::array<DoorTime, 48> g_door_times{};
  uint32_t g_door_time_next = 0;
  DoorTime &DoorTimeOf(const uint8_t *base, uint32_t door)
  {
    const uint32_t vt = Load32(base, door);
    for (DoorTime &t : g_door_times)
    {
      if (t.door == door && t.vtable == vt)
      {
        return t;
      }
    }
    DoorTime &t = g_door_times[g_door_time_next++ % g_door_times.size()];
    t = DoorTime();
    t.door = door;
    t.vtable = vt;
    return t;
  }
  uint32_t DoorName(PPCContext &ctx, uint8_t *base, uint32_t name, int slot)
  {
    const DoorSlot &s = kDoorSlots[slot];
    const char *asked = reinterpret_cast<const char *>(base + name);
    // The wooden name, or an empty one (a door that is silent at that moment today: identity pass, Wed 18 Nov, a
    // Rooftops door asked for empty names).
    const bool wooden = std::strncmp(asked, s.wood, 64) == 0 || asked[0] == 0;
    const uint32_t door = ctx.r31.u32;
    const std::string model = DoorModelKey(base, door);
    bool metal = false;
    for (std::string_view m : kMetalDoors)
    {
      metal = metal || model == m;
    }
    metal = metal && On(); // (Sun 22 Nov: the round-door fix below also comes here with pdz_restored_sounds off)
    // Sun 22 Nov (pdz_fix_round_door_wood): the round doors (model door_level0_circular_door, animations level0circledoor*: Datacore, Trinity Infiltration /
    // Escape) play Rare's metal level-0 door sounds from their animation AND, from their template's door slots, Rare's
    // wooden door set on top. With the switch on, only the wooden names are dropped at these doors (an empty name: the
    // door player starts nothing); the animation's own sounds are untouched. On by default (Sun 22 Nov, after listening); off = as Rare's data has it.
    const bool round = model.find("level0_circular_door") != std::string::npos || model.find("level0circledoor") != std::string::npos;
    const bool round_wood = round && asked[0] != 0 && std::strncmp(asked, "XACT_SOUNDBANK_STOCK_DOOR_WOOD_", 31) == 0;
    uint32_t out = name;
    const char *what = "kept (not a metal swinging door model)";
    if (round_wood && REXCVAR_GET(pdz_fix_round_door_wood))
    {
      const uint32_t silent = Text(base, kNameSilent);
      out = silent ? silent : name;
      what = silent ? "-> nothing (the round door's wooden layer is dropped; pdz_fix_round_door_wood on)"
                    : "kept (no guest memory for the empty name)";
    }
    else if (round_wood)
    {
      what = "kept (Rare's wooden layer on the round door; pdz_fix_round_door_wood off)";
    }
    else if (round)
    {
      what = "kept (a round door; not a wooden name)";
    }
    else if (metal && !wooden)
    {
      what = "kept (the door's own template sound)";
    }
    else if (metal && !REXCVAR_GET(pdz_snd_metal_doors))
    {
      what = "kept (pdz_snd_metal_doors off)";
    }
    else if (metal && slot == 1)
    {
      // (sound3) fully open: nothing. (A hinge squeak still owed for this opening may yet come: a short swing.)
      const uint32_t silent = Text(base, kNameSilent);
      out = silent ? silent : name;
      what = "-> nothing (the metal door's opening sound has played; pdz_snd_metal_doors)";
      DoorTime &t = DoorTimeOf(base, door);
      if (t.began != std::chrono::steady_clock::time_point{})
      {
        t.swing_s = std::chrono::duration<float>(std::chrono::steady_clock::now() - t.began).count();
        t.began = {};
      }
    }
    else if (metal && slot == 2 && !g_squeak_call && DoorTimeOf(base, door).swing_s >= 0.0f &&
             DoorTimeOf(base, door).swing_s < float(REXCVAR_GET(pdz_snd_metal_door_squeak_fits_s)))
    {
      // (Wed 18 Nov night) closing starts, and this door's swing is too short for the hinge squeak and the slam:
      // only the slam (when it shuts).
      DoorTime &t = DoorTimeOf(base, door);
      const uint32_t silent = Text(base, kNameSilent);
      out = silent ? silent : name;
      what = "-> nothing (its closing is too short for the hinge squeak and the slam: only the slam; pdz_snd_metal_doors)";
      if (!t.said)
      {
        t.said = true;
        REXLOG_INFO("pdz_snd_bymodel: the metal door ({}) at ({:.0f} {:.0f} {:.0f}) swings in {:.2f} s: too short for the hinge "
                    "squeak and the slam (under pdz_snd_metal_door_squeak_fits_s {:.2f} s): closing plays only the slam",
                    model, LoadF(base, door + 48), LoadF(base, door + 52), LoadF(base, door + 56), t.swing_s,
                    REXCVAR_GET(pdz_snd_metal_door_squeak_fits_s));
      }
    }
    else if (metal && slot == 0)
    {
      // (sound3) this door's own opening sound, from its place: METAL_DOOR_OPEN (slot name 0) or METAL_SWING_OPEN
      // (slot name 1).
      // (The setup's own place, setup +4, when it reads as one within 3 m of the door: it never moves. Else the
      // door's place as it starts to open, which is its closed place.)
      float place[3] = {LoadF(base, door + 48), LoadF(base, door + 52), LoadF(base, door + 56)};
      const uint32_t dsetup = Load32(base, door + 84);
      if (Readable(base, dsetup, 16))
      {
        const float sx = LoadF(base, dsetup + 4), sy = LoadF(base, dsetup + 8), sz = LoadF(base, dsetup + 12);
        if (std::fabs(sx - place[0]) < 300.0f && std::fabs(sy - place[1]) < 300.0f && std::fabs(sz - place[2]) < 300.0f)
        {
          place[0] = sx, place[1] = sy, place[2] = sz;
        }
      }
      const int32_t hx = int32_t(std::lround(place[0] / 50.0f)), hy = int32_t(std::lround(place[1] / 50.0f)),
                    hz = int32_t(std::lround(place[2] / 50.0f));
      uint32_t h = uint32_t(hx) * 73856093u ^ uint32_t(hy) * 19349663u ^ uint32_t(hz) * 83492791u;
      h ^= h >> 15;
      h *= 0x2C1B3C6Du;
      h ^= h >> 12;
      const int first = (h & 1u) ? 1 : 0;
      uint32_t m = Loaded(ctx, base, kNameDoor0 + first);
      int used = first;
      if (!m)
      {
        used = 1 - first;
        m = Loaded(ctx, base, kNameDoor0 + used);
      }
      out = m ? m : name;
      what = !m ? "kept (the metal names are not in the loaded banks)"
                : used == 0 ? "-> this door's own opening sound, METAL_DOOR_OPEN (pdz_snd_metal_doors)"
                            : "-> this door's own opening sound, METAL_SWING_OPEN (pdz_snd_metal_doors)";
      if (m)
      {
        // (Wed 18 Nov night: no hinge squeak while it opens: the doors open too fast, and it played after the door
        // had stopped. Opening = this door's own opening sound only. The swing is timed for the closing rule.)
        SqueakForget(door);
        DoorTimeOf(base, door).began = std::chrono::steady_clock::now();
      }
    }
    else if (metal)
    {
      if (!g_squeak_call)
      {
        SqueakForget(door); // (the door's next moment: a squeak still owed for its opening is dropped)
      }
      const uint32_t m = Loaded(ctx, base, kNameDoor0 + slot);
      out = m ? m : name;
      what = m ? "-> the metal door set (pdz_snd_metal_doors)" : "kept (the metal name is not in the loaded banks)";
    }
    // One line per model and moment a level.
    NewLevelIfSo(base);
    bool seen = false;
    for (uint32_t i = 0; i < g_door_seen_n && !seen; ++i)
    {
      seen = g_door_seen[i].slot == slot && g_door_seen[i].model == model;
    }
    if (!seen && g_door_seen_n < g_door_seen.size())
    {
      g_door_seen[g_door_seen_n++] = {model, slot};
      REXLOG_INFO("pdz_snd_bymodel: a door ({}) at ({:.0f} {:.0f} {:.0f}), {}: asked {} -> {} {} (first this level)",
                  model.empty() ? "no model name" : model, LoadF(base, door + 48), LoadF(base, door + 52),
                  LoadF(base, door + 56), s.moment, Short(base, name), Short(base, out), what);
    }
    return out;
  }

  // --- swivel chairs ---
  struct Chair
  {
    uint32_t prop = 0, vtable = 0, setup = 0;
    uint32_t motion = 0; // its Havok motion; 1 = looked for, not found
    bool fallen = false, logged = false;
    std::chrono::steady_clock::time_point last{}, fell{};
  };
  std::array<Chair, 32> g_chairs{};
  uint32_t g_chairs_n = 0;

  Chair *ChairOf(uint8_t *base, uint32_t prop)
  {
    for (uint32_t i = 0; i < g_chairs_n; ++i)
    {
      Chair &c = g_chairs[i];
      if (c.prop == prop)
      {
        if (c.vtable == Load32(base, prop) && c.setup == Load32(base, prop + 84))
        {
          return &c;
        }
        c = Chair{}; // another object at that address now
        c.prop = prop;
        c.vtable = Load32(base, prop);
        c.setup = Load32(base, prop + 84);
        return &c;
      }
    }
    if (g_chairs_n >= g_chairs.size())
    {
      return nullptr;
    }
    Chair &c = g_chairs[g_chairs_n++];
    c = Chair{};
    c.prop = prop;
    c.vtable = Load32(base, prop);
    c.setup = Load32(base, prop + 84);
    return &c;
  }

  // How upright the chair stands: its up axis' height (1 upright, 0 on its side); NAN if not known.
  float Upright(uint8_t *base, Chair &c)
  {
    const float p[3] = {LoadF(base, c.prop + 48), LoadF(base, c.prop + 52), LoadF(base, c.prop + 56)};
    if (!c.motion)
    {
      c.motion = 1;
      for (uint32_t at = 96; at < 0x400 && c.motion == 1 && Readable(base, c.prop + at, 4); at += 4)
      {
        const uint32_t body = Load32(base, c.prop + at);
        const uint32_t m = Guest(body) && Readable(base, body, 84) ? Load32(base, body + 80) : 0;
        if (Guest(m) && Readable(base, m, 112) && std::fabs(LoadF(base, m + 96) * 100.0f - p[0]) < 5.0f &&
            std::fabs(LoadF(base, m + 100) * 100.0f - p[1]) < 5.0f &&
            std::fabs(LoadF(base, m + 104) * 100.0f - p[2]) < 5.0f)
        {
          c.motion = m;
        }
      }
    }
    if (c.motion <= 1)
    {
      return NAN;
    }
    if (!pdz::guestmem::Ok(base, c.motion, 72))
    {
      c.motion = 0; // (Sun 22 Nov night (audit): a remembered motion no longer mapped: looked for again next time)
      return NAN;
    }
    return LoadF(base, c.motion + 68); // column 1 (the chair's own up) in the world, its y
  }

  uint32_t ChairName(PPCContext &ctx, uint8_t *base, uint32_t name, bool slide)
  {
    const uint32_t prop = slide ? (Guest(ctx.r28.u32 + ctx.r25.u32) ? Load32(base, ctx.r28.u32 + ctx.r25.u32) : 0)
                                : (Guest(ctx.r30.u32) ? Load32(base, ctx.r30.u32) : 0);
    if (!Guest(prop) || ModelKey(base, prop).find(kChairModel) == std::string::npos)
    {
      return name;
    }
    if (!REXCVAR_GET(pdz_snd_swivel_chairs))
    {
      return name;
    }
    NewLevelIfSo(base);
    Chair *c = ChairOf(base, prop);
    if (!c)
    {
      return name;
    }
    const float up = Upright(base, *c);
    const bool known = !std::isnan(up);
    if (known && up > 0.906f) // within 25 degrees of upright: standing (again)
    {
      c->fallen = false;
    }
    const bool tipped = known && up < 0.5f; // more than 60 degrees over
    const auto now = std::chrono::steady_clock::now();
    const char *what;
    uint32_t out = name;
    if (slide)
    {
      if (tipped || !known)
      {
        return name; // lying down (or not known): Rare's slide sound
      }
      const uint32_t m = Loaded(ctx, base, kNameRoll);
      out = m ? m : name;
      what = m ? "rolls: STOCK_SWIVEL_CHAIR_ROLL" : "rolls: the roll sound is not in the loaded banks, Rare's kept";
    }
    else if (tipped && !c->fallen)
    {
      c->fallen = true;
      c->fell = c->last = now;
      const uint32_t m = Loaded(ctx, base, kNameFall);
      out = m ? m : name;
      what = m ? "has fallen over: STOCK_SWIVEL_CHAIR_FALL" : "has fallen over: the fall sound is not in the loaded banks";
    }
    else
    {
      const double gap = REXCVAR_GET(pdz_snd_swivel_chair_gap_s);
      const bool soon = std::chrono::duration<double>(now - c->last).count() < gap ||
                        std::chrono::duration<double>(now - c->fell).count() < 0.6;
      if (soon)
      {
        const uint32_t silent = Text(base, kNameSilent);
        return silent ? silent : name; // (an empty name: Rare's routine skips the lookup and plays nothing)
      }
      c->last = now;
      const uint32_t m = Loaded(ctx, base, kNameBounce);
      out = m ? m : name;
      what = m ? "knocks: STOCK_SWIVEL_CHAIR_BOUNCE" : "knocks: the bounce sound is not in the loaded banks";
    }
    if (!c->logged)
    {
      c->logged = true;
      REXLOG_INFO("pdz_snd_bymodel: a swivel chair at ({:.0f} {:.0f} {:.0f}) {} (its up axis {}; first sound of this chair "
                  "this level)",
                  LoadF(base, prop + 48), LoadF(base, prop + 52), LoadF(base, prop + 56), what,
                  known ? fmt::format("{:.2f}", up) : std::string("not found: every contact is a knock"));
    }
    return out;
  }

  // --- fuse boxes ---
  struct Fusebox
  {
    uint32_t prop = 0, vtable = 0, setup = 0;
    float at[3] = {0, 0, 0};
    std::atomic<uint32_t> sound{0};
    bool gone = false, logged_start = false, logged_fail = false;
    std::chrono::steady_clock::time_point retry{};
    float range_cm = 0.0f;                           // (Fri 20 Nov night: its range for this start)
    std::chrono::steady_clock::time_point log_at{};  // (Fri 20 Nov night: the next refusal / stop line)
    uint64_t starts = 0;
  };
  std::array<Fusebox, 8> g_boxes{};
  uint32_t g_boxes_n = 0;
  uint32_t g_where = 0; // guest scratch for a position
  std::atomic<bool> g_level_reset{false};
  // Hums of a level or attempt before that the game has not released itself (OnRelease): stopped at the next Tick.
  std::array<std::atomic<uint32_t>, 8> g_orphans{};
  void Orphan(uint32_t sound)
  {
    for (auto &o : g_orphans)
    {
      uint32_t none = 0;
      if (!sound || o.compare_exchange_strong(none, sound))
      {
        return;
      }
    }
  }

  void Forget()
  {
    for (uint32_t i = 0; i < g_boxes.size(); ++i)
    {
      Orphan(g_boxes[i].sound.exchange(0));
      g_boxes[i].prop = 0;
      g_boxes[i].gone = false;
      g_boxes[i].logged_start = false;
      g_boxes[i].logged_fail = false;
      g_boxes[i].retry = {};
      g_boxes[i].range_cm = 0.0f;
      g_boxes[i].log_at = {};
      g_boxes[i].starts = 0;
    }
    g_boxes_n = 0;
    g_chairs_n = 0;
    g_door_seen_n = 0;
    g_door_models_n = 0;
    for (DoorSqueak &q : g_squeaks)
    {
      q.door = 0;
    }
  }

  void NewLevelIfSo(const uint8_t *base)
  {
    const uint32_t session = Load32(base, kGame);
    if (!Guest(session))
    {
      return;
    }
    if (session != g_session || base[session + 25] != g_level)
    {
      g_session = session;
      g_level = base[session + 25];
      Forget(); // (a hum still playing is stopped at the next Tick)
    }
  }

  void StopHum(PPCContext &ctx, uint8_t *base, Fusebox &b)
  {
    const uint32_t sound = b.sound.exchange(0);
    if (!sound)
    {
      return;
    }
    uint32_t kept = sound;
    pdz::snd::Stop(ctx, base, kept); // (Wed 18 Nov: a kept sound of the helper; one already freed is left alone)
  }

  // why: 0 started, 1 the cue is not in the loaded banks, 2 the sound manager made no sound (too far from the
  // listener: it is tried again every 2 s, so the hum starts when Jo comes within its range).
  uint32_t StartHum(PPCContext &ctx, uint8_t *base, const Fusebox &b, int &why)
  {
    why = 1;
    const uint32_t name = Loaded(ctx, base, kNameHum);
    if (!g_where)
    {
      g_where = REX_KERNEL_MEMORY()->SystemHeapAlloc(64);
    }
    if (!name || !g_where)
    {
      return 0;
    }
    why = 2;
    // (Wed 18 Nov: a kept sound of the helper, so it is always stopped and freed, at a level end too.)
    pdz::snd::Result r;
    const float volume = float(REXCVAR_GET(pdz_snd_fusebox_hum_reach) ? REXCVAR_GET(pdz_snd_fusebox_hum_reach_volume)
                                                                       : REXCVAR_GET(pdz_snd_fusebox_hum_volume));
    const uint32_t sound = pdz::snd::Start(ctx, base, kHum, b.at, volume, r, 20);
    if (!sound)
    {
      return 0;
    }
    why = 0;
    return sound;
  }
} // namespace

namespace bymodel_sounds
{
  uint32_t CueName(PPCContext &ctx, uint8_t *base, uint32_t name)
  {
    const uint32_t lr = uint32_t(ctx.lr);
    int door_slot = -1;
    for (int i = 0; i < 4; ++i)
    {
      door_slot = kDoorSlots[i].lr == lr ? i : door_slot;
    }
    const bool chair_site = lr == kBounceLookup || lr == kSlideLookup;
    // (Sun 22 Nov: a door moment also comes through with pdz_restored_sounds off when pdz_fix_round_door_wood is on.)
    if ((door_slot < 0 && !chair_site) || !Guest(name) ||
        !(On() || (door_slot >= 0 && REXCVAR_GET(pdz_fix_round_door_wood))))
    {
      return name;
    }
    if (door_slot >= 0)
    {
      return DoorName(ctx, base, name, door_slot);
    }
    return ChairName(ctx, base, name, lr == kSlideLookup);
  }

  void OnPropBody(uint8_t *base, uint32_t prop)
  {
    if (!Readable(base, prop, 96) || !Guest(Load32(base, kGame)) ||
        ModelKey(base, prop).find(kFuseboxModel) == std::string::npos)
    {
      return;
    }
    NewLevelIfSo(base);
    for (uint32_t i = 0; i < g_boxes_n; ++i)
    {
      if (g_boxes[i].prop == prop)
      {
        return;
      }
    }
    if (g_boxes_n >= g_boxes.size())
    {
      return;
    }
    Fusebox &b = g_boxes[g_boxes_n++];
    b.prop = prop;
    b.vtable = Load32(base, prop);
    b.setup = Load32(base, prop + 84);
    for (int k = 0; k < 3; ++k)
    {
      b.at[k] = LoadF(base, b.setup + 4 + 4 * k); // (readable: ModelKey read +140 of it)
    }
    b.sound.store(0);
    b.gone = false;
    b.logged_start = false;
    b.logged_fail = false;
    b.retry = {};
    REXLOG_INFO("pdz_snd_bymodel: a fuse box (electrical_fusebox_test) at ({:.0f} {:.0f} {:.0f}) is remembered for its "
                "hum ({} this level)",
                b.at[0], b.at[1], b.at[2], g_boxes_n);
  }

  void Tick(PPCContext &ctx, uint8_t *base)
  {
    if (g_level_reset.exchange(false, std::memory_order_relaxed))
    {
      // A restart: the fuse boxes of the attempt before that are no longer the same objects are forgotten (their
      // sounds were the level's: the game stops them).
      for (uint32_t i = 0; i < g_boxes_n;)
      {
        Fusebox &b = g_boxes[i];
        if (Guest(b.prop) && pdz::guestmem::Ok(base, b.prop, 88) && Load32(base, b.prop) == b.vtable &&
            Load32(base, b.prop + 84) == b.setup) // (Sun 22 Nov night (audit): Ok)
        {
          ++i;
          continue;
        }
        Orphan(b.sound.exchange(0));
        Fusebox &last = g_boxes[--g_boxes_n];
        if (&last != &b)
        {
          b.prop = last.prop, b.vtable = last.vtable, b.setup = last.setup;
          std::memcpy(b.at, last.at, sizeof(b.at));
          b.sound.store(last.sound.exchange(0));
          b.gone = last.gone, b.logged_start = last.logged_start, b.logged_fail = last.logged_fail, b.retry = last.retry;
        }
      }
      g_chairs_n = 0;
      g_door_seen_n = 0;
      g_door_models_n = 0;
    }
    for (auto &o : g_orphans)
    {
      const uint32_t sound = o.exchange(0);
      if (sound)
      {
        uint32_t kept = sound;
        pdz::snd::Stop(ctx, base, kept);
        REXLOG_INFO("pdz_snd_bymodel: a fuse box hum of the level (or attempt) before, still playing, is stopped");
      }
    }
    // (sound3) The hinges of the metal doors that are swinging open.
    for (DoorSqueak &q : g_squeaks)
    {
      if (!q.door)
      {
        continue;
      }
      const double since = std::chrono::duration<double>(std::chrono::steady_clock::now() - q.began).count();
      // (Sun 22 Nov night (audit): a remembered door only while its memory is mapped.)
      if (!On() || !REXCVAR_GET(pdz_snd_metal_doors) || !pdz::guestmem::Ok(base, q.door, kDoorPlaying + 4) ||
          Load32(base, q.door) != q.vtable || since > 2.5)
      {
        q.door = 0;
        continue;
      }
      if (since < REXCVAR_GET(pdz_snd_metal_door_squeak_delay_s))
      {
        continue;
      }
      // Not before the opening sound has ended: the door's playing sound is gone, or it has ended (state 6 / 8).
      const uint32_t playing = Load32(base, q.door + kDoorPlaying);
      if (Guest(playing))
      {
        const uint32_t state = Load32(base, playing + 340);
        if (state != 6 && state != 8)
        {
          continue;
        }
      }
      const uint32_t door = q.door;
      q.door = 0;
      g_squeak_call = true;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = door;
        frame.ctx.r4.u64 = 3; // closing starts: METAL_DOOR_SQUEAK for these doors (the name hook above)
        sub_82563788(frame.ctx, base);
      }
      g_squeak_call = false;
    }
    if (!g_boxes_n)
    {
      return;
    }
    if (!SameLevel(base))
    {
      Forget();
      return;
    }
    const bool want = On() && REXCVAR_GET(pdz_snd_fusebox_hum);
    const auto now = std::chrono::steady_clock::now();
    for (uint32_t i = 0; i < g_boxes_n; ++i)
    {
      Fusebox &b = g_boxes[i];
      if (b.gone)
      {
        continue;
      }
      // (Sun 22 Nov night (audit): a remembered fuse box only while its memory is mapped; else it is gone.)
      const bool mapped = pdz::guestmem::Ok(base, b.prop, 88);
      const bool zeroed = mapped && Load32(base, b.prop + 48) == 0 && Load32(base, b.prop + 52) == 0 && Load32(base, b.prop + 56) == 0;
      const bool alive = mapped && Load32(base, b.prop) == b.vtable && Load32(base, b.prop + 84) == b.setup && !zeroed;
      if (!alive)
      {
        b.gone = true;
        const bool was = b.sound.load() != 0;
        StopHum(ctx, base, b);
        REXLOG_INFO("pdz_snd_bymodel: the fuse box at ({:.0f} {:.0f} {:.0f}) is destroyed: {}", b.at[0], b.at[1], b.at[2],
                    was ? "its hum stops" : "no hum was playing");
        continue;
      }
      if (!want)
      {
        StopHum(ctx, base, b);
        continue;
      }
      // Where it is: its live position (prop +48, Rare's place for a prop's sounds; not zeroed, see above). Its
      // setup's +4 is not always set (identity pass, Wed 18 Nov: the Rooftops fuse box's was 0 0 0).
      for (int k = 0; k < 3; ++k)
      {
        b.at[k] = LoadF(base, b.prop + 48 + 4 * k);
      }
      // Wed 18 Nov (sound3): the NEAREST human player decides (split screen), and the hum is kept only while a
      // player is near its range (pdz_snd_fusebox_hum_range_m: Rare's substation hum range): started within the
      // range, stopped and freed 3 m past it, so a far fuse box holds no sound object.
      float range_cm = float(REXCVAR_GET(pdz_snd_fusebox_hum_range_m)) * 100.0f;
      float body[3] = {};
      float near_cm = pdz::snd::NearestPlayerCm(base, b.at, body);
      // (Fri 20 Nov night, pdz_snd_fusebox_hum_reach: the listener's distance, and a range of the box's height above the
      // player's floor (or its height below / above the camera, whichever is more) + pdz_snd_fusebox_hum_reach_m.)
      const bool reach = REXCVAR_GET(pdz_snd_fusebox_hum_reach);
      float lis[3] = {};
      const float lis_cm = pdz::snd::ListenerCm(base, b.at, lis);
      if (reach && lis_cm >= 0.0f && near_cm >= 0.0f)
      {
        const float above_floor = std::clamp(b.at[1] - (body[1] - 80.0f), 0.0f, 600.0f);
        const float to_camera = std::min(std::fabs(b.at[1] - lis[1]), 600.0f);
        if (!b.sound.load())
        {
          b.range_cm = std::max(above_floor, to_camera) + float(REXCVAR_GET(pdz_snd_fusebox_hum_reach_m)) * 100.0f;
        }
        range_cm = b.range_cm > 0.0f ? b.range_cm : range_cm;
        near_cm = lis_cm;
      }
      if (b.sound.load())
      {
        const bool alive_now = pdz::snd::Alive(base, b.sound.load());
        if (near_cm < 0.0f || near_cm > range_cm + (reach ? 150.0f : 300.0f) || !alive_now)
        {
          StopHum(ctx, base, b);
          b.range_cm = 0.0f;
          if (reach && now >= b.log_at)
          {
            b.log_at = now + std::chrono::seconds(5);
            REXLOG_INFO("pdz_snd_bymodel: the fuse box at ({:.0f} {:.0f} {:.0f}): STOCK_FUSEBOX_HUM_LP stopped ({}; the listener "
                        "{:.1f} m from it, range {:.1f} m)",
                        b.at[0], b.at[1], b.at[2], alive_now ? "the player has gone" : "it ended or the game freed it",
                        near_cm / 100.0f, range_cm / 100.0f);
          }
        }
        continue;
      }
      const float inside = reach ? std::min(50.0f, range_cm * 0.2f) : 0.0f; // (started a little inside the range)
      if (now < b.retry || near_cm < 0.0f || near_cm > range_cm - inside)
      {
        if (reach && near_cm >= 0.0f && near_cm < range_cm + 300.0f && now >= b.log_at)
        {
          b.log_at = now + std::chrono::seconds(10); // (near, not started: why, every 10 s at most)
          REXLOG_INFO("pdz_snd_bymodel: the fuse box at ({:.0f} {:.0f} {:.0f}): no hum yet: the listener is {:.1f} m from it, it starts "
                      "within {:.1f} m (range {:.1f} m = its height + {:.1f} m){}",
                      b.at[0], b.at[1], b.at[2], near_cm / 100.0f, (range_cm - inside) / 100.0f, range_cm / 100.0f,
                      REXCVAR_GET(pdz_snd_fusebox_hum_reach_m), now < b.retry ? " (waiting to try again)" : "");
        }
        continue;
      }
      int why = 0;
      if (reach)
      {
        pdz::snd::NextRangeM(range_cm / 100.0f); // (its own range, for the range picker)
      }
      const uint32_t sound = StartHum(ctx, base, b, why);
      pdz::snd::NextRangeM(0.0f);
      b.sound.store(sound);
      b.retry = now + std::chrono::seconds(2); // (not started, or released by the game: tried again in 2 s)
      if (reach)
      {
        const uint64_t n = ++b.starts;
        if (n <= 6 || (n & (n - 1)) == 0 || (!sound && now >= b.log_at))
        {
          b.log_at = now + std::chrono::seconds(5);
          const float vol = float(REXCVAR_GET(pdz_snd_fusebox_hum_reach_volume));
          REXLOG_INFO("pdz_snd_bymodel: the fuse box at ({:.0f} {:.0f} {:.0f}): STOCK_FUSEBOX_HUM_LP {} (the listener {:.1f} m away, "
                      "range {:.1f} m = its height + {:.1f} m; loudness {:.2f}: about {:.2f} at the listener; {}; {} asked)",
                      b.at[0], b.at[1], b.at[2],
                      sound ? "started" : why == 1 ? "is not in the loaded banks" : "REFUSED by the game's sound manager",
                      near_cm / 100.0f, range_cm / 100.0f, REXCVAR_GET(pdz_snd_fusebox_hum_reach_m), vol,
                      vol * vol * std::max(0.0f, 1.0f - near_cm / range_cm), pdz::snd::Load(base, 10), n);
        }
        continue;
      }
      if ((sound && !b.logged_start) || (!sound && !b.logged_fail))
      {
        (sound ? b.logged_start : b.logged_fail) = true;
        const float d = near_cm / 100.0f;
        REXLOG_INFO("pdz_snd_bymodel: the fuse box at ({:.0f} {:.0f} {:.0f}): STOCK_FUSEBOX_HUM_LP {} (loudness {:.2f}; the "
                    "nearest player {:.1f} m away){}",
                    b.at[0], b.at[1], b.at[2],
                    sound ? "started" : why == 1 ? "is not in the loaded banks" : "not started yet (no sound given)",
                    REXCVAR_GET(pdz_snd_fusebox_hum_volume), d,
                    sound ? "" : why == 2 ? ": tried again every 2 s (a sound too far from the listener is refused)" : "");
      }
    }
  }

  void OnRelease(uint32_t sound)
  {
    if (!sound)
    {
      return;
    }
    for (Fusebox &b : g_boxes)
    {
      uint32_t s = sound;
      b.sound.compare_exchange_strong(s, 0);
    }
    for (auto &o : g_orphans)
    {
      uint32_t s = sound;
      o.compare_exchange_strong(s, 0);
    }
  }

  void LevelReset()
  {
    g_level_reset.store(true, std::memory_order_relaxed);
  }

  // Thu 19 Nov: a door of model door_rusty_door. Only Rare's swinging-door class (vtable 820C27C8, hit slot +0xB4 =
  // sub_82521988) is asked for its model; the model is cached per door setup (DoorModelKey).
  bool IsRustyDoor(uint8_t *base, uint32_t object)
  {
    if (object < 0x10000u || object >= 0xF0000000u || (object & 3u) || Load32(base, object) != 0x820C27C8u)
    {
      return false;
    }
    return DoorModelKey(base, object).find("door_rusty_door") != std::string::npos;
  }
} // namespace bymodel_sounds
