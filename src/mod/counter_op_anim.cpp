// Counter-Operative round 5 (item 0): the crash of the hand test (guest read of 0x14 in 821EA0E0, game thread).
// Built only with PDZ_COUNTEROP.
//
// What Rare does: a human's update (825001F0) tests for a wall or an obstacle next to him (82501A00: a bit mask at
// human +0x1CC0); when the mask changes it turns it into a "gun position" (82504F68: 0xF, 0x10, 0x12 ...) and plays
// AnimType_Gun_PositionIdle (0x54) for it on his third-person body. The animation comes from his body's animation set
// ([vtable +0x58]() -> +0x10): its gun position table (set +0xD24: +0 entries, +8 count; an entry's byte +0xF1 is the
// position it serves, +0xF2 "usable") is searched by 82505B30 for the entries of that position, plus 0x1B when the
// weapon in his hands is a two-handed one ([human +0xFF0] +0x430 bit 0). 825056E0 then takes one of the found entries
// at random and looks the AnimType up in it (821F0840). There is no test for "none found": with an empty list the
// random pick reads a stack byte as the entry index, the lookup returns nothing, and the caller at 82501340 plays the
// empty pointer (826946A8 -> 826947E0 -> 821EA0E0 reads entry +0x14).
// Jo's and the co-op partners' sets have every position for every gun they can hold. A guard's body does not: the
// set player 2 gets with a guard's body (the re-dress of round 4) has no entry for some position / weapon pairs. In
// the hand test: the gangster body (48) with the SMG (gun type 5), next to a wall, 0.2 s after the spawn.
//
// What this file does, in a running Counter-Op game only: when 82505B30 finds nothing, the search is run again for
// the same position with the other hand count (one-handed entries for a two-handed gun and the other way round), then,
// if that finds nothing either, the first usable entry of the set is used. So the list is never empty and the pick is
// always a real entry of Rare's table. If that entry has no animation of the asked type, the lookup still returns
// nothing: the play hook in anim_log.cpp (826946A8) then leaves the play out (for every game type: an empty entry
// always crashed).
#include <cstdint>
#include <cstring>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

bool PdzCounterOpRunning();     // counter_op.cpp
uint32_t PdzCounterOpPlayer2(); // player 2's character while a counter-op game runs, else 0

REXCVAR_DEFINE_BOOL(pdz_counterop_fix_gun_position, true, "Gameplay",
                    "Counter-Operative (round 5, default on): when a guard body's animation set has no gun position "
                    "entry for the weapon player 2 holds (Rare's list comes back empty and the game crashed: gangster "
                    "body with an SMG next to a wall), the same position for the other hand count is used, else the "
                    "set's first usable entry. Off = Rare's code alone (testing: the crash)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_test_gun_positions, false, "Testing",
                    "TESTING TOOL (counter-op): 3 s after each change of player 2's body or weapon, log what Rare's "
                    "own lookup returns for the gun position idle animation of every wall / corner / low cover "
                    "position (nothing = the empty pointer of the hand test's crash)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_82505B30); // (character, position, &list {bytes.., +8 write pointer}, r6): the set's entries of a position

namespace
{
  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    uint32_t v;
    std::memcpy(&v, base + a, 4);
    return __builtin_bswap32(v);
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    v = __builtin_bswap32(v);
    std::memcpy(base + a, &v, 4);
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

  int g_lines = 0;
  uint64_t g_fallbacks = 0, g_other_hand = 0, g_first_usable = 0, g_none = 0;

  // The animation set's gun position table of a character: entries array and count (0 when unreadable).
  bool Table(PPCContext &ctx, uint8_t *base, uint32_t character, uint32_t &entries, uint32_t &count, uint32_t &set)
  {
    entries = count = set = 0;
    // Sun 22 Nov night (audit): a readable character whose vtable is in the game image; every link below tested.
    if (!pdz::guestmem::Ok(base, character, 4))
    {
      return false;
    }
    const uint32_t vt = Load32(base, character);
    if (vt < 0x82000000u || vt >= 0x83000000u || !pdz::guestmem::Ok(base, vt + 0x58, 4))
    {
      return false;
    }
    PPCFunc *fn = pdz::GuestFunction(Load32(base, vt + 0x58), "a character's animation state (vtable +0x58)");
    if (!fn)
    {
      return false;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = character;
    fn(frame.ctx, base);
    const uint32_t state = frame.ctx.r3.u32;
    if (!pdz::guestmem::Ok(base, state, 0x14))
    {
      return false;
    }
    set = Load32(base, state + 0x10);
    if (!pdz::guestmem::Ok(base, set, 0xD28))
    {
      return false;
    }
    const uint32_t table = Load32(base, set + 0xD24);
    if (!pdz::guestmem::Ok(base, table, 12))
    {
      return false;
    }
    entries = Load32(base, table);
    count = Load32(base, table + 8) & 0xFF;
    return pdz::guestmem::Ok(base, entries, 4 * count + 4);
  }
} // namespace

REX_HOOK_RAW(sub_82505B30)
{
  const uint32_t character = ctx.r3.u32, position = ctx.r4.u32, list = ctx.r5.u32, r6 = ctx.r6.u32;
  __imp__sub_82505B30(ctx, base);
  if (!Ptr(list) || Load32(base, list + 8) != list)
  {
    return; // Rare found entries (or the list isn't the caller's stack list)
  }
  if (!PdzCounterOpRunning() || !REXCVAR_GET(pdz_counterop_fix_gun_position))
  {
    return;
  }
  ++g_fallbacks;
  // The hand count Rare added: +0x1B for a two-handed weapon in his hands.
  // Sun 22 Nov night (audit): the character, his weapon and its definition read only where readable.
  const uint32_t weapon = pdz::guestmem::Ok(base, character, 0xFF4) ? Load32(base, character + 0xFF0) : 0;
  bool two_handed = false;
  if (pdz::guestmem::Ok(base, weapon, 0x434) && pdz::guestmem::Ok(base, Load32(base, weapon + 8), 4))
  {
    const uint32_t word = Load32(base, Load32(base, weapon + 8));
    two_handed = ((word >> 4) & 1) && (Load32(base, weapon + 0x430) & 1);
  }
  const char *how = "nothing";
  int32_t used = -1;
  {
    // The same position for the other hand count: Rare's own search, asked with the position moved by 0x1B.
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = character;
    frame.ctx.r4.u64 = two_handed ? uint64_t(int64_t(int32_t(position) - 0x1B)) : uint64_t(position + 0x1B);
    frame.ctx.r5.u64 = list;
    frame.ctx.r6.u64 = r6;
    __imp__sub_82505B30(frame.ctx, base);
  }
  uint32_t entries = 0, count = 0, set = 0;
  const bool have_table = Table(ctx, base, character, entries, count, set);
  if (Load32(base, list + 8) != list)
  {
    ++g_other_hand;
    how = two_handed ? "the same position's one-handed entry" : "the same position's two-handed entry";
    used = base[list];
  }
  else if (have_table)
  {
    for (uint32_t i = 0; i < count; ++i)
    {
      const uint32_t e = Load32(base, entries + 4 * i);
      if (pdz::guestmem::Ok(base, e, 0xF3) && base[e + 0xF2]) // Sun 22 Nov night (audit)
      {
        base[list] = uint8_t(i);
        Store32(base, list + 8, list + 1);
        used = int32_t(i);
        break;
      }
    }
    if (used >= 0)
    {
      ++g_first_usable;
      how = "the set's first usable entry";
    }
  }
  if (used < 0)
  {
    ++g_none;
  }
  if (g_lines < 24)
  {
    ++g_lines;
    std::string all;
    if (have_table)
    {
      for (uint32_t i = 0; i < count && i < 64; ++i)
      {
        const uint32_t e = Load32(base, entries + 4 * i);
        if (pdz::guestmem::Ok(base, e, 0xF3))
        {
          all += fmt::format("{}{}{}", all.empty() ? "" : " ", base[e + 0xF1], base[e + 0xF2] ? "" : "(off)");
        }
      }
    }
    REXLOG_INFO("pdz_counterop: gun position: Rare's list for position {} ({}) is empty for character {:08X}{} (body "
                "{}, animation set {:08X} with {} gun position entries serving positions: {}): used {} (entry {}); "
                "without this the game read an empty animation (the crash of the hand test)",
                position, two_handed ? "two-handed weapon: +27" : "one-handed weapon", character,
                character == PdzCounterOpPlayer2() ? " (player 2)" : "", Load32(base, character + 0x14A0), set, count,
                all.empty() ? "?" : all, how, used);
  }
}

// Testing / diagnosis: player 2's stance state (human +0x1CAC; 3 runs the gun position block of 825001F0), the
// position in use (+0x1CB0) and the wall mask Rare's probe found (+0x1CC0), logged when they change (first 60).
void PdzCounterOpAnimWatch(const uint8_t *base, uint32_t p2c)
{
  static uint32_t last[3] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
  static int lines = 0;
  if (!Ptr(p2c) || lines >= 60)
  {
    return;
  }
  const uint32_t now[3] = {Load32(base, p2c + 0x1CAC), Load32(base, p2c + 0x1CB0), Load32(base, p2c + 0x1CC0)};
  if (now[0] != last[0] || now[1] != last[1] || now[2] != last[2])
  {
    ++lines;
    REXLOG_INFO("pdz_counterop: gun position watch: player 2 stance state {} -> {}, position in use {:X} -> {:X}, wall "
                "mask {:X} -> {:X}; weapon object {:08X}, body {}",
                int32_t(last[0]), int32_t(now[0]), last[1], now[1], last[2], now[2], Load32(base, p2c + 0xFF0),
                Load32(base, p2c + 0x14A0));
    last[0] = now[0], last[1] = now[1], last[2] = now[2];
  }
}

// TESTING TOOL (pdz_counterop_test_gun_positions): 3 s after each change of player 2's body or weapon, Rare's own
// lookup 825056E0(character, AnimType_Gun_PositionIdle 0x54, position, &out, 0, 0, 0) is asked for every position
// 82504F68 can produce (0xC / 0xE corners, 0xF / 0x10 / 0x11 / 0x12 low cover, 0x14 a wall ahead). "nothing" is the
// empty pointer Rare's caller at 82501340 plays without a test: the crash. With pdz_counterop_fix_gun_position off
// the answers are Rare's alone.
REX_EXTERN(sub_825056E0);
REX_EXTERN(sub_826946A8); // play an animation entry (hooked in anim_log.cpp)
REXCVAR_DEFINE_BOOL(pdz_counterop_test_gun_position_play, false, "Testing",
                    "TESTING TOOL (counter-op): after pdz_counterop_test_gun_positions' lookups, the first empty "
                    "answer is played as Rare's caller plays it (with pdz_counterop_fix_gun_position off: the crash "
                    "of the hand test)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
void PdzCounterOpAnimProbe(PPCContext &ctx, uint8_t *base, uint32_t p2c)
{
  static uint32_t seen_body = 0, seen_weapon = 0, scratch = 0;
  static int wait = -1;
  if (!REXCVAR_GET(pdz_counterop_test_gun_positions) || !Ptr(p2c))
  {
    return;
  }
  const uint32_t body = Load32(base, p2c + 0x14A0), weapon = Load32(base, p2c + 0xFF0);
  if (body != seen_body || weapon != seen_weapon)
  {
    seen_body = body;
    seen_weapon = weapon;
    wait = 180;
    return;
  }
  if (wait < 0 || --wait > 0 || !Ptr(weapon))
  {
    return;
  }
  wait = -1;
  if (!scratch)
  {
    scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(16);
  }
  if (!scratch)
  {
    return;
  }
  // The call as 825012D4 makes it: (character, 0x54, position, &out, 1, 0, 0). Every position value is asked.
  std::string gaps, names;
  int nothing = 0, first_nothing = -1, kinds = 0;
  std::string last_name;
  for (uint32_t position = 0; position < 0x40; ++position)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = p2c;
    frame.ctx.r4.u64 = 0x54;
    frame.ctx.r5.u64 = position;
    frame.ctx.r6.u64 = scratch;
    frame.ctx.r7.u64 = 1;
    frame.ctx.r8.u64 = 0;
    frame.ctx.r9.u64 = 0;
    sub_825056E0(frame.ctx, base);
    const uint32_t entry = frame.ctx.r3.u32;
    if (Ptr(entry))
    {
      const uint32_t np = Load32(base, entry);
      std::string name;
      for (int i = 0; Ptr(np) && i < 56 && base[np + i]; ++i)
      {
        name += char(base[np + i]);
      }
      if (name != last_name && kinds < 14)
      {
        ++kinds;
        names += fmt::format("{}{:X}: {}", names.empty() ? "" : "; ", position, name);
        last_name = name;
      }
    }
    else
    {
      if (first_nothing < 0)
      {
        first_nothing = int(position);
      }
      ++nothing;
      gaps += fmt::format("{}{:X}", gaps.empty() ? "" : " ", position);
      last_name.clear();
    }
  }
  REXLOG_INFO("pdz_counterop: gun position test: player 2 with body {} and weapon object {:08X} (fix {}): Rare's "
              "lookup of the gun position idle animation for positions 0-3F: {} give nothing{}{}; animations found "
              "(position: name, where it changes): {}",
              body, weapon, REXCVAR_GET(pdz_counterop_fix_gun_position) ? "on" : "OFF", nothing,
              nothing ? " (the empty pointer Rare plays without a test): " : "", gaps, names.empty() ? "none" : names);
  if (REXCVAR_GET(pdz_counterop_test_gun_position_play) && first_nothing >= 0)
  {
    // What 82501340 does with that answer: play it. Fix off: Rare's code reads the empty entry (the crash).
    REXLOG_INFO("pdz_counterop: gun position test: playing the answer for position {:X} as Rare's 82501340 does "
                "(an empty entry){}",
                first_nothing,
                REXCVAR_GET(pdz_counterop_fix_gun_position) ? "" : "; fix OFF: the crash of the hand test is expected");
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = p2c;
    frame.ctx.r4.u64 = 0;
    frame.ctx.r5.u64 = p2c + 0x1CF0;
    frame.ctx.r6.u64 = 0;
    frame.ctx.r8.u64 = 4;
    frame.ctx.r9.u64 = 1;
    frame.ctx.r10.u64 = 0;
    frame.ctx.f1.f64 = 0.0;
    frame.ctx.f2.f64 = 0.0;
    sub_826946A8(frame.ctx, base);
    REXLOG_INFO("pdz_counterop: gun position test: the play of the empty entry returned {:08X}: no crash",
                frame.ctx.r3.u32);
  }
}

// counter_op.cpp's level-end line.
std::string PdzCounterOpAnimSummary()
{
  const std::string s =
      fmt::format("gun position lists Rare left empty {} (other hand count used {}, first usable entry {}, none {})",
                  g_fallbacks, g_other_hand, g_first_usable, g_none);
  g_fallbacks = g_other_hand = g_first_usable = g_none = 0;
  g_lines = 0;
  return s;
}
