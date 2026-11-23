// Counter-Operative, Fri 20 Nov night (play log 031, Trinity Escape): the agent's crosshair turned GREEN over player 2.
// Built only with PDZ_COUNTEROP (it reads PdzCounterOpAgent / PdzCounterOpPlayer2 from counter_op.cpp).
//
// Rare's crosshair colour (TU3, read in the code; RESEARCH_reticle_Fri20Nov.md has the details):
//  - The crosshair object (class 82263970 / 82264528, vtable 8209A17C) keeps a colour state at +0x40, read by its
//    colour picker sub_82263CA8: 1 = red (sub_825AA1E8(0): 1.0 / 0.1 / 0.0, colour-blind option 54 = 1: 0.7 / 0.1 /
//    0.0), 2 = green (sub_825AA128(1): 0 / 1 / 0; option 54 variants light green, cyan, blue, white), anything else
//    the crosshair's own colour from its XML.
//  - The holder's weapon writes that state every frame: sub_82588078 (on foot, from the player update 825B2D08 at
//    82588204: state = holder vtable +0x2AC (holder, holder +0x1F34 = the object under the crosshair), 0 when
//    nothing is there), and the same call for a vehicle's gunner (82685F80 at 82686CEC) and 8258E6D8.
//  - Vtable +0x2AC is sub_8254E1C0 for every character (the humans' vtable 820C7DE0 and the guards' 820C45A0):
//    (viewer r3, object r4) -> object +0x5C when it is an attached part / weapon (class word bit 0x02000000 or class
//    +8 bit 0x8); a vehicle (class word bit 0x20000000) -> its occupant (+0x364, vt +0x60); dead (vt +0xE8) -> 0;
//    a character: +0x1698 byte -> 0; else Rare's friend test sub_8251EBD0(viewer, object) -> friend 2, enemy 1 (with
//    the object's +0x40 bit 0x4: friend 2, enemy 0); a prop: its own rules, the friend test again for most.
//  So green over player 2 means Rare's friend test, asked from the AGENT about him (or about what the crosshair
//  resolved to), said "friend". The friend test the hit handler asks is the other way round (victim, attacker), so his
//  wounds counting proves nothing about this direction. 8251EBD0 is friend when the other IS the viewer, when an
//  owner link (object +0x68 bit 0x20, list +0x70, node type 5) joins the two either way, or by the masks
//  (other +0x80 & (my +0x80 | my +0x82)) and not (other +0x80 & my +0x84), allies / enemies swapped while my +0x1608
//  (Psychosis) > 0. The line below says which of these it was.
//
// pdz_counterop_reticle_enemy (default on): while a Counter-Op game runs, when Rare's answer is 2 (green) for the
// agent's crosshair over player 2 or a thing of his (his weapon, his spiderbot, the vehicle he rides), or for his
// crosshair over the agent or a thing of hers, the answer becomes Rare's enemy answer for that character (1 = red;
// 0 when the character's +0x40 bit 0x4 is set, as Rare's own enemy branch gives). Nothing else changes: the friend
// test itself, the hit rules, the allies' rules and real co-op (no Counter-Op game: the hook passes straight
// through) are Rare's. Off = Rare's answer.
#include <cstdint>
#include <cstring>
#include <string>

#include <fmt/format.h>
#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov night (audit): pdz::guestmem::Ok

uint32_t PdzCounterOpPlayer2(); // counter_op.cpp: 0 unless a Counter-Op game is running
uint32_t PdzCounterOpAgent();   // counter_op.cpp: 0 unless a Counter-Op game is running
bool PdzCounterOpIsHis(const uint8_t *base, uint32_t object);              // counter_op_unheard.cpp
bool PdzCounterOpIsOf(const uint8_t *base, uint32_t object, uint32_t who); // counter_op_unheard.cpp (round 12)

REXCVAR_DEFINE_BOOL(pdz_counterop_reticle_enemy, true, "Gameplay",
                    "Counter-Operative (Fri 20 Nov night, default on): the agent's crosshair is red over player 2 (and "
                    "his over her), never the green of a friend. Rare's crosshair colour (8254E1C0) asks Rare's friend "
                    "test from the viewer's side; when that says friend for the other human, the enemy colour is used. "
                    "Off = Rare's answer")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(__imp__sub_8254E1C0); // the crosshair's colour state (viewer r3, object under the crosshair r4) -> 0 / 1 / 2
REX_EXTERN(sub_8251EBD0);        // Rare's friend test (me r3, other r4) -> byte (only for the diagnostic line)

namespace
{
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u && (a & 3u) == 0; }
  uint32_t L32(const uint8_t *b, uint32_t a)
  {
    const uint8_t *p = b + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  uint16_t L16(const uint8_t *b, uint32_t a) { return uint16_t((uint32_t(b[a]) << 8) | b[a + 1]); }
  float LF(const uint8_t *b, uint32_t a)
  {
    const uint32_t v = L32(b, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }

  constexpr uint32_t kClass = 0x0C;        // 2 human, 0x21 guard / NPC, 8 / 0x2B props
  constexpr uint32_t kFlags = 0x40;        // bit 0x4: Rare's enemy colour is 0 (none) for this character
  constexpr uint32_t kParent = 0x5C;       // a weapon / kit: its owner; a rider: his vehicle
  constexpr uint32_t kLinkFlags = 0x68;    // bit 0x20: the owner-link list +0x70 is in use (8251EC00)
  constexpr uint32_t kLinks = 0x70;        // nodes {type, value, next}; type 5 = the owner
  constexpr uint32_t kTeamBits = 0x80, kAllies = 0x82, kEnemies = 0x84;
  constexpr uint32_t kPsychosis = 0x1608;  // float > 0: allies and enemies swapped (825A60D8 / 8252B050)
  constexpr uint32_t kRed = 1, kGreen = 2; // the crosshair's colour state (82263CA8)

  uint32_t LinkOwner(const uint8_t *b, uint32_t o)
  {
    if (!Ptr(o) || !(L32(b, o + kLinkFlags) & 0x20))
    {
      return 0;
    }
    int guard = 0;
    for (uint32_t n = L32(b, o + kLinks); Ptr(n) && guard < 32; n = L32(b, n + 8), ++guard)
    {
      if (L32(b, n) == 5)
      {
        return L32(b, n + 4);
      }
    }
    return 0;
  }

  // 'object' is 'who' or a thing of who's: the existing Counter-Op rules (PdzCounterOpIsHis covers his spiderbot
  // too), and the vehicle a rider sits in (the rider's +0x5C), which Rare's colour routine resolves to its occupant.
  bool Belongs(const uint8_t *b, uint32_t object, uint32_t who, bool his)
  {
    if (!Ptr(object) || !Ptr(who))
    {
      return false;
    }
    if (object == who || L32(b, who + kParent) == object)
    {
      return true;
    }
    return his ? PdzCounterOpIsHis(b, object) : PdzCounterOpIsOf(b, object, who);
  }

  // Masks only, as 8251EBD0 reads them after its link checks (the viewer's vtable +0x1CC gives allies / enemies).
  bool MaskFriend(const uint8_t *b, uint32_t me, uint32_t other)
  {
    uint16_t allies = uint16_t(L16(b, me + kTeamBits) | L16(b, me + kAllies)), enemies = L16(b, me + kEnemies);
    if (LF(b, me + kPsychosis) > 0.0f)
    {
      const uint16_t t = allies;
      allies = enemies;
      enemies = t;
    }
    const uint16_t team = L16(b, other + kTeamBits);
    return (team & allies) != 0 && (team & enemies) == 0;
  }

  bool RareFriend(PPCContext &ctx, uint8_t *base, uint32_t me, uint32_t other)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = me;
    frame.ctx.r4.u64 = other;
    sub_8251EBD0(frame.ctx, base);
    return (frame.ctx.r3.u32 & 0xFF) != 0;
  }

  // Which part of Rare's friend test made 'viewer' count 'object' (resolving to the human 'who') a friend.
  std::string Cause(PPCContext &ctx, uint8_t *base, uint32_t viewer, uint32_t object, uint32_t who)
  {
    if (object == viewer || L32(base, object + kParent) == viewer || LinkOwner(base, object) == viewer)
    {
      return "the object under the crosshair is the viewer's own (itself, its +0x5C or its owner link is the viewer)";
    }
    const uint32_t lw = LinkOwner(base, who), lv = LinkOwner(base, viewer);
    if (lw == viewer || lv == who)
    {
      return fmt::format("an owner link joins the two humans (his link owner {:08X}, the viewer's {:08X})", lw, lv);
    }
    if (MaskFriend(base, viewer, who))
    {
      return "the team masks (from the viewer's side)";
    }
    return fmt::format("neither the masks nor an owner link: Rare's friend test viewer -> him says {} (if 'no', the "
                       "colour routine resolved the object to someone else: +0x5C {:08X}, class {:X})",
                       RareFriend(ctx, base, viewer, who) ? "YES" : "no", L32(base, object + kParent),
                       L32(base, object + kClass));
  }

  // The game thread only. A line when the answer over the other human changes (aiming onto him counts as a change).
  constexpr uint32_t kOff = 0xFFFFFFFFu;  // the crosshair was last on something else
  uint64_t g_changed = 0;                 // frames whose green answer was turned into the enemy one (this session)
  uint64_t g_green_lines = 0, g_kept_lines = 0;
  uint32_t g_last_state[2] = {kOff, kOff}; // per direction (0 the agent looking, 1 player 2 looking)
} // namespace

REX_HOOK_RAW(sub_8254E1C0)
{
  const uint32_t viewer = ctx.r3.u32, object = ctx.r4.u32;
  __imp__sub_8254E1C0(ctx, base);
  if (!REXCVAR_GET(pdz_counterop_reticle_enemy))
  {
    return;
  }
  const uint32_t p2 = PdzCounterOpPlayer2(), agent = PdzCounterOpAgent();
  if (!Ptr(p2) || !Ptr(agent) || !Ptr(viewer) || !Ptr(object) || (viewer != agent && viewer != p2))
  {
    return; // no Counter-Op game (real co-op, single player, multiplayer), or not one of the two humans looking
  }
  const bool agent_view = viewer == agent;
  const uint32_t who = agent_view ? p2 : agent; // the other human
  const int dir = agent_view ? 0 : 1;
  if (!pdz::guestmem::Ok(base, who, kPsychosis + 4)) // Sun 22 Nov night (audit): the other human readable (else Rare's)
  {
    return;
  }
  if (!Belongs(base, object, who, agent_view))
  {
    g_last_state[dir] = kOff;
    return;
  }
  const uint32_t rare = ctx.r3.u32;
  if (rare != kGreen)
  {
    // For the test: Rare's own answer over the other human whenever it changes (first 20, then 1 in 200).
    if (g_last_state[dir] != rare && (++g_kept_lines <= 20 || g_kept_lines % 200 == 0))
    {
      REXLOG_INFO("pdz_counterop: reticle: {} crosshair over {} (object {:08X}, class {:X}): Rare's colour state {} "
                  "({}), kept",
                  agent_view ? "the agent's" : "player 2's", agent_view ? "player 2" : "the agent", object,
                  L32(base, object + kClass), rare, rare == kRed ? "red, an enemy" : "none");
    }
    g_last_state[dir] = rare;
    return;
  }
  const uint32_t enemy = (L32(base, who + kFlags) & 0x4) ? 0u : kRed; // Rare's enemy branch for that character
  ctx.r3.u64 = enemy;
  ++g_changed;
  if (g_last_state[dir] != kGreen && (++g_green_lines <= 10 || g_green_lines % 50 == 0))
  {
    REXLOG_WARN("pdz_counterop: reticle: #{} ({} frames so far): {} crosshair over {} (object {:08X}, class {:X}, "
                "vtable {:08X}): Rare's "
                "colour state 2 (green, a friend) -> {} ({}) (pdz_counterop_reticle_enemy). Why Rare said friend: {}; "
                "masks: viewer {:04X} / {:04X} / {:04X} (Psychosis {:.2f}), {} {:04X} / {:04X} / {:04X}; owner links "
                "(+0x68 bit 0x20): viewer {}, {} {}",
                g_green_lines, g_changed, agent_view ? "the agent's" : "player 2's", agent_view ? "player 2" : "the agent", object,
                L32(base, object + kClass), L32(base, object), enemy, enemy == kRed ? "red, an enemy" : "none",
                Cause(ctx, base, viewer, object, who), L16(base, viewer + kTeamBits), L16(base, viewer + kAllies),
                L16(base, viewer + kEnemies), LF(base, viewer + kPsychosis), agent_view ? "his" : "hers",
                L16(base, who + kTeamBits), L16(base, who + kAllies), L16(base, who + kEnemies),
                (L32(base, viewer + kLinkFlags) & 0x20) ? "set" : "clear", agent_view ? "his" : "hers",
                (L32(base, who + kLinkFlags) & 0x20) ? "set" : "clear");
  }
  g_last_state[dir] = kGreen;
}
