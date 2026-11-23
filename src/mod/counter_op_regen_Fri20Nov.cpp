// Counter-Operative, round 16 (item 4, Fri 20 Nov): the agent's health regeneration. LOG ONLY: nothing here changes
// the game. Built only with PDZ_COUNTEROP (it reads PdzCounterOpAgent from counter_op.cpp).
//
// What the game does (TU3, read in the code; RESEARCH_regen_Fri20Nov.md has the details):
//  - Regeneration: sub_825A7848(human r3, f1 dt), called every update from the player update sub_825A79B8
//    (825A7C34). Its participant's timer +0xA0C counts up by dt while under 5.0 s (82089540); on the frame it reaches
//    5.0 s the health (+0x3D4, vtable +0x14C flag 0) is raised IN ONE STEP to the regen cap (+0x3D8, vtable +0x150
//    = sub_825A94B8), the step at most 200 (8208BCF8) and rounded to a whole number (sub_824B3DE8), and the health
//    bar is shown (sub_82268030). Then nothing until the next hit.
//  - Every hit: the player damage handler sub_825AAF50 sets the timer back to 0 and lowers the cap by the hit's
//    lasting share, round(damage x table 82D2A3E0[kind]). The kind comes with the hit and depends on the DIFFICULTY
//    and on the kind of hit, never on single player against co-op: bullets (sub_82236730) Agent 0, Secret Agent 1,
//    Perfect Agent 2, Dark Agent 7, multiplayer 6; blows (sub_82547AD8) 0 / 1 / 2 / 2, multiplayer 3; blasts
//    (sub_82236AB8) 2 / 2 / 4 / 4, multiplayer 4; the spiderbot's pounce (821B18F0 -> sub_82547068 r8) 3 always.
//    The table: [0] 0, [1] 0.10, [2] 0.25, [3] 0, [4] 0.75, [6] 0.50, [7] 1.0.
//  - A full reset (sub_825A9F08: health, armour and cap 100) on a fresh start.
// So Counter-Op (Rare's co-op) and single player regenerate by the same rule. On Agent (the play log's difficulty)
// bullets, blows and pounces leave no lasting damage: 5 s after the last hit she is back to 100, in single player too.
//
// This file: one line per level start (the rule in force at the level's difficulty) and, rate-limited, a line for each
// regen step of the agent (from, to, the cap, the lasting damage since the last full bar) and each fall of her cap.
#include <bit>
#include <cmath>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

uint32_t PdzCounterOpAgent(); // counter_op.cpp: the agent's character while a counter-op game runs, else 0

REXCVAR_DEFINE_BOOL(pdz_counterop_regen_log, true, "Testing",
                    "Counter-Operative: log the agent's health regeneration (log only): a line at each level start with "
                    "the game's rule at that difficulty, then each regen step and each fall of her regen cap (at most "
                    "40 lines a level)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_825A7848); // regeneration (human r3, f1 dt)

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8;       // -> game info: +4 difficulty (0 Agent .. 3 Dark Agent), +25 level,
                                               //    +40 game type (0 single player, 1 co-op, 2 / 3 multiplayer)
  constexpr uint32_t kKindTable = 0x82D2A3E0;  // float[8]: the lasting share of a hit, by its kind
  constexpr uint32_t kParticipantOf = 0x19A0;  // human -> participant
  constexpr uint32_t kPartHealth = 0x3D4;      // participant: health
  constexpr uint32_t kPartCap = 0x3D8;         // participant: the regen cap
  constexpr uint32_t kPartTimer = 0xA0C;       // participant: seconds since the last hit (stops at 5)
  constexpr int kMaxLines = 40;                // a level

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a) { return std::bit_cast<float>(Load32(base, a)); }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && (a & 3u) == 0; }

  // The game thread only.
  uint32_t g_agent = 0;
  int g_level = -1;
  int g_lines = 0;
  float g_cap = -1.0f;      // the cap last seen
  float g_full_cap = 100.0f; // the cap at the level start (for "lasting damage so far")

  float Share(const uint8_t *base, int kind) { return LoadF(base, kKindTable + 4u * uint32_t(kind)); }

  // The kinds Rare gives each sort of hit at a difficulty outside multiplayer (see the header).
  void Kinds(uint32_t difficulty, int &bullet, int &blow, int &blast)
  {
    bullet = difficulty == 0 ? 0 : difficulty == 1 ? 1 : difficulty == 2 ? 2 : 7;
    blow = difficulty == 0 ? 0 : difficulty == 1 ? 1 : 2;
    blast = difficulty <= 1 ? 2 : 4;
  }

  const char *DifficultyName(uint32_t d)
  {
    return d == 0 ? "Agent" : d == 1 ? "Secret Agent" : d == 2 ? "Perfect Agent" : d == 3 ? "Dark Agent" : "?";
  }

  void LevelStart(const uint8_t *base, uint32_t so, float cap)
  {
    const uint32_t difficulty = Load32(base, so + 4), type = Load32(base, so + 40);
    int bullet = 0, blow = 0, blast = 0;
    Kinds(difficulty, bullet, blow, blast);
    REXLOG_INFO("pdz_counterop: regen: level {} start, difficulty {} ({}), game type {}: the game's own rule, the same "
                "in single player and co-op (nothing of ours changes it): 5.0 s after her last hit her health is "
                "raised in one step to her regen cap (now {:.0f}); each hit lowers the cap by its lasting share: "
                "bullets {:.0f} %, blows {:.0f} %, blasts {:.0f} %, a spiderbot's pounce {:.0f} %",
                int(base[so + 25]), difficulty, DifficultyName(difficulty), type, cap, Share(base, bullet) * 100.0f,
                Share(base, blow) * 100.0f, Share(base, blast) * 100.0f, Share(base, 3) * 100.0f);
  }
} // namespace

// sub_825A7848(human r3, f1 dt): a human's regeneration, every update. Cheap when it is not the agent: one cvar read
// and one call. No syscalls.
REX_HOOK_RAW(sub_825A7848)
{
  const uint32_t c = ctx.r3.u32;
  if (!REXCVAR_GET(pdz_counterop_regen_log))
  {
    __imp__sub_825A7848(ctx, base);
    return;
  }
  const uint32_t agent = PdzCounterOpAgent();
  const uint32_t part = (agent && c == agent && Ptr(c)) ? Load32(base, c + kParticipantOf) : 0;
  const uint32_t so = Load32(base, kGame);
  if (!Ptr(part) || !Ptr(so))
  {
    __imp__sub_825A7848(ctx, base);
    return;
  }
  const float before = LoadF(base, part + kPartHealth), cap = LoadF(base, part + kPartCap);
  const float since_hit = LoadF(base, part + kPartTimer) + float(ctx.f1.f64); // the timer after this frame's dt
  __imp__sub_825A7848(ctx, base);
  const float after = LoadF(base, part + kPartHealth);

  const int level = int(base[so + 25]);
  if (agent != g_agent || level != g_level)
  {
    g_agent = agent;
    g_level = level;
    g_lines = 0;
    g_cap = cap;
    g_full_cap = cap;
    LevelStart(base, so, cap);
  }
  if (std::isfinite(cap) && cap > g_full_cap)
  {
    g_full_cap = cap; // a full reset (sub_825A9F08) raised it
  }
  if (g_lines >= kMaxLines)
  {
    g_cap = cap;
    return;
  }
  if (std::isfinite(cap) && g_cap >= 0.0f && cap < g_cap - 0.01f)
  {
    ++g_lines;
    REXLOG_INFO("pdz_counterop: regen: her regen cap {:.0f} -> {:.0f} (the lasting share of a hit); she can now "
                "regenerate only to {:.0f} (lasting damage this level {:.0f})",
                g_cap, cap, cap, g_full_cap - cap);
  }
  g_cap = cap;
  if (std::isfinite(after) && after > before + 0.01f && g_lines < kMaxLines)
  {
    ++g_lines;
    REXLOG_INFO("pdz_counterop: regen: her health {:.1f} -> {:.1f} in one step, {:.1f} s after her last hit; {} "
                "(cap {:.0f}, lasting damage this level {:.0f})",
                before, after, since_hit, after >= cap - 0.5f ? "up to her cap" : "stopped short of her cap", cap,
                g_full_cap - cap);
  }
}
