// Counter-Operative round 10 (item 1): player 2 is no stimulus for Rare's AI. Built only with PDZ_COUNTEROP.
//
// The rule: the guards treat the agent exactly as in single player; nothing player 2 does reaches their alert system.
// What Rare's AI takes in, and where each input of his is dropped (all read in the TU3 code, round 10 helper A):
//   - HEARING. Every character and every thrown / placed weapon has a perception slot (0x82E5D590, 170 x 0x50 bytes:
//     +4 owner, +0x28 footstep radius, +0x30 gunfire radius, +0x44 flags). The hearing loop 8218AAC8(listener) treats
//     every slot with flags bit 0x4 and a radius as a source; a listener in range gets Rare's "heard gunfire" inputs
//     (+6260 0x2 / 0x8, the +6252 latch, HEAR_GUNFIRE, the squad target copy) or the footstep noise pad. For the
//     length of each call the slots owned by player 2, or by a thing of his, lose bit 0x4: his shots (all four writers
//     of +0x30), his steps and his grenade's own bang are heard by nobody.
//   - NEAR MISS. 8217A1C8(segment, segment, source), called once per shot, gives every character within 150 cm of
//     the bullet's line the near-miss input (+6260 0x100). Not called for a shot of his.
//   - SOUND EVENTS. 8218AD40(position, radius) has no causer; its callers are told apart by the return address:
//     a detonation (825B88B8: the owner is noted by a hook on it), an exploding prop (825EE430: its causer is in r26),
//     the decoy (r30 = the weapon), a melee hit (r30 = the striker). Dropped when the causer is his.
//     Round 11: breaking glass (8222A008, hooked: the breaker is its r4, or the bullet impact's r25) is dropped for him.
//     NOT covered (no causer in reach of the call): a falling body, vehicles, sentry props.
//   - SIGHT. 82189F18 is one viewer / target test. A character who is neither his friend (Rare's 8251EBD0) nor on
//     the agent's side does not see him: a bystander without the guards' team bit would otherwise take an armed
//     human for an enemy (enemy seen, valid target, the UNCONCEAL lines). His own side still sees him, so a guard
//     who watches the agent shoot him reacts as to any friend shot in view, as in single player.
//   - His hits on his own side already do not count (round 8, 82545698); his triggers and level events are dropped in
//     counter_op_triggers.cpp, which uses PdzCounterOpIsHis below for "a thing of his".
// Doors raise no AI input (no sound event, no stimulus writer in door code). The alarm, the alarm run and "alerted by
// a friend" are consequences of the inputs above, never raised by a human directly.
#include <chrono>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)
#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sun 22 Nov: the causer's links read only where readable

uint32_t PdzCounterOpPlayer2(); // counter_op.cpp: 0 unless a Counter-Op game is running
uint32_t PdzCounterOpAgent();
uint32_t PdzCounterOpSpiderBot();              // counter_op_spider.cpp (round 11): the bot player 2 drives, or 0
bool PdzCounterOpSpiderIsHis(uint32_t object); // that bot, or an explosive of it
bool PdzCounterOpSpiderHiddenFrom(PPCContext &ctx, uint8_t *base, uint32_t viewer, uint32_t target);
// Round 13 a (counter_op_allies.cpp / counter_op_vehicles.cpp, see counter_op_rules13.h):
bool PdzCounterOpAlliesIgnoreP2();
void PdzCounterOpAllySeenSkip(PPCContext &ctx, uint8_t *base, uint32_t it, uint32_t out);
void PdzCounterOpRooftopsSeenSkip(PPCContext &ctx, uint8_t *base, uint32_t it, uint32_t out); // round 13 e, counter_op_rooftops.cpp
uint32_t PdzCounterOpRules13OwnerOf(const uint8_t *base, uint32_t object);
bool PdzCounterOpVehicleNoDamage(const uint8_t *base, uint32_t prop);

REXCVAR_DEFINE_BOOL(pdz_counterop_p2_unheard, true, "Gameplay",
                    "Counter-Operative (round 10, default on): nothing player 2 does is a stimulus for the guards: "
                    "his shots, steps, grenades, blasts and near misses are not heard, and bystanders do not take "
                    "him for an enemy. Off = round 9 (his gunfire alerts them)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_p2_unheard_log, true, "Gameplay",
                    "Counter-Operative: log what was kept from the guards (the first of each kind, then a count "
                    "every 10 s)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_p2_no_prop_damage, true, "Gameplay",
                    "Counter-Operative (round 11, default on): player 2's bullets, blows and blasts take no health "
                    "off a prop and break no part of it (consoles, cameras, alarms stay whole). Explosive props "
                    "(barrels), glass and doors are left as they are. Off = round 10 (he can destroy props)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_p2_props_scripted_only, true, "Gameplay",
                    "Counter-Operative (round 12, default on; needs pdz_counterop_p2_no_prop_damage): player 2 may "
                    "break ordinary props by any route (crates, barrels, lights, glass: cover is fair game); only a "
                    "prop with Rare's level script attached (setup messages, a registered trigger, a template "
                    "trigger that sends a level event) takes no damage from him, glass and doors included. Off = "
                    "round 11 (no prop but barrels, doors and glass takes his damage)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  uint32_t L32(const uint8_t *b, uint32_t a)
  {
    const uint8_t *p = b + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void S32(uint8_t *b, uint32_t a, uint32_t v)
  {
    b[a] = uint8_t(v >> 24);
    b[a + 1] = uint8_t(v >> 16);
    b[a + 2] = uint8_t(v >> 8);
    b[a + 3] = uint8_t(v);
  }
  constexpr uint32_t kSlots = 0x82E5D590, kSlotSize = 0x50, kSlotCount = 170; // 8218AAE4 / 8218AB3C
  constexpr uint32_t kSlotOwner = 0x04, kSlotFlags = 0x44, kMakesNoise = 0x4; // the source test at 8218AB84

  // Rare's owner link (the friend test 8251EBD0, 8251EBF8-8251EC38): object +0x68 bit 0x20, a list at +0x70 of
  // {type, value, next}; type 5 = the owner.
  uint32_t LinkOwner(const uint8_t *b, uint32_t o)
  {
    if (!(L32(b, o + 0x68) & 0x20))
    {
      return 0;
    }
    int guard = 0;
    // Sun 22 Nov (round 17): a level script's prop damage after a jetpack rider's death passed a causer whose link
    // chain led into unmapped memory (read of guest 49464251, a crash): each node is read only where readable.
    for (uint32_t n = L32(b, o + 0x70); Ptr(n) && guard < 32 && pdz::guestmem::Ok(b, n, 12); n = L32(b, n + 8), ++guard)
    {
      if (L32(b, n) == 5)
      {
        return L32(b, n + 4);
      }
    }
    return 0;
  }
  // Who caused it, as Rare's near miss resolves its source (8252CA0C-8252CA74): class (+0xC) 4 = a weapon: +0x5C;
  // 0x2C: [+0xB80] +0x5C; 0x2E: +0x5C4; else the +0x5C that round 8's friendly-fire rule reads, or the owner link.
  bool IsHis(const uint8_t *b, uint32_t o, uint32_t p2)
  {
    if (!p2 || !Ptr(o))
    {
      return false;
    }
    if (PdzCounterOpSpiderIsHis(o))
    {
      return true; // round 11: the spiderbot he drives, or its blast
    }
    if (o == p2)
    {
      return true;
    }
    if (!pdz::guestmem::Ok(b, o, 0x5C8)) // Sun 22 Nov: not an object we can read (a script's causer): not his
    {
      return false;
    }
    const uint32_t cls = L32(b, o + 0xC);
    if (cls == 0x2C && pdz::guestmem::Ok(b, o + 0xB80, 4))
    {
      const uint32_t w = L32(b, o + 0xB80);
      if (Ptr(w) && pdz::guestmem::Ok(b, w + 0x5C, 4) && L32(b, w + 0x5C) == p2)
      {
        return true;
      }
    }
    if (cls == 0x2E && L32(b, o + 0x5C4) == p2)
    {
      return true;
    }
    return L32(b, o + 0x5C) == p2 || LinkOwner(b, o) == p2 ||
           (cls == 4 && PdzCounterOpRules13OwnerOf(b, o) == p2); // round 13 a (G2): the gun of a turret he mans
  }
  uint32_t P2() { return REXCVAR_GET(pdz_counterop_p2_unheard) ? PdzCounterOpPlayer2() : 0; }
  thread_local int t_his_blast = 0; // inside 825B88B8 for a thing of player 2's
  thread_local int t_his_glass = 0; // inside 8222A008 (a world pane breaks) when the breaker is player 2 or his

  // Round 11 (ONLY JO). Rare's destructible part of a prop ("comp": prop +0x74 for the plain prop class, embedded in
  // others; comp +0 = the prop, +8 state 1 = whole / 3 = destroyed / 4 = destroy pending, +0x14 last attacker):
  //   825EFA40(comp, causer r4, f1 health damage, f2 armour damage) is the one funnel that takes health off
  //   (-> 825EFB30 -> at zero 825EE350 -> 825EE430, the destroy / explode routine). Its 24 callers: the bullet hit
  //   825EE9D0 (at 825EECA0, r25 = attacker, r27 = hit record), the blast 825F0128 (825F02B8, causer = the blast's
  //   r5), the blow 825EF240 (825EF38C), 825EF408, the vehicle / turret classes and the level scripts.
  //   82416360(model [prop +0x134], prop r4, f1, part r6, material r7, 1) breaks a part of a prop that has a damage
  //   model (comp +0x1C); r3 = 1 when that destroyed the prop (-> Destroyed trigger 82518128, state 3). It has no
  //   causer argument: from the bullet hit (return 825EEF5C) the attacker is r25, from the blow (825EF3D8) r28.
  // What stays: an explosive prop (the prop's template, vt +0x48 = 82520860 -> [[prop +0x64] +4], byte +0x21 == 1:
  // the test 825EE56C-825EE574 that makes 825EE430 spawn the blast), a door (vtable 820C27C8), and a hit on a glass
  // material (0xF / 0x11: Rare's own pane tests 8223FB6C-8223FB7C, 825994C4-825994D4).
  constexpr uint32_t kDoorVtable = 0x820C27C8, kPlainTemplateGetter = 0x82520860;
  bool GlassMaterial(uint32_t m) { return m == 0xF || m == 0x11; }
  bool PropKeepsDamage(const uint8_t *b, uint32_t prop)
  {
    const uint32_t vt = L32(b, prop);
    if (vt == kDoorVtable)
    {
      return true;
    }
    if (pdz::guestmem::Ok(b, vt, 0x4C) && L32(b, vt + 0x48) == kPlainTemplateGetter) // Sun 22 Nov night (audit): Ok
    {
      const uint32_t inst = L32(b, prop + 0x64);
      const uint32_t tpl = pdz::guestmem::Ok(b, inst, 8) ? L32(b, inst + 4) : 0;
      if (pdz::guestmem::Ok(b, tpl, 0x22) && b[tpl + 0x21] == 1)
      {
        return true; // explodes when destroyed: a barrel
      }
    }
    return false;
  }
  uint32_t PropRule() { return REXCVAR_GET(pdz_counterop_p2_no_prop_damage) ? PdzCounterOpPlayer2() : 0; }

  // Round 12 (item 9): the new prop rule. A prop the level script listens to, as Rare's trigger routine 825178B0
  // finds its messages (a record = kind byte, message type byte, event byte, flag byte):
  //   the two setup messages (prop +0x54 -> setup +0x1E / +0x22: 825178C8 / 825178F0), the triggers the script
  //   registered on it (+0x68 bit 0x10, the +0x70 list's type 4 entry: 8251790C-82517970), and its template's four
  //   ([[prop +0x64] +4] +0xCC .. +0xD8: 82517974-825179FC) when one sends a level event (message type 8: an
  //   ordinary breakable's template messages to other props are not counted).
  // Returns the reason, or nullptr for an ordinary prop.
  const char *PropScripted(const uint8_t *b, uint32_t prop, uint32_t &m1, uint32_t &m2)
  {
    const uint32_t setup = L32(b, prop + 0x54);
    m1 = pdz::guestmem::Ok(b, setup, 0x26) ? L32(b, setup + 0x1E) : 0; // Sun 22 Nov night (audit): Ok on each link
    m2 = pdz::guestmem::Ok(b, setup, 0x26) ? L32(b, setup + 0x22) : 0;
    if (m1 || m2)
    {
      return "Rare's level script is attached (its setup messages)";
    }
    if (L32(b, prop + 0x68) & 0x10)
    {
      int guard = 0;
      for (uint32_t n = L32(b, prop + 0x70); Ptr(n) && pdz::guestmem::Ok(b, n, 12) && guard < 32; n = L32(b, n + 8), ++guard)
      {
        if (L32(b, n) == 4)
        {
          if (Ptr(L32(b, n + 4)))
          {
            return "the level script registered a trigger on it";
          }
          break;
        }
      }
    }
    const uint32_t inst = L32(b, prop + 0x64);
    const uint32_t tpl = pdz::guestmem::Ok(b, inst, 8) ? L32(b, inst + 4) : 0;
    if (pdz::guestmem::Ok(b, tpl, 0xDC))
    {
      for (uint32_t off = 0xCC; off <= 0xD8; off += 4)
      {
        const uint32_t w = L32(b, tpl + off);
        if ((w >> 24) != 0 && ((w >> 16) & 0xFF) == 8)
        {
          return "its template sends a level event";
        }
      }
    }
    return nullptr;
  }
  bool NewPropRule() { return REXCVAR_GET(pdz_counterop_p2_props_scripted_only); }
  uint64_t g_prop_refused = 0;
  void PropRefused(const uint8_t *b, uint32_t prop, const char *what)
  {
    ++g_prop_refused;
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last >= std::chrono::seconds(1))
    {
      last = now;
      REXLOG_INFO("pdz_counterop: prop {:08X} (class {}, vtable {:08X}) not damaged: caused by player 2 ({}; {} "
                  "refusal(s) so far)",
                  prop, L32(b, prop + 0xC), L32(b, prop), what, g_prop_refused);
    }
  }

  // Round 12: the causer is his, or a plain prop he was the last to damage (a barrel he set off: its blast is his
  // doing; the plain class's destructible part is prop +0x74, its +0 the prop, +0x14 the last attacker).
  bool HisOrHisProp(const uint8_t *b, uint32_t causer, uint32_t p2)
  {
    if (IsHis(b, causer, p2))
    {
      return true;
    }
    // Sun 22 Nov night (audit): IsHis may have said no because the causer is not readable: every link tested.
    if (!NewPropRule() || !pdz::guestmem::Ok(b, causer, 0x78))
    {
      return false;
    }
    const uint32_t vt = L32(b, causer);
    if (!pdz::guestmem::Ok(b, vt, 0x4C) || L32(b, vt + 0x48) != kPlainTemplateGetter)
    {
      return false;
    }
    const uint32_t comp = L32(b, causer + 0x74);
    return pdz::guestmem::Ok(b, comp, 0x18) && L32(b, comp) == causer && IsHis(b, L32(b, comp + 0x14), p2);
  }
  // True = this damage of his is refused (and logged with the reason).
  bool RefuseHisPropDamage(const uint8_t *b, uint32_t prop, bool glass, const char *route)
  {
    if (!NewPropRule())
    {
      if (glass || PropKeepsDamage(b, prop))
      {
        return false; // round 11's rule
      }
      PropRefused(b, prop, route);
      return true;
    }
    uint32_t m1 = 0, m2 = 0;
    const char *why = PropScripted(b, prop, m1, m2);
    if (!why)
    {
      return false; // an ordinary prop: he may break it
    }
    static uint64_t refused = 0;
    static uint32_t last_prop = 0;
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    ++refused;
    if (prop != last_prop || now - last >= std::chrono::seconds(1))
    {
      last_prop = prop;
      last = now;
      REXLOG_INFO("pdz_counterop: prop {:08X} (class {}, vtable {:08X}, setup messages {:08X} {:08X}) not damaged by "
                  "player 2 ({}): {} ({} refusal(s) so far)",
                  prop, L32(b, prop + 0xC), L32(b, prop), m1, m2, route, why, refused);
    }
    return true;
  }

  // What was kept from the guards: 0 noise sources muted, 1 near misses, 2 sound events, 3 sight tests.
  uint64_t g_kept[4] = {};
  uint64_t g_kept_logged[4] = {};
  uint64_t g_calls = 0;
  void Kept(int kind, const char *first)
  {
    if (++g_kept[kind] == 1 && REXCVAR_GET(pdz_counterop_p2_unheard_log))
    {
      REXLOG_INFO("pdz_counterop: unheard: {} (the first this session; counts follow every 10 s while they rise)", first);
    }
  }
} // namespace

// counter_op_triggers.cpp: an object that is player 2 or a thing of his (his weapon, grenade, mine, blast).
bool PdzCounterOpIsHis(const uint8_t *base, uint32_t object)
{
  return IsHis(base, object, PdzCounterOpPlayer2());
}

// Round 12 (counter_op_only_jo.cpp, counter_op_pvp.cpp): 'object' is 'who' or a thing of who's (a weapon, a thrown or
// placed explosive, a blast), by the same owner links as above; the spiderbot rule is not part of it.
bool PdzCounterOpIsOf(const uint8_t *base, uint32_t object, uint32_t who)
{
  if (!Ptr(who) || !Ptr(object))
  {
    return false;
  }
  if (object == who)
  {
    return true;
  }
  if (!pdz::guestmem::Ok(base, object, 0x5C8)) // Sun 22 Nov night (audit): as IsHis: an object we can read
  {
    return false;
  }
  const uint32_t cls = L32(base, object + 0xC);
  if (cls == 0x2C && pdz::guestmem::Ok(base, object + 0xB80, 4))
  {
    const uint32_t w = L32(base, object + 0xB80);
    if (Ptr(w) && pdz::guestmem::Ok(base, w + 0x5C, 4) && L32(base, w + 0x5C) == who)
    {
      return true;
    }
  }
  if (cls == 0x2E && L32(base, object + 0x5C4) == who)
  {
    return true;
  }
  return L32(base, object + 0x5C) == who || LinkOwner(base, object) == who;
}

// [1] The hearing loop (one caller, 8218AFCC; two listeners a frame).
// Round 14 (Tue 17 Nov, the merge with the 0.9.5 release candidate): the stealth layer hooks this routine too
// (lab_stealth.cpp). One hook only can exist: it is lab_stealth.cpp's, which calls the two halves below around
// everything it does (Rare's routine included).
int PdzCounterOpHearingMute(uint8_t *base, uint32_t muted[8])
{
  const uint32_t p2 = P2();
  int n = 0;
  if (p2)
  {
    for (uint32_t i = 0; i < kSlotCount && n < 8; ++i)
    {
      const uint32_t s = kSlots + i * kSlotSize, f = L32(base, s + kSlotFlags);
      if ((f & 1) && (f & kMakesNoise) && IsHis(base, L32(base, s + kSlotOwner), p2))
      {
        S32(base, s + kSlotFlags, f & ~kMakesNoise);
        muted[n++] = s;
      }
    }
  }
  return n;
}
void PdzCounterOpHearingUnmute(uint8_t *base, const uint32_t muted[8], int n)
{
  const uint32_t p2 = P2();
  for (int i = 0; i < n; ++i)
  {
    S32(base, muted[i] + kSlotFlags, L32(base, muted[i] + kSlotFlags) | kMakesNoise);
  }
  if (n)
  {
    Kept(0, "player 2's perception slot makes no noise for Rare's hearing loop (8218AAC8): nobody hears his shots, "
            "steps or grenades");
  }
  if (p2 && REXCVAR_GET(pdz_counterop_p2_unheard_log) && (++g_calls % 1200) == 0)
  {
    bool rose = false;
    for (int k = 1; k < 4; ++k)
    {
      rose = rose || g_kept[k] != g_kept_logged[k];
    }
    if (rose)
    {
      REXLOG_INFO("pdz_counterop: unheard: kept from the guards so far: {} near-miss pass(es) of his shots, {} sound "
                  "event(s) of his blasts / blows, {} sight test(s) of bystanders on him (hearing loop calls with "
                  "his slot muted: {})",
                  g_kept[1], g_kept[2], g_kept[3], g_kept[0]);
      for (int k = 0; k < 4; ++k)
      {
        g_kept_logged[k] = g_kept[k];
      }
    }
  }
}

// [2] The near miss of one shot (sole caller 82240474 in the shot routine 8223FC00; nothing is returned).
REX_EXTERN(__imp__sub_8217A1C8);
REX_HOOK_RAW(sub_8217A1C8)
{
  if (IsHis(base, ctx.r5.u32, P2()))
  {
    Kept(1, "a shot of player 2's is not passed to Rare's near-miss test (8217A1C8): nobody near its line reacts");
    return;
  }
  __imp__sub_8217A1C8(ctx, base);
}

// [3] A weapon's detonation (r3 = its fire / detonate part, +4 = the weapon or projectile object): its sound event
// is raised at 825B8D20 with the object in no saved register, so the owner is noted here.
REX_EXTERN(__imp__sub_825B88B8);
REX_HOOK_RAW(sub_825B88B8)
{
  const uint32_t part = ctx.r3.u32;
  const bool his = Ptr(part) && IsHis(base, L32(base, part + 4), P2());
  t_his_blast += his ? 1 : 0;
  __imp__sub_825B88B8(ctx, base);
  t_his_blast -= his ? 1 : 0;
}

// [4] Rare's sound event (r3 -> position, f1 radius).
// (Round 14: hooked by lab_stealth.cpp, which asks this first; true = the event is dropped.)
bool PdzCounterOpSoundEventDropped(PPCContext &ctx, uint8_t *base)
{
  if (const uint32_t p2 = P2())
  {
    const uint32_t from = uint32_t(ctx.lr);
    const bool drop = (from == 0x825B8D24 && t_his_blast > 0) || (from == 0x825EE8C4 && IsHis(base, ctx.r26.u32, p2)) ||
                      (from == 0x825A2BD4 && IsHis(base, ctx.r30.u32, p2)) || (from == 0x821589E0 && ctx.r30.u32 == p2) ||
                      (from == 0x8222A234 && t_his_glass > 0); // (round 11: a pane he broke)
    if (drop)
    {
      Kept(2, from == 0x825B8D24   ? "the sound event of a detonation of player 2's (a grenade, a mine, a rocket) is dropped"
              : from == 0x825EE8C4 ? "the sound event of a prop player 2 blew up is dropped"
              : from == 0x825A2BD4 ? "the sound event of player 2's decoy is dropped"
              : from == 0x8222A234 ? "the sound event of a pane of glass player 2 broke is dropped (the pane breaks)"
                                   : "the sound event of player 2's melee blow is dropped");
      return true;
    }
  }
  return false;
}

// [5] One viewer / target sight test (sole caller 8218A490; entry r1 +0x6C = the target, +0x64 = the viewer; the
// byte answer in r3, 0 = not seen).
REX_EXTERN(sub_8251EBD0);
// (Round 14: hooked by lab_stealth.cpp, which asks this first; true = answered 'not seen' in r3.)
bool PdzCounterOpSightAnswered(PPCContext &ctx, uint8_t *base)
{
  const uint32_t p2 = P2();
  if (const uint32_t bot = PdzCounterOpSpiderBot();
      bot && L32(base, ctx.r1.u32 + 0x6C) == bot &&
      PdzCounterOpSpiderHiddenFrom(ctx, base, L32(base, ctx.r1.u32 + 0x64), bot))
  {
    Kept(3, "the spiderbot player 2 drives is not seen by a character who would take it for an enemy (Rare's sight "
            "test 82189F18 answered 'not seen')");
    ctx.r3.u64 = 0;
    return true;
  }
  if (p2 && L32(base, ctx.r1.u32 + 0x6C) == p2)
  {
    const uint32_t viewer = L32(base, ctx.r1.u32 + 0x64), agent = PdzCounterOpAgent();
    bool sees = viewer == agent || viewer == p2 || !Ptr(viewer);
    if (!sees)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = viewer;
      frame.ctx.r4.u64 = p2;
      sub_8251EBD0(frame.ctx, base); // his own side: sees him as before
      sees = (frame.ctx.r3.u32 & 0xFF) != 0;
    }
    if (!sees && Ptr(agent) && !PdzCounterOpAlliesIgnoreP2()) // round 13 a (G1): her allies do not see him either
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = viewer;
      frame.ctx.r4.u64 = agent;
      sub_8251EBD0(frame.ctx, base); // the agent's allies: he is their enemy, as any guard is
      sees = (frame.ctx.r3.u32 & 0xFF) != 0;
    }
    if (!sees)
    {
      Kept(3, "a character who is neither on player 2's side nor on the agent's does not see him (Rare's sight test "
              "82189F18 answered 'not seen'): an armed human is no enemy sighting for a bystander");
      ctx.r3.u64 = 0;
      return true;
    }
  }
  return false;
}

// [6] Health off a prop (comp r3, causer r4, f1 / f2). No return value.
REX_EXTERN(__imp__sub_825EFA40);
REX_HOOK_RAW(sub_825EFA40)
{
  if (const uint32_t p2 = PropRule())
  {
    const uint32_t comp = ctx.r3.u32;
    if (Ptr(comp) && HisOrHisProp(base, ctx.r4.u32, p2))
    {
      const uint32_t prop = L32(base, comp);
      // From the bullet hit (the call at 825EECA0) r27 is the hit record; +0x6C its material.
      const uint32_t from = uint32_t(ctx.lr);
      const bool glass = from == 0x825EECA4 && Ptr(ctx.r27.u32) && GlassMaterial(L32(base, ctx.r27.u32 + 0x6C));
      if (PdzCounterOpVehicleNoDamage(base, prop)) // round 13 a (G2): a vehicle, or a gun mounted on one
      {
        return;
      }
      if (Ptr(prop) && RefuseHisPropDamage(base, prop, glass,
                                           from == 0x825EECA4   ? "health, a shot"
                                           : from == 0x825F02BC ? "health, a blast"
                                           : from == 0x825EF390 ? "health, a blow"
                                                                : "health"))
      {
        return;
      }
    }
  }
  __imp__sub_825EFA40(ctx, base);
}

// [7] A part of a prop's damage model breaks (r4 = the prop, r7 = the material; r3 = 1: the prop is destroyed).
REX_EXTERN(__imp__sub_82416360);
REX_HOOK_RAW(sub_82416360)
{
  if (const uint32_t p2 = PropRule())
  {
    const uint32_t from = uint32_t(ctx.lr);
    const uint32_t causer = from == 0x825EEF5C ? ctx.r25.u32 : from == 0x825EF3D8 ? ctx.r28.u32 : 0;
    const uint32_t prop = ctx.r4.u32;
    if (causer && HisOrHisProp(base, causer, p2) && Ptr(prop) &&
        RefuseHisPropDamage(base, prop, GlassMaterial(ctx.r7.u32),
                            from == 0x825EEF5C ? "a part of its damage model, a shot"
                                               : "a part of its damage model, a blow"))
    {
      ctx.r3.u64 = 0; // not destroyed
      return;
    }
  }
  __imp__sub_82416360(ctx, base);
}

// [8] A pane of the level's glass breaks: 8222A008(glass manager r3, breaker r4, pane r5, position r6, r7, f1).
// Callers: the melee blow 82158C38 (r4 = the striker), a thrown / flying weapon 825B6714 (r4 = that object), the
// bullet impact 8223FB9C in 8223F6B8 (r4 = 0; the shot's source, a weapon or a character, is that function's r25,
// still live here). The pane's sound event is raised at 8222A230 (return 8222A234) with no cause: noted here.
REX_EXTERN(__imp__sub_8222A008);
REX_HOOK_RAW(sub_8222A008)
{
  bool his = false;
  if (const uint32_t p2 = P2())
  {
    const uint32_t breaker = ctx.r4.u32 ? ctx.r4.u32 : (uint32_t(ctx.lr) == 0x8223FBA0 ? ctx.r25.u32 : 0);
    his = IsHis(base, breaker, p2);
  }
  t_his_glass += his ? 1 : 0;
  __imp__sub_8222A008(ctx, base);
  t_his_glass -= his ? 1 : 0;
}

// [6] The seen-list iterator (r3 = the iterator: +4 the viewer's slot, +8 the cursor, +0xC a mask; r4 -> the pair's
// flags; returns the next object the viewer has on record, 0 at the end). Every consumer of "what do I see" reads the
// list through it (the NPC sight update at 825806D0 / 825809D0, the spiderbots at 825C91D8, twelve more). A pair's
// record is only rebuilt when the viewer's next sight pass is complete, so a viewer who saw the bot before player 2
// took it would find it once more: the iterator steps over it for him.
REX_EXTERN(__imp__sub_8218B498);
REX_HOOK_RAW(sub_8218B498)
{
  const uint32_t it = ctx.r3.u32, out = ctx.r4.u32;
  __imp__sub_8218B498(ctx, base);
  PdzCounterOpRooftopsSeenSkip(ctx, base, it, out); // round 13 e: Rooftops, Jack never finds the hidden guard player 2 drives in it
  PdzCounterOpAllySeenSkip(ctx, base, it, out); // round 13 a (G1): an ally of the agent never finds player 2 in it
  const uint32_t bot = PdzCounterOpSpiderBot();
  if (!bot || ctx.r3.u32 != bot || !Ptr(it))
  {
    return;
  }
  const uint32_t slot = L32(base, it + 4);
  const uint32_t viewer = slot < kSlotCount ? L32(base, kSlots + slot * kSlotSize + kSlotOwner) : 0;
  if (!PdzCounterOpSpiderHiddenFrom(ctx, base, viewer, bot))
  {
    return;
  }
  Kept(3, "the spiderbot player 2 drives is left out of the seen list of a character who would take it for an enemy "
          "(Rare's iterator 8218B498)");
  ctx.r3.u64 = it; // the cursor (+8) is already past the bot (8218B574-8218B57C)
  ctx.r4.u64 = out;
  __imp__sub_8218B498(ctx, base);
  PdzCounterOpAllySeenSkip(ctx, base, it, out); // round 13 a (G1)
}
