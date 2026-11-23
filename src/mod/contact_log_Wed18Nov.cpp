// Prop knock sounds: one log line per contact attempt (Wed 18 Nov 2026, job sound3). Log only: nothing in the game
// is changed here. TU3 addresses. Switch pdz_snd_contact_log (on).
//
// Rare's contact sound routine sub_82511A90 (twice a frame: before and after the physics step), for each prop of
// the world list:
//   1. its contact record must be on (prop +428; kept on by the physics step for a prop marked at its set-up, bit
//      0x80000000 of prop +64: a prop whose template had no contact flag then is never tracked);
//   2. its start timer (prop +432) must have reached 0: it starts at -1000 and, once the body is active, counts up
//      from about -1 s, so a body's first second or so of movement is mute (Rare's own: no clatter while a level
//      settles, or from a piece just blown off);
//   3. the template (the prop's vfunc +72) must have a contact flag (+80 effect, +81 sound);
//   4. the contact's speed along its normal (prop +364, m/s; Havok's callbacks only note a speed above 0.2, and set
//      it back to 0 for the heavier prop of a pair when it weighs more than twice the other) must be above 0;
//   5. then, best first (near the listener, fast), at most 2 new sounds a call and 4 alive at a time: the speed
//      must be above the template's lowest speed (+348; +352 the loudest), the bounce slot (+172) must name a cue
//      of the loaded banks, and sub_821FF428 must give a sound (it returns to 82512078); its loudness is
//      (speed - lowest) / (loudest - lowest), never below 0.5.
// Here, for the props of the templates named in kWatched (metal crates, the security camera, every drum and its
// blown-off pieces): before Rare's routine runs, each such prop with a contact this step is looked at and what
// step stops it is logged; a sound Rare asks for (its start seen in the hook of sub_821FF428) is logged with the
// cue and whether the start was refused; a contact that passed every step but was not asked for (the 2 a call /
// 4 alive limits, or a better candidate) is logged as such. At most one line per prop and reason a second.
#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

REXCVAR_DECLARE(bool, pdz_restored_sounds);
REXCVAR_DEFINE_BOOL(pdz_snd_contact_log, true, "Debug",
                    "Log only: one 'pdz_snd_contact:' line per knock of a metal crate, a security camera or a drum "
                    "(the impact speed, whether the game asked for its knock sound, the cue, whether the start was "
                    "refused, or which of the game's own rules kept it silent)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_82511A90); // Rare's contact sound routine (f1 = dt)

namespace pdz::prop_contacts
{
  std::string TemplateOf(uint8_t *base, uint32_t rec); // restored_sounds_r2_Tue17Nov.cpp
  void OnContactSound(const uint8_t *base, uint32_t ref, uint32_t record, uint32_t sound);
}

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;                     // +24 -> the world
  constexpr uint32_t kWorldFirst = 43212, kWorldEnd = 43216; // world: object nodes (+4 object, +12 next)
  constexpr uint32_t kRecord = 340;                          // prop: its contact record
  constexpr uint32_t kRecSpeed = 24, kRecOn = 88, kRecTimer = 92;
  constexpr uint32_t kBouncesAlive = 0x82D9F484;
  constexpr const char *kWatched[] = {"metal_crate", "electrical\\camera", "drum"};

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && (a & 3u) == 0; }

  // Template record -> its name, or "" for one we do not watch (the game thread only; cleared when it grows).
  std::unordered_map<uint32_t, std::string> g_templates;
  struct Key
  {
    uint32_t prop;
    int reason;
    bool operator==(const Key &o) const { return prop == o.prop && reason == o.reason; }
  };
  struct KeyHash
  {
    size_t operator()(const Key &k) const { return size_t(k.prop) * 31u + size_t(k.reason); }
  };
  std::unordered_map<Key, std::chrono::steady_clock::time_point, KeyHash> g_last;
  uint32_t g_lines = 0;
  constexpr uint32_t kMaxLines = 600;
  struct Expected
  {
    uint32_t prop;
    float speed;
    std::string tpl;
    bool asked;
  };
  std::vector<Expected> g_expected; // within one call of Rare's routine
  bool g_inside = false;

  bool Limit(uint32_t prop, int reason)
  {
    if (g_lines >= kMaxLines)
    {
      return false;
    }
    const auto now = std::chrono::steady_clock::now();
    if (g_last.size() > 2048)
    {
      g_last.clear();
    }
    auto &t = g_last[{prop, reason}];
    if (now - t < std::chrono::seconds(1))
    {
      return false;
    }
    t = now;
    ++g_lines;
    return true;
  }

  const std::string &Watched(PPCContext &ctx, uint8_t *base, uint32_t prop, uint32_t &rec)
  {
    static const std::string none;
    rec = 0;
    const uint32_t vtable = Load32(base, prop);
    if (vtable < 0x82000000u || vtable >= 0x83000000u || !pdz::guestmem::Ok(base, vtable + 72, 4)) // (Sun 22 Nov night, audit)
    {
      return none;
    }
    PPCFunc *fn = pdz::GuestFunction(Load32(base, vtable + 72));
    if (!fn)
    {
      return none;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = prop;
      fn(frame.ctx, base);
      rec = frame.ctx.r3.u32;
    }
    if (!Guest(rec) || !pdz::guestmem::Ok(base, rec, 356)) // (Sun 22 Nov night, audit: +81 / +172 / +348 / +352 are read)
    {
      rec = 0;
      return none;
    }
    if (g_templates.size() > 4096)
    {
      g_templates.clear();
    }
    auto it = g_templates.find(rec);
    if (it == g_templates.end())
    {
      std::string name = pdz::prop_contacts::TemplateOf(base, rec);
      bool watched = false;
      for (const char *w : kWatched)
      {
        watched = watched || name.find(w) != std::string::npos;
      }
      it = g_templates.emplace(rec, watched ? name : std::string()).first;
    }
    return it->second;
  }
} // namespace

REX_HOOK_RAW(sub_82511A90)
{
  if (!REXCVAR_GET(pdz_snd_contact_log) || g_lines >= kMaxLines)
  {
    __imp__sub_82511A90(ctx, base);
    return;
  }
  g_expected.clear();
  const uint32_t world = Load32(base, kGame + 24);
  if (Guest(world))
  {
    const uint32_t end = Load32(base, world + kWorldEnd);
    uint32_t node = Load32(base, world + kWorldFirst);
    for (int i = 0; Guest(node) && node != end && i < 16384; ++i, node = Load32(base, node + 12))
    {
      const uint32_t prop = Load32(base, node + 4);
      // Sun 22 Nov night (audit): the prop mapped up to its contact record's timer, and (as Rare's routine tests before
      // its own vfunc +72 call) its contact record on: no virtual call on an object Rare would skip.
      if (!Guest(prop) || !pdz::guestmem::Ok(base, prop, kRecord + kRecTimer + 4) || !Guest(Load32(base, prop + 232)) ||
          base[prop + kRecord + kRecOn] == 0)
      {
        continue;
      }
      const float speed = LoadF(base, prop + kRecord + kRecSpeed);
      if (!(speed > 0.0f))
      {
        continue; // (no contact noted this step: nearly every prop, nearly every frame)
      }
      uint32_t rec = 0;
      const std::string &tpl = Watched(ctx, base, prop, rec);
      if (tpl.empty())
      {
        continue;
      }
      const bool on = base[prop + kRecord + kRecOn] != 0;
      const float timer = LoadF(base, prop + kRecord + kRecTimer);
      const float lo = LoadF(base, rec + 348), hi = LoadF(base, rec + 352);
      const uint32_t name = Load32(base, rec + 172);
      const char *cue = name >= 0x10000u && name < 0xF0000000u && pdz::guestmem::Ok(base, name, 64) // (Sun 22 Nov night, audit)
                            ? reinterpret_cast<const char *>(base + name)
                            : ""; // (text: any alignment)
      int reason = 0;
      std::string why;
      if (!on)
      {
        reason = 1;
        why = "its contact record is off (the prop was not marked for contact sounds at its set-up)";
      }
      else if (timer < 0.0f)
      {
        reason = 2;
        why = fmt::format("its start timer is at {:.2f} s (the game keeps a body mute until it has been moving for about "
                          "a second: the game's own rule)",
                          timer);
      }
      else if (base[rec + 81] == 0)
      {
        reason = 3;
        why = "its template's contact sound flag is off";
      }
      else if (!(speed > lo))
      {
        reason = 4;
        why = fmt::format("slower than its template's lowest speed {:.2f} m/s (the game's own threshold)", lo);
      }
      else if (!cue[0])
      {
        reason = 5;
        why = "its template's knock slot is empty";
      }
      if (reason == 0)
      {
        g_expected.push_back({prop, speed, tpl, false});
        continue;
      }
      if (Limit(prop, reason))
      {
        REXLOG_INFO("pdz_snd_contact: {} at ({:.0f} {:.0f} {:.0f}) knocked at {:.2f} m/s: NO SOUND ASKED: {} (knock slot {}, "
                    "speeds {:.2f} to {:.2f} m/s)",
                    tpl, LoadF(base, prop + 48), LoadF(base, prop + 52), LoadF(base, prop + 56), speed, why,
                    cue[0] ? std::string_view(cue, strnlen(cue, 63)).substr(std::min<size_t>(15, strnlen(cue, 63))) : std::string_view("(empty)"), lo,
                    hi);
      }
    }
  }
  const uint32_t alive_before = Load32(base, kBouncesAlive);
  g_inside = true;
  __imp__sub_82511A90(ctx, base);
  g_inside = false;
  for (const Expected &e : g_expected)
  {
    if (!e.asked && Limit(e.prop, 6))
    {
      REXLOG_INFO("pdz_snd_contact: {} at ({:.0f} {:.0f} {:.0f}) knocked at {:.2f} m/s: NO SOUND ASKED although it passed the "
                  "game's tests: {} knock sound(s) were alive (the game allows 4, and 2 new ones a call, nearest and "
                  "fastest first), or no listener, or its cue is not in the loaded banks",
                  e.tpl, LoadF(base, e.prop + 48), LoadF(base, e.prop + 52), LoadF(base, e.prop + 56), e.speed, alive_before);
    }
  }
  g_expected.clear();
}

namespace pdz::prop_contacts
{
  // From the hook of sub_821FF428 when it was called by the contact routine's knock site (it returns to 82512078):
  // ref = the cue reference (the name), record = the contact record (the position argument), sound = what came back.
  void OnContactSound(const uint8_t *base, uint32_t ref, uint32_t record, uint32_t sound)
  {
    if (!g_inside || !Guest(record) || record < kRecord)
    {
      return;
    }
    const uint32_t prop = record - kRecord;
    for (Expected &e : g_expected)
    {
      if (e.prop != prop)
      {
        continue;
      }
      e.asked = true;
      if (Limit(prop, sound ? 7 : 8))
      {
        const char *name = ref >= 0x10000u && ref < 0xF0000000u ? reinterpret_cast<const char *>(base + ref) : "";
        const std::string_view cue(name, strnlen(name, 96));
        REXLOG_INFO("pdz_snd_contact: {} at ({:.0f} {:.0f} {:.0f}) knocked at {:.2f} m/s: the game asked for {}: {}", e.tpl,
                    LoadF(base, prop + 48), LoadF(base, prop + 52), LoadF(base, prop + 56), e.speed,
                    cue.size() > 15 ? cue.substr(15) : cue,
                    sound ? "started"
                          : "THE START WAS REFUSED (the sound manager gave no sound: out of the cue's range, its kind's limit, "
                            "or no sound object free)");
      }
      return;
    }
  }
} // namespace pdz::prop_contacts
