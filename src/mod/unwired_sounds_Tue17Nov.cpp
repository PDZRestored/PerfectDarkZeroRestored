// pdz_fix_unwired_sounds (sound audit, Tue 17 Nov 2026; a game fix, on by default, needs pdz_game_fixes). TU3 addresses.
//
// Sounds Rare recorded for one prop and one moment, where the prop's template asks for a generic sound at that same
// moment instead. Only the name that is looked up changes: who plays it, when, where and how loud stay Rare's own
// call, so nothing about loudness, timing or the AI changes.
//
// 1. Security camera, bullet hit. The template electrical\camera has two sound names, GUNS_BULLETHIT_METAL and
//    STOCK_CRATE_HIT_METAL. The guns bank has GUNS_BULLETHIT_CAMERA (five one-shot takes, as GUNS_BULLETHIT_METAL
//    has) and nothing in the game or its data names it. When the camera template's own copy of
//    GUNS_BULLETHIT_METAL is looked up, GUNS_BULLETHIT_CAMERA is looked up instead.
//
// How the template's slot is told from every other use of that name: the lookup (sub_82202D20, hooked in
// cut_scenarios.cpp) gets a pointer to the name. A template's names are its own copies in the level data, and its
// "...\proptemplate\electrical\camera\propattributes.xml" path precedes them by about 490 bytes (the same layout in
// every package: 454 to 496 bytes for all 208 templates). So: the name is GUNS_BULLETHIT_METAL, it is not a string of
// the game's code, and the last propattributes.xml path within 640 bytes before it is the camera's. Any other
// GUNS_BULLETHIT_METAL (surfaces, other props, the code's own string) is left alone. This is the test the vent
// grate's fall sound uses (lab_grate_sound.cpp), with the template's path instead of its neighbouring names.
//
// 2. Tue 17 Nov night (job fix2, item 10): voice lines the game builds with a "_MALE" ending that the voice never
//    recorded, where the same voice has the clearly matching recording. Only when the asked name is not in the loaded
//    banks and the target is (both looked up with Rare's resolver):
//    - <VOICE>_FRIEND_DIES_MALE -> <VOICE>_FRIEND_DIES_AWARE_TEAM_MALE (log 018: SCI2 and GANGSTA3 asked in play;
//      every trooper, gangster, elite, scientist and woman voice recorded it). Not when the same voice said a body or
//      friend-dies line (SEE_BODY_*, FRIEND_DIES_*: Rare's own, or the stealth layer's witness / body lines, which are
//      looked up here too) in the last 4 s: no doubling.
//    - <VOICE>_AFFIRMATIVE_MALE -> <VOICE>_AFFIRMATIVE (log 018: GANGSTA1 / GANGSTA2; log 010 on Outpost: ELITE1-3;
//      every one of those voices recorded AFFIRMATIVE, and tonight's census heard GANGSTA1_AFFIRMATIVE played).
//    Not remapped (no matching recording in that voice): WOMAN1 / WOMAN2 SEE_BODY_TEAM (the woman voices have no
//    SEE_BODY line at all).
//
// Left out on purpose (see the audit report): the vent cover's fall sound is already restored by
// pdz_restore_grate_sound; the lamp templates (light\general*) are not named after a desk lamp or a metal fitting;
// GUNS_BULLETHIT_CRATE and GUNS_BULLETHIT_PHONE have no template that is unambiguously theirs.
//
// Cost: every other cue name fails one short string compare. For GUNS_BULLETHIT_METAL: a 16-entry table of name pointers; the
// system is asked about memory (QueryProtect) once per new pointer per level.
#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <cstring>
#include <mutex>
#include <string_view>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/stack.h>
#include <rex/system/kernel_state.h>

#include "../game_cvars.h"
#include "fix_log.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DEFINE_BOOL(pdz_fix_unwired_sounds, true, "Gameplay",
                    "Game fix: sounds Rare recorded for one prop and never linked, played in place of the generic "
                    "sound the prop asks for at the same moment (same loudness and timing; nothing else changes). "
                    "So far: a bullet hitting a security camera plays the camera's own hit sound "
                    "(GUNS_BULLETHIT_CAMERA) instead of the plain metal one; voice lines the game asks for with a "
                    "'male' ending a voice never recorded (a friend dies, 'affirmative') play that voice's own "
                    "matching line. Off: Rare's data")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_82202C78); // the name resolver (r3 = name): the cue, or -1

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8; // -> the game state / session block (+25 the mission)
  constexpr uint32_t kImageBegin = 0x82000000u, kImageEnd = 0x83400000u;
  constexpr char kAsked[] = "XACT_SOUNDBANK_GUNS_BULLETHIT_METAL";
  constexpr char kCue[] = "XACT_SOUNDBANK_GUNS_BULLETHIT_CAMERA";
  constexpr char kTemplate[] = "proptemplate\\electrical\\camera"; // ... \propattributes.xml
  constexpr char kPathTail[] = "\\propattributes.xml";
  constexpr uint32_t kBack = 640;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xFFFF0000u; }

  // Mapped, readable guest memory (asks the system: once per new name pointer).
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

  struct Known
  {
    uint32_t name = 0;
    bool camera = false;
  };
  std::mutex g_mutex;
  std::array<Known, 16> g_known{};
  uint32_t g_known_next = 0;
  uint64_t g_level = ~uint64_t(0); // (session pointer << 8) | mission: the table is for this level's data
  uint32_t g_cue = 0;              // kCue as a guest string
  int g_cue_there = -1;            // this level: -1 not asked yet, 0 not in the loaded banks, 1 there
  bool g_logged = false, g_absent_logged = false;

  // Is the name at `name` the camera template's own copy? (The caller has checked the text and the address range.)
  bool CameraSlot(uint8_t *base, uint32_t name)
  {
    if (name <= 0x10000u + kBack || !Readable(base, name - kBack, kBack))
    {
      return false;
    }
    const std::string_view window(reinterpret_cast<const char *>(base + name - kBack), kBack);
    const size_t tail = window.rfind(kPathTail); // the last template path before the name: this name's template
    const size_t length = sizeof(kTemplate) - 1;
    return tail != std::string_view::npos && tail >= length && window.compare(tail - length, length, kTemplate) == 0;
  }
} // namespace

namespace
{
  // 2. (fix2) The _MALE voice lines.
  struct VoiceRemap
  {
    uint32_t guest = 0;
    int there = -1; // -1 not asked yet (this level), 0 not in the banks, 1 there
    bool logged = false;
  };
  std::mutex g_voice_mutex;
  std::unordered_map<std::string, VoiceRemap> g_voice_remaps;   // target name -> its guest copy
  std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_body_said; // voice -> last body line looked up
  uint64_t g_voice_level = ~uint64_t(0);

  int32_t ResolveName(PPCContext &ctx, uint8_t *base, uint32_t name)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = name;
    sub_82202C78(frame.ctx, base);
    return int32_t(frame.ctx.r3.u32);
  }

  uint32_t VoiceLine(PPCContext &ctx, uint8_t *base, uint32_t name, std::string_view text)
  {
    constexpr std::string_view kPre = "XACT_SOUNDBANK_";
    constexpr std::string_view kFriend = "_FRIEND_DIES_MALE", kAffirm = "_AFFIRMATIVE_MALE";
    if (text.size() < kPre.size() + 4 || text.compare(0, kPre.size(), kPre) != 0)
    {
      return name;
    }
    const bool body_line = text.find("_FRIEND_DIES") != std::string_view::npos || text.find("_SEE_BODY") != std::string_view::npos;
    const bool friend_male = text.size() > kFriend.size() && text.compare(text.size() - kFriend.size(), kFriend.size(), kFriend) == 0;
    const bool affirm_male = text.size() > kAffirm.size() && text.compare(text.size() - kAffirm.size(), kAffirm.size(), kAffirm) == 0;
    if (!body_line && !affirm_male)
    {
      return name;
    }
    const size_t us = text.find('_', kPre.size());
    if (us == std::string_view::npos)
    {
      return name;
    }
    const std::string voice(text.substr(kPre.size(), us - kPre.size()));
    std::lock_guard<std::mutex> lock(g_voice_mutex);
    const uint32_t session = Load32(base, kGame);
    const uint64_t level = (uint64_t(session) << 8) | (Guest(session) ? base[session + 25] : 0xFFu);
    if (level != g_voice_level)
    {
      g_voice_level = level;
      for (auto &[k, v] : g_voice_remaps)
      {
        v.there = -1; // (another level's banks)
      }
      g_body_said.clear();
    }
    const auto now = std::chrono::steady_clock::now();
    if (body_line && !friend_male)
    {
      g_body_said[voice] = now; // (a body / friend-dies line of this voice: Rare's own or the stealth layer's)
      return name;
    }
    if (friend_male)
    {
      const auto it = g_body_said.find(voice);
      if (it != g_body_said.end() && now - it->second < std::chrono::seconds(4))
      {
        return name; // (the same voice said a body line a moment ago: no second one)
      }
    }
    if (ResolveName(ctx, base, name) != -1)
    {
      return name; // (recorded after all: Rare's own)
    }
    const std::string target = std::string(kPre) + voice + (friend_male ? "_FRIEND_DIES_AWARE_TEAM_MALE" : "_AFFIRMATIVE");
    VoiceRemap &r = g_voice_remaps[target];
    if (!r.guest)
    {
      r.guest = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(target.size() + 1));
      if (r.guest)
      {
        std::memcpy(base + r.guest, target.c_str(), target.size() + 1);
      }
    }
    if (!r.guest)
    {
      return name;
    }
    if (r.there < 0)
    {
      r.there = ResolveName(ctx, base, r.guest) != -1 ? 1 : 0;
    }
    if (r.there != 1)
    {
      return name;
    }
    if (friend_male)
    {
      g_body_said[voice] = now;
    }
    if (!r.logged)
    {
      r.logged = true;
      REXLOG_INFO("pdz_fix_unwired_sounds: {} is not recorded; the same voice's {} is played instead (the first time)",
                  text.substr(kPre.size()), target.substr(kPre.size()));
    }
    fix_log::Count("unwired sound");
    return r.guest;
  }
} // namespace

namespace unwired_sounds
{
  // From the hook of sub_82202D20: the name to look up for `name`.
  uint32_t CueName(PPCContext &ctx, uint8_t *base, uint32_t name)
  {
    if (!Guest(name))
    {
      return name;
    }
    const char *asked = reinterpret_cast<const char *>(base + name);
    if (REXCVAR_GET(pdz_fix_unwired_sounds) && REXCVAR_GET(pdz_game_fixes) && asked[0] == 'X')
    {
      // (fix2, part 2: the _MALE voice lines; also notes every body / friend-dies line by voice)
      const std::string_view text(asked, strnlen(asked, 96));
      const uint32_t voice_line = VoiceLine(ctx, base, name, text);
      if (voice_line != name)
      {
        return voice_line;
      }
    }
    // A stray control character in front of the name (found by the MISSING log, Tue 17 Nov: the diagnostic unit's
    // template writes "\nXACT_SOUNDBANK_STOCK_CHEMICAL_ANALYSER", so the game's 15-character skip lands one short and
    // the recorded cue is never found): look the name up from its real start. Only control characters: a stray
    // letter (the multiplayer "T_XACT_..." line) is left as the game has it.
    if (static_cast<unsigned char>(asked[0]) < 32 && asked[0] != 0 &&
        std::strncmp(asked + 1, "XACT_SOUNDBANK_", 15) == 0)
    {
      if (!REXCVAR_GET(pdz_fix_unwired_sounds) || !REXCVAR_GET(pdz_game_fixes))
      {
        return name;
      }
      static bool stray_logged = false;
      if (!stray_logged)
      {
        stray_logged = true;
        REXLOG_INFO("pdz_fix_unwired_sounds: a sound name with a stray character 0x{:02X} in front of it is looked "
                    "up from its real start: {} (the first one seen)",
                    static_cast<unsigned char>(asked[0]), std::string_view(asked + 1 + 15, strnlen(asked + 1 + 15, 96)));
      }
      return name + 1;
    }
    // Cheap reject: any other name differs within its first 16 to 36 characters (strncmp stops there or at its end).
    if (std::strncmp(asked, kAsked, sizeof(kAsked)) != 0)
    {
      return name;
    }
    if ((name >= kImageBegin && name < kImageEnd) || !REXCVAR_GET(pdz_fix_unwired_sounds) ||
        !REXCVAR_GET(pdz_game_fixes))
    {
      return name; // the game's own string (surfaces and code), or the fix is off
    }
    const uint32_t session = Load32(base, kGame);
    const uint64_t level = (uint64_t(session) << 8) | (Guest(session) ? base[session + 25] : 0xFFu);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (level != g_level)
    {
      g_level = level;
      g_known = {};
      g_known_next = 0;
      g_cue_there = -1;
    }
    const Known *known = nullptr;
    for (const Known &k : g_known)
    {
      if (k.name == name)
      {
        known = &k;
        break;
      }
    }
    if (!known)
    {
      Known &slot = g_known[g_known_next++ % g_known.size()];
      slot.name = name;
      slot.camera = CameraSlot(base, name);
      known = &slot;
    }
    if (!known->camera)
    {
      return name;
    }
    if (g_cue_there < 0)
    {
      if (!g_cue)
      {
        g_cue = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(sizeof(kCue)));
        if (g_cue)
        {
          std::memcpy(base + g_cue, kCue, sizeof(kCue));
        }
      }
      g_cue_there = 0;
      if (g_cue)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = g_cue;
        sub_82202C78(frame.ctx, base);
        g_cue_there = int32_t(frame.ctx.r3.u32) != -1 ? 1 : 0;
      }
    }
    if (g_cue_there != 1)
    {
      if (!g_absent_logged)
      {
        g_absent_logged = true;
        REXLOG_INFO("pdz_fix_unwired_sounds: electrical/camera bullet hit: GUNS_BULLETHIT_CAMERA is not in the loaded "
                    "banks: GUNS_BULLETHIT_METAL is left");
      }
      return name;
    }
    if (!g_logged)
    {
      g_logged = true;
      REXLOG_INFO("pdz_fix_unwired_sounds: electrical/camera bullet hit: GUNS_BULLETHIT_METAL -> GUNS_BULLETHIT_CAMERA "
                  "(the template's own name at {:08X})",
                  name);
    }
    fix_log::Count("unwired sound");
    return g_cue;
  }
} // namespace unwired_sounds
