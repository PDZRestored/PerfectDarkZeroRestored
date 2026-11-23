// Counter-Operative round 13: Mansion's last fight. Built only with PDZ_COUNTEROP.
//
// Mansion | Infiltration (mission 4, level 'mans', level script vtable 0x820A3BD0). The last part is the "deathmatch"
// against Mai Hem in a closed arena (around x 1000, y 2220, z -27300), with eight terracotta warriors standing round it.
//   - Mai Hem: the level script keeps her in a two-slot holder at script +0x970 ([holder +0x2C] + 8 x slot + 4; slot 1
//     is the second Mai Hem of a co-op game). Her AI script is MAI_HEM_SCRIPT (kind 8, vtable 0x82091F0C, tick
//     821BD3C0). The "phasing" is HER OWN AI SCRIPT: it reads her health as a share of the start health (global
//     0x82D3EA84), and at 0.65 / 0.5 / 0.45 / 0.25 (and six small steps between 0.45 and 0.25) it vanishes (821BDBF8:
//     level message 8,0xD0 then 8,0x9A), moves her to a random pad and shows her again (821BDB18: Rare's move 825AD148,
//     level message 8,0x99). The level script only hides / shows the sender of those messages (82338CA4-82338E54).
//   - Terracotta warriors: script +0x578 + 8 x n (n 0-7). They stand as statues with no AI script; the level messages
//     8,0x2D / 8,0x2E / 8,0x2F wake one / two / all (82339638: their setup's COMBAT script is pushed, woken mask at
//     script +0x418).
// OPTION A (pdz_counterop_mansion_maihem): player 2 plays Mai Hem.
//   - The fight is "on" while the agent stands in the arena, Mai Hem (slot 0) lives and her AI script runs. Then
//     nobody else is picked: a guard he still has elsewhere in the level is handed back to Rare's AI (no death) and he
//     takes Mai Hem as soon as she is "in" (her AI script not vanishing / gone / coming back / waiting).
//   - While he stands in for her the real Mai Hem is hidden and her AI script does not tick, so it never phases and
//     the level script (which only reacts to her script's wait flag) waits for nothing: no stall, no swap.
//   - His health is her share: pdz_counterop_mansion_maihem_health x (her health / her start health). When his share
//     reaches the next of Rare's marks (0.65, 0.5, 0.45, the six steps down to 0.2833, 0.25) she is HANDED BACK:
//     he leaves the world without a death, she is shown where he stood with his share of her health, and Rare's own
//     scripts run the phase (the vanish, the terracotta warriors, the new pad). When she is "in" again he takes her
//     again. Before the last mark he cannot die (as Rare's Mai Hem cannot skip a mark): a hit that would end him
//     hands her back at the mark instead. Below 0.25 his death is her death, through Rare's kill (counter_op.cpp's
//     usual path), and the level script ends the fight as it always does.
// What else is here:
//   pdz_counterop_test_mansion_log    TESTING TOOL: the fight's state, once a second and on each change.
//   pdz_counterop_test_mansion_health TESTING TOOL: each change to a value in (0, 1]: Mai Hem's health is set to that
//                                     share of her start health (as if the agent had shot her that far down).
//   pdz_counterop_test_mansion_kill   TESTING TOOL: each change to 1: Rare's kill on Mai Hem, credited to the agent.
// Call-ins: counter_op.cpp (the level tick, Reset, the class allow-ins beside helper d's for the twins, the pick, the
// health, the give-back), each marked "round 13 g". Interface: counter_op_mansion.h.
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/system/kernel_state.h>

#include "counter_op_mansion.h"
#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

REXCVAR_DEFINE_BOOL(pdz_counterop_mansion_maihem, true, "Gameplay",
                    "Counter-Operative (round 13, Mansion, default on): in the last fight player 2 plays Mai Hem. At each of "
                    "Rare's health marks she is handed back to Rare's AI for the phase (her vanish, the terracotta "
                    "warriors, her new place) and he takes her again when she is back; below the last mark his death "
                    "is her death and the fight ends as Rare ends it. Off = he keeps the guard he has (he sits the "
                    "fight out)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_mansion_instant, true, "Gameplay",
                    "Counter-Operative on Mansion (round 16, default on): when Mai Hem's fight begins while player 2 "
                    "is another guard, he becomes Mai Hem on the next tick (no 2.5 s wait out of the world); and once "
                    "Mai Hem is dead no other guard is ever given to him again on that level (through the end "
                    "cutscene and the win screen he watches the agent). Off = round 15")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_mansion_terracotta, false, "Gameplay",
                    "Counter-Operative (round 13, Mansion, with pdz_counterop_mansion_maihem): while Mai Hem is gone "
                    "(Rare's phases) player 2 plays one of the terracotta warriors Rare has woken: unarmed (Rare's "
                    "unarmed item: punch), the guard rule's health; when it dies, the next woken one, or Mai Hem "
                    "once she is back. Off = he watches until she is back")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_mansion_maihem_health, 600.0, "Gameplay",
                      "Counter-Operative (round 13, Mansion): player 2's health as a full-health Mai Hem (he gets "
                      "her share of it each time he takes her). Rare's own Mai Hem has 1000 / 1500 / 1200 by "
                      "difficulty against the agent's guns at guard damage; between the two players the "
                      "multiplayer damage applies")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_test_mansion_log, false, "Testing",
                    "TESTING TOOL (counter-op, Mansion): the last fight's state is logged once a second and on each "
                    "change: the level script's flag words, Mai Hem (both slots), her AI script's phase, the eight "
                    "terracotta warriors, both players")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_test_mansion_health, 0.0, "Testing",
                      "TESTING TOOL (counter-op, Mansion): each change to a value above 0 and at most 1: Mai Hem's "
                      "health is set to that share of her start health, as if the agent had shot her that far down")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_INT32(pdz_counterop_test_mansion_kill, 0, "Testing",
                     "TESTING TOOL (counter-op, Mansion): each change to 1: Rare's kill on Mai Hem, credited to the "
                     "agent; each change to 2 (or 3, 4: the same again): Rare's kill on every woken terracotta warrior "
                     "that lives, credited to the agent")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_mansion_auto, false, "Testing",
                    "TESTING TOOL (counter-op, Mansion): the last fight drives itself for a scripted run: a few "
                    "seconds after player 2 takes Mai Hem his health is put just under her next mark (in her last "
                    "phase: to his floor, which is his own death), and woken terracotta warriors are killed for the "
                    "agent 6 s after they wake")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_test_mansion_p2_share, 0.0, "Testing",
                      "TESTING TOOL (counter-op, Mansion): each change to a value above 0 and below 1 while player 2 "
                      "is Mai Hem: his health is set to that share of pdz_counterop_mansion_maihem_health, as if the "
                      "agent had shot him that far down")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8, kLevel = 0x1C, kLevelTarget = 0x2D8, kLevelTargetScript = 0x74;
  constexpr uint32_t kScriptVtable = 0x820A3BD0, kMaiHemScriptVtable = 0x82091F0C;
  constexpr uint32_t kMaiHolder = 0x970, kWarriors = 0x578, kFlagBlock = 0x3EC, kStartHealth = 0x82D3EA84;
  constexpr uint32_t kLevelState = 0x82E426F8;
  constexpr uint32_t kPosition = 48, kSetup = 84, kHealth = 120, kArmour = 124, kTeamBits = 128, kHidden = 6272,
                     kBodyId = 5280, kHeadId = 5284, kParticipantOf = 6560, kSetupId = 52;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    uint32_t v;
    std::memcpy(&v, base + a, 4);
    return __builtin_bswap32(v);
  }
  uint16_t Load16(const uint8_t *base, uint32_t a) { return uint16_t((base[a] << 8) | base[a + 1]); }
  float LoadFloat(const uint8_t *base, uint32_t a)
  {
    const uint32_t bits = Load32(base, a);
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

  std::string GuestString(const uint8_t *base, uint32_t p, size_t max = 63)
  {
    std::string out;
    for (size_t i = 0; Ptr(p) && i < max; ++i)
    {
      const char ch = char(base[p + i]);
      if (!ch)
      {
        break;
      }
      out += (ch < 32 || ch > 126) ? '?' : ch;
    }
    return out;
  }

  uint32_t Script(const uint8_t *base)
  {
    const uint32_t level = Load32(base, kGame + kLevel);
    const uint32_t target = pdz::guestmem::Ok(base, level, kLevelTarget + 4) ? Load32(base, level + kLevelTarget) : 0;
    const uint32_t script = pdz::guestmem::Ok(base, target, kLevelTargetScript + 4) ? Load32(base, target + kLevelTargetScript) : 0;
    return pdz::guestmem::Ok(base, script, 4) && Load32(base, script) == kScriptVtable ? script : 0;
  }

  // Mai Hem of a slot (0, or 1 = the second one of a co-op game), dead or alive (Rare's own reader 822F5410 answers
  // "none" for a dead one).
  uint32_t MaiHem(const uint8_t *base, uint32_t script, int slot)
  {
    // Sun 22 Nov night (audit): the holder's array and Mai Hem read only where readable.
    const uint32_t array = pdz::guestmem::Ok(base, script + kMaiHolder + 0x2C, 4) ? Load32(base, script + kMaiHolder + 0x2C) : 0;
    const uint32_t c = pdz::guestmem::Ok(base, array + uint32_t(slot) * 8 + 4, 4) && Ptr(array)
                           ? Load32(base, array + uint32_t(slot) * 8 + 4)
                           : 0;
    return pdz::guestmem::Ok(base, c, kHidden + 4) ? c : 0;
  }

  // The script of a given class on a character's stack: the current one ([c +0x18D4 +0x528] -0x84, 82171048), else
  // the stack's list (821710B8: nodes {script, _, next} from [-0xC] to [-8]).
  uint32_t StackScript(const uint8_t *base, uint32_t c, uint32_t vtable, uint32_t &current_vtable)
  {
    current_vtable = 0;
    // Sun 22 Nov night (audit): every link of the AI stack read only where readable.
    if (!pdz::guestmem::Ok(base, c + 0x18D4 + 0x528, 4))
    {
      return 0;
    }
    const uint32_t top = Load32(base, c + 0x18D4 + 0x528);
    if (!Ptr(top) || !pdz::guestmem::Ok(base, top - 0x84, 0x84))
    {
      return 0;
    }
    const uint32_t cur = Load32(base, top - 0x84);
    current_vtable = pdz::guestmem::Ok(base, cur, 4) ? Load32(base, cur) : 0;
    if (current_vtable == vtable)
    {
      return cur;
    }
    uint32_t node = Load32(base, top - 0xC);
    const uint32_t end = Load32(base, top - 8);
    for (int n = 0; n < 32 && pdz::guestmem::Ok(base, node, 12) && node != end; ++n)
    {
      const uint32_t obj = Load32(base, node);
      if (pdz::guestmem::Ok(base, obj, 4) && Load32(base, obj) == vtable)
      {
        return obj;
      }
      node = Load32(base, node + 8);
    }
    return 0;
  }

  // Sun 22 Nov night (audit): a vtable slot of a character, or 0 unless the character is readable, its vtable is in
  // the game image and the slot is not the runtime's pure-virtual handler.
  uint32_t Slot(const uint8_t *base, uint32_t c, uint32_t off)
  {
    if (!pdz::guestmem::Ok(base, c, 4))
    {
      return 0;
    }
    const uint32_t vt = Load32(base, c);
    if (vt < 0x82000000u || vt >= 0x83000000u || !pdz::guestmem::Ok(base, vt + off, 4))
    {
      return 0;
    }
    const uint32_t fn = Load32(base, vt + off);
    return fn == 0x82AA2BC8u ? 0 : fn;
  }

  double HealthGetter(PPCContext &ctx, uint8_t *base, uint32_t c)
  {
    const uint32_t slot = Slot(base, c, 0x14C); // Sun 22 Nov night (audit)
    PPCFunc *fn = slot ? pdz::GuestFunction(slot, "mansion: health getter") : nullptr;
    if (!fn)
    {
      return -1.0;
    }
    rex::CallFrame frame(ctx); // copies r1 / r13 only
    frame.ctx.r3.u64 = c;
    frame.ctx.r4.u64 = 0;
    fn(frame.ctx, base);
    return frame.ctx.f1.f64;
  }

  std::string Id(const uint8_t *base, uint32_t c)
  {
    // (Sat 21 Nov evening: every step tested as mapped memory)
    const uint32_t setup = pdz::guestmem::Ok(base, c, kSetup + 4) ? Load32(base, c + kSetup) : 0;
    const uint32_t p = pdz::guestmem::Ok(base, setup, kSetupId + 4) ? Load32(base, setup + kSetupId) : 0;
    return pdz::guestmem::Ok(base, p, 4) ? GuestString(base, p, 4) : std::string();
  }

  uint32_t g_script = 0;
  uint32_t g_sum = 0xFFFFFFFFu;
  double g_health_tool = 0.0, g_share_tool = 0.0;
  int32_t g_kill_tool = 0;
  bool g_bodies_logged = false;
  std::chrono::steady_clock::time_point g_next, g_soonest;

  void BodiesLog(const uint8_t *base)
  {
    const uint32_t lists = Load32(base, kGame + 8);
    for (const uint32_t list_offset : {0u, 0xCu})
    {
      const uint32_t list = Ptr(lists) ? Load32(base, lists + list_offset) : 0;
      const uint32_t entries = Ptr(list) ? Load32(base, list) : 0;
      const int32_t count = Ptr(list) ? int32_t(Load32(base, list + 8)) : 0;
      for (int32_t i = 0; Ptr(entries) && i < count && i < 1024; ++i)
      {
        const uint32_t e = Load32(base, entries + uint32_t(i) * 4);
        if (!Ptr(e))
        {
          continue;
        }
        const uint32_t id = Load32(base, e);
        if (list_offset == 0 && (id == 13 || id == 33 || id == 52 || id == 53))
        {
          REXLOG_INFO("pdz_counterop: mansion: body {} entry {:08X}: model ref '{}', animation set '{}' (+64 {:08X}), "
                      "hands name '{}' (+20 {:08X}), ragdoll ref '{}', model +68 {:08X}",
                      id, e, GuestString(base, Load32(base, e + 4)), GuestString(base, Load32(base, e + 12)),
                      Load32(base, e + 64), GuestString(base, Load32(base, e + 16)), Load32(base, e + 20),
                      GuestString(base, Load32(base, e + 32)), Load32(base, e + 68));
        }
        if (list_offset == 0xC && (id == 106 || id == 1))
        {
          REXLOG_INFO("pdz_counterop: mansion: head {} entry {:08X}: model ref '{}', model +12 {:08X}", id, e,
                      GuestString(base, Load32(base, e + 4)), Load32(base, e + 12));
        }
      }
    }
  }

  std::string CharacterText(PPCContext &ctx, uint8_t *base, uint32_t c)
  {
    uint32_t cur_vt = 0;
    StackScript(base, c, 0, cur_vt);
    return fmt::format("{:08X} vt {:08X} id '{}' body {} head {} at ({:.0f}, {:.0f}, {:.0f}) health {:.1f} armour {:.1f} "
                       "getter {:.1f} hidden {:08X} dead {:08X} flags +0x187C {:08X} {:08X} masks {:04X}/{:04X}/{:04X} "
                       "script vt {:08X} gun prop {:08X}",
                       c, Load32(base, c), Id(base, c), Load32(base, c + kBodyId), Load32(base, c + kHeadId),
                       LoadFloat(base, c + kPosition), LoadFloat(base, c + kPosition + 4),
                       LoadFloat(base, c + kPosition + 8), LoadFloat(base, c + kHealth), LoadFloat(base, c + kArmour),
                       HealthGetter(ctx, base, c), Load32(base, c + kHidden), Load32(base, c + 0x44),
                       Load32(base, c + 0x187C), Load32(base, c + 0x1880), Load16(base, c + kTeamBits),
                       Load16(base, c + kTeamBits + 2), Load16(base, c + kTeamBits + 4), cur_vt,
                       Load32(base, c + 0xFF0));
  }
  // ---- option A: the fight's state, refreshed by the tick ----
  struct Fight
  {
    uint32_t mai = 0;           // Mai Hem (slot 0) once seen with the option on; cleared by Reset
    bool on = false;            // the fight is on this tick
    bool in = false;            // she can be taken now
    bool giveback = false;      // counter_op.cpp is asked to hand player 2's body back this tick
    bool giveback_boss = false; // ... and that body is Mai Hem
    float at[3] = {};           // where he stood
    float share = 1.0f;         // his share of her health at the hand-back
    int32_t back_phase = -1, back_steps = -1; // her script's phase / step count at the last hand-back
    float passed_mark = 2.0f;   // a mark her script did not act on (never asked twice)
    bool told_on = false;
    bool over = false, told_over = false; // round 16 (item 8b): Mai Hem is dead: the level's last body is gone
    int wait_lines = 0;                   // round 16 (item 8a): why the fight is not on yet, after the cutscene
    std::chrono::steady_clock::time_point wait_line_at{};
    float mirrored = -1.0f;
    uint32_t warrior[8] = {};   // the terracotta warriors (script +0x578 + 8 x n), refreshed while the fight is on
    uint8_t woken = 0;          // the level script's woken mask (+0x418)
    bool warriors_ok = false;   // pdz_counterop_mansion_terracotta: a woken warrior may be taken now
    std::chrono::steady_clock::time_point back_at{};
  };
  Fight f;
  uint32_t g_scratch = 0;

  void StoreFloat(uint8_t *base, uint32_t a, float v)
  {
    uint32_t bits;
    std::memcpy(&bits, &v, 4);
    bits = __builtin_bswap32(bits);
    std::memcpy(base + a, &bits, 4);
  }

  void SetHealth(PPCContext &ctx, uint8_t *base, uint32_t c, float health)
  {
    const uint32_t slot = Slot(base, c, 0x158); // Sun 22 Nov night (audit)
    PPCFunc *set = slot ? pdz::GuestFunction(slot, "mansion: health setter") : nullptr;
    if (!set)
    {
      return;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c;
    frame.ctx.f1.f64 = double(health);
    frame.ctx.r5.u64 = 0;
    set(frame.ctx, base);
  }

  // The next of Rare's marks below her script's phase (821BD2F0; the six steps of phase 4: 821BD808-821BD8E0).
  float NextMark(const uint8_t *base, uint32_t mai_script)
  {
    const int32_t phase = int32_t(Load32(base, mai_script + 0x88)), steps = int32_t(Load32(base, mai_script + 0xD0));
    switch (phase)
    {
    case 0:
    case 1:
      return 0.65f;
    case 2:
      return 0.5f;
    case 3:
      return 0.45f;
    case 4:
      return steps >= 1 && steps < 6 ? 0.45f - 0.033333f * float(steps) : steps >= 6 ? 0.25f : 0.45f;
    default:
      return 0.0f; // phase 5: no mark left, his death is hers
    }
  }
} // namespace

namespace counter_op_mansion
{
  void Reset() { f = Fight(); }
  namespace
  {
    int WarriorIndex(uint32_t character)
    {
      for (int n = 0; character && n < 8; ++n)
      {
        if (f.warrior[n] == character)
        {
          return n;
        }
      }
      return -1;
    }
  } // namespace
  bool IsBoss(uint32_t character) { return character && (character == f.mai || WarriorIndex(character) >= 0); }
  bool Pickable(uint32_t character)
  {
    if (!f.on || !character)
    {
      return false;
    }
    if (character == f.mai)
    {
      return f.in;
    }
    const int n = WarriorIndex(character);
    return n >= 0 && f.warriors_ok && !f.in && (f.woken & (1u << n));
  }
  uint32_t GunFor(uint32_t character, uint32_t gun)
  {
    return WarriorIndex(character) >= 0 ? 15u : gun; // Rare's unarmed item (what its strip 8254BF38 gives)
  }
  bool FightHolds() { return f.on; }
  float SpawnHealth(const uint8_t *base, uint32_t character, float hp)
  {
    if (character != f.mai || !character || !f.on)
    {
      return hp; // a warrior keeps the guard rule's health
    }
    const float start = LoadFloat(base, kStartHealth);
    float share = start > 0.0f ? LoadFloat(base, character + kHealth) / start : 1.0f;
    share = !(share > 0.02f) ? 0.02f : share > 1.0f ? 1.0f : share;
    float full = float(REXCVAR_GET(pdz_counterop_mansion_maihem_health));
    full = !(full >= 50.0f) ? 50.0f : full > 5000.0f ? 5000.0f : full;
    const float mine = full * share < 3.0f ? 3.0f : full * share;
    REXLOG_INFO("pdz_counterop: mansion: player 2 takes Mai Hem {:08X}: her health {:.1f} of {:.1f} = a share of {:.3f}: "
                "his health {:.1f} (of {:.0f}; the guard rule's {:.1f} is not used)",
                character, LoadFloat(base, character + kHealth), start, share, mine, full, hp);
    f.mirrored = -1.0f;
    return mine;
  }
  bool GiveBackNow() { return f.giveback; }
  bool GiveBackForBoss() { return f.giveback && !f.giveback_boss && REXCVAR_GET(pdz_counterop_mansion_instant); }
  bool Over() { return f.over && REXCVAR_GET(pdz_counterop_mansion_instant); }
  void AfterGiveBack(PPCContext &ctx, uint8_t *base)
  {
    const bool boss = f.giveback_boss;
    f.giveback = false;
    f.giveback_boss = false;
    f.back_at = std::chrono::steady_clock::now();
    if (!boss || !Ptr(f.mai))
    {
      return;
    }
    const uint32_t mai = f.mai;
    if (!g_scratch)
    {
      g_scratch = REX_KERNEL_MEMORY()->SystemHeapAlloc(0x40);
    }
    PPCFunc *move = pdz::GuestFunction(0x825AD148, "mansion: move");
    const float before[3] = {LoadFloat(base, mai + kPosition), LoadFloat(base, mai + kPosition + 4),
                             LoadFloat(base, mai + kPosition + 8)};
    if (g_scratch && move && (f.at[0] != 0.0f || f.at[2] != 0.0f))
    {
      StoreFloat(base, g_scratch, f.at[0]);
      StoreFloat(base, g_scratch + 4, f.at[1]);
      StoreFloat(base, g_scratch + 8, f.at[2]);
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = mai;
      frame.ctx.r4.u64 = g_scratch;
      frame.ctx.r5.u64 = 0;
      move(frame.ctx, base);
    }
    const float start = LoadFloat(base, kStartHealth);
    const float health_before = LoadFloat(base, mai + kHealth);
    const float want = start * (f.share < 0.01f ? 0.01f : f.share);
    SetHealth(ctx, base, mai, want);
    // Her AI script must go on where it stopped. If Rare's restore started it afresh (phase 0), it would run every
    // phase again from the top, healing her to each mark: its phase and step count are put back.
    uint32_t cur_vt = 0;
    const uint32_t ms = StackScript(base, mai, kMaiHemScriptVtable, cur_vt);
    if (Ptr(ms))
    {
      const int32_t phase = int32_t(Load32(base, ms + 0x88)), steps = int32_t(Load32(base, ms + 0xD0));
      const bool restarted = phase < f.back_phase || (phase == f.back_phase && steps < f.back_steps);
      if (restarted)
      {
        const uint32_t ph = __builtin_bswap32(uint32_t(f.back_phase)), st = __builtin_bswap32(uint32_t(f.back_steps));
        std::memcpy(base + ms + 0x88, &ph, 4);
        std::memcpy(base + ms + 0x8C, &ph, 4);
        std::memcpy(base + ms + 0xD0, &st, 4);
      }
      REXLOG_INFO("pdz_counterop: mansion: her AI script after the restore: {:08X} (current script class {:08X}), state "
                  "{}, phase {} steps {} (at the hand-back: phase {} steps {}){}",
                  ms, cur_vt, Load32(base, ms + 0xC), phase, steps, f.back_phase, f.back_steps,
                  restarted ? ": STARTED AFRESH: phase and steps put back" : ": it goes on where it stopped");
    }
    else
    {
      REXLOG_WARN("pdz_counterop: mansion: no MAI_HEM script on her stack after the restore (current script class "
                  "{:08X})",
                  cur_vt);
    }
    REXLOG_INFO("pdz_counterop: mansion: Mai Hem {:08X} handed back to Rare's AI: moved from ({:.0f}, {:.0f}, {:.0f}) to "
                "where player 2 stood ({:.0f}, {:.0f}, {:.0f}), now at ({:.0f}, {:.0f}, {:.0f}); health {:.1f} -> {:.1f} "
                "(his share {:.3f} of her start health {:.1f}); hidden {:08X}; her script's phase was {} (steps {})",
                mai, before[0], before[1], before[2], f.at[0], f.at[1], f.at[2], LoadFloat(base, mai + kPosition),
                LoadFloat(base, mai + kPosition + 4), LoadFloat(base, mai + kPosition + 8), health_before,
                LoadFloat(base, mai + kHealth), f.share, start, Load32(base, mai + kHidden), f.back_phase, f.back_steps);
  }
} // namespace counter_op_mansion

namespace
{
  // Option A's part of the tick: is the fight on, can she be taken, must a body be handed back.
  void FightTick(PPCContext &ctx, uint8_t *base, uint32_t script, uint32_t p1c, uint32_t p2c, uint32_t current_guard,
                 bool out, bool cutscene)
  {
    f.on = false;
    f.in = false;
    f.giveback = false;
    f.giveback_boss = false;
    if (!REXCVAR_GET(pdz_counterop_mansion_maihem) || !script || !Ptr(p1c) || !Ptr(p2c))
    {
      return;
    }
    const uint32_t mai = MaiHem(base, script, 0);
    if (!mai)
    {
      if (f.told_on && !f.over)
      {
        f.over = true; // round 16 (item 8b): she was in her fight and is gone from the script's slot
        REXLOG_INFO("pdz_counterop: mansion: Mai Hem is gone from the level script's slot after her fight: the last "
                    "fight is over");
      }
      return;
    }
    uint32_t cur_vt = 0;
    const uint32_t ms = StackScript(base, mai, kMaiHemScriptVtable, cur_vt);
    const bool alive = LoadFloat(base, mai + kHealth) > 0.0f && !(Load32(base, mai + 0x44) & 8);
    const float ay = LoadFloat(base, p1c + kPosition + 4), az = LoadFloat(base, p1c + kPosition + 8);
    const bool agent_in_arena = ay > 1500.0f && az < -20000.0f; // the arena: y 2200, z -26300 .. -28700
    // Round 16 (item 8b): Mai Hem seen in her fight and now dead = the level's last body is gone for good.
    if (f.told_on && !alive && !f.over)
    {
      f.over = true;
      REXLOG_INFO("pdz_counterop: mansion: Mai Hem {:08X} is dead: the last fight is over; {}", mai,
                  REXCVAR_GET(pdz_counterop_mansion_instant)
                      ? "player 2 gets no other body on this level (he watches the agent to the end)"
                      : "pdz_counterop_mansion_instant off: the picks go on (round 15)");
    }
    if (!alive || !Ptr(ms) || Load32(base, ms + 0xC) != 1 || !agent_in_arena || cutscene || (base[kLevelState] & 3))
    {
      // Round 16 (item 8a): log why, after the cutscene, while the agent is in the arena and she lives.
      const auto now = std::chrono::steady_clock::now();
      if (alive && Ptr(ms) && (ay > 1500.0f || az < -20000.0f) && !cutscene && !f.told_on && f.wait_lines < 12 && now - f.wait_line_at >= std::chrono::milliseconds(400))
      {
        f.wait_line_at = now;
        ++f.wait_lines;
        REXLOG_INFO("pdz_counterop: mansion: Mai Hem {:08X} lives and her script runs, the fight is not on yet: "
                    "her MAI_HEM script {:08X} (current class {:08X}), its state {} (1 = on), level state bits {}; the "
                    "agent at y {:.0f} z {:.0f} ({}in the arena box y > 1500, z < -20000)",
                    mai, ms, cur_vt, Ptr(ms) ? int32_t(Load32(base, ms + 0xC)) : -1, int(base[kLevelState] & 3), ay, az,
                    agent_in_arena ? "" : "NOT ");
      }
      return;
    }
    f.mai = mai;
    f.on = true;
    const int32_t phase = int32_t(Load32(base, ms + 0x88)), steps = int32_t(Load32(base, ms + 0xD0));
    const int32_t cloak = int32_t(Load32(base, ms + 0xB4));
    const auto now = std::chrono::steady_clock::now();
    const bool settled = now - f.back_at > std::chrono::milliseconds(1500);
    f.in = cloak == 0 && !base[ms + 0xDC] && !base[ms + 0xD4] && !(Load32(base, mai + 0x1880) & 0x00800000u) && settled;
    // Option B on top (pdz_counterop_mansion_terracotta): while she is gone, a warrior Rare has woken.
    f.warriors_ok = REXCVAR_GET(pdz_counterop_mansion_terracotta) && settled && !f.in &&
                    (cloak != 0 || base[ms + 0xDC] || base[ms + 0xD4]);
    f.woken = base[script + 0x418];
    for (int n = 0; n < 8; ++n)
    {
      const uint32_t w = Load32(base, script + kWarriors + uint32_t(n) * 8);
      f.warrior[n] = REXCVAR_GET(pdz_counterop_mansion_terracotta) && Ptr(w) ? w : 0;
    }
    if (!f.told_on)
    {
      f.told_on = true;
      REXLOG_INFO("pdz_counterop: mansion: the fight is on (pdz_counterop_mansion_maihem): Mai Hem {:08X} (class {:08X}, "
                  "kill slot {:08X}), health {:.1f} of {:.1f}, her script {:08X} phase {}; player 2 {} (guard {:08X}); "
                  "nobody but Mai Hem is picked until the fight ends",
                  mai, Load32(base, mai), Slot(base, mai, 0x2E4), LoadFloat(base, mai + kHealth), // (Sun 22 Nov night (audit): Slot)
                  LoadFloat(base, kStartHealth), ms, phase, out ? "is out of the world" : "has a body", current_guard);
    }
    if (out)
    {
      return; // counter_op.cpp's pick takes her when Pickable()
    }
    const uint32_t part = Load32(base, p2c + kParticipantOf);
    if (current_guard != mai)
    {
      // He is somebody else (a guard taken before the fight, or a warrior): that body goes back to Rare's AI, once she
      // can be taken.
      if (f.in && Ptr(current_guard))
      {
        f.giveback = true;
        REXLOG_INFO("pdz_counterop: mansion: the fight is on and Mai Hem can be taken: the guard player 2 has ({:08X}) is "
                    "handed back to Rare's AI (no death)",
                    current_guard);
      }
      return;
    }
    if (!Ptr(part))
    {
      return;
    }
    // He is Mai Hem. His share of her health; the real one follows it (the level script reads it).
    float full = float(REXCVAR_GET(pdz_counterop_mansion_maihem_health));
    full = !(full >= 50.0f) ? 50.0f : full > 5000.0f ? 5000.0f : full;
    const float mine = LoadFloat(base, part + 980);
    float share = mine / full;
    share = share < 0.0f ? 0.0f : share > 1.0f ? 1.0f : share;
    const float start = LoadFloat(base, kStartHealth);
    if (std::fabs(share - f.mirrored) > 0.004f && share > 0.01f)
    {
      f.mirrored = share;
      StoreFloat(base, mai + kHealth, start * share); // hidden, her AI not ticking: the plain field
    }
    const float mark = NextMark(base, ms);
    const bool dying = mine <= 1.05f; // counter_op.cpp's own-death floor (1.0)
    if (mark <= 0.0f || mark >= f.passed_mark - 0.0001f)
    {
      return; // the last part (or a mark her script did not act on): his death is hers
    }
    if (share > mark + 0.0005f && !dying)
    {
      return;
    }
    if (f.back_phase == phase && f.back_steps == steps && f.back_at != std::chrono::steady_clock::time_point{})
    {
      // She was handed back at this very phase / step before and her script did not move on: not again.
      f.passed_mark = mark;
      REXLOG_INFO("pdz_counterop: mansion: Mai Hem's script did not act on the mark {:.3f} after the last hand-back "
                  "(phase {} steps {} unchanged): that mark is not asked again",
                  mark, phase, steps);
      return;
    }
    PPCFunc *origin = pdz::GuestFunction(0x82151210, "mansion: origin");
    uint32_t o = 0;
    if (origin)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = p2c;
      origin(frame.ctx, base);
      o = frame.ctx.r3.u32;
    }
    for (int i = 0; i < 3; ++i)
    {
      f.at[i] = Ptr(o) ? LoadFloat(base, o + uint32_t(i) * 4) : LoadFloat(base, p2c + kPosition + uint32_t(i) * 4);
    }
    f.share = share < mark ? share : mark;
    if (dying)
    {
      f.share = mark; // a hit that would have ended him before the last mark: she is at the mark, as Rare's own
    }
    f.back_phase = phase;
    f.back_steps = steps;
    f.giveback = true;
    f.giveback_boss = true;
    REXLOG_INFO("pdz_counterop: mansion: player 2 as Mai Hem reached Rare's mark {:.3f} (his health {:.1f} of {:.0f} = "
                "{:.3f}{}; her script's phase {} steps {}): she is handed back to Rare's AI for the phase",
                mark, mine, full, share, dying ? ", a hit that would have ended him" : "", phase, steps);
  }
  // TESTING: Rare's kill on every woken terracotta warrior that lives, credited to the agent.
  void KillWokenWarriors(PPCContext &ctx, uint8_t *base, uint32_t script, uint32_t p1c, const char *who,
                         uint32_t skip = 0)
  {
    const uint8_t woken = base[script + 0x418];
    std::string done;
    for (int n = 0; n < 8; ++n)
    {
      const uint32_t w = Load32(base, script + kWarriors + uint32_t(n) * 8);
      if (!pdz::guestmem::Ok(base, w, kHidden + 4) || w == skip || // Sun 22 Nov night (audit): Ok !(woken & (1u << n)) || LoadFloat(base, w + kHealth) <= 0.0f ||
          (Load32(base, w + 0x44) & 8))
      {
        continue; // (skip: the warrior player 2 stands in for dies with him, through counter_op.cpp's own path)
      }
      const uint32_t wslot = Slot(base, w, 0x2E4); // Sun 22 Nov night (audit)
      PPCFunc *wkill = wslot ? pdz::GuestFunction(wslot, "mansion: kill") : nullptr;
      if (!wkill)
      {
        continue;
      }
      const bool hidden = (Load32(base, w + kHidden) & 0x10) != 0;
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = w;
      frame.ctx.r4.u64 = p1c;
      frame.ctx.r5.u64 = 5;
      frame.ctx.r6.u64 = 1;
      wkill(frame.ctx, base);
      done += fmt::format(" {}:{:08X}{} health {:.1f}", n + 1, w, hidden ? " (HIDDEN)" : "", LoadFloat(base, w + kHealth));
    }
    REXLOG_INFO("pdz_counterop: mansion: {}: woken warriors (mask {:02X}) killed through Rare's kill, credited to the "
                "agent:{}",
                who, woken, done.empty() ? " none" : done);
  }

  // TESTING (pdz_counterop_test_mansion_auto): the fight drives itself, whatever the timing of a run: as Mai Hem his
  // health is put just under her next mark after a few seconds (in her last phase: to his floor, his own death);
  // woken warriors are killed for the agent 6 s after they wake.
  void AutoTest(PPCContext &ctx, uint8_t *base, uint32_t script, uint32_t p1c, uint32_t p2c, uint32_t current_guard,
                bool out)
  {
    static std::chrono::steady_clock::time_point her_since{}, warriors_since{};
    static bool acted = false;
    const auto none = std::chrono::steady_clock::time_point{};
    if (!f.on || !Ptr(f.mai))
    {
      her_since = warriors_since = none;
      acted = false;
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    bool woken_alive = false;
    const uint8_t woken = base[script + 0x418];
    for (int n = 0; n < 8; ++n)
    {
      const uint32_t w = Load32(base, script + kWarriors + uint32_t(n) * 8);
      woken_alive = woken_alive || (Ptr(w) && w != current_guard && (woken & (1u << n)) &&
                                    LoadFloat(base, w + kHealth) > 0.0f && !(Load32(base, w + 0x44) & 8));
    }
    if (!woken_alive)
    {
      warriors_since = none;
    }
    else if (warriors_since == none)
    {
      warriors_since = now;
    }
    else if (now - warriors_since > std::chrono::seconds(6))
    {
      warriors_since = none;
      KillWokenWarriors(ctx, base, script, p1c, "AUTO test", current_guard);
    }
    const uint32_t part = Ptr(p2c) ? Load32(base, p2c + kParticipantOf) : 0;
    // As a terracotta warrior (pdz_counterop_mansion_terracotta): 10 s, then his floor (his own death).
    static std::chrono::steady_clock::time_point warrior_since{};
    if (!out && Ptr(part) && current_guard && current_guard != f.mai && counter_op_mansion::IsBoss(current_guard))
    {
      if (warrior_since == none)
      {
        warrior_since = now;
      }
      else if (now - warrior_since > std::chrono::seconds(10))
      {
        warrior_since = none;
        const float before = LoadFloat(base, part + 980);
        StoreFloat(base, part + 980, 1.0f);
        StoreFloat(base, part + 984, 1.0f);
        StoreFloat(base, p2c + kHealth, 1.0f);
        REXLOG_INFO("pdz_counterop: mansion: AUTO test: player 2 has been a terracotta warrior ({:08X}) for 10 s: his "
                    "health {:.1f} -> his floor (his own death)",
                    current_guard, before);
      }
    }
    else
    {
      warrior_since = none;
    }
    if (out || current_guard != f.mai || !Ptr(part))
    {
      her_since = none;
      acted = false;
      return;
    }
    if (her_since == none)
    {
      her_since = now;
      acted = false;
    }
    uint32_t cur_vt = 0;
    const uint32_t ms = StackScript(base, f.mai, kMaiHemScriptVtable, cur_vt);
    if (acted || !Ptr(ms))
    {
      return;
    }
    const int32_t phase = int32_t(Load32(base, ms + 0x88));
    const int hold_s = phase <= 1 ? 9 : phase == 4 ? 3 : 5;
    if (now - her_since < std::chrono::seconds(hold_s))
    {
      return;
    }
    acted = true;
    const float mark = NextMark(base, ms);
    const float full = float(REXCVAR_GET(pdz_counterop_mansion_maihem_health));
    const float before = LoadFloat(base, part + 980);
    const float want = mark > 0.0f && mark < f.passed_mark - 0.0001f ? full * (mark - 0.004f) : 1.0f;
    if (want < before)
    {
      StoreFloat(base, part + 980, want);
      StoreFloat(base, part + 984, want);
      StoreFloat(base, p2c + kHealth, want);
    }
    REXLOG_INFO("pdz_counterop: mansion: AUTO test: player 2 has been Mai Hem for {} s in her phase {}: his health {:.1f} "
                "-> {:.1f} ({})",
                hold_s, phase, before, LoadFloat(base, part + 980),
                want > 1.0f ? fmt::format("just under her next mark {:.3f}", mark) : std::string("his floor: his own death"));
  }
} // namespace

// counter_op.cpp's level tick.
void PdzCounterOpMansionTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t p2c, uint32_t current_guard, bool out,
                             bool cutscene)
{
  FightTick(ctx, base, Script(base), p1c, p2c, current_guard, out, cutscene);
  const bool log = REXCVAR_GET(pdz_counterop_test_mansion_log);
  const double health_tool = REXCVAR_GET(pdz_counterop_test_mansion_health);
  const int32_t kill_tool = REXCVAR_GET(pdz_counterop_test_mansion_kill);
  const double share_tool = REXCVAR_GET(pdz_counterop_test_mansion_p2_share);
  const bool auto_test = REXCVAR_GET(pdz_counterop_test_mansion_auto);
  if (!log && !auto_test && health_tool == g_health_tool && kill_tool == g_kill_tool && share_tool == g_share_tool)
  {
    return;
  }
  const uint32_t script = Script(base);
  if (!script)
  {
    return;
  }
  if (auto_test && Ptr(p1c))
  {
    AutoTest(ctx, base, script, p1c, p2c, current_guard, out);
  }
  if (script != g_script)
  {
    g_script = script;
    g_sum = 0xFFFFFFFFu;
    g_bodies_logged = false;
    REXLOG_INFO("pdz_counterop: mansion: level script {:08X}: Mai Hem slots {:08X} / {:08X}, level-state byte {:02X}, "
                "game type {}, difficulty {}",
                script, MaiHem(base, script, 0), MaiHem(base, script, 1), base[kLevelState],
                Load32(base, Load32(base, kGame) + 0x28), Load32(base, Load32(base, kGame) + 4));
  }
  const uint32_t mai = MaiHem(base, script, 0);
  // ---- testing tool: her health as a share of the start health ----
  if (health_tool != g_health_tool)
  {
    g_health_tool = health_tool;
    if (health_tool > 0.0 && health_tool <= 1.0 && mai)
    {
      const float start = LoadFloat(base, kStartHealth);
      const float before = LoadFloat(base, mai + kHealth);
      const uint32_t set_slot = Slot(base, mai, 0x158); // Sun 22 Nov night (audit)
      PPCFunc *set = set_slot ? pdz::GuestFunction(set_slot, "mansion: health setter") : nullptr;
      if (set && start > 0.0f)
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = mai;
        frame.ctx.f1.f64 = double(start) * health_tool;
        frame.ctx.r5.u64 = 0;
        set(frame.ctx, base);
      }
      REXLOG_INFO("pdz_counterop: mansion: HEALTH tool: Mai Hem {:08X} health {:.1f} -> {:.1f} (share {:.2f} of the "
                  "start health {:.1f}), getter {:.1f}",
                  mai, before, LoadFloat(base, mai + kHealth), health_tool, start, HealthGetter(ctx, base, mai));
    }
  }
  // ---- testing tool: Rare's kill on her ----
  if (kill_tool != g_kill_tool)
  {
    g_kill_tool = kill_tool;
    const uint32_t kill_slot = mai ? Slot(base, mai, 0x2E4) : 0; // Sun 22 Nov night (audit)
    PPCFunc *fn = kill_slot ? pdz::GuestFunction(kill_slot, "mansion: kill") : nullptr;
    if (kill_tool == 1 && fn && Ptr(p1c))
    {
      const float before = LoadFloat(base, mai + kHealth);
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = mai;
      frame.ctx.r4.u64 = p1c;
      frame.ctx.r5.u64 = 5;
      frame.ctx.r6.u64 = 1;
      fn(frame.ctx, base);
      REXLOG_INFO("pdz_counterop: mansion: KILL tool: Mai Hem {:08X} through her vtable +0x2E4 ({:08X}), credited to the "
                  "agent: health {:.1f} -> {:.1f}, dead +0x44 {:08X}, hidden {:08X}",
                  mai, kill_slot, before, LoadFloat(base, mai + kHealth), Load32(base, mai + 0x44),
                  Load32(base, mai + kHidden));
    }
    else if (kill_tool == 1)
    {
      REXLOG_INFO("pdz_counterop: mansion: KILL tool: nothing to kill (Mai Hem {:08X})", mai);
    }
    else if (kill_tool >= 2 && Ptr(p1c))
    {
      KillWokenWarriors(ctx, base, script, p1c, "KILL tool");
    }
  }
  // ---- testing tool: player 2's health as Mai Hem ----
  if (share_tool != g_share_tool)
  {
    g_share_tool = share_tool;
    const uint32_t part = Ptr(p2c) ? Load32(base, p2c + kParticipantOf) : 0;
    if (share_tool > 0.0 && share_tool < 1.0 && !out && mai && current_guard == mai && Ptr(part))
    {
      const float full = float(REXCVAR_GET(pdz_counterop_mansion_maihem_health));
      const float before = LoadFloat(base, part + 980);
      const float want = float(double(full) * share_tool);
      if (want < before)
      {
        StoreFloat(base, part + 980, want);
        StoreFloat(base, part + 984, want);
        StoreFloat(base, p2c + kHealth, want);
      }
      REXLOG_INFO("pdz_counterop: mansion: P2 SHARE tool: player 2 (Mai Hem) health {:.1f} -> {:.1f} (share {:.3f} of "
                  "{:.0f})",
                  before, LoadFloat(base, part + 980), share_tool, full);
    }
    else if (share_tool > 0.0)
    {
      REXLOG_INFO("pdz_counterop: mansion: P2 SHARE tool: value {:.3f} not applied (player 2 {}, his guard {:08X}, Mai "
                  "Hem {:08X})",
                  share_tool, out ? "is out" : "is in the world", current_guard, mai);
    }
  }
  if (!log)
  {
    return;
  }
  if (!g_bodies_logged)
  {
    g_bodies_logged = true;
    BodiesLog(base);
    for (int n = 0; n < 8; ++n)
    {
      const uint32_t w = Load32(base, script + kWarriors + uint32_t(n) * 8);
      if (Ptr(w))
      {
        REXLOG_INFO("pdz_counterop: mansion: warrior {}: {}", n + 1, CharacterText(ctx, base, w));
      }
    }
  }
  const uint32_t block = Load32(base, script + kFlagBlock);
  uint32_t w[7] = {};
  for (int i = 0; i < 7; ++i)
  {
    w[i] = Ptr(block) ? Load32(base, block + uint32_t(i) * 4) : 0;
  }
  // (word 4 bit 0x00040000 and word 6 bit 0x4 flip every frame: left out of the change test)
  uint32_t sum = w[0] ^ (w[1] * 3u) ^ (w[2] * 5u) ^ (w[3] * 7u) ^ ((w[4] & ~0x00040000u) * 11u) ^ (w[5] * 13u) ^
                 (Load32(base, script + 0x748) * 19u) ^ (Load32(base, script + 0x418) * 23u) ^
                 (Load32(base, script + 0x3F8) * 29u) ^ (Load32(base, script + 0x3FC) * 31u) ^
                 (uint32_t(base[kLevelState]) * 37u) ^ (cutscene ? 41u : 0u) ^ (mai * 43u);
  std::string mai_text = "Mai Hem: none";
  if (mai)
  {
    uint32_t cur_vt = 0;
    const uint32_t s = StackScript(base, mai, kMaiHemScriptVtable, cur_vt);
    mai_text = "Mai Hem: " + CharacterText(ctx, base, mai);
    sum ^= ((Load32(base, mai + kHidden) & 0x10) * 47u) ^ (Load32(base, mai + 0x1880) & 0x00800000u) ^
           (Load32(base, mai + 0x44) * 53u) ^ (cur_vt * 59u);
    if (Ptr(s))
    {
      mai_text += fmt::format(
          "; her script {:08X}: state +0xC {}, phase now {} wanted {}, sight {} (+0xB0), cloak state {} (+0xB4: 0 seen, 3 "
          "vanishing, 1 gone, 2 coming back), plan {} (before {}), steps {} (+0xD0), flags D4 {} D5 {} D6 {} DC {} DD {} "
          "DE {} DF {} EC {}, pad {} (+0x94), tuning {:.2f} {:.2f} {:.2f}",
          s, Load32(base, s + 0xC), int32_t(Load32(base, s + 0x88)), int32_t(Load32(base, s + 0x8C)),
          int32_t(Load32(base, s + 0xB0)), int32_t(Load32(base, s + 0xB4)), int32_t(Load32(base, s + 0xB8)),
          int32_t(Load32(base, s + 0xBC)), int32_t(Load32(base, s + 0xD0)), base[s + 0xD4], base[s + 0xD5],
          base[s + 0xD6], base[s + 0xDC], base[s + 0xDD], base[s + 0xDE], base[s + 0xDF], base[s + 0xEC],
          int32_t(Load32(base, s + 0x94)), LoadFloat(base, s + 0xE0), LoadFloat(base, s + 0xE4),
          LoadFloat(base, s + 0xE8));
      sum ^= (Load32(base, s + 0x88) * 61u) ^ (Load32(base, s + 0x8C) * 67u) ^ (Load32(base, s + 0xB4) * 71u) ^
             (Load32(base, s + 0xD0) * 73u);
    }
    else
    {
      mai_text += " ; no MAI_HEM script on her stack";
    }
  }
  std::string warriors;
  for (int n = 0; n < 8; ++n)
  {
    const uint32_t c = Load32(base, script + kWarriors + uint32_t(n) * 8);
    if (!Ptr(c))
    {
      warriors += fmt::format(" {}:none", n + 1);
      continue;
    }
    uint32_t cur_vt = 0;
    StackScript(base, c, 0, cur_vt);
    const bool dead = LoadFloat(base, c + kHealth) <= 0.0f || (Load32(base, c + 0x44) & 8);
    warriors += fmt::format(" {}:{}{}{} {:.0f}hp ({:.0f},{:.0f},{:.0f}) s{:04X}", n + 1, dead ? "DEAD" : "alive",
                            (Load32(base, c + kHidden) & 0x10) ? "/hidden" : "", cur_vt ? "" : "/noscript",
                            LoadFloat(base, c + kHealth), LoadFloat(base, c + kPosition),
                            LoadFloat(base, c + kPosition + 4), LoadFloat(base, c + kPosition + 8), cur_vt & 0xFFFFu);
    sum ^= ((uint32_t(dead) << n) * 79u) ^ (cur_vt * uint32_t(n + 83));
  }
  const auto now = std::chrono::steady_clock::now();
  const bool changed = sum != g_sum;
  if (now < g_soonest || (!changed && now < g_next))
  {
    return; // at most four lines a second, whatever flips
  }
  g_sum = sum;
  g_soonest = now + std::chrono::milliseconds(250);
  g_next = now + std::chrono::seconds(2);
  const uint32_t part = Ptr(p2c) ? Load32(base, p2c + kParticipantOf) : 0;
  REXLOG_INFO(
      "pdz_counterop: mansion: {}flag words {:08X} {:08X} {:08X} {:08X} {:08X} {:08X} {:08X}, stage +0x748 {}, counts "
      "+0x3F8 {} +0x3FC {} +0x9E8 {} +0x9A4 {}, warrior masks woken {:02X} gone {:02X} ({:02X} {:02X}), level state "
      "{:02X}, cutscene {}; agent {:08X} health {:.1f} at ({:.0f}, {:.0f}, {:.0f}); player 2 {:08X} {} body {} health "
      "{:.1f} max {:.1f} at ({:.0f}, {:.0f}, {:.0f}) || {} || slot 1 {:08X} || warriors:{}",
      changed ? "CHANGE: " : "", w[0], w[1], w[2], w[3], w[4], w[5], w[6], int32_t(Load32(base, script + 0x748)),
      int32_t(Load32(base, script + 0x3F8)), int32_t(Load32(base, script + 0x3FC)),
      int32_t(Load32(base, script + 0x9E8)), int32_t(Load32(base, script + 0x9A4)), base[script + 0x418],
      base[script + 0x419], base[script + 0x41A], base[script + 0x41B], base[kLevelState], cutscene ? 1 : 0, p1c,
      Ptr(p1c) ? LoadFloat(base, p1c + kHealth) : -1.0f, Ptr(p1c) ? LoadFloat(base, p1c + kPosition) : 0.0f,
      Ptr(p1c) ? LoadFloat(base, p1c + kPosition + 4) : 0.0f, Ptr(p1c) ? LoadFloat(base, p1c + kPosition + 8) : 0.0f,
      p2c, out ? "OUT" : "in the world", Ptr(p2c) ? Load32(base, p2c + kBodyId) : 0,
      Ptr(p2c) ? LoadFloat(base, p2c + kHealth) : -1.0f, Ptr(part) ? LoadFloat(base, part + 980) : -1.0f,
      Ptr(p2c) ? LoadFloat(base, p2c + kPosition) : 0.0f, Ptr(p2c) ? LoadFloat(base, p2c + kPosition + 4) : 0.0f,
      Ptr(p2c) ? LoadFloat(base, p2c + kPosition + 8) : 0.0f, mai_text, MaiHem(base, script, 1), warriors);
}
