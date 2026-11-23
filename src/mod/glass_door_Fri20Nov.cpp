// Datacore's glass tank door (Fri 20 Nov 2026; pdz_snd_glass_tank_door, on). TU3 addresses. Needs pdz_restored_sounds.
//
// The scene: the scientist stands in a glass tank with spiderbots on its sides; once they are shot off he says he will
// restart the power, the tank opens and he walks to a terminal. The tester heard that opening SILENT (Fri 20 Nov 06:35).
// Research (research notes: RESEARCH_glassdoor_Fri20Nov.md, read in code and level data):
//   - The tank door is NOT a door object: model aid_darkcharacter_xenon_props_door_door_glasstank, template "example",
//     placed once in the level's static group at about (8402 -4697 2530). Rare's door update and door sound player
//     (sub_82561240 / sub_82563788) never see it, so no door slot asks for anything.
//   - The release is Rare's ExitTankScript::OnBegin, sub_822E8148 (r3 = the script, +0x10 the scientist): it sets his
//     flags, calls the level script's check (sub_822F50F0) and queues his exit animations and walk. None of it makes a
//     sound call. Nothing in the game or the level asks for STOCK_GLASS_CONTAINER_DOOR (Rare's own recording, 3.8 s).
// Here: when ExitTankScript begins, Rare's STOCK_GLASS_CONTAINER_DOOR is played once at the tank door (the placed prop
// of that model; its fixed place if it cannot be found), with Rare's door range (10 m, the picker's "_DOOR_" rule; the
// cue's name has no trailing underscore, so Rare's picker would give its 50 m default). Rare's script can restart him
// ("Resetting scientist because he doesn't seem to be exitting the tank"): at most once per level.
#include <bit>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/func.h>

#include "restored_voice_Wed18Nov.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DECLARE(bool, pdz_restored_sounds);
REXCVAR_DEFINE_BOOL(pdz_snd_glass_tank_door, true, "Gameplay",
                    "Restored sound (Fri 20 Nov): on Datacore, the scientist's glass tank makes the game's own glass "
                    "container door sound when it opens to let him out. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_822E8148); // ExitTankScript::OnBegin (r3 = the script)

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8; // +0 -> the session (+25 level), +24 -> the world
  constexpr uint32_t kWorldFirst = 43212, kWorldEnd = 43216;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
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
  bool ModelHas(uint8_t *base, uint32_t object, const char *part)
  {
    const uint32_t setup = Readable(base, object, 96) ? Load32(base, object + 84) : 0;
    const uint32_t name = Readable(base, setup, 144) ? Load32(base, setup + 140) : 0;
    if (!Readable(base, name, 96))
    {
      return false;
    }
    const char *c = reinterpret_cast<const char *>(base + name);
    return std::string(c, strnlen(c, 95)).find(part) != std::string::npos;
  }
  uint64_t g_played_key = ~0ull; // the session and level it played in
} // namespace

REX_HOOK_RAW(sub_822E8148)
{
  __imp__sub_822E8148(ctx, base);
  if (!REXCVAR_GET(pdz_restored_sounds) || !REXCVAR_GET(pdz_snd_glass_tank_door))
  {
    return;
  }
  const uint32_t session = Load32(base, kGame);
  const uint64_t key = (uint64_t(session) << 8) | (Guest(session) ? base[session + 25] : 0xFFu);
  if (key == g_played_key)
  {
    REXLOG_INFO("pdz_snd_glass_tank_door: the scientist's tank exit began again (Rare's reset): no second sound");
    return;
  }
  g_played_key = key;
  // The tank door prop (once: this runs once a level).
  float at[3] = {8402.0f, -4697.0f, 2530.0f};
  bool found = false;
  const uint32_t world = Load32(base, kGame + 24);
  if (Guest(world))
  {
    const uint32_t end = Load32(base, world + kWorldEnd);
    uint32_t node = Load32(base, world + kWorldFirst);
    for (int i = 0; Guest(node) && node != end && i < 16384 && !found; ++i, node = Load32(base, node + 12))
    {
      const uint32_t o = Load32(base, node + 4);
      if (Guest(o) && ModelHas(base, o, "door_door_glasstank"))
      {
        at[0] = LoadF(base, o + 48), at[1] = LoadF(base, o + 52), at[2] = LoadF(base, o + 56);
        found = true;
      }
    }
  }
  pdz::snd::NextRangeM(10.0f); // Rare's door range
  const pdz::snd::Result r = pdz::snd::Once(ctx, base, "XACT_SOUNDBANK_STOCK_GLASS_CONTAINER_DOOR", at, 1.0f);
  pdz::snd::NextRangeM(0.0f);
  REXLOG_INFO("pdz_snd_glass_tank_door: the scientist's tank opens (Rare's ExitTankScript begins): STOCK_GLASS_CONTAINER_DOOR {} "
              "at ({:.0f} {:.0f} {:.0f}) ({}), the nearest player {:.1f} m away",
              pdz::snd::Text(r), at[0], at[1], at[2], found ? "the tank door prop" : "the tank door's place: the prop was not found",
              pdz::snd::NearestPlayerCm(base, at) / 100.0f);
}
