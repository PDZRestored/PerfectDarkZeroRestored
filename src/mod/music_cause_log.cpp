// Music cause diagnostic (pdz_music_log): logging only.
//
// One line whenever the level music changes in a single-player level:
//   pdz_music: [clock] <previous> -> <new>: <trigger> (...)
// - Interactive level (Silence / Stealth / Speedup): the wanted level
//   (manager 82E682C8 +172) as Rare's per-frame picker stores it. The trigger
//   is worked out from the same state the picker reads, in the same frame:
//   the script override, the level alarm byte, the alarm / radio runner
//   count, and the count of engaged guards (walked with Rare's own iterator,
//   as the picker does), with who joined or dropped out since the last line.
// - Music type (None / Interactive / Tune1-4 / Death): the caller's return
//   address names the level script moment.
// Nothing is written to the game except Rare's iterator chain (linked and
// unlinked again within the call, as the picker itself does).
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

#include "../game_cvars.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit)
#include "music_cause_log.h"

REX_EXTERN(sub_82527378); // game-state object iterator: next
REX_EXTERN(sub_825272F0); // game-state object iterator: unlink

namespace
{
  // --- the game (Title Update 3 addresses) ---
  constexpr uint32_t kGame = 0x82D6BFD8;          // +0 game info, +24 object list, +28 world, +32 level object, +68 state
  constexpr uint32_t kMusic = 0x82E682C8;         // music manager (launch 82E67500)
  constexpr uint32_t kPlayerState = kMusic + 148; // 0 stopped, 1 paused, 2 playing, 3 changing
  constexpr uint32_t kCurrent = kMusic + 168;     // interactive level playing
  constexpr uint32_t kWanted = kMusic + 172;      // interactive level wanted
  constexpr uint32_t kType = kMusic + 176;        // music type playing
  constexpr uint32_t kTypeWanted = kMusic + 180;  // music type requested
  constexpr uint32_t kAuto = kMusic + 184;        // byte: the automatic picker is on
  constexpr uint32_t kCalmTimer = 0x82E2F3F0;     // float: seconds with nobody engaged
  constexpr uint32_t kAiClock = 0x82E2F334;       // float: the level's AI clock
  constexpr uint32_t kPlayerPointer = 0x82E3C3E4; // the picker counts only when this is set
  constexpr uint32_t kIteratorChain = 0x82E3C398; // head of the chain of live object iterators
  constexpr uint32_t kCoopObject = 0x82E34210;    // -> +36 byte: 0 = takes the level from the other machine
  constexpr uint32_t kWorldLevel = 728;           // world -> level; level +84 alarm byte, +116 level script
  constexpr uint32_t kWorldRunners = 761;         // world: signed byte, guards running to an alarm / radio
  constexpr uint32_t kScriptOverride = 964;       // level script: IaLevel override, 3 = none
  constexpr uint32_t kFlags = 6252;               // character: 0x2 has a target, 0x10 alerted
  constexpr uint32_t kFlagsA = 6268;              // character: 0x400000 (branch A, never set in the shipped game)
  constexpr uint32_t kLastSeen = 12536;           // character: double, AI clock of his last sighting
  constexpr float kSeenWindow = 20.0f;            // 8208C57C
  constexpr float kHold = 1.0f;                   // 820DFD34
  constexpr uint32_t kIteratorSize = 32;          // Rare's iterator is 0x1C bytes

  uint32_t Load32(const uint8_t *base, uint32_t address)
  {
    const uint8_t *p = base + address;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }

  void Store32(uint8_t *base, uint32_t address, uint32_t value)
  {
    uint8_t *p = base + address;
    p[0] = uint8_t(value >> 24);
    p[1] = uint8_t(value >> 16);
    p[2] = uint8_t(value >> 8);
    p[3] = uint8_t(value);
  }

  float LoadFloat(const uint8_t *base, uint32_t address)
  {
    const uint32_t bits = Load32(base, address);
    float value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  double LoadDouble(const uint8_t *base, uint32_t address)
  {
    const uint64_t bits = (uint64_t(Load32(base, address)) << 32) | Load32(base, address + 4);
    double value;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  bool Ptr(uint32_t address)
  {
    return address >= 0x00010000u && address < 0xFFFF0000u;
  }

  const char *LevelName(uint32_t level)
  {
    static const char *const names[] = {"Silence", "Stealth", "Speedup"};
    return level < 3 ? names[level] : "?";
  }

  const char *TypeName(uint32_t type)
  {
    static const char *const names[] = {"None", "Interactive", "Tune1", "Tune2", "Tune3", "Tune4", "Death"};
    return type < 7 ? names[type] : "?";
  }

  std::string GuestString(const uint8_t *base, uint32_t address, size_t limit)
  {
    if (!Ptr(address))
    {
      return {};
    }
    const char *p = reinterpret_cast<const char *>(base + address);
    size_t n = 0;
    while (n < limit && p[n] >= 0x20 && p[n] < 0x7F)
    {
      ++n;
    }
    return std::string(p, n);
  }

  // "s3g4@8A1B2C00": the character's setup id ([[c+0x54]+0x34]) and address.
  std::string Who(const uint8_t *base, uint32_t c)
  {
    // Sun 22 Nov night (audit): every step read only where mapped (a character gone from the list may be freed; the
    // name pointer may be text), as sound_heard's Who.
    const uint32_t data = pdz::guestmem::Ok(base, c, 0x58) ? Load32(base, c + 0x54) : 0;
    const uint32_t name = pdz::guestmem::Ok(base, data, 0x38) ? Load32(base, data + 0x34) : 0;
    std::string id = pdz::guestmem::Ok(base, name, 8) ? GuestString(base, name, 8) : std::string();
    char text[16];
    std::snprintf(text, sizeof(text), "@%08X", c);
    return (id.empty() ? std::string("?") : id) + text;
  }

  struct Guard
  {
    uint32_t c = 0, flags = 0;
    double last_seen = 0.0;
    float since = 0.0f; // clock - last_seen
    bool engaged = false;
  };

  std::vector<Guard> g_prev, g_now;  // every character of the last / this walk
  bool g_have_prev = false;
  std::vector<std::string> g_events; // joins / drops since the last logged line
  uint32_t g_iterator = 0;           // guest: Rare's object iterator
  bool g_alarm = false;
  int g_runners = 0;
  float g_alarm_since = 0.0f, g_runners_since = 0.0f; // AI clock of the last change

  void Event(std::string text)
  {
    if (g_events.size() >= 8)
    {
      g_events.erase(g_events.begin());
    }
    g_events.push_back(std::move(text));
  }

  const Guard *Find(const std::vector<Guard> &list, uint32_t c)
  {
    for (const Guard &g : list)
    {
      if (g.c == c)
      {
        return &g;
      }
    }
    return nullptr;
  }

  // The picker's own test (launch 821F8914-821F8964).
  bool Engaged(uint32_t flags, uint32_t flags_a, float since)
  {
    const bool a = (flags_a & 0x400000u) != 0;
    if (a && (flags & 0x10))
    {
      return true;
    }
    return (flags & 0x2) && !a && (flags & 0x10) && since < kSeenWindow;
  }

  // Walks the characters as the picker does (launch 821F8844-821F8970: type
  // filter 33, object flag [[object+8]+8] & 0x10). Returns the engaged count.
  int Walk(PPCContext &ctx, uint8_t *base, std::vector<Guard> &out)
  {
    out.clear();
    if (!g_iterator)
    {
      g_iterator = REX_KERNEL_MEMORY()->SystemHeapAlloc(kIteratorSize);
      if (!g_iterator)
      {
        return 0;
      }
    }
    const uint32_t list = Load32(base, kGame + 24);
    if (!Ptr(list))
    {
      return 0;
    }
    const float clock = LoadFloat(base, kAiClock);
    const uint32_t it = g_iterator;
    Store32(base, it + 0x00, 0);           // current node
    Store32(base, it + 0x04, 0);           // current bucket
    Store32(base, it + 0x08, 0xFFFFFFFFu); // bucket index, -1 before the first
    Store32(base, it + 0x0C, 33);          // type filter: characters
    base[it + 0x10] = 0;
    Store32(base, it + 0x14, list);
    Store32(base, it + 0x18, Load32(base, kIteratorChain));
    Store32(base, kIteratorChain, it);
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = it;
      sub_82527378(frame.ctx, base);
    }
    int count = 0;
    uint32_t node = Load32(base, it);
    for (int guard = 0; Ptr(node) && guard < 4096; ++guard)
    {
      const uint32_t object = Load32(base, node + 4);
      const uint32_t next = Load32(base, node + 0xC);
      Store32(base, it, next);
      const uint32_t bucket = Load32(base, it + 4);
      if (Ptr(bucket) && Load32(base, bucket + 4) == next)
      {
        rex::CallFrame frame(ctx); // this bucket is done: on to the next
        frame.ctx.r3.u64 = it;
        sub_82527378(frame.ctx, base);
      }
      const uint32_t kind = Ptr(object) ? Load32(base, object + 8) : 0;
      if (Ptr(kind) && (Load32(base, kind + 8) & 0x10))
      {
        Guard g;
        g.c = object;
        g.flags = Load32(base, object + kFlags);
        g.last_seen = LoadDouble(base, object + kLastSeen);
        g.since = float(double(clock) - g.last_seen);
        g.engaged = Engaged(g.flags, Load32(base, object + kFlagsA), g.since);
        count += g.engaged ? 1 : 0;
        out.push_back(g);
      }
      node = Load32(base, it);
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = it;
    sub_825272F0(frame.ctx, base);
    return count;
  }

  // Who joined / dropped out of the engaged set since the last walk, and why.
  void Diff(const uint8_t *base)
  {
    char text[160];
    for (const Guard &g : g_now)
    {
      const Guard *p = Find(g_prev, g.c);
      if (!g.engaged || (p && p->engaged))
      {
        continue;
      }
      std::string why;
      if (!p)
      {
        why = "new in the character list";
      }
      else
      {
        if (!(p->flags & 0x10) && (g.flags & 0x10))
        {
          why += "alerted (bit 0x10 set: a COMBAT / SEARCH / alarm script started); ";
        }
        if (!(p->flags & 0x2) && (g.flags & 0x2))
        {
          why += "got a target (bit 0x2); ";
        }
        if (g.last_seen != p->last_seen)
        {
          std::snprintf(text, sizeof(text), "saw the target (last sighting was %.1f s old)", p->since);
          why += text;
        }
        if (why.empty())
        {
          std::snprintf(text, sizeof(text), "flags %08X -> %08X", p->flags, g.flags);
          why = text;
        }
      }
      Event("+" + Who(base, g.c) + ": " + why);
    }
    for (const Guard &p : g_prev)
    {
      if (!p.engaged)
      {
        continue;
      }
      const Guard *n = Find(g_now, p.c);
      if (n && n->engaged)
      {
        continue;
      }
      if (!n)
      {
        Event("-" + Who(base, p.c) + ": gone from the character list (dead / removed)");
      }
      else if (n->since >= kSeenWindow)
      {
        Event("-" + Who(base, p.c) + ": has not seen the target for 20 s");
      }
      else
      {
        std::snprintf(text, sizeof(text), ": flags %08X -> %08X", p.flags, n->flags);
        Event("-" + Who(base, p.c) + text);
      }
    }
  }

  // The picker's gate (launch 821F8DD0 = T 82200E30).
  bool Gate(const uint8_t *base)
  {
    const uint32_t info = Load32(base, kGame);
    const uint32_t state = Load32(base, kGame + 68);
    if (!Ptr(info) || !Ptr(state) || Load32(base, info + 8) != 21)
    {
      return false;
    }
    const uint32_t mode = Load32(base, state + 12);
    return (mode == 0 || mode == 4) && Load32(base, kType) == 1 && base[kAuto] != 0;
  }

  // The return addresses of the calls of the type change (T 82200C68).
  const char *TypeCaller(uint32_t lr)
  {
    switch (lr)
    {
    case 0x8222FF9C: return "player death (any level, 8222FF58)";
    case 0x82327F40: return "Laboratory script update: objective 2 completed (level flag word +0x10 bit 0)";
    case 0x8231F11C: return "Laboratory script setup: checkpoint restart past objective 2";
    case 0x8231F248: return "Laboratory script setup: checkpoint restart past objective 2 (second site)";
    case 0x82337564: return "Mansion script update: the Mai Hem fight starts";
    case 0x82336980: return "Mansion script update: back to the layers (level04_outro preload)";
    case 0x82369004: return "Streets script update: subway descent";
    case 0x8236A148: return "Streets script update: to the interactive layers";
    case 0x822C3F74: return "Trinity Infiltration script setup";
    case 0x822CBB24: return "Trinity Infiltration script update";
    case 0x823013F8: return "Datacore script update: CamSpy";
    case 0x8230150C: return "Datacore script update: music off (beam / explosion)";
    case 0x82301568: return "Datacore script update: action";
    case 0x8234B5C8: return "Rooftops script update: dropship boss";
    case 0x8235C2E0: return "Temple script update: main";
    case 0x8235F0A4: return "Temple script update: CamSpy";
    case 0x82285AF0: return "River script (82281170)";
    case 0x8228F91C: return "River script (8228EE90)";
    case 0x8228E3B0: return "River script (82288888): river chase";
    case 0x822A638C: return "Bridge script update: assault";
    case 0x8230F26C: return "Jungle script setup";
    case 0x82315F94: return "Jungle script update (82315F94)";
    case 0x82317C7C: return "Jungle script update (82317C7C)";
    case 0x82318064: return "Jungle script update: village fight";
    case 0x82318094: return "Jungle script update: jungle fight";
    case 0x8237E044: return "Outpost script (8237DFF0): the end tune";
    default: return "unknown caller (SetMusicType 82200870 tail call or a new site)";
    }
  }

  // The tune a type plays: the level's music record ([[kGame+32]+36]) +72..+84.
  std::string TuneName(const uint8_t *base, uint32_t type)
  {
    if (type < 2 || type > 5)
    {
      return {};
    }
    const uint32_t holder = Load32(base, kGame + 32);
    const uint32_t record = Ptr(holder) ? Load32(base, holder + 36) : 0;
    return Ptr(record) ? GuestString(base, Load32(base, record + 72 + 4 * (type - 2)), 96) : std::string();
  }
} // namespace

namespace music_cause_log
{
  bool Enabled()
  {
    return REXCVAR_GET(pdz_music_log);
  }

  Pick BeforePick(const PPCContext &ctx, const uint8_t *base)
  {
    Pick p;
    p.world = ctx.r3.u32;
    p.wanted = Load32(base, kWanted);
    p.lr = uint32_t(ctx.lr);
    p.dt = float(ctx.f1.f64);
    return p;
  }

  void AfterPick(PPCContext &ctx, uint8_t *base, const Pick &before)
  {
    if (!Gate(base) || !Ptr(before.world))
    {
      return; // the picker did nothing this frame
    }
    const float clock = LoadFloat(base, kAiClock);
    const uint32_t wanted = Load32(base, kWanted);
    const uint32_t current = Load32(base, kCurrent);
    const uint32_t state = Load32(base, kPlayerState);
    const float timer = LoadFloat(base, kCalmTimer); // after the picker's own update
    const uint32_t level = Load32(base, before.world + kWorldLevel);
    const bool alarm = Ptr(level) && base[level + 84] != 0;
    const int runners = int8_t(base[before.world + kWorldRunners]);
    if (alarm != g_alarm)
    {
      g_alarm = alarm;
      g_alarm_since = clock;
    }
    if (runners != g_runners)
    {
      g_runners = runners;
      g_runners_since = clock;
    }

    // The picker's own path, from the state it read.
    char text[256];
    std::string cause;
    int expected = -1; // -1: not worked out (co-op)
    int count = -1;    // -1: the picker did not count
    const uint32_t info = Load32(base, kGame);
    const uint32_t game_type = Load32(base, info + 40);
    const uint32_t coop_object = Load32(base, kCoopObject);
    const uint32_t script = Ptr(level) ? Load32(base, level + 116) : 0;
    const uint32_t override_level = Ptr(script) ? Load32(base, script + kScriptOverride) : 3;
    const uint32_t player = Load32(base, kPlayerPointer);
    if (game_type > 1)
    {
      std::snprintf(text, sizeof(text), "game type %u: fixed Stealth", game_type);
      cause = text;
      expected = 1;
    }
    else if (game_type == 1 && Load32(base, info + 32) != 0 && Ptr(coop_object) && base[coop_object + 36] == 0)
    {
      cause = "co-op: the level sent by the other machine";
    }
    else if (override_level != 3)
    {
      std::snprintf(text, sizeof(text), "level script override (script +964 = %u)", override_level);
      cause = text;
      expected = int(override_level);
    }
    else
    {
      if (player != 0) // the picker skips the count only when there is no player object
      {
        count = Walk(ctx, base, g_now);
        if (g_have_prev)
        {
          Diff(base);
        }
        g_prev.swap(g_now);
        g_have_prev = true;
      }
      const bool speedup_playing = state != 0 && current == 2;
      if (count >= 2)
      {
        std::snprintf(text, sizeof(text), "%d guards engaged (alerted, with a target seen in the last 20 s)", count);
        cause = text;
        expected = 2;
      }
      else if (count == 1)
      {
        cause = speedup_playing ? "1 guard still engaged: Speedup kept" : "exactly 1 guard engaged";
        expected = speedup_playing ? 2 : 0;
      }
      else if (speedup_playing && timer < kHold)
      {
        std::snprintf(text, sizeof(text), "nobody engaged for %.2f s: Speedup held for 1 s", timer);
        cause = text;
        expected = 2;
      }
      else
      {
        expected = (alarm || runners > 0) ? 0 : 1;
        cause = count < 0 ? "no player object, nobody counted; " : "nobody engaged; ";
        if (alarm)
        {
          std::snprintf(text, sizeof(text), "the level alarm is on (for %.1f s)", clock - g_alarm_since);
          cause += text;
        }
        if (runners > 0)
        {
          std::snprintf(text, sizeof(text), "%s%d guard(s) running to an alarm / radio (for %.1f s)", alarm ? ", " : "",
                        runners, clock - g_runners_since);
          cause += text;
        }
        if (!alarm && runners <= 0)
        {
          std::snprintf(text, sizeof(text), "no alarm (off for %.1f s), no alarm / radio runner", clock - g_alarm_since);
          cause += text;
        }
      }
    }
    if (wanted == before.wanted)
    {
      return;
    }

    std::string engaged;
    for (const Guard &g : g_prev) // g_prev holds this frame's walk after the swap
    {
      if (count >= 0 && g.engaged)
      {
        std::snprintf(text, sizeof(text), " %s (seen %.1f s ago)", Who(base, g.c).c_str(), g.since);
        engaged += text;
      }
    }
    std::string events;
    for (const std::string &e : g_events)
    {
      events += " [" + e + "]";
    }
    g_events.clear();
    std::string check;
    if (expected >= 0 && uint32_t(expected) != wanted)
    {
      check = std::string(" UNEXPLAINED: worked out ") + LevelName(uint32_t(expected));
    }
    REXLOG_INFO("pdz_music: [{:8.2f}] {} -> {}: {}{}{}{}{}{} (picker 82200928 from {:08X}; playing {}, player state {}, "
                "alarm {}, runners {})",
                clock, LevelName(before.wanted), LevelName(wanted), cause, engaged.empty() ? "" : "; engaged:", engaged,
                events.empty() ? "" : "; since the last line:", events, check, before.lr, LevelName(current), state,
                alarm ? "on" : "off", runners);
  }

  Type BeforeType(const PPCContext &ctx, const uint8_t *base)
  {
    Type t;
    t.from = Load32(base, kType);
    t.to = Load32(base, kTypeWanted);
    t.lr = uint32_t(ctx.lr);
    return t;
  }

  void AfterType(const uint8_t *base, const Type &before)
  {
    if (before.from == before.to)
    {
      return;
    }
    const uint32_t now = Load32(base, kType);
    const std::string tune = TuneName(base, before.to);
    REXLOG_INFO("pdz_music: [{:8.2f}] type {} -> {}{}{}{}: {} (from {:08X}){}", LoadFloat(base, kAiClock),
                TypeName(before.from), TypeName(before.to), tune.empty() ? "" : " \"", tune, tune.empty() ? "" : "\"",
                TypeCaller(before.lr), before.lr, now == before.to ? "" : " NOT APPLIED (no music record)");
  }

  void AfterSetup(const uint8_t *base)
  {
    g_prev.clear();
    g_now.clear();
    g_events.clear();
    g_have_prev = false;
    g_alarm = false;
    g_runners = 0;
    g_alarm_since = g_runners_since = 0.0f;
    REXLOG_INFO("pdz_music: level music setup: type {}, starting level {}, automatic picker {}",
                TypeName(Load32(base, kType)), LevelName(Load32(base, kWanted)), base[kAuto] ? "on" : "off");
  }
} // namespace music_cause_log
