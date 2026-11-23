// Counter-Operative: level triggers meant for the agent ignore player 2 (round 2, design item 8). Draft, not built.
//
// Two places, both Rare's own central points (research notes: round2\c\FINDINGS.md has the evidence):
//
// (a) Data triggers: sub_825178B0(prop, activator, trigger kind) is the one function every prop / volume / door /
//     terminal trigger of the level data goes through. It looks the kind up in the prop's two setup messages
//     (setup +0x1E, +0x22), its registered triggers (+0x70 list, type 4) and its template's four (+0xCC..+0xD8), and
//     sends each match through 82517A50 (to the level script's OnCommand via 8214E520, or to a prop by index). The
//     volumes (obb 825D68xx-825D6E18, sphere 825D5E20) fire the human-only kinds (enter / exit / contact / faces /
//     pass-through) for any character whose participant is a human (vt+0x298 -> +0xC -> +0xC byte), so player 2
//     fires them exactly like the agent: Subway's Killian's ship (event 35) is three ContactVolume obbvolumes plus
//     a Shot trigger on the dropship. With pdz_counterop_block_triggers, a trigger whose activator is player 2 (or
//     a vehicle he rides: object type +0xC == 0x32, rider +0x600) is dropped when its kind is a presence / use kind,
//     and with pdz_counterop_block_shot_triggers also when it is a shot kind. Destroyed, door open / close (their
//     activator is the door itself), BlownUp, ExplosivesPlaced, Damaged, Startup and ContactVolumeWithAI always
//     pass: a prop he destroys is gone either way and the level must hear of it.
//     The volumes keep one state byte per player (+0x360 + the participant's +0x674 index), so dropping player 2's
//     message leaves the agent's own enter / contact to fire normally later.
//
// (b) Level scripts: every level script handles player 2 explicitly as player 1's partner (player 1 +0x1A88, 6792):
//     "every human dead" fail checks, "both players below y -2000" (Subway Update 82362908-82362960, flag 173), "either
//     player has item 0x10 / 0x22" (8236385C-823638E4), activator == player 1 / player 2 flags in OnCommand
//     (82365C74-82365CDC). The "both players" checks would hold the agent back until the counter-operative walks
//     there too. With pdz_counterop_solo_scripts, player 1's +0x1A88 reads 0 while the level manager runs the script
//     (Setup 8214DB68, Update 8214DC28, flag handler 8214DD38, messages / OnCommand 8214E520), so every level script
//     sees a solo agent: the same null partner it sees in every solo game, so every script path this takes is one
//     solo play runs. Nothing else is changed; the field is put back after the call (nesting counted).
//
// Not covered here (FINDINGS.md): level scripts that read player 1 only (about 100-140 reads per test level) already
// ignore player 2; Rare's per-human HUD loops (8264EC88 / 8264EEF8, 82151520 / 82151630) and the per-level human loop
// at level start (82508B40 +0x1590 / +0x1558) are not triggers; Datacore's six scientist "nearest human" helpers
// (822ECB38 ...) and 82153AB8 ("can any human see it") still see him (cosmetic, UNSURE).
//
// Call-in points for counter_op.cpp: none needed beyond PdzCounterOpIsPlayer2 (already exported there). Add this
// file to the PDZ_COUNTEROP target_sources line in CMakeLists.txt.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

#include "counter_op_guards.h"  // round 4: counter_op_wake (which wake groups have fired)
#include "counter_op_jetpack.h" // round 2: a Use press on a jetpack (the remount check)
#include "render/game_pace.h"   // SpLevelName
#include <string>
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

REXCVAR_DEFINE_BOOL(pdz_counterop_block_triggers, true, "Gameplay",
                    "Counter-Operative (default on): the level's own triggers meant for the agent ignore player 2: a "
                    "trigger volume he walks into, a terminal or switch he uses, a sensor that sees him send nothing "
                    "(Subway: he no longer launches Killian's ship). Props he destroys are still reported, and doors "
                    "work as for any guard")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_block_shot_triggers, true, "Gameplay",
                    "Counter-Operative (default on): also ignore the level's 'shot' triggers when player 2 is the "
                    "shooter (Subway: shooting the dropship launches it for the agent only)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_solo_scripts, true, "Gameplay",
                    "Counter-Operative (default on): the level scripts see the agent alone (no co-op partner), as in a "
                    "solo game, so 'both players must be here' checks wait for the agent only and the scripts never "
                    "move, equip or count player 2")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_solo_game_type, "all", "Gameplay",
                      "Counter-Operative (round 5): while a level script runs (its Setup, Update, flag handler and "
                      "messages) the game type reads SINGLE PLAYER, so the script takes its solo branches: the solo "
                      "companion's orders, lines, objectives and fail conditions (Jack on Rooftops). companions "
                      "= rounds 5-7: on the levels where co-op hands a solo companion's part to player 2 (Rooftops, "
                      "River, Trinity Escape, Jungle, Outpost); all = every level (the default since round 8: every gate co-op makes wait for the second player "
                      "opens for the agent alone); off = round 4 (co-op branches)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_event_log_all, false, "Testing",
                    "TESTING TOOL (counter-op, round 9): every level event message is logged (not only each event's "
                    "first), up to 600 lines a level")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_p2_no_events, true, "Gameplay",
                    "Counter-Operative (round 10, default on): player 2 sets off no level event at all: also not "
                    "through a thing of his (a grenade, a mine, a blast as the activator), not by shooting the agent "
                    "(her own 'shot' messages other than the under-fire clock 210; Nightclub's 41), and Nightclub's "
                    "talkers are not disturbed by the sight of his gun. Off = round 9")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_solo_ai, true, "Gameplay",
                    "Counter-Operative (round 10, default on): the AI functions found to differ between single player "
                    "and co-op (the guards' aim build-up against a human, their choice between the two humans) run "
                    "with the single-player settings. Off = Rare's co-op settings")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_trigger_log, true, "Gameplay",
                    "Counter-Operative: log each trigger dropped for player 2 (first 40, then every 200th)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

bool PdzCounterOpIsPlayer2(uint32_t character); // counter_op.cpp: started, pdz_counterop on, this is player 2
bool PdzCounterOpRunning();                     // counter_op.cpp: a supported counter-op game is running in this level
bool PdzCounterOpIsHis(const uint8_t *base, uint32_t object); // counter_op_unheard.cpp (round 10): player 2 or a thing of his
bool PdzCounterOpSpiderIsHis(uint32_t object); // counter_op_spider.cpp (round 11)
void PdzCounterOpEnvTrigger(PPCContext &ctx, uint8_t *base, uint32_t prop, uint32_t activator, uint32_t kind); // counter_op_view.cpp (round 12, item 5)
uint32_t PdzCounterOpPlayer2();                 // counter_op.cpp
bool PdzCounterOpSoloLevel(const uint8_t *base); // counter_op.cpp (round 5): a co-op game that is / will be Counter-Op

REX_EXTERN(__imp__sub_825178B0); // fire a prop trigger: (prop, activator, kind)
REX_EXTERN(__imp__sub_8214DB68); // level manager: the script's Setup (vt+0x08), start of level
REX_EXTERN(__imp__sub_8214DC28); // level manager: the script's Update (vt+0x0C), every frame (f1 = step)
REX_EXTERN(__imp__sub_8214DD38); // level manager: the flag-change handler (vt+0x14), every frame
REX_EXTERN(__imp__sub_8214E520); // level manager: send a message (manager, source, activator, r6, msg) -> OnCommand

namespace
{
  constexpr uint32_t kPlayerOne = 0x82E3C3E4; // player 1's character
  constexpr uint32_t kPartner = 6792;         // player 1 +0x1A88: the co-op partner (player 2's character)
  constexpr uint32_t kObjectType = 0x0C;      // 2 = character, 0x32 = vehicle
  constexpr uint32_t kVehicleRider = 0x600;   // a vehicle's rider (825D69A0)
  constexpr uint32_t kTypeVehicle = 0x32;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    base[a] = uint8_t(v >> 24);
    base[a + 1] = uint8_t(v >> 16);
    base[a + 2] = uint8_t(v >> 8);
    base[a + 3] = uint8_t(v);
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

  // PropTrigger_* (enum names at 820DB040.., order confirmed by the level data: 08 contact, 0C hacked, 0E door).
  const char *KindName(uint32_t kind)
  {
    static const char *const names[] = {
        "None", "Destroyed", "Shot", "Interact", "EnterVolume", "ExitVolume", "SeenPlayer", "PassedThrough",
        "ContactVolume", "ContactRedFace", "ContactGreenFace", "ContactBlueFace", "HackedByDataThief",
        "IdentifiedBySoundAlyser", "DoorOpening", "DoorClosing", "PassRToG", "PassRToB", "PassGToB", "PassGToR",
        "PassBToR", "PassBToG", "PassRToGAndBack", "PassRToBAndBack", "PassGToBAndBack", "ContactVolumeWithAI",
        "LaserTripWire", "InteractGadget", "Startup"};
    if (kind < sizeof(names) / sizeof(names[0]))
    {
      return names[kind];
    }
    if (kind >= 29 && kind <= 43)
    {
      return "PartShot";
    }
    switch (kind)
    {
    case 44:
      return "BlownUpWithExplosives";
    case 45:
      return "ExplosivesPlaced";
    case 46:
      return "Damaged";
    case 47:
      return "Struck";
    default:
      return "?";
    }
  }

  // Presence / use kinds: the agent's own (the volumes fire them for humans only).
  bool PresenceKind(uint32_t kind)
  {
    return (kind >= 3 && kind <= 13) || (kind >= 16 && kind <= 24) || kind == 26 || kind == 27;
  }
  bool ShotKind(uint32_t kind) { return kind == 2 || (kind >= 29 && kind <= 43) || kind == 47; }

  bool IsPlayer2Activator(const uint8_t *base, uint32_t activator)
  {
    if (!Ptr(activator))
    {
      return false;
    }
    if (PdzCounterOpIsPlayer2(activator))
    {
      return true;
    }
    // Round 10 (item 1): a thing of his as the activator (his weapon, grenade, mine, blast).
    if (REXCVAR_GET(pdz_counterop_p2_no_events) && PdzCounterOpIsHis(base, activator))
    {
      return true;
    }
    return pdz::guestmem::Ok(base, activator, kVehicleRider + 4) && // Sun 22 Nov night (audit): a readable activator
           Load32(base, activator + kObjectType) == kTypeVehicle &&
           PdzCounterOpIsPlayer2(Load32(base, activator + kVehicleRider));
  }

  std::atomic<uint32_t> g_dropped{0};

  // (b): player 1's partner field reads 0 for the duration of one level-manager call into the script.
  int g_depth = 0;
  uint32_t g_saved_one = 0, g_saved_partner = 0;

  // Round 5 (item 5): the game type ([0x82D6BFD8] + 40: 0 solo, 1 co-op) reads solo for the duration of the call.
  // The level scripts test it inline (Rooftops 38 times): Jack's orders, his lines with him as the speaker and the
  // solo objectives are in the "not co-op" branches.
  constexpr uint32_t kGameState = 0x82D6BFD8;
  uint32_t g_type_at = 0; // where the 1 was replaced by 0 (0 = not flipped)
  uint64_t g_type_flips = 0;

  // 0 off, 1 the companion levels, 2 every level.
  int TypeMode()
  {
    const std::string m = REXCVAR_GET(pdz_counterop_solo_game_type);
    return m == "all" ? 2 : m == "off" || m == "false" || m == "0" ? 0 : 1;
  }
  bool CompanionLevel(const uint8_t *base)
  {
    static std::string last_raw;
    static bool last = false;
    std::string raw;
    const std::string mission = pdz::pace::SpLevelName(base, &raw);
    if (raw != last_raw)
    {
      last_raw = raw;
      last = mission == "rooftops" || mission == "river" || mission == "trinity2" || mission == "jungle" ||
             mission == "outpost";
      REXLOG_INFO("pdz_counterop: companions: level '{}' ({}): {}", mission, raw,
                  last ? "co-op gives a single-player companion's part to player 2 here: the level script runs as "
                         "in single player (the game type reads solo inside its calls), so the companion keeps his "
                         "solo orders, lines and rules"
                       : "no single-player companion is replaced by player 2 here: the script keeps its co-op "
                         "branches (pdz_counterop_solo_game_type=all changes that)");
    }
    return last;
  }

  struct SoloScripts
  {
    uint8_t *base;
    bool active = false;
    bool flipped = false;
    explicit SoloScripts(uint8_t *b) : base(b)
    {
      if (g_depth++ > 0)
      {
        return;
      }
      const bool solo_level = PdzCounterOpSoloLevel(base);
      const int mode = TypeMode();
      if (solo_level && mode != 0 && (mode == 2 || CompanionLevel(base)))
      {
        const uint32_t so = Load32(base, kGameState);
        if (Ptr(so) && Load32(base, so + 40) == 1)
        {
          Store32(base, so + 40, 0);
          g_type_at = so + 40;
          flipped = true;
          ++g_type_flips;
        }
      }
      if (!REXCVAR_GET(pdz_counterop_solo_scripts))
      {
        return;
      }
      const uint32_t one = Load32(base, kPlayerOne);
      if (!Ptr(one))
      {
        return;
      }
      const uint32_t partner = Load32(base, one + kPartner);
      // Before Start() (the script's Setup) nobody is "player 2" yet: any partner of a level that will be Counter-Op.
      if (!Ptr(partner) || !(PdzCounterOpIsPlayer2(partner) || solo_level))
      {
        return;
      }
      g_saved_one = one;
      g_saved_partner = partner;
      Store32(base, one + kPartner, 0);
      active = true;
    }
    ~SoloScripts()
    {
      --g_depth;
      if (flipped)
      {
        if (Load32(base, g_type_at) == 0)
        {
          Store32(base, g_type_at, 1);
        }
        else
        {
          REXLOG_WARN("pdz_counterop: companions: the game type changed during a level script call ({}); left as "
                      "Rare set it",
                      Load32(base, g_type_at));
        }
        g_type_at = 0;
      }
      if (!active)
      {
        return;
      }
      // Put the partner back unless Rare's call relinked something there itself (never seen; logged).
      if (Load32(base, g_saved_one + kPartner) == 0)
      {
        Store32(base, g_saved_one + kPartner, g_saved_partner);
      }
      else
      {
        REXLOG_WARN("pdz_counterop: triggers: player 1 +0x1A88 changed during a level script call ({:08X}); left "
                    "as Rare set it",
                    Load32(base, g_saved_one + kPartner));
      }
    }
  };
} // namespace

// Round 5 (item 5): the agent's allies in the level (setup team byte 1: Jack, Jonathan, the pilot, the CI squads),
// logged a few times a level: there from the start, alive, where. Called from counter_op.cpp's tick.
void PdzCounterOpCompanionLog(const uint8_t *base)
{
  static std::chrono::steady_clock::time_point since;
  const auto now_tp = std::chrono::steady_clock::now();
  static uint32_t script_seen = 0;
  static int lines = 0;
  static double next_at = 0.0;
  const uint32_t owner = Load32(base, kGameState + 0x1C);
  const uint32_t target = pdz::guestmem::Ok(base, owner, 0x2D8 + 4) ? Load32(base, owner + 0x2D8) : 0;
  const uint32_t script = pdz::guestmem::Ok(base, target, 0x74 + 4) ? Load32(base, target + 0x74) : 0;
  if (script != script_seen)
  {
    script_seen = script;
    since = now_tp;
    lines = 0;
    next_at = 0.0;
  }
  const double level_seconds = std::chrono::duration<double>(now_tp - since).count();
  if (!script || lines >= 8 || level_seconds < next_at)
  {
    return;
  }
  next_at = level_seconds + (lines < 2 ? 5.0 : 30.0);
  ++lines;
  std::string text;
  int count = 0;
  uint32_t node = Load32(base, 0x82E454A0 + 0x710);
  const uint32_t end = Load32(base, 0x82E454A0 + 0x714);
  for (int i = 0; pdz::guestmem::Ok(base, node, 12) && node != end && i < 2048; ++i) // Sun 22 Nov night (audit): Ok
  {
    const uint32_t c = Load32(base, node);
    node = Load32(base, node + 8);
    const uint32_t setup = Ptr(c) && pdz::guestmem::Ok(base, c, 124) ? Load32(base, c + 84) : 0; // (Sat 21 Nov evening: any list member)
    if (!pdz::guestmem::Ok(base, setup, 60) || base[setup + 56] != 1)
    {
      continue;
    }
    const uint32_t idp = Load32(base, setup + 52);
    std::string id;
    for (int k = 0; pdz::guestmem::Ok(base, idp, 4) && k < 4 && base[idp + k]; ++k)
    {
      id += char(base[idp + k]);
    }
    uint32_t hb = Load32(base, c + 120);
    float health;
    std::memcpy(&health, &hb, 4);
    float pos[3];
    for (int k = 0; k < 3; ++k)
    {
      const uint32_t b = Load32(base, c + 48 + 4 * k);
      std::memcpy(&pos[k], &b, 4);
    }
    ++count;
    if (count <= 12)
    {
      text += (text.empty() ? "" : "; ") + std::string("'") + id + "' " +
              ((Load32(base, c + 6272) & 0x10) ? "asleep" : "awake") + ", health " + std::to_string(int(health)) +
              ", at (" + std::to_string(int(pos[0])) + ", " + std::to_string(int(pos[1])) + ", " +
              std::to_string(int(pos[2])) + ")";
    }
  }
  REXLOG_INFO("pdz_counterop: companions: {:.0f} s into the level: {} character(s) on the agent's side by Rare's "
              "setups (team 1){}{}; script calls run as single player so far: {}",
              level_seconds, count, count ? ": " : "", text, g_type_flips);
}

bool PdzCounterOpScriptTypeFlip() { return g_type_at != 0; }
// Round 6 (item 1): at a level boundary no script call is running: a switch still set is put back (true = it was).
bool PdzCounterOpTriggersReset(uint8_t *base)
{
  bool fixed = false;
  if (g_type_at != 0 && g_depth == 0)
  {
    if (Load32(base, g_type_at) == 0)
    {
      Store32(base, g_type_at, 1);
    }
    g_type_at = 0;
    fixed = true;
  }
  g_dropped = 0;
  return fixed;
}
uint64_t PdzCounterOpScriptTypeFlips()
{
  const uint64_t n = g_type_flips;
  g_type_flips = 0;
  return n;
}

// sub_825178B0(prop, activator, kind): fire a prop's triggers of one kind.
REX_HOOK_RAW(sub_825178B0)
{
  const uint32_t prop = ctx.r3.u32, activator = ctx.r4.u32, kind = ctx.r5.u32;
  // Round 2 (CALL-IN J8): every Use on a vehicle ends in its Interact trigger (825D2E90 -> 825199F0 at 825D32B8),
  // seated or not; the jetpack code checks 0.3 s later whether Rare's mount seated him.
  if (kind == 3 && IsPlayer2Activator(base, activator))
  {
    counter_op_jetpack::OnInteract(base, prop, activator, kind);
  }
  // A shot trigger on the agent himself (the player setups' "Shot" message, event 210 on every test level) is his
  // own being shot: it passes whoever fired (UNSURE what 210 drives; FINDINGS 8.4).
  const bool shot = ShotKind(kind) && REXCVAR_GET(pdz_counterop_block_shot_triggers) &&
                    prop != Load32(base, kPlayerOne);
  if (REXCVAR_GET(pdz_counterop_block_triggers) && (PresenceKind(kind) || shot) &&
      IsPlayer2Activator(base, activator) && !PdzCounterOpSpiderIsHis(prop)) // (round 11: the driven bot's own triggers pass)
  {
    const uint32_t n = ++g_dropped;
    // Round 12 (item 10): his Interact / InteractGadget presses are always logged (one per press): a mission item he
    // 'used' shows here with its prop, whatever the count.
    if (REXCVAR_GET(pdz_counterop_trigger_log) && (n <= 40 || n % 200 == 0 || kind == 3 || kind == 27))
    {
      const uint32_t setup = Load32(base, prop + 0x54);
      REXLOG_INFO("pdz_counterop: trigger #{} dropped for player 2: {} ({}) on prop {:08X} (setup messages "
                  "{:08X} {:08X}), from {:08X}",
                  n, KindName(kind), kind, prop, Ptr(setup) ? Load32(base, setup + 0x1E) : 0,
                  Ptr(setup) ? Load32(base, setup + 0x22) : 0, uint32_t(ctx.lr));
    }
    // Round 12 (item 5): the dropped trigger's sky / fog / weather / lighting messages still reach Rare (his own
    // view only; counter_op_view.cpp, pdz_counterop_env_triggers).
    PdzCounterOpEnvTrigger(ctx, base, prop, activator, kind);
    return;
  }
  __imp__sub_825178B0(ctx, base);
}

// The level manager's four calls into the level script (b).
void PdzCounterOpSpiderPreSetup(uint8_t *base); // counter_op_spider.cpp (round 7, a testing tool)
REX_HOOK_RAW(sub_8214DB68)
{
  PdzCounterOpSpiderPreSetup(base);
  void PdzCounterOpRiverPreSetup(uint8_t *base); PdzCounterOpRiverPreSetup(base); // round 13b r: River's checkpoint test tool (off: nothing)
  SoloScripts solo(base);
  __imp__sub_8214DB68(ctx, base);
}

REX_HOOK_RAW(sub_8214DC28)
{
  SoloScripts solo(base);
  __imp__sub_8214DC28(ctx, base);
}

REX_HOOK_RAW(sub_8214DD38)
{
  SoloScripts solo(base);
  __imp__sub_8214DD38(ctx, base);
}

// Round 8 (item 1): the one co-op gate found OUTSIDE the four script calls. Bridge: an ally's own task (82294680,
// state 14) waits at pad 0xD36 until the agent is within about 7 m of him and, in co-op, the partner too; then he
// opens the doors ahead (seven door messages) and moves on. With only the partner hidden he would wait for ever:
// the task runs as single player as well.
REX_EXTERN(__imp__sub_82294680);
REX_HOOK_RAW(sub_82294680)
{
  SoloScripts solo(base);
  __imp__sub_82294680(ctx, base);
}

// Round 9 (item 0): Datacore's lift never came down. The lift is called by the script when the last scientist's
// "destroyed" message (8/56) arrives; that scientist's own wait task (update 822EB078) holds him at a pad until the
// agent has passed him AND, when player 1 has a partner, the partner too (822EB3E8: AI task code, outside the four
// script calls). Player 2's parked body never passes, so the scientist waited for ever. That test, and the other
// functions of the same task library that read player 1's partner, run as single player as well.
REX_EXTERN(__imp__sub_822EB3E8);
REX_HOOK_RAW(sub_822EB3E8)
{
  SoloScripts solo(base);
  __imp__sub_822EB3E8(ctx, base);
  static uint32_t lines = 0;
  if (PdzCounterOpRunning() && ++lines <= 3)
  {
    REXLOG_INFO("pdz_counterop: datacore: the last scientist's wait (822EB3E8) asked as single player: answer {} "
                "(logged 3 times)",
                ctx.r3.u32 & 0xFF);
  }
}
#define PDZ_COUNTEROP_SOLO_TASK(sub)     REX_EXTERN(__imp__##sub);              REX_HOOK_RAW(sub)                      {                                        SoloScripts solo(base);                __imp__##sub(ctx, base);             }
PDZ_COUNTEROP_SOLO_TASK(sub_822E91E0) // the leader task's ctor (reads the partner at 822E929C)
PDZ_COUNTEROP_SOLO_TASK(sub_822F26A8) // 822F275C / 822F2790
PDZ_COUNTEROP_SOLO_TASK(sub_822F4E18) // 822F4F38
PDZ_COUNTEROP_SOLO_TASK(sub_822E6DC8) // 822E6EA4 / 822E6F24
PDZ_COUNTEROP_SOLO_TASK(sub_822F75D8) // 822F8924
// Round 9's search of every level's task library for the same shape (a task outside the script's four calls that
// looks at the partner):
PDZ_COUNTEROP_SOLO_TASK(sub_8234E9F0) // Temple: an ally's task; its follow target becomes the partner when one exists
PDZ_COUNTEROP_SOLO_TASK(sub_8236C4C0) // Outpost: an ally's task; holds while the partner's body stands beside him
PDZ_COUNTEROP_SOLO_TASK(sub_822CF218) // Trinity Escape: 'a player is near' / 'a player holds the item': Jo only
#undef PDZ_COUNTEROP_SOLO_TASK

// Round 10 (item 1): "where Rare uses co-op settings for the AI because there are two players, Counter-Op uses the
// single-player settings". Found outside the level scripts (round 10 helper A; the list in the report):
//   8214CF98 the NPC aim build-up: only when the game type is single player (8214D2DC-8214D32C) is the build-up rate
//            against a human target multiplied by a factor; co-op skips it;
//   82581E50 a guard picks the nearer of {player 1, her partner} by pointer, not by sight;
//   821BAC68, 821BD3C0, 821BDD80, 821BE2B0 one AI class family (vtables 82091EDC / 82091F28): its target among the
//            two humans, the same way.
// They run as the level scripts do: game type single player, no partner, for the length of the call.
#define PDZ_COUNTEROP_SOLO_AI(sub)                                                                                     \
  REX_EXTERN(__imp__##sub);                                                                                            \
  REX_HOOK_RAW(sub)                                                                                                    \
  {                                                                                                                    \
    if (!REXCVAR_GET(pdz_counterop_solo_ai))                                                                           \
    {                                                                                                                  \
      __imp__##sub(ctx, base);                                                                                         \
      return;                                                                                                          \
    }                                                                                                                  \
    SoloScripts solo(base);                                                                                            \
    __imp__##sub(ctx, base);                                                                                           \
  }
PDZ_COUNTEROP_SOLO_AI(sub_8214CF98)
PDZ_COUNTEROP_SOLO_AI(sub_82581E50)
PDZ_COUNTEROP_SOLO_AI(sub_821BAC68)
PDZ_COUNTEROP_SOLO_AI(sub_821BD3C0)
PDZ_COUNTEROP_SOLO_AI(sub_821BDD80)
PDZ_COUNTEROP_SOLO_AI(sub_821BE2B0)
#undef PDZ_COUNTEROP_SOLO_AI

// For hooks in other files (lab_cut_content.cpp's 82319B18: the Laboratory scientist task cowers to "the nearer
// player" and sends 8/47 for him; with this it only knows the agent): the same single-player scope.
void *PdzCounterOpSoloEnter(uint8_t *base) { return new SoloScripts(base); }
void PdzCounterOpSoloLeave(void *scope) { delete static_cast<SoloScripts *>(scope); }

// Round 10 (item 1): Nightclub's enforcers. The three ringleaders and their buddies run a generic talk task (update
// 821CB210, made by the Nightclub script at 822B6C98); while they talk Jo can listen in with the Audioscope. Its
// "disturbed?" test 821CBA60(task; +0x10 the character) answers yes when (a) the character has flag +0x187C
// 0x00400000 and his seen target has something in his hands, (b) four characters are already disturbed, or (c) any
// stimulus bit of 821D1888 is up. On yes: his alert latch, the level event 21 (sent with no source and no
// activator, so the event hook cannot tell whose doing it was), and the talk ends: Jo can no longer scan them.
// Player 2 always has a gun in his hands. When the seen target is player 2, cause (a) is switched off for the call;
// (b) and (c) stay Rare's (his noise never reaches (c): counter_op_unheard.cpp).
REX_EXTERN(__imp__sub_821CBA60);
REX_HOOK_RAW(sub_821CBA60)
{
  const uint32_t task = ctx.r3.u32;
  const uint32_t p2 = REXCVAR_GET(pdz_counterop_p2_no_events) ? PdzCounterOpPlayer2() : 0;
  const uint32_t who = p2 && Ptr(task) ? Load32(base, task + 0x10) : 0;
  if (Ptr(who))
  {
    // 821CBA70-821CBAA0: the list at +0x3168 when [+0x316C] != 0, else +0x3150; its +4 = the seen target
    const uint32_t list = Load32(base, who + 0x316C) != 0 ? who + 0x3168 : who + 0x3150;
    const uint32_t seen = Load32(base, list + 4);
    const uint32_t flags = Load32(base, who + 0x187C);
    if (Ptr(seen) && seen == p2 && (flags & 0x00400000u))
    {
      Store32(base, who + 0x187C, flags & ~0x00400000u);
      __imp__sub_821CBA60(ctx, base);
      Store32(base, who + 0x187C, Load32(base, who + 0x187C) | 0x00400000u);
      static uint32_t lines = 0;
      if (++lines <= 5 || lines % 2000 == 0)
      {
        REXLOG_INFO("pdz_counterop: nightclub: a talker ({:08X}) has player 2 as his seen target: the sight of his "
                    "gun does not disturb the talk (cause (a) of 821CBA60 off for this call; Rare's answer from its "
                    "other causes: {}) (#{})",
                    who, (ctx.r3.u32 & 0xFF) ? "DISTURBED" : "not disturbed", lines);
      }
      return;
    }
  }
  __imp__sub_821CBA60(ctx, base);
  if (p2 && (ctx.r3.u32 & 0xFF))
  {
    static uint32_t lines = 0;
    if (++lines <= 12)
    {
      const uint32_t list = Ptr(who) ? (Load32(base, who + 0x316C) != 0 ? who + 0x3168 : who + 0x3150) : 0;
      const uint32_t seen = list ? Load32(base, list + 4) : 0;
      REXLOG_INFO("pdz_counterop: nightclub: a talker ({:08X}) is DISTURBED (Rare's 821CBA60 answered yes; level event "
                  "21 follows): his seen target {:08X} ({}), his stimulus inputs +6260 {:08X}, flags +0x187C {:08X} "
                  "(#{})",
                  who, seen, seen == Load32(base, kPlayerOne) ? "the agent" : seen == p2 ? "player 2" : "other / none",
                  Ptr(who) ? Load32(base, who + 6260) : 0, Ptr(who) ? Load32(base, who + 0x187C) : 0, lines);
    }
  }
}

// Round 3 (item 11). In real play Killian's ship (Subway) launched when player 2 walked into the hangar, though every
// prop trigger he set off there was dropped in the scripted test. The launch is the Subway script's flag 87, written in
// one place only: OnCommand's case for message type 8 / event 35 (82365F64 -> 82366FE8), and OnCommand is reached
// through this send (manager r3, source r4, activator r5, r6, message r7: 82517AC8-82517AF0). So the gate sits here,
// whatever raised the event:
//   - any level message whose activator is player 2 (or a vehicle he rides) is dropped, on every level: a second
//     layer under the prop-trigger hook above;
//   - Subway's event 35 passes only when the activator is the agent (or a vehicle she rides): a guard's stray shot, a
//     grenade or no activator at all no longer starts the ship while Counter-Op runs.
// Each level event's first message is logged with its activator and Rare's caller, so a hand test names the path.
namespace
{
  constexpr uint32_t kSubwayScriptVtable = 0x820A6030; // Subway's level script (OnCommand 82364E40)
  constexpr uint32_t kManagerScript = 0x74;            // level manager +0x74 = the level script (8214DAC0)
  constexpr uint8_t kMsgLevelEvent = 8, kShipEvent = 35;
  uint64_t g_seen_events[4] = {};
  uint32_t g_seen_manager = 0;
  std::atomic<uint32_t> g_msg_dropped{0};

  bool IsAgentActivator(const uint8_t *base, uint32_t activator)
  {
    const uint32_t one = Load32(base, kPlayerOne);
    if (!Ptr(activator) || !Ptr(one))
    {
      return false;
    }
    return activator == one ||
           (pdz::guestmem::Ok(base, activator, kVehicleRider + 4) && // Sun 22 Nov night (audit): a readable activator
            Load32(base, activator + kObjectType) == kTypeVehicle && Load32(base, activator + kVehicleRider) == one);
  }
} // namespace

REX_HOOK_RAW(sub_8214E520)
{
  const uint32_t manager = ctx.r3.u32, activator = ctx.r5.u32, msg = ctx.r7.u32;
  if (PdzCounterOpRunning() && REXCVAR_GET(pdz_counterop_block_triggers) && Ptr(msg) && Ptr(manager))
  {
    const uint8_t type = base[msg + 1], event = base[msg + 2];
    const uint32_t script = Load32(base, manager + kManagerScript);
    const bool subway = pdz::guestmem::Ok(base, script, 4) && Load32(base, script) == kSubwayScriptVtable;
    // The agent's own "Shot" message (her setup's event 210, sent with her as the source whoever shot her: guards
    // send it too) passes when player 2 is the shooter, as round 2 let its prop trigger pass.
    // Round 10 (item 1): of those only event 210 passes (every level: the script's 'under fire' clock, which holds
    // hints back; she really is under fire). Any other event her Shot trigger sends is the level learning that the
    // guards have opened fire on her: Nightclub's 41 (822B5C04) sets 'the agent was shot first' for good, which
    // takes away what her own first shot at a guard does in single player.
    const bool her_shot = IsPlayer2Activator(base, activator) && ctx.r4.u32 == Load32(base, kPlayerOne);
    const bool p2 = IsPlayer2Activator(base, activator) && !PdzCounterOpSpiderIsHis(ctx.r4.u32) && // (round 11: its own messages pass)
                    (!her_shot || (REXCVAR_GET(pdz_counterop_p2_no_events) && !(type == kMsgLevelEvent && event == 210)));
    if (type == kMsgLevelEvent)
    {
      static uint32_t all_lines = 0;
      if (manager != g_seen_manager)
      {
        all_lines = 0;
      }
      if (REXCVAR_GET(pdz_counterop_test_event_log_all) && ++all_lines <= 600)
      {
        REXLOG_INFO("pdz_counterop: TEST TOOL: level event {} flag {}: source {:08X}, activator {:08X} ({}), caller {:08X}",
                    event, int(base[msg + 3]), ctx.r4.u32, activator,
                    p2 ? "player 2" : IsAgentActivator(base, activator) ? "the agent" : Ptr(activator) ? "another object" : "none",
                    uint32_t(ctx.lr));
      }
      if (manager != g_seen_manager)
      {
        g_seen_manager = manager;
        std::memset(g_seen_events, 0, sizeof(g_seen_events));
      }
      uint64_t &word = g_seen_events[event >> 6];
      const uint64_t bit = 1ull << (event & 63);
      if (REXCVAR_GET(pdz_counterop_trigger_log) && !(word & bit))
      {
        word |= bit;
        REXLOG_INFO("pdz_counterop: level event {} (first time): source {:08X}, activator {:08X} ({}), caller {:08X}",
                    event, ctx.r4.u32, activator,
                    p2 ? "player 2" : IsAgentActivator(base, activator) ? "the agent" : Ptr(activator) ? "another object" : "none",
                    uint32_t(ctx.lr));
      }
    }
    const bool ship = subway && type == kMsgLevelEvent && event == kShipEvent && !IsAgentActivator(base, activator);
    if (p2 || ship)
    {
      const uint32_t n = ++g_msg_dropped;
      if (REXCVAR_GET(pdz_counterop_trigger_log) && (n <= 40 || n % 200 == 0))
      {
        REXLOG_INFO("pdz_counterop: level message #{} dropped (type {}, event {}): activator {:08X} is {} (source "
                    "{:08X}, caller {:08X}){}{}",
                    n, type, event, activator,
                    p2 ? (PdzCounterOpIsPlayer2(activator) ? "player 2" : "a thing of player 2's") : "not the agent",
                    ctx.r4.u32, uint32_t(ctx.lr), ship ? ": Killian's ship starts only for the agent" : "",
                    her_shot ? ": the agent's own 'shot' message, sent because player 2 shot her (only the under-fire "
                               "clock 210 passes)"
                             : "");
      }
      return;
    }
  }
  if (PdzCounterOpRunning() && Ptr(msg))
  {
    counter_op_wake::OnLevelMessage(base, base[msg + 1], base[msg + 2]); // round 4: which wake groups have fired
  }
  SoloScripts solo(base);
  __imp__sub_8214E520(ctx, base);
}
