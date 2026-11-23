// Data Thief stages, log only (Thu 19 Nov 2026). The hand test of Thu 19 Nov: on Nightclub, as on the Laboratory, the
// Data Thief waits about 6 s before it spins. The research (job folder, RESEARCH_datathief_Thu19Nov.md) found no fixed
// timer, script wait or speech wait in Rare's chain; the spin starts when the gadget's attach animation has ended.
// These lines time each stage so the next play log shows which one takes the ~6 s. Nothing is changed: every hook
// calls Rare's function first (or around it) and only reads.
//
//   pdz_datathief: <stage> | AI clock <s> | <s> since the last stage | the gadget's state and fields
//
// Stages (TU3): accepted at the terminal (sub_8258ABB0 returns bit 0x2), raised (message 0xE in state 3 / 5 of the
// gadget's message handler sub_8257AE10), attach / waiting (sub_8257B440: state 7, the attach animation), spin starts
// (message 0xE in state 7, then the subgame's start sub_82647898), done (success sub_8257B630, the terminal's complete
// sub_8258B360), put away (message 0xE in state 8).
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(pdz_datathief_log, true, "Debug",
                    "Log only: one 'pdz_datathief:' line per stage of a Data Thief hack (accepted, raised, attach, spin "
                    "starts, done, put away) with the time since the stage before. No behaviour change")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_8258ABB0); // the terminal's use prompt (r3 terminal, r4 user): r3 bit 0x2 = accepted
REX_EXTERN(__imp__sub_8257AE10); // the Data Thief's message handler (r3 gadget, r4 message: +4 u16 its kind, 0xE animation done)
REX_EXTERN(__imp__sub_8257B440); // the attach (r3 gadget, r4 terminal, r5 data): state 7, the attach animation
REX_EXTERN(__imp__sub_82647898); // the minigame's start (r3 subgame): state 0 -> 1
REX_EXTERN(__imp__sub_8257B630); // success: the detach (r3 gadget): state 8
REX_EXTERN(__imp__sub_8258B360); // the terminal's hack complete (r3 terminal)

namespace
{
  constexpr uint32_t kAiClock = 0x82E2F334;
  constexpr uint32_t kItemType = 12, kState = 1356, kSubgame = 1316;
  constexpr uint32_t kDataThief = 16;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  uint16_t Load16(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return uint16_t((uint32_t(p[0]) << 8) | p[1]);
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && (a & 3u) == 0; }

  float g_last = -1e9f; // the AI clock at the last stage (the game thread only)

  void Stage(const uint8_t *base, const char *what, const std::string &more)
  {
    const float now = LoadF(base, kAiClock);
    const float since = g_last > -1e8f && now >= g_last ? now - g_last : -1.0f;
    g_last = now;
    REXLOG_INFO("pdz_datathief: {} | AI clock {:.2f} s | {} | {}", what, now,
                since >= 0.0f ? fmt::format("{:.2f} s since the last stage", since) : std::string("the first stage"), more);
  }
  bool IsThief(const uint8_t *base, uint32_t g) { return Guest(g) && Load32(base, g + kItemType) == kDataThief; }
}

REX_HOOK_RAW(sub_8258ABB0)
{
  const uint32_t terminal = ctx.r3.u32;
  __imp__sub_8258ABB0(ctx, base);
  if ((ctx.r3.u32 & 0x2u) && REXCVAR_GET(pdz_datathief_log))
  {
    Stage(base, "1 ACCEPTED at the terminal (the Data Thief is selected; the gun goes away)",
          fmt::format("terminal {:08X}, its busy state +1168 {}", terminal, Guest(terminal) ? Load32(base, terminal + 1168) : 0));
  }
}

REX_HOOK_RAW(sub_8257AE10)
{
  const uint32_t g = ctx.r3.u32, msg = ctx.r4.u32;
  const bool log = REXCVAR_GET(pdz_datathief_log) && IsThief(base, g) && Guest(msg) && Load16(base, msg + 4) == 0xE;
  const uint32_t before = log ? Load32(base, g + kState) : 0;
  __imp__sub_8257AE10(ctx, base);
  if (!log)
  {
    return;
  }
  const uint32_t after = Load32(base, g + kState);
  if (before == 3 || before == 5)
  {
    Stage(base, "2 RAISED (its raise animation ended)", fmt::format("state {} -> {}", before, after));
  }
  else if (before == 7)
  {
    Stage(base, "4 SPIN STARTS (the attach animation ended: the minigame and its loop animation start)",
          fmt::format("state {} -> {}, subgame {:08X}", before, after, Load32(base, g + kSubgame)));
  }
  else if (before == 8)
  {
    Stage(base, "6 PUT AWAY (the detach animation ended)", fmt::format("state {} -> {}", before, after));
  }
}

REX_HOOK_RAW(sub_8257B440)
{
  const uint32_t g = ctx.r3.u32, terminal = ctx.r4.u32;
  const bool log = REXCVAR_GET(pdz_datathief_log) && IsThief(base, g);
  __imp__sub_8257B440(ctx, base);
  if (log)
  {
    Stage(base, "3 ATTACH: WAITING (the hack is accepted; Rare plays the attach animation; the spin starts when it ends)",
          fmt::format("terminal {:08X}, state now {}, attach animation +1272 {:08X} / +1276 {:08X}, speed +1288 {:.2f}, flags +1292 "
                      "{:08X}, subgame {:08X}",
                      terminal, Load32(base, g + kState), Load32(base, g + 1272), Load32(base, g + 1276), LoadF(base, g + 1288),
                      Load32(base, g + 1292), Load32(base, g + kSubgame)));
  }
}

REX_HOOK_RAW(sub_82647898)
{
  const uint32_t sub = ctx.r3.u32;
  const uint32_t before = Guest(sub) ? Load32(base, sub + 32) : 0;
  __imp__sub_82647898(ctx, base);
  if (REXCVAR_GET(pdz_datathief_log) && Guest(sub))
  {
    Stage(base, "4b the minigame runs (its cursor can be stopped from here)",
          fmt::format("subgame {:08X} state {} -> {}", sub, before, Load32(base, sub + 32)));
  }
}

REX_HOOK_RAW(sub_8257B630)
{
  const uint32_t g = ctx.r3.u32;
  __imp__sub_8257B630(ctx, base);
  if (REXCVAR_GET(pdz_datathief_log) && IsThief(base, g))
  {
    Stage(base, "5 DONE: the hack succeeded (the detach animation starts)", fmt::format("state now {}", Load32(base, g + kState)));
  }
}

REX_HOOK_RAW(sub_8258B360)
{
  const uint32_t terminal = ctx.r3.u32;
  __imp__sub_8258B360(ctx, base);
  if (REXCVAR_GET(pdz_datathief_log))
  {
    Stage(base, "5b the terminal counts the hack as complete", fmt::format("terminal {:08X}", terminal));
  }
}
