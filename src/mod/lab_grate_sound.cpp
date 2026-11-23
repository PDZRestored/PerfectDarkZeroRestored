// Vent grates (every level that has them; first found on the Laboratory): the fall sound Rare recorded for them
// (pdz_restore_grate_sound, a restoration, on by default). TU3 addresses.
//
// The stock bank has STOCK_AIRVENT_GRATE_FALL (several takes), but nothing in the game or its data names it. The
// grate (model ..._props_structure_vent_large, template hanger_vent_cover) has the falling monitor's sound in its
// fall slot instead, and its loose piece has its impact sounds switched off.
//
// Rare's fall sounds: once a frame sub_82511A90 goes through the props that hit something; a prop whose template has
// impact sounds on (+81) and whose impact (record +24) is above the template's threshold (+348) gets the cue named in
// the template's fall slot (+172): name lookup at 8251204C (sub_82202D20), started by sub_821FF428 / sub_822061C8 at
// the impact, loudness from the impact between the threshold and the template's full-volume value (+352).
//
// A punch or a shot does not make the grate fall as a whole: it breaks (health 1) and its loose pieces are thrown.
// - A punch: Rare's melee-impact sound (sub_822003A0) plays the hit prop's fall slot, at the hit: for a grate the
//   falling monitor's sound (name lookup at 822004FC).
// - A shot: only the template's bullet-hit slot (bullet hit on metal) plays, at the hit.
//
// The restoration (every level since Sun 15 Nov: the Nightclub's grates have the same data bug; before that the
// Laboratory only):
// - whenever the grate template's fall slot is looked up (the punch, or the per-frame fall sounds' lookup at 8251204C
//   for a grate that fell as a whole), the name looked up is STOCK_AIRVENT_GRATE_FALL. Who plays it, where and how
//   loud stay Rare's own (the same call as before). A grate that falls as a whole gets one sound a fall: its further
//   bounces within pdz_grate_sound_gap_s are silent;
// - the level's grates are remembered when their bodies are made (OnPropBody, from the prop body setup hook) and
//   looked at once a frame (Tick, from the physics update hook): when one no longer exists (it broke) and its fall
//   slot was not played in the last second (so it was not a punch), the fall sound is started at the grate's
//   authored spot with Rare's own calls (sub_82202D20, sub_821FF428, sub_822061C8), at pdz_grate_sound_volume.
// The template's fall slot is known by its own copy of the name in the level data (it follows the template's
// bullet-hit and smash names): no other template has that sequence.
//
// pdz_sound_log also logs every fall sound here (prop, model, cue, impact), each time it plays.
#include "lab_grate_sound.h"

#include <array>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>

#include "restored_voice_Wed18Nov.h"
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>

#include "../game_cvars.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DEFINE_BOOL(pdz_restore_grate_sound, true, "Gameplay",
                    "Every level with vent grates (the Laboratory, the Nightclub and others): a vent grate that is "
                    "knocked off plays the fall sound Rare recorded for it "
                    "(STOCK_AIRVENT_GRATE_FALL, never used by the game) where it lands, as loud as Rare's other "
                    "falling props. Off = Rare's data (the falling monitor's sound, or nothing)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_grate_sound_gap_s, 3.0, "Gameplay",
                      "pdz_restore_grate_sound: after a grate's fall sound, further bounces of the same grate stay "
                      "silent for this many seconds (the recording holds the whole clatter)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_DOUBLE(pdz_grate_sound_volume, 1.0, "Gameplay",
                      "pdz_restore_grate_sound: loudness (0 to 1) of the grate's fall sound when a shot (or "
                      "anything but a punch) knocks it off; a punch uses Rare's own melee-impact loudness")
    .range(0.0, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_82202C78);        // the name resolver (r3 = name): the cue, or -1
REX_EXTERN(sub_82202D20);        // cue reference (r3 = ref {char name[256]; cue}, r4 = name)
REX_EXTERN(sub_821FF428);        // sound from a reference (r3 = ref, r4 = position or 0, r5, r6): the sound, or 0
REX_EXTERN(sub_822061C8);        // play (r3 = sound, f1 = delay)

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;     // -> the game state / session block (+8 = 21 in a level, +25 mission)
  constexpr uint32_t kFallLookup = 0x82512050; // the return address of the fall slot's name lookup in sub_82511A90
  constexpr char kGrateModel[] = "structure_vent_large";
  constexpr char kRareName[] = "XACT_SOUNDBANK_STOCK_MONITOR_FALL"; // the grate template's fall slot in Rare's data
  constexpr uint32_t kSoundRefSize = 260;                             // {char name[256]; cue id}
  constexpr uint32_t kSoundVolume = 332;                              // the sound's loudness (0 to 1)
  constexpr char kCue[] = "XACT_SOUNDBANK_STOCK_AIRVENT_GRATE_FALL";
  constexpr char kSilent[] = "XACT_SOUNDBANK_STOCK_AIRVENT_GRATE_FALL_NONE"; // resolves to nothing: no sound

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xFFFF0000u; }
  // Mapped, readable guest memory (asks the system: not for per-frame use).
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

  uint32_t GuestString(uint8_t *base, const char *text)
  {
    const uint32_t n = uint32_t(std::strlen(text) + 1);
    const uint32_t address = REX_KERNEL_MEMORY()->SystemHeapAlloc(n);
    if (address)
    {
      std::memcpy(base + address, text, n);
    }
    return address;
  }

  // The prop's model name (its setup, +84 -> +140), or "".
  std::string Model(uint8_t *base, uint32_t prop)
  {
    const uint32_t setup = Readable(base, prop, 96) ? Load32(base, prop + 84) : 0;
    const uint32_t name = Readable(base, setup, 144) ? Load32(base, setup + 140) : 0;
    if (!Readable(base, name, 96))
    {
      return {};
    }
    const char *c = reinterpret_cast<const char *>(base + name);
    size_t n = 0;
    while (n < 95 && c[n] >= 32 && c[n] < 127)
    {
      ++n;
    }
    return std::string(c, n);
  }

  struct Fallen
  {
    uint32_t prop = 0;
    std::chrono::steady_clock::time_point at{};
  };
  std::array<Fallen, 8> g_fallen{};
  uint32_t g_cue = 0, g_silent = 0;
  bool g_missing_logged = false;

  // The level's grates (game thread): the prop, what it looked like when its body was made, its authored spot.
  struct Watched
  {
    uint32_t prop = 0, vtable = 0, setup = 0;
    float at[3] = {};
  };
  std::array<Watched, 32> g_watched{};
  uint32_t g_watched_n = 0, g_watched_session = 0, g_where = 0;
  uint8_t g_watched_level = 0xFF; // the mission (session +25) the watched grates belong to
  std::chrono::steady_clock::time_point g_slot_played{}; // the last time the grate's fall slot was swapped

  // In a game (any level: the grate is known by its model and by its template's cue names, not by the level).
  bool InGame(const uint8_t *base)
  {
    return Guest(Load32(base, kGame));
  }

  // The fall cue as a guest string, if the loaded banks have it.
  uint32_t Cue(PPCContext &ctx, uint8_t *base)
  {
    if (!g_cue)
    {
      g_cue = GuestString(base, kCue);
      g_silent = GuestString(base, kSilent);
    }
    bool there = false;
    if (g_cue)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g_cue;
      sub_82202C78(frame.ctx, base);
      there = int32_t(frame.ctx.r3.u32) != -1;
    }
    if (!there && !g_missing_logged)
    {
      g_missing_logged = true;
      REXLOG_INFO("pdz_restore_grate_sound: STOCK_AIRVENT_GRATE_FALL is not in the loaded banks: Rare's own sound is "
                  "left");
    }
    return there ? g_cue : 0;
  }
} // namespace

namespace lab_grate_sound
{
  uint32_t CueName(PPCContext &ctx, uint8_t *base, uint32_t name)
  {
    if (!name)
    {
      return name;
    }
    const bool log = REXCVAR_GET(pdz_sound_log);
    const bool restore = REXCVAR_GET(pdz_restore_grate_sound);
    if (!log && !restore)
    {
      return name;
    }
    const bool fall_site = uint32_t(ctx.lr) == kFallLookup;
    const char *asked = reinterpret_cast<const char *>(base + name);
    // The grate template's own fall slot, wherever Rare plays it from (the knock-off itself plays it too, from the
    // break code): its copy of the falling monitor's name, which in the level data follows the template's other two
    // cue names (bullet hit on metal, the monitor smash). No other template has that pair before it.
    bool grate_slot = false;
    if (std::memcmp(asked, kRareName, sizeof(kRareName)) == 0 && name > 0x10000u + 96 &&
        pdz::guestmem::Ok(base, name - 96, 96)) // (Sun 22 Nov night (audit): the bytes before are mapped)
    {
      const std::string before(reinterpret_cast<const char *>(base + name - 96), 96);
      grate_slot = before.find("XACT_SOUNDBANK_STOCK_MONITOR_SMASH") != std::string::npos;
    }
    if (!fall_site && !grate_slot)
    {
      return name;
    }
    // sub_82511A90 at its call: r30 -> its list entry (+0 the prop, -4 the collision record), r31 = the record.
    const uint32_t entry = fall_site ? ctx.r30.u32 : 0, record = fall_site ? ctx.r31.u32 : 0;
    const uint32_t prop = Guest(entry) ? Load32(base, entry) : 0;
    const std::string model = Model(base, prop);
    const uint32_t session = Load32(base, kGame);
    const bool grate = Guest(session) && (grate_slot || model.find(kGrateModel) != std::string::npos);
    uint32_t out = name;
    const char *what = "";
    if (grate && restore)
    {
      if (Cue(ctx, base) == 0)
      {
        // not in the loaded banks: Rare's own sound is left
      }
      else
      {
        const auto now = std::chrono::steady_clock::now();
        const double gap = REXCVAR_GET(pdz_grate_sound_gap_s);
        Fallen *slot = nullptr, *oldest = &g_fallen[0];
        for (Fallen &f : g_fallen)
        {
          if (f.prop == prop)
          {
            slot = &f;
            break;
          }
          if (f.at < oldest->at)
          {
            oldest = &f;
          }
        }
        // (Only a grate that falls as a whole bounces: at the break itself there is one lookup and no prop here.)
        const bool again = fall_site && slot && std::chrono::duration<double>(now - slot->at).count() < gap;
        if (again && g_silent)
        {
          out = g_silent;
          what = " -> silent (a further bounce of the same fall)";
        }
        else
        {
          if (!slot)
          {
            slot = oldest;
            slot->prop = prop;
          }
          slot->at = now;
          g_slot_played = now;
          out = g_cue;
          what = " -> STOCK_AIRVENT_GRATE_FALL (pdz_restore_grate_sound)";
        }
      }
    }
    if (log || (grate && out != name))
    {
      const char *text = reinterpret_cast<const char *>(base + name);
      if (!fall_site)
      {
        REXLOG_INFO("pdz_sound: a vent grate's fall slot asked for from {:08X} (the knock-off): {}{}", uint32_t(ctx.lr),
                    std::string(asked, strnlen(asked, 96)), what);
        return out;
      }
      REXLOG_INFO("pdz_sound: fall sound: prop {:08X} '{}' at ({:.0f} {:.0f} {:.0f}) impact {:.2f}: {}{}", prop, model,
                  Guest(prop) ? LoadF(base, prop + 48) : 0.0f, Guest(prop) ? LoadF(base, prop + 52) : 0.0f,
                  Guest(prop) ? LoadF(base, prop + 56) : 0.0f, Guest(record) ? LoadF(base, record + 24) : -1.0f,
                  std::string(text, strnlen(text, 96)), what);
    }
    return out;
  }
} // namespace lab_grate_sound

namespace lab_grate_sound
{
  void OnPropBody(uint8_t *base, uint32_t prop)
  {
    if (!REXCVAR_GET(pdz_restore_grate_sound) || !InGame(base) || !Readable(base, prop, 96) ||
        Load32(base, prop + 12) != 8)
    {
      return; // only placed props (type 8), as the grates are
    }
    if (Model(base, prop).find(kGrateModel) == std::string::npos)
    {
      return;
    }
    const uint32_t session = Load32(base, kGame);
    if (session != g_watched_session || base[session + 25] != g_watched_level)
    {
      g_watched_session = session;
      g_watched_level = base[session + 25];
      g_watched_n = 0;
    }
    for (uint32_t i = 0; i < g_watched_n; ++i)
    {
      if (g_watched[i].prop == prop)
      {
        return;
      }
    }
    if (g_watched_n >= g_watched.size())
    {
      return;
    }
    Watched &w = g_watched[g_watched_n++];
    w.prop = prop;
    w.vtable = Load32(base, prop);
    w.setup = Load32(base, prop + 84);
    for (int i = 0; Readable(base, w.setup, 16) && i < 3; ++i)
    {
      w.at[i] = LoadF(base, w.setup + 4 + 4 * i);
    }
  }

  // Stealth round 3c (Sun 15 Nov night, fix 1): a level start (first load, mission restart, reload). A flag only (any
  // thread); the next Tick drops, WITHOUT a sound, every remembered grate that is no longer the object it was (a
  // mission restart keeps the session and the mission number, so the test below would take the attempt before's
  // grates for broken ones). Grates of the new attempt already remembered stay.
  std::atomic<bool> g_level_reset{false};
  void LevelReset()
  {
    g_level_reset.store(true, std::memory_order_relaxed);
  }

  void Tick(PPCContext &ctx, uint8_t *base)
  {
    if (g_level_reset.exchange(false, std::memory_order_relaxed))
    {
      uint32_t dropped = 0;
      for (uint32_t i = 0; i < g_watched_n;)
      {
        const Watched w = g_watched[i];
        // Sun 22 Nov night (audit): readable first; the attempt before's props may be gone.
        const bool ok = pdz::guestmem::Ok(base, w.prop, 96);
        const bool zeroed =
            ok && Load32(base, w.prop + 48) == 0 && Load32(base, w.prop + 52) == 0 && Load32(base, w.prop + 56) == 0;
        if (ok && Load32(base, w.prop) == w.vtable && Load32(base, w.prop + 84) == w.setup && !zeroed)
        {
          ++i;
          continue;
        }
        g_watched[i] = g_watched[--g_watched_n];
        ++dropped;
      }
      g_fallen = {};
      if (dropped && REXCVAR_GET(pdz_sound_log))
      {
        REXLOG_INFO("pdz_restore_grate_sound: level start: {} grate(s) of the attempt before forgotten without a sound "
                    "({} of this attempt watched)",
                    dropped, g_watched_n);
      }
    }
    if (!g_watched_n)
    {
      return;
    }
    if (Load32(base, kGame) != g_watched_session || !InGame(base) || base[g_watched_session + 25] != g_watched_level)
    {
      g_watched_n = 0; // another level: nothing to watch
      return;
    }
    for (uint32_t i = 0; i < g_watched_n;)
    {
      const Watched w = g_watched[i];
      if (!pdz::guestmem::Ok(base, w.prop, 96))
      {
        g_watched[i] = g_watched[--g_watched_n]; // Sun 22 Nov night (audit): its memory is gone: forgotten, no sound
        continue;
      }
      // Alive: still the same object, and its position is not zeroed (a broken grate's is, at once; its memory is
      // released about a second later).
      const bool zeroed = Load32(base, w.prop + 48) == 0 && Load32(base, w.prop + 52) == 0 && Load32(base, w.prop + 56) == 0;
      if (Load32(base, w.prop) == w.vtable && Load32(base, w.prop + 84) == w.setup && !zeroed)
      {
        ++i;
        continue;
      }
      g_watched[i] = g_watched[--g_watched_n]; // it broke: the prop is no more
      const auto now = std::chrono::steady_clock::now();
      const bool log = REXCVAR_GET(pdz_sound_log);
      if (!REXCVAR_GET(pdz_restore_grate_sound))
      {
        continue;
      }
      if (std::chrono::duration<double>(now - g_slot_played).count() < 1.0)
      {
        if (log)
        {
          REXLOG_INFO("pdz_sound: the vent grate at ({:.0f} {:.0f} {:.0f}) is knocked off: its fall sound has just been "
                      "played by Rare's own call (a punch)",
                      w.at[0], w.at[1], w.at[2]);
        }
        continue;
      }
      const uint32_t cue = Cue(ctx, base);
      if (!g_where)
      {
        g_where = REX_KERNEL_MEMORY()->SystemHeapAlloc(64);
      }
      uint32_t sound = 0;
      if (cue && g_where)
      {
        std::memset(base + g_where, 0, 64);
        for (int k = 0; k < 3; ++k)
        {
          const uint32_t bits = std::bit_cast<uint32_t>(w.at[k]);
          uint8_t *p = base + g_where + 4 * k;
          p[0] = uint8_t(bits >> 24), p[1] = uint8_t(bits >> 16), p[2] = uint8_t(bits >> 8), p[3] = uint8_t(bits);
        }
        rex::CallFrame frame(ctx);
        const std::string zeros(kSoundRefSize, 0);
        const uint32_t ref = rex::ppc::stack_push(frame.ctx, base, zeros.data(), kSoundRefSize);
        frame.ctx.r3.u64 = ref;
        frame.ctx.r4.u64 = cue;
        sub_82202D20(frame.ctx, base);
        frame.ctx.r3.u64 = ref;
        frame.ctx.r4.u64 = g_where;
        frame.ctx.r5.u64 = 0;
        frame.ctx.r6.u64 = 0;
        sub_821FF428(frame.ctx, base);
        sound = frame.ctx.r3.u32;
      }
      if (sound)
      {
        const uint32_t bits = std::bit_cast<uint32_t>(float(REXCVAR_GET(pdz_grate_sound_volume)));
        uint8_t *p = base + sound + kSoundVolume;
        p[0] = uint8_t(bits >> 24), p[1] = uint8_t(bits >> 16), p[2] = uint8_t(bits >> 8), p[3] = uint8_t(bits);
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = sound;
        frame.ctx.f1.f64 = 0.0;
        sub_822061C8(frame.ctx, base);
        pdz::snd::FreeWhenEnded(base, sound); // (Wed 18 Nov: it kept its sound object for ever)
      }
      REXLOG_INFO("pdz_sound: the vent grate at ({:.0f} {:.0f} {:.0f}) is knocked off (not by a punch): {} "
                  "(pdz_restore_grate_sound)",
                  w.at[0], w.at[1], w.at[2],
                  sound ? "STOCK_AIRVENT_GRATE_FALL started at the grate" : "its fall sound could not be started");
    }
  }
} // namespace lab_grate_sound
