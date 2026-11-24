// Counter-Operative, round 2 (h): player 2's first-person hands when the level has none for his guard body.
// Built only with PDZ_COUNTEROP (CMakeLists.txt's counter-op list). Evidence and addresses: the round 2 (h) report.
//
// How Rare loads player models (TU3, read in the code):
// - Assets are found or loaded by name: 821F41A8(name) -> record (+0 name, +4 raw data, +8 data, +0x1C refs u16,
//   +0x1E flags u16, 0x4 = loaded). Found: one more reference. Not found: a load from the asset's file (a package:
//   PackageData\<hash>.pkg), allowed unless the game state ([[82D6BFD8]] +8) is 21 (in the level) and 82E2F36B is 0.
//   821F4148(name, &out) loads a package by name (out = its data when loaded); 821F4D28(name) is the pure lookup.
// - A package registers EVERY asset it holds when it loads (821F7DB8, the package kind's handler, dispatched by
//   827020C0 with r3 = the package's name, r4 = its data: count at +0x20, 0x20-byte records from +0x24, +0 name);
//   the first registration of a name wins. There is no later "instantiate" step: a hands model the lookup can't find
//   is in no resident package.
// - SP / co-op: the level's package aid_darkpackage_<level> (82162860, heap 10), charanims, the gun packages
//   (DarkGunLoad 824F1C38: aid_darkpackage_gun_<name>, loaded with the heap / mode as they are). A human's hands come
//   from 82414070(body entry, 1): +16 by name (fallback trooper_hands) -> +20.
// - Multiplayer (8233CAC8): team packages aid_darkpackage_multi_char_team0..5 (names at 82D27658, 64 bytes each),
//   three characters each (bodies 82D27580[3k+i], heads 82D27610[3k+i]); without teams
//   aid_darkpackage_multi_char_noteams (82D277D8: bodies 82D277FC[0..5], heads 82D27814[0..5]); each character's
//   body entry goes through 82414070(entry, 1), so its hands come from that package.
//
// pdz_counterop_hands_probe (Testing, off): logging only, in every game mode. One line per package that loads (the
// hands / bodies / heads it registers) and, when a level has loaded (822795D0, state 4 -> 21), the body list with
// each entry's hands and where they are, the MP character packages (on disk? registered?) with their characters,
// and whether a package named after each hands model exists.
// pdz_counterop_load_hands (on): when the guard body has no hands and none of the hands lists is resident,
// PdzCounterOpLoadHands() loads one more package by name through Rare's loader, the way DarkGunLoad does, from the
// player's own game data at run time, and returns the hands from it (male soldier hands for a male guard body,
// non-Jo female hands for a female one). Every step is a 'pdz_counterop: hands load:' line; nothing falls back
// silently.
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

#include "render/game_pace.h" // SpLevelName
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

REXCVAR_DEFINE_BOOL(pdz_counterop_hands_probe, false, "Testing",
                    "Counter-Operative research (logging only, any game mode): one 'hands probe:' line per package "
                    "that loads (the player hands, bodies and heads it registers) and, when a level has loaded, the "
                    "level's body list (each body's hands: name, resident, which package), Rare's multiplayer "
                    "character packages (present, registered, their characters) and the per-model package names")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_load_hands, true, "Gameplay",
                    "Counter-Operative (default on): when player 2's guard body has no first-person hands in this "
                    "level, load a package that has hands of the guard's sex (male soldier hands for a male guard, "
                    "non-Jo female hands for a female guard) through Rare's own loader at the level start, instead "
                    "of the co-op partner's arms. Off = the co-op partner's arms (logged)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_hands_source_male, "auto", "Gameplay",
                      "Counter-Operative: where player 2's male hands come from when the level has none: 'package|"
                      "hands' pairs, comma separated (short names allowed: 'multi_char_team2|privateforce_a_hands' = "
                      "aid_darkpackage_multi_char_team2 | aid_darkcharacter_xenonhands_privateforce_a_hands), tried in "
                      "order. 'auto' = Rare's multiplayer character packages, their male characters' hands")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_hands_source_female, "auto", "Gameplay",
                      "Counter-Operative: as pdz_counterop_hands_source_male, for a female guard body ('auto' = the "
                      "multiplayer packages' female characters whose hands are not Jo's)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_load_hands_skip, "nightclub", "Gameplay",
                      "Counter-Operative: levels (comma separated, Rare's short names) with no room for a hands "
                      "package at Rare's memory sizes: a package is loaded there only when the LevelGPUWC heap was made "
                      "at least pdz_counterop_hands_extra_mb larger than Rare's (pdz_level_memory_scale). Nightclub "
                      "fills 202.9 of Rare's 218.3 MB by itself and one MP character package needs a 15.8 MB block: "
                      "at Rare's size the load was refused (Rare's disc-error screen or a crash). Without the room he "
                      "keeps the co-op body's arms there, logged as a warning")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_UINT32(pdz_counterop_hands_extra_mb, 40, "Gameplay",
                      "Counter-Operative: the extra LevelGPUWC memory (MB above Rare's 218.3) a level of "
                      "pdz_counterop_load_hands_skip needs before its hands packages are loaded (two packages: 32 MB)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_hands_prefer, "trooper,swat,privateforce,vtroop,soldier,guard", "Gameplay",
                      "Counter-Operative: with the 'auto' sources, hands names containing one of these words are "
                      "tried first, in this order")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_821F7DB8); // the package kind's load handler: (package name, package data); registers its assets
REX_EXTERN(__imp__sub_822795D0); // the level load's end: player creation (825B3ED0), then state 4 -> 21
REX_EXTERN(sub_821F4D28);        // registry lookup by name -> record or 0 (no load, no reference)
REX_EXTERN(sub_821F4CD0);        // registry lookup by an asset's data pointer -> record or 0
REX_EXTERN(sub_821F59E8);        // an asset's type index (data) -> index into the 20-byte type table 0x82D2B4E0
REX_EXTERN(sub_821F4148);        // load a package (or any asset) by name: (name, &data out)
REX_EXTERN(sub_821F5E58);        // an asset's file path: (name, out path [0x104], 0)
REX_EXTERN(sub_82225080);        // does this game path open? (path) -> 1 / 0
REX_EXTERN(sub_82413FD8);        // an asset by name with a fallback: (name, fallback) -> record (takes a reference)

// level_memory.cpp: the heaps at this point of a level's life (a log line with pdz_level_memory_log); reset_after
// starts a new "fullest since" period.
void PdzLevelMemoryMark(const uint8_t *base, const char *what, bool reset_after);
namespace pdz::native
{
  void OnGuestMemoryChange(const char *why); // native_textures.cpp (pdz_native_texture_guarded_read)
}
// level_memory.cpp: how many bytes larger than Rare's a heap was made (pdz_level_memory_scale / _sizes); free bytes.
uint32_t PdzLevelMemoryExtra(const uint8_t *base, const char *name);
uint32_t PdzLevelMemoryFree(const uint8_t *base, const char *name);

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;          // -> +0 the game (+8 state), +8 the lists, +0x2C the asset manager
  constexpr uint32_t kStateInLevel = 21;          // [[kGame] +8]: 4 = loading the level, 21 = in it
  constexpr uint32_t kLoadByName = 0x82E2F383;    // byte: Rare's package loaders run only when it is set
  constexpr uint32_t kLoadInLevel = 0x82E2F36B;   // byte: loads allowed in state 21 (UpdateDLCState 8250FF08 sets it)
  constexpr uint32_t kTeamPackages = 0x82D27658;  // 7 x 64-byte names: multi_char_team0..5, then multi_char_noteams
  constexpr uint32_t kTeamBodies = 0x82D27580, kTeamHeads = 0x82D27610; // [3k + i]
  constexpr uint32_t kNoTeamBodies = 0x82D277FC, kNoTeamHeads = 0x82D27814; // [0..5]
  constexpr uint32_t kSharedPackage = 0x82088D48; // "aid_darkpackage_multi_char_shared"
  constexpr uint32_t kNoTeamLook = 6, kNoTeamCount = 6; // look 6 = the no-team package
  // A body list entry ([kGame + 8] +0 -> {+0 entries, +8 count}).
  constexpr uint32_t kBodyId = 0, kBodyModelRef = 4, kBodyAnimName = 12, kBodyHandsRef = 16, kBodyHands = 20,
                     kBodyModel = 0x44;
  // An asset record.
  constexpr uint32_t kRecRefs = 0x1C, kRecFlags = 0x1E, kRecLoaded = 0x4;
  constexpr const char *kHandsKind = "aid_darkcharacter_xenonhands_";
  constexpr const char *kPackagePrefix = "aid_darkpackage_";

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  uint32_t Load16(const uint8_t *base, uint32_t a) { return (uint32_t(base[a]) << 8) | base[a + 1]; }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  std::string GuestString(const uint8_t *base, uint32_t p, size_t max = 127)
  {
    std::string out;
    // Sun 22 Nov night (audit): each byte read only where mapped
    for (size_t i = 0; Ptr(p) && i < max && pdz::guestmem::Ok(base, p + uint32_t(i), 1) && base[p + i]; ++i)
    {
      out += char(base[p + i]);
    }
    return out;
  }
  std::string Lower(std::string s)
  {
    for (char &c : s)
    {
      c = char(std::tolower(static_cast<unsigned char>(c)));
    }
    return s;
  }
  bool Has(const std::string &s, const char *part) { return s.find(part) != std::string::npos; }

  uint32_t Call(PPCContext &ctx, uint8_t *base, PPCFunc *fn, uint32_t r3, uint32_t r4 = 0, uint32_t r5 = 0)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = r3;
    frame.ctx.r4.u64 = r4;
    frame.ctx.r5.u64 = r5;
    fn(frame.ctx, base);
    return frame.ctx.r3.u32;
  }

  // Guest scratch: two names, a path and an out word (game thread only: level load, the player factory).
  constexpr uint32_t kNameSize = 0x100, kPathSize = 0x110;
  uint32_t g_scratch = 0;
  uint32_t Scratch()
  {
    if (!g_scratch)
    {
      g_scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(2 * kNameSize + kPathSize + 16);
    }
    return g_scratch;
  }
  // A host string as a guest string in scratch slot 0 or 1; 0 when too long or no scratch.
  uint32_t GuestName(uint8_t *base, const std::string &s, int slot = 0)
  {
    const uint32_t at = Scratch();
    if (!at || s.size() + 1 > kNameSize)
    {
      return 0;
    }
    const uint32_t p = at + uint32_t(slot) * kNameSize;
    std::memcpy(base + p, s.c_str(), s.size() + 1);
    return p;
  }
  uint32_t OutWord() { return Scratch() ? Scratch() + 2 * kNameSize + kPathSize : 0; }

  uint32_t GameState(const uint8_t *base)
  {
    const uint32_t game = Load32(base, kGame);
    return Ptr(game) ? Load32(base, game + 8) : 0;
  }
  uint32_t Manager(const uint8_t *base) { return Load32(base, kGame + 0x2C); }

  // Registry lookup by guest name (821F4D28 copies the name; nothing is loaded or referenced).
  uint32_t Lookup(PPCContext &ctx, uint8_t *base, uint32_t name)
  {
    return Ptr(name) && base[name] ? Call(ctx, base, sub_821F4D28, name) : 0;
  }
  uint32_t LookupName(PPCContext &ctx, uint8_t *base, const std::string &name, int slot = 0)
  {
    return Lookup(ctx, base, GuestName(base, name, slot));
  }
  std::string RecordText(const uint8_t *base, uint32_t rec)
  {
    if (!Ptr(rec))
    {
      return "not registered";
    }
    const uint32_t flags = Load16(base, rec + kRecFlags);
    return fmt::format("registered {:08X} ({}, flags {:04X}, refs {})", rec,
                       flags & kRecLoaded ? "loaded" : "NOT loaded", flags, Load16(base, rec + kRecRefs));
  }

  // Rare's file path for an asset name and whether it opens (821F5E58 + 82225080: a package is
  // PackageData\<hash>.pkg). Uses scratch slot 1 for the name.
  bool FileExists(PPCContext &ctx, uint8_t *base, const std::string &name, std::string &path)
  {
    const uint32_t gname = GuestName(base, name, 1);
    if (!gname)
    {
      path = "(no scratch)";
      return false;
    }
    const uint32_t out = Scratch() + 2 * kNameSize;
    base[out] = 0;
    Call(ctx, base, sub_821F5E58, gname, out, 0);
    path = GuestString(base, out, kPathSize - 1);
    return path.size() && Call(ctx, base, sub_82225080, out) != 0;
  }

  // The level's body list.
  struct BodyList
  {
    uint32_t entries = 0;
    int32_t count = 0;
  };
  BodyList Bodies(const uint8_t *base)
  {
    BodyList b;
    const uint32_t lists = Load32(base, kGame + 8);
    const uint32_t list = Ptr(lists) ? Load32(base, lists) : 0;
    if (Ptr(list))
    {
      b.entries = Load32(base, list);
      b.count = std::min<int32_t>(int32_t(Load32(base, list + 8)), 512);
    }
    return b;
  }
  uint32_t BodyEntry(const uint8_t *base, uint32_t id)
  {
    const BodyList b = Bodies(base);
    for (int32_t i = 0; Ptr(b.entries) && i < b.count; ++i)
    {
      const uint32_t e = Load32(base, b.entries + uint32_t(i) * 4);
      if (Ptr(e) && Load32(base, e + kBodyId) == id)
      {
        return e;
      }
    }
    return 0;
  }
  bool Female(const uint8_t *base, uint32_t entry)
  {
    return Has(Lower(GuestString(base, Load32(base, entry + kBodyAnimName))), "joanims");
  }
  std::string Sex(const uint8_t *base, uint32_t entry)
  {
    const std::string anim = Lower(GuestString(base, Load32(base, entry + kBodyAnimName)));
    return Has(anim, "joanims") ? "female" : Has(anim, "henchman") ? "male" : "male? ('" + anim + "')";
  }
  // Hands that would put Jo's (or another named woman's / the x-ray) arms on a guard.
  bool NamedHands(const std::string &hands)
  {
    const std::string h = Lower(hands);
    return h.empty() || Has(h, "_noname_") || Has(h, "jo_") || Has(h, "chandra") || Has(h, "maihem") ||
           Has(h, "skeleton") || Has(h, "xray");
  }

  // The MP character packages: look k (0..5 teams, 6 = no teams) -> name and body ids (read live).
  std::string TeamPackage(const uint8_t *base, uint32_t k) { return GuestString(base, kTeamPackages + k * 64, 63); }
  std::vector<uint32_t> TeamBodies(const uint8_t *base, uint32_t k)
  {
    std::vector<uint32_t> ids;
    if (k == kNoTeamLook)
    {
      for (uint32_t i = 0; i < kNoTeamCount; ++i)
      {
        ids.push_back(Load32(base, kNoTeamBodies + i * 4));
      }
    }
    else
    {
      for (uint32_t i = 0; i < 3; ++i)
      {
        ids.push_back(Load32(base, kTeamBodies + (k * 3 + i) * 4));
      }
    }
    return ids;
  }
  uint32_t TeamHead(const uint8_t *base, uint32_t k, uint32_t i)
  {
    return k == kNoTeamLook ? Load32(base, kNoTeamHeads + i * 4) : Load32(base, kTeamHeads + (k * 3 + i) * 4);
  }

  // What the probe has seen: lower-cased asset name -> the package that registered it; the packages this file
  // loaded (for the lifetime check at the next level); this level's load attempts (package -> result).
  std::mutex g_mutex;
  std::map<std::string, std::string> g_registered_by;
  std::set<std::string> g_loaded_by_us;
  std::map<std::string, bool> g_tried_this_level;

  std::string PackageOf(const std::string &asset)
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    const auto it = g_registered_by.find(Lower(asset));
    return it == g_registered_by.end() ? std::string("(not seen loading)") : it->second;
  }

  void Dump(PPCContext &ctx, uint8_t *base, const char *where)
  {
    const uint32_t mgr = Manager(base);
    REXLOG_INFO("pdz_counterop: hands probe: [{}] state {} (21 = in the level), load by name 82E2F383 = {}, in-level "
                "loads 82E2F36B = {}, asset manager {:08X} (+0x18 heap on {}, +0x1C heap {}, +0x20 mode {})",
                where, GameState(base), base[kLoadByName], base[kLoadInLevel], mgr, Ptr(mgr) ? base[mgr + 0x18] : 0,
                Ptr(mgr) ? Load32(base, mgr + 0x1C) : 0, Ptr(mgr) ? Load32(base, mgr + 0x20) : 0);
    const BodyList b = Bodies(base);
    std::set<std::string> hands_names;
    for (int32_t i = 0; Ptr(b.entries) && i < b.count; ++i)
    {
      const uint32_t e = Load32(base, b.entries + uint32_t(i) * 4);
      if (!Ptr(e))
      {
        continue;
      }
      const uint32_t hands_ref = Load32(base, e + kBodyHandsRef);
      const std::string hands = GuestString(base, hands_ref);
      const uint32_t rec = Lookup(ctx, base, hands_ref);
      if (!hands.empty() && !Has(Lower(hands), "_noname_"))
      {
        hands_names.insert(hands);
      }
      REXLOG_INFO("pdz_counterop: hands probe: [{}] body {:3} id {:3} '{}' ({}, anim '{}'): hands '{}' +20 {:08X}, "
                  "model +0x44 {:08X}; hands {}, package {}",
                  where, i, Load32(base, e + kBodyId), GuestString(base, Load32(base, e + kBodyModelRef)), Sex(base, e),
                  GuestString(base, Load32(base, e + kBodyAnimName)), hands, Load32(base, e + kBodyHands),
                  Load32(base, e + kBodyModel), RecordText(base, rec), Ptr(rec) ? PackageOf(hands) : "-");
    }
    REXLOG_INFO("pdz_counterop: hands probe: [{}] body list: {} entries", where, b.count);
    // Rare's MP character packages and their characters.
    for (uint32_t k = 0; k <= kNoTeamLook; ++k)
    {
      const std::string pkg = TeamPackage(base, k);
      std::string path;
      const bool on_disk = FileExists(ctx, base, pkg, path);
      const uint32_t rec = LookupName(ctx, base, pkg);
      std::string chars;
      uint32_t i = 0;
      for (const uint32_t id : TeamBodies(base, k))
      {
        const uint32_t e = BodyEntry(base, id);
        chars += fmt::format("{}body {} head {}: {}", chars.empty() ? "" : "; ", id, TeamHead(base, k, i++),
                             Ptr(e) ? fmt::format("'{}' {} hands '{}'",
                                                  GuestString(base, Load32(base, e + kBodyModelRef)), Sex(base, e),
                                                  GuestString(base, Load32(base, e + kBodyHandsRef)))
                                    : std::string("not in this level's body list"));
      }
      REXLOG_INFO("pdz_counterop: hands probe: [{}] MP package '{}' ({} '{}'): {}; {}", where, pkg,
                  on_disk ? "on disk" : "NOT on disk", path, RecordText(base, rec), chars);
    }
    {
      const std::string shared = GuestString(base, kSharedPackage, 63);
      std::string path;
      const bool on_disk = FileExists(ctx, base, shared, path);
      REXLOG_INFO("pdz_counterop: hands probe: [{}] MP package '{}' ({} '{}'): {}", where, shared,
                  on_disk ? "on disk" : "NOT on disk", path, RecordText(base, LookupName(ctx, base, shared)));
    }
    // A package named after each hands model (the naming level / background packages use; unproven for models).
    for (const std::string &hands : hands_names)
    {
      if (hands.size() <= 4 || hands.compare(0, 4, "aid_") != 0)
      {
        continue;
      }
      const std::string pkg = kPackagePrefix + hands.substr(4);
      std::string path;
      const bool on_disk = FileExists(ctx, base, pkg, path);
      REXLOG_INFO("pdz_counterop: hands probe: [{}] per-model package '{}': {} ('{}')", where, pkg,
                  on_disk ? "ON DISK" : "not on disk", path);
    }
    std::set<std::string> ours;
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      ours = g_loaded_by_us;
    }
    for (const std::string &pkg : ours)
    {
      REXLOG_INFO("pdz_counterop: hands probe: [{}] lifetime: '{}' (loaded by pdz_counterop_load_hands in a level): "
                  "{}",
                  where, pkg, RecordText(base, LookupName(ctx, base, pkg)));
    }
  }

  // ---- The loader ------------------------------------------------------------------------------------------------

  struct Source
  {
    std::string package, hands, why;
  };

  std::vector<std::string> SplitList(const std::string &list)
  {
    std::vector<std::string> out;
    std::string item;
    for (const char ch : list + ",")
    {
      if (ch == ',')
      {
        while (!item.empty() && item.back() == ' ')
        {
          item.pop_back();
        }
        if (!item.empty())
        {
          out.push_back(item);
        }
        item.clear();
      }
      else if (ch != ' ' || !item.empty())
      {
        item += ch;
      }
    }
    return out;
  }

  std::vector<Source> Sources(const uint8_t *base, bool female, std::string &notes)
  {
    const std::string list = female ? std::string(REXCVAR_GET(pdz_counterop_hands_source_female))
                                    : std::string(REXCVAR_GET(pdz_counterop_hands_source_male));
    std::vector<Source> out;
    if (Lower(list) != "auto")
    {
      for (const std::string &pair : SplitList(list))
      {
        const size_t bar = pair.find_first_of("|:"); // 'package|hands' or 'package:hands'
        if (bar == std::string::npos)
        {
          notes += fmt::format("'{}' is not 'package|hands'; ", pair);
          continue;
        }
        Source s;
        s.package = pair.substr(0, bar);
        s.hands = pair.substr(bar + 1);
        if (s.package.compare(0, 4, "aid_") != 0)
        {
          s.package = kPackagePrefix + s.package;
        }
        if (s.hands.compare(0, 4, "aid_") != 0)
        {
          s.hands = kHandsKind + s.hands;
        }
        s.why = "from the cvar";
        out.push_back(s);
      }
      return out;
    }
    // auto: Rare's MP character packages, characters of this sex with their own (not a named woman's) hands.
    const std::vector<std::string> prefer = SplitList(Lower(std::string(REXCVAR_GET(pdz_counterop_hands_prefer))));
    std::vector<std::pair<size_t, Source>> ranked;
    uint32_t missing = 0;
    for (uint32_t k = 0; k <= kNoTeamLook; ++k)
    {
      for (const uint32_t id : TeamBodies(base, k))
      {
        const uint32_t e = BodyEntry(base, id);
        if (!Ptr(e))
        {
          ++missing;
          continue;
        }
        const std::string hands = GuestString(base, Load32(base, e + kBodyHandsRef));
        if (Female(base, e) != female || NamedHands(hands))
        {
          continue;
        }
        size_t rank = prefer.size();
        for (size_t p = 0; p < prefer.size(); ++p)
        {
          if (Has(Lower(hands), prefer[p].c_str()))
          {
            rank = p;
            break;
          }
        }
        Source s{TeamPackage(base, k), hands,
                 fmt::format("MP body {} '{}'", id, GuestString(base, Load32(base, e + kBodyModelRef)))};
        // Team packages (three characters) before the no-team one (six): smaller.
        ranked.push_back({rank * 10 + (k == kNoTeamLook ? 1 : 0), s});
      }
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    std::set<std::string> seen;
    for (const auto &r : ranked)
    {
      if (seen.insert(r.second.package + "|" + Lower(r.second.hands)).second)
      {
        out.push_back(r.second);
      }
    }
    if (missing)
    {
      notes += fmt::format("{} of Rare's MP character bodies are not in this level's body list (their hands names "
                           "can't be read here: fill pdz_counterop_hands_source_{} from an MP probe); ",
                           missing, female ? "female" : "male");
    }
    return out;
  }

  // Rare's package load by name, the way DarkGunLoad (824F1C38) does it: heap and mode as they are. In the level
  // (state 21) Rare's own loader refuses; 82E2F36B is lifted for the call as UpdateDLCState does, and restored.
  bool LoadPackage(PPCContext &ctx, uint8_t *base, const std::string &pkg, std::string &how)
  {
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      const auto it = g_tried_this_level.find(pkg);
      if (it != g_tried_this_level.end())
      {
        how = it->second ? "already loaded for this level" : "already failed in this level";
        return it->second;
      }
    }
    std::string path;
    if (!FileExists(ctx, base, pkg, path))
    {
      how = fmt::format("not in the game data ('{}' doesn't open)", path);
      std::lock_guard<std::mutex> lock(g_mutex);
      g_tried_this_level[pkg] = false;
      return false;
    }
    const uint32_t gname = GuestName(base, pkg, 0), out = OutWord();
    if (!gname || !out)
    {
      how = "no guest scratch memory";
      return false;
    }
    const uint32_t state = GameState(base);
    const uint8_t in_level_before = base[kLoadInLevel];
    const bool lift = state == kStateInLevel && !in_level_before;
    if (lift)
    {
      base[kLoadInLevel] = 1;
    }
    const uint32_t before = Lookup(ctx, base, gname);
    std::memset(base + out, 0, 4);
    const auto t0 = std::chrono::steady_clock::now();
    Call(ctx, base, sub_821F4148, gname, out);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (lift)
    {
      base[kLoadInLevel] = in_level_before;
    }
    const uint32_t data = Load32(base, out);
    const uint32_t rec = Lookup(ctx, base, gname);
    const uint32_t mgr = Manager(base);
    how = fmt::format("'{}' ({}): {} in {:.1f} ms; data {:08X}; {}; state {}{}, heap {} / mode {} (memory used is "
                      "not visible from the record)",
                      pkg, path, Ptr(before) ? "was already registered" : "loaded by Rare's 821F4148", ms, data,
                      RecordText(base, rec), state, lift ? " (82E2F36B lifted for the call)" : "",
                      Ptr(mgr) ? Load32(base, mgr + 0x1C) : 0, Ptr(mgr) ? Load32(base, mgr + 0x20) : 0);
    const bool ok = Ptr(data);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_tried_this_level[pkg] = ok;
    if (ok && !Ptr(before))
    {
      g_loaded_by_us.insert(pkg);
    }
    return ok;
  }
  // A level of pdz_counterop_load_hands_skip gets its hands packages only with a larger LevelGPUWC heap.
  bool RoomForHands(const uint8_t *base)
  {
    return PdzLevelMemoryExtra(base, "LevelGPUWC") >= uint32_t(REXCVAR_GET(pdz_counterop_hands_extra_mb)) * 1048576u;
  }
} // namespace

// ==== Hooks ==========================================================================================================

// sub_821F7DB8(package name, package data): Rare's package handler (827020C0 dispatches it by kind). Rare's first;
// then, with the probe on, what this package registered.
REX_HOOK_RAW(sub_821F7DB8)
{
  const uint32_t name = ctx.r3.u32, data = ctx.r4.u32;
  __imp__sub_821F7DB8(ctx, base);
  if (!REXCVAR_GET(pdz_counterop_hands_probe) || !Ptr(name) || !Ptr(data))
  {
    return;
  }
  const std::string pkg = GuestString(base, name);
  const int32_t count = std::min<int32_t>(int32_t(Load32(base, data + 0x20)), 100000);
  std::string hands, bodies, heads;
  int32_t skipped = 0;
  for (int32_t i = 0; i < count; ++i)
  {
    const uint32_t rec = data + 0x24 + uint32_t(i) * 0x20;
    const std::string asset = Lower(GuestString(base, Load32(base, rec)));
    const bool h = Has(asset, "xenonhands_"), b = Has(asset, "xenonbodies_"), d = Has(asset, "xenonheads_");
    if (!h && !b && !d)
    {
      continue;
    }
    // The record itself is what Rare registers; another record under the name = registered earlier, skipped.
    const uint32_t now = Lookup(ctx, base, Load32(base, rec));
    const bool ours = now == rec;
    skipped += !ours;
    const std::string item = asset.substr(asset.find(h ? "xenonhands_" : b ? "xenonbodies_" : "xenonheads_")) +
                             (ours ? "" : " (already registered)");
    std::string &list = h ? hands : b ? bodies : heads;
    list += (list.empty() ? "" : ", ") + item;
    if (ours)
    {
      std::lock_guard<std::mutex> lock(g_mutex);
      g_registered_by[asset] = pkg;
    }
  }
  REXLOG_INFO("pdz_counterop: hands probe: package '{}' registers {} assets (state {}): hands [{}], bodies [{}], "
              "heads [{}]{}",
              pkg, count, GameState(base), hands, bodies, heads,
              skipped ? fmt::format(" ({} character names were already registered by another package)", skipped)
                      : std::string());
}

// sub_822795D0: the end of a level load (players created, then state 4 -> 21). Rare's first, then the probe's dump;
// this level's load attempts are forgotten (the next level starts clean).
REX_HOOK_RAW(sub_822795D0)
{
  __imp__sub_822795D0(ctx, base);
  PdzLevelMemoryMark(base, "level ready (822795D0 ended): the heaps after the load; fingerprint = the load's requests", true);
  if (REXCVAR_GET(pdz_counterop_hands_probe))
  {
    PPCContext saved = ctx;
    Dump(saved, base, "level loaded");
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_tried_this_level.clear();
}

// Round 6 (item 1): the level load job (pushed by the level loop 8222D4A8, run on Rare's load thread). Logging: what the asset registry still holds from the game before (the packages this file loaded) when
// the next level's load starts, and whether the job ended with the level object built.
REX_EXTERN(__imp__sub_822788D8);
REX_HOOK_RAW(sub_822788D8)
{
  std::set<std::string> ours;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ours = g_loaded_by_us;
  }
  std::string held;
  {
    PPCContext saved = ctx;
    for (const std::string &pkg : ours)
    {
      const uint32_t rec = LookupName(saved, base, pkg, 0);
      held += fmt::format("{}'{}': {}", held.empty() ? "" : "; ", pkg, Ptr(rec) ? RecordText(base, rec) : "not registered");
    }
  }
  const uint32_t mgr = Manager(base);
  REXLOG_INFO("pdz_counterop: level load job starts: state {}, load-by-name {}, load-in-level {}, heap {} / mode {}; "
              "packages loaded by Counter-Op earlier in this session: {}",
              GameState(base), base[kLoadByName], base[kLoadInLevel], Ptr(mgr) ? Load32(base, mgr + 0x1C) : 0,
              Ptr(mgr) ? Load32(base, mgr + 0x20) : 0, held.empty() ? "none" : held);
  PdzLevelMemoryMark(base, "level load job starts (822788D8)", true);
  pdz::native::OnGuestMemoryChange("level load job starts");
  __imp__sub_822788D8(ctx, base);
  PdzLevelMemoryMark(base, "level load job ended (822788D8)", false);
  const uint32_t owner = Load32(base, kGame + 0x1C);
  REXLOG_INFO("pdz_counterop: level load job ended: state {}, level object {:08X}", GameState(base),
              pdz::guestmem::Ok(base, owner, 0x2D8 + 4) ? Load32(base, owner + 0x2D8) : 0);
}
// Round 6 (item 1): THE CAUSE of the crash / disc-error screen in a second Counter-Op game of one session. A package
// Rare loads for a level is released by Rare when the level is unloaded; the multiplayer character packages this file
// loads for player 2's hands (LoadPackage) had no such release, so after the level they stayed in the asset registry
// ("registered, loaded, refs 1") while the level heap they lived in was freed. The next level then found them
// "already registered" and read freed memory: Subway -> Rooftops ended in Rare's disc-error screen or in a level load
// that gave up (no player 2: the crash of the hand test).
// Rare's level unload (82278xxx) ends with 821F4B10, the registry sweep that unloads every record nobody holds, right
// before the level heap goes. Just before that sweep our packages are released the way Rare releases its own
// (as 821F4A40 does: one reference less, unloaded through 821F5180 at 0, which also takes its assets out of the
// registry).
REX_EXTERN(__imp__sub_821F4B10);
REX_EXTERN(sub_821F5180); // Rare's unload of one registry record (what its release 821F4A40 calls at 0 references)
REXCVAR_DEFINE_BOOL(pdz_counterop_release_hands, true, "Gameplay",
                    "Counter-Operative (default on, round 6): the multiplayer character packages loaded for player "
                    "2's hands are released when the level is unloaded, as Rare releases a level's own packages. Off "
                    "= rounds 2-5: they stayed registered after their memory was freed (a second Counter-Op game in "
                    "one session could end in the disc-error screen or a crash)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_HOOK_RAW(sub_821F4B10)
{
  std::set<std::string> ours;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    ours = g_loaded_by_us;
  }
  if (!ours.empty() && REXCVAR_GET(pdz_counterop_release_hands))
  {
    PPCContext saved = ctx;
    std::string text;
    for (const std::string &pkg : ours)
    {
      const uint32_t before = LookupName(saved, base, pkg, 0);
      const uint32_t refs = Ptr(before) ? Load16(base, before + kRecRefs) : 0;
      uint32_t released = 0;
      for (uint32_t i = 0; i < refs && i < 8; ++i)
      {
        // Rare's release 821F4A40 finds the record by its asset id (821F4CD0 -> 821F5738, a search of ids; a name or
        // a name pointer finds nothing and it then reads a null record: two crashes of this round's own tests). Its
        // body is: references - 1, and at 0 Rare's unload 821F5180(record). The same here, on the record itself.
        const uint32_t rec = LookupName(saved, base, pkg, 0);
        if (!Ptr(rec))
        {
          break;
        }
        const uint32_t left = (Load16(base, rec + kRecRefs) - 1u) & 0xFFFFu;
        base[rec + kRecRefs] = uint8_t(left >> 8);
        base[rec + kRecRefs + 1] = uint8_t(left);
        ++released;
        if (left == 0)
        {
          Call(saved, base, sub_821F5180, rec);
          break;
        }
      }
      const uint32_t after = LookupName(saved, base, pkg, 0);
      text += fmt::format("{}'{}': {} -> {} reference(s) released -> {}", text.empty() ? "" : "; ", pkg,
                          Ptr(before) ? RecordText(base, before) : "not registered", released,
                          Ptr(after) ? RecordText(base, after) : "out of the registry");
    }
    REXLOG_INFO("pdz_counterop: hands: level unload: the packages loaded for player 2's hands are released before "
                "Rare's registry sweep (821F4B10): {}",
                text);
    std::lock_guard<std::mutex> lock(g_mutex);
    g_loaded_by_us.clear();
    g_tried_this_level.clear();
  }
  __imp__sub_821F4B10(ctx, base);
}

// Round 8 (item 2): THE CAUSE of the crash when a mission was quit (and it would have come at a normal end too).
// Rare's level unload 82278088 walks the level's body list and 82414260(entry) releases one reference for every
// asset field of each entry, the player hands at +0x14 among them (821F5020(data): the record by 821F4CD0, which is
// 0 once the record is out of the registry, then record +0x1E read unchecked: the read of 0x1E). Counter-Op wrote a
// hands record into the +0x14 of guard bodies that have no player hands of their own; where the hands table found
// nothing in the level (Nightclub: no hands package) the SAME record was copied from one body's entry to the next
// at each takeover without a reference of its own. Three entries held one record with one reference: Rare's unload
// released it three times and the third release found no record.
// Now every hands pointer Counter-Op writes into an entry is a LOAN: it takes one reference of its own, and all
// loans are taken back (Rare's own value restored, the reference dropped) before Rare's unload walks the list.
REXCVAR_DEFINE_BOOL(pdz_counterop_hands_loans, true, "Gameplay",
                    "Counter-Operative (default on, round 8): the player hands Counter-Op writes into a guard body's "
                    "list entry are taken back before Rare unloads the level. Off = rounds 2-7: Rare's unload "
                    "released them once per entry (a crash when player 2 had worn two bodies without hands of their "
                    "own, as on Nightclub)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  struct Loan
  {
    uint32_t entry = 0, old = 0, record = 0; // the body entry, Rare's own +0x14 value, what we wrote
  };
  std::mutex g_loan_mutex;
  std::vector<Loan> g_loans;

  void StoreBe32(uint8_t *base, uint32_t a, uint32_t v)
  {
    base[a] = uint8_t(v >> 24), base[a + 1] = uint8_t(v >> 16), base[a + 2] = uint8_t(v >> 8), base[a + 3] = uint8_t(v);
  }
  // The registry record of an asset's data (Rare's 821F4CD0), 0 when it is not registered.
  uint32_t RecordOf(PPCContext &ctx, uint8_t *base, uint32_t data)
  {
    // As Rare's release 821F5020: the type index by 821F59E8(data); for the types whose table byte (0x82D2B4F1 + 20 x
    // index) is set the registry key is data + 32.
    if (!Ptr(data))
    {
      return 0;
    }
    const uint32_t type = Call(ctx, base, sub_821F59E8, data);
    const uint32_t key = type < 64 && base[0x82D2B4F1u + type * 20] ? data + 32 : data;
    return Call(ctx, base, sub_821F4CD0, key);
  }
  // One reference more / less on the record itself (Rare's release 821F5020 would unload the record at once when
  // only the level list's reference is left).
  void Refs(uint8_t *base, uint32_t rec, int delta)
  {
    if (!Ptr(rec))
    {
      return;
    }
    const uint32_t now = Load16(base, rec + kRecRefs);
    if (delta < 0 && now == 0)
    {
      REXLOG_WARN("pdz_counterop: hands: record {:08X} has no reference to give back", rec);
      return;
    }
    const uint32_t next = (now + uint32_t(delta)) & 0xFFFFu;
    base[rec + kRecRefs] = uint8_t(next >> 8);
    base[rec + kRecRefs + 1] = uint8_t(next);
  }
} // namespace

// The ONLY way Counter-Op writes a body entry's +0x14 (counter_op.cpp's look at load, counter_op_dress.cpp's re-dress).
void PdzCounterOpHandsLend(PPCContext &ctx, uint8_t *base, uint32_t entry, uint32_t hands)
{
  if (!Ptr(entry) || !Ptr(hands))
  {
    return;
  }
  const uint32_t now = Load32(base, entry + kBodyHands);
  if (now == hands)
  {
    return;
  }
  if (!REXCVAR_GET(pdz_counterop_hands_loans))
  {
    StoreBe32(base, entry + kBodyHands, hands); // as rounds 2-7
    return;
  }
  PPCContext saved = ctx;
  const uint32_t record = RecordOf(saved, base, hands);
  std::lock_guard<std::mutex> lock(g_loan_mutex);
  Loan *loan = nullptr;
  for (Loan &l : g_loans)
  {
    if (l.entry == entry)
    {
      loan = &l;
    }
  }
  if (loan)
  {
    Refs(base, loan->record, -1); // our earlier loan to this entry goes back
    loan->record = record;        // old stays Rare's value
  }
  else
  {
    g_loans.push_back({entry, now, record});
  }
  Refs(base, record, +1);
  StoreBe32(base, entry + kBodyHands, hands);
  REXLOG_INFO("pdz_counterop: hands: lent to body {}'s list entry {:08X}: hands {:08X} (record {:08X}, now {} "
              "reference(s)); the entry's own value {:08X} is kept for the level's unload; {} loan(s) in this level",
              Load32(base, entry + kBodyId), entry, hands, record, Ptr(record) ? Load16(base, record + kRecRefs) : 0,
              loan ? loan->old : now, g_loans.size());
}

// Before Rare's unload walks the body list (touch = true), and when a level starts (touch = false: the old level's
// entries are gone).
void PdzCounterOpHandsTakeBack(uint8_t *base, bool touch, const char *why)
{
  std::vector<Loan> loans;
  {
    std::lock_guard<std::mutex> lock(g_loan_mutex);
    loans.swap(g_loans);
  }
  if (loans.empty())
  {
    return;
  }
  std::string text;
  uint32_t back = 0;
  for (const Loan &l : loans)
  {
    if (!touch)
    {
      continue;
    }
    StoreBe32(base, l.entry + kBodyHands, l.old);
    Refs(base, l.record, -1);
    ++back;
    text += fmt::format("{}body {} (entry {:08X}): record {:08X} -> Rare's own {:08X}", text.empty() ? "" : "; ",
                        Load32(base, l.entry + kBodyId), l.entry, l.record, l.old);
  }
  REXLOG_INFO("pdz_counterop: hands: {}: {} loan(s) of player hands, {} taken back{}{}", why, loans.size(), back,
              touch ? ": " : " (not touched: that level is gone)", text);
}

// Rare's level unload (the load thread's job from game state 0xD): our loans go back first.
REX_EXTERN(__imp__sub_82278088);
REX_HOOK_RAW(sub_82278088)
{
  PdzCounterOpHandsTakeBack(base, true, "level unload (82278088)");
  PdzLevelMemoryMark(base, "level unload starts (82278088): fullest = in play", true);
  pdz::native::OnGuestMemoryChange("level unload starts");
  __imp__sub_82278088(ctx, base);
  PdzLevelMemoryMark(base, "level unload ended (82278088)", true);
}

// ==== Call-in points =================================================================================================

// counter_op.cpp, the sub_825B3D58 hook before ChooseLook: the same dump, at player 2's factory call.
void PdzCounterOpHandsProbe(PPCContext &ctx, uint8_t *base, const char *where)
{
  if (REXCVAR_GET(pdz_counterop_hands_probe))
  {
    Dump(ctx, base, where);
  }
}

// counter_op.cpp, BodyUsable: the guard body has no hands of its own and none of the hands lists is resident. Loads a
// package with hands of the body's sex through Rare's loader and returns those hands (Rare's 82413FD8, which takes
// the reference the loader would). 0 = none (why says exactly why; the caller logs its fallback).
uint32_t PdzCounterOpLoadHands(PPCContext &ctx, uint8_t *base, uint32_t body_entry, std::string &why)
{
  if (!REXCVAR_GET(pdz_counterop_load_hands))
  {
    why = "pdz_counterop_load_hands is off";
    return 0;
  }
  if (!Ptr(body_entry))
  {
    why = "no body entry";
    return 0;
  }
  if (!base[kLoadByName])
  {
    why = "this build doesn't load packages by name (82E2F383 = 0)";
    REXLOG_WARN("pdz_counterop: hands load: {}", why);
    return 0;
  }
  {
    std::string raw;
    const std::string mission = pdz::pace::SpLevelName(base, &raw);
    const std::string skip = "," + Lower(std::string(REXCVAR_GET(pdz_counterop_load_hands_skip))) + ",";
    if (!mission.empty() && skip.find("," + Lower(mission) + ",") != std::string::npos && !RoomForHands(base))
    {
      why = fmt::format("not loaded on {} (pdz_counterop_load_hands_skip: no room for an MP character package at "
                        "Rare's LevelGPUWC size; pdz_level_memory_scale gives it)",
                        mission);
      REXLOG_WARN("pdz_counterop: hands load: {}; he keeps the co-op body's arms here", why);
      return 0;
    }
  }
  const bool female = Female(base, body_entry);
  std::string notes;
  const std::vector<Source> sources = Sources(base, female, notes);
  std::string tried;
  for (const Source &s : sources)
  {
    // Already resident (an earlier body's load, or another package): no load at all.
    uint32_t rec = LookupName(ctx, base, s.hands, 1);
    std::string how = "already resident";
    if (!Ptr(rec) || !(Load16(base, rec + kRecFlags) & kRecLoaded))
    {
      if (!LoadPackage(ctx, base, s.package, how))
      {
        REXLOG_INFO("pdz_counterop: hands load: {} hands '{}' ({}): package {}", female ? "female" : "male", s.hands,
                    s.why, how);
        tried += fmt::format("{}'{}' from '{}': {}", tried.empty() ? "" : "; ", s.hands, s.package, how);
        continue;
      }
      rec = LookupName(ctx, base, s.hands, 1);
    }
    if (!Ptr(rec) || !(Load16(base, rec + kRecFlags) & kRecLoaded))
    {
      REXLOG_INFO("pdz_counterop: hands load: {} hands '{}' ({}): package {}; but the hands are {}",
                  female ? "female" : "male", s.hands, s.why, how, RecordText(base, rec));
      tried += fmt::format("{}'{}' not in '{}'", tried.empty() ? "" : "; ", s.hands, s.package);
      continue;
    }
    const uint32_t asset = Call(ctx, base, sub_82413FD8, GuestName(base, s.hands, 1), 0);
    why = fmt::format("a {} body (animation '{}'): '{}' ({:08X}) from {} ({}: {})", female ? "female" : "male",
                      GuestString(base, Load32(base, body_entry + kBodyAnimName)), s.hands, asset, s.package, s.why,
                      how);
    REXLOG_INFO("pdz_counterop: hands load: {}", why);
    return asset;
  }
  why = fmt::format("a {} body: no hands could be loaded ({}{}{})", female ? "female" : "male", notes,
                    sources.empty() ? "no source for this sex" : "tried: ", tried);
  REXLOG_WARN("pdz_counterop: hands load: {}", why);
  return 0;
}

// ---- Round 4 (item 3c): first-person hands per guard body ------------------------------------------------------
// Rare made 22 first-person hands models; its multiplayer body list pairs most guard bodies with one. The table
// names, per guard body id, the hands to use in order of preference (the first one resident in the level wins); the
// packages list names the MP character packages loaded at a mission's load so those hands are resident.
REXCVAR_DEFINE_STRING(pdz_counterop_hands_table,
                      "48:gangster_hands|naked_hands_male,50:naked_hands_male,51:naked_hands_male,"
                      "62:naked_hands_male,77:naked_hands_male,18:naked_hands_male,"
                      "75:biker_hands|naked_hands_male,76:biker_hands|naked_hands_male,"
                      "52:privateforce_a_hands,53:privateforce_b_hands|privateforce_a_hands,"
                      "54:Vtroop_hands|jo_adventure_hands,"
                      "55:trooper_hands|CI_swat_hands,56:trooper_hands|CI_swat_hands,57:trooper_hands|CI_swat_hands,"
                      "84:trooper_hands|CI_swat_hands,67:trooper_hands|CI_swat_hands,69:trooper_hands,70:trooper_hands,"
                      "66:CI_trooper_hands,64:CI_swat_hands,"
                      "default:trooper_hands|CI_swat_hands|naked_hands_male|CI_trooper_hands",
                      "Gameplay",
                      "Counter-Operative: player 2's first-person hands per guard body: 'body id:hands|hands' entries, "
                      "comma separated (short names of aid_darkcharacter_xenonhands_*), the first hands resident in the "
                      "level win; 'default' is for a body not listed. Used at every spawn (pdz_counterop_dress)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_hands_packages,
                      "nightclub:multi_char_team5+multi_char_team4,"
                      "subway:multi_char_team5+multi_char_team4,rooftops:multi_char_team5,datacore:multi_char_team5,"
                      "trinity1:multi_char_team5,mansion:multi_char_team1,laboratory:multi_char_team1,"
                      "river:multi_char_team1",
                      "Gameplay",
                      "Counter-Operative: the multiplayer character packages loaded with a mission for player 2's "
                      "hands: 'mission:package+package' entries, comma separated (short names of aid_darkpackage_*). "
                      "Missions in pdz_counterop_load_hands_skip load none")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Tue 24 Nov (1.0.2): Bridge also loads multi_char_team3 (the security girl, body 54, is on Bridge too: four takeovers
// there got the male trooper_hands), and Jungle and Temple also load multi_char_team1 (their body 52 guards got
// trooper_hands; their own privateforce_a_hands are in that package, as on Mansion, the Laboratory and River).
// Mon 23 Nov night (1.0.1): River and the Laboratory also load multi_char_team3 by default: it holds the security
// girl's own first-person hands (Vtroop_hands, the hands table's first choice for body 54); without it she had Jo's
// (jo_adventure_hands). Run b10_river: 's1g1' (body 54): "hands: 'Vtroop_hands' (body 54 is listed)".
// Sun 22 Nov (round 17, item 8): on by default for the four trooper levels (log 043: Jungle's troopers had the
// Carrington swat hands, "not in this level: trooper_hands"). Rare's urban trooper package (multi_char_team5) holds
// trooper_hands; pdz_level_memory_scale 1.25 makes LevelGPUWC 54.6 MB larger than Rare's, more than the 40 MB asked.
REXCVAR_DEFINE_STRING(pdz_counterop_hands_packages_more,
                      "jungle:multi_char_team5+multi_char_team1,temple:multi_char_team5+multi_char_team1,"
                      "outpost:multi_char_team5,bridge:multi_char_team5+multi_char_team3,"
                      "river:multi_char_team3,laboratory:multi_char_team3",
                      "Gameplay",
                      "Counter-Operative (Sun 22 Nov: on for the trooper levels): more hands packages per mission "
                      "(same form as pdz_counterop_hands_packages). Default: the urban trooper package on Jungle, "
                      "Temple, Outpost and Bridge, so their troopers get trooper hands, not the Carrington swat hands "
                      "(empty = as before). Also possible: river:multi_char_team3,laboratory:multi_char_team3 (the "
                      "security girl's own hands; she has Jo's); "
                      "loaded only when the LevelGPUWC heap was made at least pdz_counterop_hands_extra_mb larger than "
                      "Rare's (pdz_level_memory_scale): the troopers' own hands on Jungle / Temple / Outpost / Bridge "
                      "(they got the CI swat hands) and the security girl's on River / Laboratory (she got Jo's)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// At the mission's load (counter_op.cpp ChooseLook, in Rare's player factory, where round 2's hands load ran): the
// mission's packages of pdz_counterop_hands_packages through Rare's loader.
void PdzCounterOpHandsPreload(PPCContext &ctx, uint8_t *base)
{
  if (!REXCVAR_GET(pdz_counterop_load_hands) || !base[kLoadByName])
  {
    return;
  }
  std::string raw;
  const std::string mission = Lower(pdz::pace::SpLevelName(base, &raw));
  const std::string skip = "," + Lower(std::string(REXCVAR_GET(pdz_counterop_load_hands_skip))) + ",";
  if (mission.empty())
  {
    return;
  }
  if (skip.find("," + mission + ",") != std::string::npos)
  {
    if (!RoomForHands(base))
    {
      REXLOG_WARN("pdz_counterop: hands: {}: no hands package is loaded: LevelGPUWC is only {:.1f} MB larger than "
                  "Rare's (needs {} MB: pdz_level_memory_scale); he keeps the co-op body's arms here",
                  mission, double(PdzLevelMemoryExtra(base, "LevelGPUWC")) / 1048576.0,
                  uint32_t(REXCVAR_GET(pdz_counterop_hands_extra_mb)));
      return;
    }
    REXLOG_INFO("pdz_counterop: hands: {}: LevelGPUWC was made {:.1f} MB larger than Rare's ({:.1f} MB free now): this "
                "level's hands packages are loaded",
                mission, double(PdzLevelMemoryExtra(base, "LevelGPUWC")) / 1048576.0,
                double(PdzLevelMemoryFree(base, "LevelGPUWC")) / 1048576.0);
  }
  std::vector<std::string> entries = SplitList(std::string(REXCVAR_GET(pdz_counterop_hands_packages)));
  if (RoomForHands(base))
  {
    for (std::string &more : SplitList(std::string(REXCVAR_GET(pdz_counterop_hands_packages_more))))
    {
      entries.push_back(std::move(more));
    }
  }
  std::vector<std::string> done;
  for (const std::string &entry : entries)
  {
    const size_t colon = entry.find(':');
    if (colon == std::string::npos || Lower(entry.substr(0, colon)) != mission)
    {
      continue;
    }
    std::string rest = entry.substr(colon + 1) + "+";
    size_t from = 0;
    for (size_t plus = rest.find('+'); plus != std::string::npos; from = plus + 1, plus = rest.find('+', from))
    {
      std::string pkg = rest.substr(from, plus - from);
      if (pkg.empty())
      {
        continue;
      }
      if (pkg.rfind(kPackagePrefix, 0) != 0)
      {
        pkg = std::string(kPackagePrefix) + pkg;
      }
      if (std::find(done.begin(), done.end(), pkg) != done.end())
      {
        continue;
      }
      done.push_back(pkg);
      std::string how;
      const bool ok = LoadPackage(ctx, base, pkg, how);
      REXLOG_INFO("pdz_counterop: hands: {}: package '{}' for the guards' hands: {} ({})", mission, pkg,
                  ok ? "loaded" : "NOT loaded", how);
    }
  }
}

// Round 5 (item 6): a level of pdz_counterop_hands_late (Nightclub: a load DURING its level load broke the level) gets
// its hands package once the level runs, on the first Counter-Op tick (before his first guard), through the same
// loader. token = something that changes with the level (counter_op.cpp's level state object).
REXCVAR_DEFINE_STRING(pdz_counterop_hands_late, "", "Gameplay",
                      "Counter-Operative: '<level>:<package>[+<package>]' entries (comma separated) loaded once the "
                      "level RUNS instead of during its load, for levels in pdz_counterop_load_hands_skip (try "
                      "'nightclub:multi_char_team5'). Empty = off")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
void PdzCounterOpHandsLate(PPCContext &ctx, uint8_t *base, uint32_t token)
{
  static uint32_t done_for = 0;
  if (!token || token == done_for)
  {
    return;
  }
  done_for = token;
  const std::string late = std::string(REXCVAR_GET(pdz_counterop_hands_late));
  if (late.empty() || !REXCVAR_GET(pdz_counterop_load_hands))
  {
    return;
  }
  std::string raw;
  const std::string mission = Lower(pdz::pace::SpLevelName(base, &raw));
  for (const std::string &entry : SplitList(late))
  {
    const size_t colon = entry.find(':');
    if (colon == std::string::npos || Lower(entry.substr(0, colon)) != mission)
    {
      continue;
    }
    std::string rest = entry.substr(colon + 1) + "+";
    size_t from = 0;
    for (size_t plus = rest.find('+'); plus != std::string::npos; from = plus + 1, plus = rest.find('+', from))
    {
      std::string pkg = rest.substr(from, plus - from);
      if (pkg.empty())
      {
        continue;
      }
      if (pkg.rfind(kPackagePrefix, 0) != 0)
      {
        pkg = std::string(kPackagePrefix) + pkg;
      }
      std::string how;
      const bool ok = LoadPackage(ctx, base, pkg, how);
      REXLOG_INFO("pdz_counterop: hands: {}: LATE load (the level runs) of package '{}' for the guards' hands: {} ({})",
                  mission, pkg, ok ? "loaded" : "NOT loaded", how);
    }
  }
}

// The hands for a guard body by pdz_counterop_hands_table: the first of its hands that is resident (registered and
// loaded) in this level, as an asset record (Rare's 82413FD8); 0 = none of them is (why names what was tried).
uint32_t PdzCounterOpHandsFor(PPCContext &ctx, uint8_t *base, uint32_t body_id, std::string &why)
{
  std::string names, fallback;
  const std::string key = fmt::format("{}", body_id);
  for (const std::string &entry : SplitList(std::string(REXCVAR_GET(pdz_counterop_hands_table))))
  {
    const size_t colon = entry.find(':');
    if (colon == std::string::npos)
    {
      continue;
    }
    const std::string k = Lower(entry.substr(0, colon));
    if (k == key)
    {
      names = entry.substr(colon + 1);
    }
    else if (k == "default")
    {
      fallback = entry.substr(colon + 1);
    }
  }
  const bool listed = !names.empty();
  std::string list = names + (names.empty() || fallback.empty() ? "" : "|") + fallback + "|";
  std::string tried;
  size_t from = 0;
  for (size_t bar = list.find('|'); bar != std::string::npos; from = bar + 1, bar = list.find('|', from))
  {
    const std::string name = list.substr(from, bar - from);
    if (name.empty())
    {
      continue;
    }
    const std::string full = name.rfind(kHandsKind, 0) == 0 ? name : std::string(kHandsKind) + name;
    const uint32_t rec = LookupName(ctx, base, full, 1);
    if (Ptr(rec) && (Load16(base, rec + kRecFlags) & kRecLoaded))
    {
      const uint32_t asset = Call(ctx, base, sub_82413FD8, GuestName(base, full, 1), 0);
      if (Ptr(asset))
      {
        why = fmt::format("'{}' ({:08X}; body {} {}{})", name, asset, body_id, listed ? "is listed" : "is not listed: the default",
                          tried.empty() ? "" : "; not in this level: " + tried);
        return asset;
      }
    }
    tried += (tried.empty() ? "" : ", ") + name;
  }
  why = fmt::format("none of the hands for body {} is in this level ({})", body_id, tried.empty() ? "empty table" : tried);
  return 0;
}
