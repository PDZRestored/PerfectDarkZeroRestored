// pdz_sound: MISSING (sound audit, Tue 17 Nov 2026; always on, pdz_sound_missing_log silences it). TU3 addresses.
//
// Every cue the game plays by name goes through sub_82202D20 (ref {char name[256]; cue}, name), whose only resolver is
// sub_82202C78: the name less its first 15 characters in the loaded banks' name table (sub_826332E8, an exact,
// case-sensitive match), then that cue's bank slot. A name that is not in the table, or whose bank slot is empty,
// gives -1, which sub_82202D20 stores at ref + 256: the request then plays nothing, and nothing in the game says so.
//
// The hook of sub_82202D20 (cut_scenarios.cpp) calls OnMiss here when the reference comes back with -1. One line per
// name per level:
//   pdz_sound: MISSING <name> | caller <return addresses> | level <name> | <where the name is kept> | <object>
// - caller: the return address into the function that asked, and the next two up the guest stack;
// - where the name is kept: a string of the game's own code, or level data; for a prop template's sound slot the
//   template (its propattributes.xml path precedes its sound names in the level data);
// - object: the prop, for the per-frame fall sounds' lookup (sub_82511A90 at 8251204C: its list entry is in r30);
//   otherwise unknown: the lookup itself does not carry the object.
// Not logged: a name our own code chose (the mod's remaps and its deliberate "silent" names), lookups made by our own
// code (their return address is not in the game's code), an empty name or an empty sound slot ("XACT_SOUNDBANK_" and
// nothing after it). When the level changes, one summary line for the level left.
//
// Cost: when the cue resolves, the hook does one load and one compare more. Everything else here runs only on a miss;
// the system is asked about memory (QueryProtect) only for a line that is actually written.
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/context.h>
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DEFINE_BOOL(pdz_sound_missing_log, true, "Debug",
                    "Log 'pdz_sound: MISSING ...' whenever the game asks for a sound by a name that no loaded sound "
                    "bank has (the cue name, who asked, the level, the prop template or object where known), once "
                    "per name per level, and a count when the level changes. Off: no such lines")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace pdz::pace
{
  std::string SpLevelName(const uint8_t *base, std::string *raw); // render/game_pace.cpp
}

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;       // -> the game state / session block (+25 the mission); +32 -> the audio
                                               // manager (+1844 -> the loaded banks' name table)
  constexpr uint32_t kFallLookup = 0x82512050; // the return address of the fall slot's name lookup in sub_82511A90
  constexpr uint32_t kCodeBegin = 0x82000000u, kCodeEnd = 0x82D00000u; // the game's code and constant data
  constexpr uint32_t kImageEnd = 0x83400000u;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xFFFF0000u; }

  // Mapped, readable guest memory (asks the system: only for a line that is written).
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

  // Return addresses on the guest stack (back chain at +0, saved LR at -8 of the caller's frame).
  std::string Callers(const PPCContext &ctx, const uint8_t *base, uint32_t lr, int depth)
  {
    std::string chain = fmt::format("{:08X}", lr);
    uint32_t sp = ctx.r1.u32;
    for (int i = 0; i < depth && Guest(sp); ++i)
    {
      const uint32_t next = Load32(base, sp);
      if (!Guest(next) || next <= sp || next - sp > 0x8000 || (next & 0xFFFF) == 0)
      {
        break;
      }
      chain += fmt::format(" < {:08X}", Load32(base, next - 8));
      sp = next;
    }
    return chain;
  }

  // A printable string at `address`, at most `limit` characters ("" if the memory is not readable).
  std::string Text(uint8_t *base, uint32_t address, size_t limit)
  {
    if (!Readable(base, address, uint32_t(limit)))
    {
      return {};
    }
    const char *c = reinterpret_cast<const char *>(base + address);
    size_t n = 0;
    while (n < limit && c[n] >= 32 && c[n] < 127)
    {
      ++n;
    }
    return std::string(c, n);
  }

  // Where the asked-for name is kept.
  std::string Home(uint8_t *base, uint32_t name)
  {
    if (name >= kCodeBegin && name < kImageEnd)
    {
      return fmt::format("name at {:08X}: a string of the game's own code", name);
    }
    // A prop template's sound names follow its "...\proptemplate\<group>\<name>\propattributes.xml" path in the level
    // data, about 490 bytes on (the first name) to about 700 (the last of eight).
    constexpr uint32_t kBack = 900;
    constexpr char kTail[] = "\\propattributes.xml";
    constexpr char kHead[] = "proptemplate\\";
    if (name > 0x10000u + kBack && Readable(base, name - kBack, kBack))
    {
      const char *begin = reinterpret_cast<const char *>(base + name - kBack);
      const std::string_view window(begin, kBack);
      const size_t tail = window.rfind(kTail);
      if (tail != std::string_view::npos)
      {
        const size_t head = window.rfind(kHead, tail);
        if (head != std::string_view::npos && tail - head < 96)
        {
          const std::string_view path = window.substr(head + sizeof(kHead) - 1, tail - head - (sizeof(kHead) - 1));
          bool printable = !path.empty();
          for (char c : path)
          {
            printable = printable && c >= 32 && c < 127;
          }
          if (printable)
          {
            return fmt::format("name at {:08X}: level data, a sound slot of prop template {}", name, path);
          }
        }
      }
    }
    return fmt::format("name at {:08X}: level data or built by code", name);
  }

  // The object, where the lookup's caller is one whose registers are known.
  std::string Object(const PPCContext &ctx, uint8_t *base, uint32_t lr)
  {
    if (lr == kFallLookup)
    {
      // sub_82511A90 at its call: r30 -> its list entry (+0 the prop). The prop's model name: setup (+84) -> +140.
      const uint32_t entry = ctx.r30.u32;
      const uint32_t prop = Readable(base, entry, 4) ? Load32(base, entry) : 0;
      const uint32_t setup = Readable(base, prop, 96) ? Load32(base, prop + 84) : 0;
      const uint32_t model = Readable(base, setup, 144) ? Load32(base, setup + 140) : 0;
      const std::string text = Text(base, model, 96);
      if (prop)
      {
        return fmt::format("object: prop {:08X}{}{} (a falling prop's impact sound)", prop, text.empty() ? "" : " model ",
                           text);
      }
    }
    return "object unknown (the lookup does not carry it)";
  }

  std::string LevelLabel(const uint8_t *base, uint32_t session)
  {
    std::string raw;
    const std::string name = pdz::pace::SpLevelName(base, &raw);
    const int mission = Guest(session) ? int(base[session + 25]) : -1;
    if (!name.empty())
    {
      return fmt::format("{} ({})", name, raw);
    }
    if (!raw.empty())
    {
      return raw;
    }
    return mission >= 0 ? fmt::format("not a single player level (session mission byte {})", mission)
                        : std::string("no game session (front end or loading)");
  }

  std::mutex g_mutex;
  std::unordered_set<uint64_t> g_seen; // name hashes of the level in g_level
  uint64_t g_level = ~uint64_t(0);     // (session pointer << 8) | mission
  std::string g_level_label;
  uint64_t g_level_lookups = 0;        // lookups that found nothing on this level (every one, not once per name)
} // namespace

namespace pdz::soundmissing
{
  // After sub_82202D20 stored -1 in a reference: `asked` = the name looked up (after the mod's remaps),
  // `asked_by_game` = the name the game passed, `lr` = the return address into the game's function that asked.
  void OnMiss(PPCContext &ctx, uint8_t *base, uint32_t asked, uint32_t asked_by_game, uint32_t lr)
  {
    if (!REXCVAR_GET(pdz_sound_missing_log) || asked != asked_by_game || lr < kCodeBegin || lr >= kCodeEnd ||
        !Guest(asked))
    {
      return;
    }
    const char *text = reinterpret_cast<const char *>(base + asked);
    const size_t length = strnlen(text, 200);
    if (length == 0 || (length <= 15 && std::strncmp(text, "XACT_SOUNDBANK_", length) == 0))
    {
      return; // an empty name or an empty sound slot: nothing was asked for
    }
    uint64_t hash = 1469598103934665603ull; // FNV-1a
    for (size_t i = 0; i < length; ++i)
    {
      hash = (hash ^ uint8_t(text[i])) * 1099511628211ull;
    }
    const uint32_t session = Load32(base, kGame);
    const uint64_t level = (uint64_t(session) << 8) | (Guest(session) ? base[session + 25] : 0xFFu);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (level != g_level)
    {
      if (!g_seen.empty())
      {
        REXLOG_INFO("pdz_sound: MISSING summary: {} name(s) asked for and not found on {} ({} lookups in all found "
                    "nothing)",
                    g_seen.size(), g_level_label, g_level_lookups);
      }
      g_seen.clear();
      g_level = level;
      g_level_lookups = 0;
      g_level_label = LevelLabel(base, session);
    }
    ++g_level_lookups;
    if (!g_seen.insert(hash).second)
    {
      return;
    }
    // Is a name table loaded at all? (sub_82202C78: [[kGame + 32] + 1844] -> the table; none = every name misses.)
    const uint32_t audio = Load32(base, kGame + 32);
    const uint32_t holder = Readable(base, audio, 1848) ? Load32(base, audio + 1844) : 0;
    const bool table = Readable(base, holder, 4) && Load32(base, holder) != 0;
    std::string name;
    name.reserve(length + 8);
    for (size_t i = 0; i < length; ++i) // control characters as \xNN, so the line stays one line
    {
      const unsigned char c = static_cast<unsigned char>(text[i]);
      if (c < 32 || c >= 127)
      {
        name += fmt::format("\\x{:02X}", c);
      }
      else
      {
        name += char(c);
      }
    }
    const bool prefixed = name.rfind("XACT_SOUNDBANK_", 0) == 0;
    REXLOG_INFO("pdz_sound: MISSING {}{} | caller {} | level {} | {} | {}{}",
                prefixed ? name.substr(15) : name,
                prefixed ? "" : " (not an XACT_SOUNDBANK_ name: the game still drops its first 15 characters)",
                Callers(ctx, base, lr, 2), g_level_label, Home(base, asked), Object(ctx, base, lr),
                table ? "" : " | no sound bank name table is loaded yet: every name misses now");
  }
} // namespace pdz::soundmissing
