// Counter-Operative round 13: Rooftops (Jack's objectives, the dropped reinforcements). Built only with PDZ_COUNTEROP.
//
// Rooftops | Escape (mission 3, level 'roof', level script vtable 0x820A4B08: Setup 82341600, Update 82345418, flag
// handler 8234B2F8, OnCommand 82347130). The study is research notes: ROOFTOPS_STUDY_Mon16Nov.md.
//
// ITEM 1, JACK (pdz_counterop_rooftops_jack). Jack's opening run is his own AI class (JACKL3, planner 821DAD80): five
// pads (0x275, 0x59, 0xF3, 0x6FF, 0x391), and at the 2nd, 3rd and 4th he waits for a request bit of the word
// 0x82E67BFC. The level script's Update calls 821DA6D0 every frame, which sets those bits from 821DB990(list, n, jack):
//   list A 0x82E67BAC = bff1, bff2, bff4   -> bit 1 (he leaves pad 0x59)
//   list B 0x82E67BC4 = bfm1, bfm2, bfm3   -> bit 2 (he leaves pad 0xF3)
//   list C 0x82E67BDC = bfb1 .. bfb4       -> bit 3 (he leaves pad 0x6FF)
// 821DB990 answers 1 when every guard of the list is dead (guard vtable +0xE8), or by Rare's own time-out: 40 s into
// the wait with neither of Jack's weapons fired for 30 s, EVERY guard of the list is killed through Rare's kill
// (vtable +0x2E4 = 8252DE88) and the answer is 1.
// A guard player 2 drives is hidden with its AI frozen: not dead. So Jack waited for player 2 to die, and when the
// time-out came Rare's kill hit the hidden guard under a living player 2. With the cvar on, the guard he drives at
// that moment reads as "not there" for that one call: its handle in the list is emptied before Rare's function and
// put back after it. Jack moves on when the other guards of the list are dead, and the time-out never kills the
// guard under him. Nothing else of Jack's run needs player 2 (no game type or partner test in the JACKL3 code
// 821D9000-821DD400; the level script's own tests already run as single player).
//
// ITEM 2, KILLIAN'S DROPPED GUARDS (pdz_counterop_rooftops_killian_drops). The dropship boss makes its troops in
// 8256E9F0(ship, count): each is a clone of the template "DropshipSpawner" (82142AD8; the template's setup id is
// 'bff1'), attached to the ship (825191A8), flagged +0x40 |= 0x02000000 and +0x44 |= 0x20, and given the task
// 82092D80 (update 821D3F28): wait for the ship's release (+0x187C bit 0x40000000), jump (821D4358), one second later
// leave the ship (82519288, +0x44 bit 0x20 cleared), and when the jump animation has ended land (821D4558: +0x186C |=
// 0x10 and three ordinary combat tasks queued). From then on he is an ordinary fighting guard. The level counts
// nothing of them (the template has no trigger message; the fight ends with the ship's own "destroyed" event 15).
// counter_op_guards.cpp refuses every clone made after the opening as a reinforcement. With the cvar on, a clone of
// that template that has landed (the two flags above, off the ship, standing still for a moment) may be taken like
// any other Rooftops guard; all the other pick rules stay. While the last fight runs (script flag 58 set, flag 22
// "ship destroyed" not set) player 2 is never "out for the rest of the mission": the next drop can give him a body.
//
// Call-ins (each marked "round 13 e"): counter_op.cpp (the level tick; EligibleLeft; the spectator's way back),
// counter_op_guards.cpp (Takeable, StillExists, OriginalsLeft), counter_op_wake.cpp (AreaAllows),
// counter_op_unheard.cpp (the seen-list iterator hook).
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>
#include <rex/ppc/stack.h>
#include <rex/runtime.h>
#include "hook_profile.h" // pdz_hook_profile (must follow <rex/hook.h>)

#include "guest_function.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

REXCVAR_DEFINE_BOOL(pdz_counterop_rooftops_jack, false, "Gameplay",
                    "Counter-Operative on Rooftops (round 13): Jack's opening run no longer waits for a guard player 2 "
                    "is driving. His three waits end when the other guards of the group are dead, and Rare's own "
                    "time-out (which kills the whole group) never kills the hidden guard under a living player 2. "
                    "Off = round 12 (Jack waits until player 2 has died as that guard)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_rooftops_jack_groups_closed, true, "Gameplay",
                    "Counter-Operative on Rooftops (round 13, default on): the ten guards Jack's opening run waits for "
                    "(bff1, bff2, bff4, bfm1-3, bfb1-4) are never given to player 2, so Jack's run is exactly "
                    "Rare's. Off = they may be taken (then use pdz_counterop_rooftops_jack; in tests Jack still "
                    "stayed at his second pad while player 2 lived as one of them)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_rooftops_killian_drops, true, "Gameplay",
                    "Counter-Operative on Rooftops (round 13, default on): in the last fight player 2 may take a guard that "
                    "Killian's dropship has dropped, once that guard has landed and fights. While that fight runs he "
                    "is never out for the rest of the mission. Killian's ship stays the game's. Off = round 12 "
                    "(dropped guards are never taken)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_rooftops_drop_ground_s, 1.5, "Gameplay",
                      "Counter-Operative on Rooftops (round 13): seconds a dropped guard must have stood on the ground "
                      "(landed, off the ship, not moving up or down) before player 2 may take him")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_rooftops_drop_clear_m, 7.0, "Gameplay",
                      "Counter-Operative on Rooftops (round 16, default 7 m; 0 = round 15): a dropped guard is ready "
                      "to be taken only when he also stands at least this far from the centre of Killian's ship (log "
                      "027: a guard with the ship's flags clear still stood inside the landed ship, and player 2 was "
                      "stuck in it)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_test_rooftops_log, false, "Testing",
                    "TESTING TOOL (counter-op, Rooftops): one line on each change of Jack's request / progress words, "
                    "the level script's flag words, Killian's ship and each dropped guard's state; Jack's place "
                    "every 15 s")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_counterop_test_rooftops, "", "Testing",
                      "TESTING TOOL (counter-op, Rooftops): one command each time the value changes. 'flag:N' sets "
                      "the level script's flag N (23 = the mid-level cutscene, which leads to the last fight); "
                      "'kill:id,id' kills the placed guards with those setup ids through Rare's kill, credited to "
                      "the agent; 'p2kill:N' = up to N times, when a dropped guard is ready to be taken and player 2 "
                      "is not one already, pdz_counterop_test_kill_p2 is raised by one")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DECLARE(int32_t, pdz_counterop_test_kill_p2); // counter_op.cpp

bool PdzCounterOpRunning();     // counter_op.cpp: a supported counter-op game is running in this level
uint32_t PdzCounterOpPlayer2(); // counter_op.cpp

bool PdzCounterOpIsAgentAlly(const uint8_t *base, uint32_t character); // counter_op_allies.cpp (round 13 a)

REX_EXTERN(__imp__sub_8218B498); // the seen-list iterator (hooked in counter_op_unheard.cpp)
REX_EXTERN(__imp__sub_821DB990); // JACKL3: is this guard list done (list, count, jack) -> byte
REX_EXTERN(sub_8252DE88);        // Rare's kill (guard, killer, death type, apply damage)

namespace
{
  constexpr uint32_t kGame = 0x82D6BFD8, kLevel = 0x1C, kLevelTarget = 0x2D8, kLevelTargetScript = 0x74;
  constexpr uint32_t kScriptVtable = 0x820A4B08; // Rooftops' level script
  constexpr uint32_t kScriptFlags = 0x3EC;       // -> five flag words (flag n = word n / 32, bit n % 32)
  constexpr uint32_t kScriptJack = 0x700;        // the script's own pointer to 'jack'
  constexpr uint32_t kFlagShipDown = 22, kFlagLastFight = 58;
  constexpr uint32_t kJackRequest = 0x82E67BFC, kJackProgress = 0x82E67C00; // JACKL3's two words
  constexpr uint32_t kJackListA = 0x82E67BAC, kJackListB = 0x82E67BC4, kJackListC = 0x82E67BDC;
  constexpr uint32_t kShipGlobal = 0x82E3C3CC; // the level's dropship boss (82348E60: lwz r3,-0x3c34(r10))
  constexpr uint32_t kShipTroops = 0x10B8;     // troops made so far (8256EC90)
  constexpr uint32_t kCharList = 0x82E454A0, kGuardVtable = 0x820C45A0;
  constexpr uint32_t kPosition = 48, kFlags40 = 0x40, kFlags44 = 0x44, kSetup = 84, kParent = 0x5C, kHealth = 120;
  constexpr uint32_t kDeadWord = 0x186C, kAiWord = 0x187C, kHidden = 6272;
  constexpr uint32_t kSetupName = 44, kSetupId = 52;
  constexpr uint32_t kInShip40 = 0x02000000u, kOnShip44 = 0x20u, kLanded186C = 0x10u, kDead186C = 0x1u;

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
  float LF(const uint8_t *b, uint32_t a)
  {
    const uint32_t v = L32(b, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

  using Clock = std::chrono::steady_clock;
  double Seconds(Clock::time_point from, Clock::time_point to)
  {
    return std::chrono::duration<double>(to - from).count();
  }

  struct Drop
  {
    Clock::time_point first_seen{}, still_since{};
    float last_y = 0.0f;
    bool landed = false, ready = false, dead = false, told_ready = false;
    uint32_t told_state = ~0u;
  };

  struct State
  {
    uint32_t script = 0;        // Rooftops' level script while a Counter-Op game runs there, else 0
    uint32_t current_guard = 0; // the guard player 2 drives now (0 = none), from the level tick
    bool out = true;
    Clock::time_point level_at{}, next_scan{}, next_jack_line{}, last_test_kill{};
    std::unordered_map<uint32_t, Drop> drops;
    std::unordered_set<uint32_t> gone; // dropped guards Rare has deleted (their pointers are freed memory)
    std::unordered_set<uint32_t> ever; // every guard seen in Rare's character list on this level
    std::unordered_set<uint32_t> told_gone;
    uint32_t ready_drops = 0; // landed, alive, not driven: what DropTakeable would allow now
    uint32_t seen_request = ~0u, seen_progress = ~0u, seen_flags[5] = {~0u, ~0u, ~0u, ~0u, ~0u};
    uint32_t seen_ship = ~0u, seen_troops = ~0u;
    uint32_t jack_lines = 0, flag_lines = 0, drop_lines = 0, hook_lines = 0, seen_lines = 0;
    uint32_t seen_target = ~0u, seen_task = ~0u;
    uint64_t seen_skips = 0;
    std::unordered_set<uint64_t> told_list;
    std::unordered_set<uint32_t> closed_told; // Jack's guards refused to player 2, each logged once
    std::string last_test;
    int test_kills_left = 0;
    bool told_fight = false;
  };
  State g;

  uint32_t ScriptOf(const uint8_t *base)
  {
    const uint32_t owner = L32(base, kGame + kLevel);
    const uint32_t target = Ptr(owner) ? L32(base, owner + kLevelTarget) : 0;
    const uint32_t script = Ptr(target) ? L32(base, target + kLevelTargetScript) : 0;
    return Ptr(script) && L32(base, script) == kScriptVtable ? script : 0;
  }
  bool Flag(const uint8_t *base, uint32_t script, uint32_t n)
  {
    const uint32_t words = L32(base, script + kScriptFlags);
    return Ptr(words) && ((L32(base, words + 4 * (n / 32)) >> (n % 32)) & 1u) != 0;
  }
  bool LastFight(const uint8_t *base, uint32_t script)
  {
    return script && Flag(base, script, kFlagLastFight) && !Flag(base, script, kFlagShipDown);
  }

  std::string IdOf(const uint8_t *base, uint32_t c)
  {
    // (Sat 21 Nov evening: every step tested as mapped memory)
    const uint32_t setup = pdz::guestmem::Ok(base, c, kSetup + 4) ? L32(base, c + kSetup) : 0;
    const uint32_t idp = pdz::guestmem::Ok(base, setup, kSetupId + 4) ? L32(base, setup + kSetupId) : 0;
    std::string id;
    for (int k = 0; pdz::guestmem::Ok(base, idp, 4) && k < 4 && base[idp + k] >= 32 && base[idp + k] < 127; ++k)
    {
      id += char(base[idp + k]);
    }
    return id;
  }
  // A clone of the dropship's troop template (the clone's setup IS the template's record; its +44 name).
  bool IsDropClone(const uint8_t *base, uint32_t c)
  {
    // Sun 22 Nov night (audit): the guard, his setup and the name text read only where readable.
    if (!pdz::guestmem::Ok(base, c, kSetup + 4) || L32(base, c) != kGuardVtable)
    {
      return false;
    }
    const uint32_t setup = L32(base, c + kSetup);
    const uint32_t name = pdz::guestmem::Ok(base, setup, kSetupName + 4) ? L32(base, setup + kSetupName) : 0;
    static constexpr char kName[] = "DropshipSpawner";
    return pdz::guestmem::Ok(base, name, sizeof(kName)) && std::memcmp(base + name, kName, sizeof(kName)) == 0;
  }
  bool Dead(const uint8_t *base, uint32_t c)
  {
    return (L32(base, c + kDeadWord) & kDead186C) != 0 || !(LF(base, c + kHealth) > 0.0f);
  }

  template <typename F> void EachGuard(const uint8_t *base, F &&f)
  {
    uint32_t node = L32(base, kCharList + 0x710);
    const uint32_t end = L32(base, kCharList + 0x714);
    for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
    {
      const uint32_t c = L32(base, node);
      node = L32(base, node + 8);
      if (Ptr(c) && L32(base, c) == kGuardVtable)
      {
        f(c);
      }
    }
  }

  void ScanDrops(const uint8_t *base, Clock::time_point now, bool log)
  {
    std::unordered_set<uint32_t> here;
    uint32_t ready = 0;
    EachGuard(base, [&](uint32_t c) {
      if (!IsDropClone(base, c))
      {
        return;
      }
      here.insert(c);
      g.gone.erase(c); // (a new guard at a deleted one's address)
      auto it = g.drops.find(c);
      const float y = LF(base, c + kPosition + 4);
      if (it == g.drops.end())
      {
        Drop d;
        d.first_seen = d.still_since = now;
        d.last_y = y;
        it = g.drops.emplace(c, d).first;
      }
      Drop &d = it->second;
      const uint32_t f40 = L32(base, c + kFlags40), f44 = L32(base, c + kFlags44), w186c = L32(base, c + kDeadWord);
      const uint32_t parent = L32(base, c + kParent);
      const bool hidden = (L32(base, c + kHidden) & 0x10) != 0;
      if (std::fabs(y - d.last_y) > 3.0f)
      {
        d.still_since = now; // still rising or falling
      }
      d.last_y = y;
      d.dead = Dead(base, c);
      // Landed: Rare's landing function has run (+0x186C bit 0x10), he has left the ship (+0x44 bit 0x20 clear, no
      // parent), is in the world and alive.
      d.landed = (w186c & kLanded186C) != 0 && !(f44 & kOnShip44) && !Ptr(parent) && !hidden && !d.dead;
      // Round 16 (item 7): and he stands outside the ship. Log 027 15:43:18: 'bff1' had the ship's bits clear
      // (+0x44 00000084: no 0x20) and the landed bit, yet stood inside the landed ship (its mode +0x1368 1: the troops
      // walk to its door first); player 2 took him there and was stuck. Counted from the ship's centre (+48).
      bool clear = true;
      float ship_m = -1.0f;
      if (const double need = REXCVAR_GET(pdz_counterop_rooftops_drop_clear_m); need > 0.0)
      {
        const uint32_t ship = L32(base, kShipGlobal);
        if (Ptr(ship))
        {
          const float dx = LF(base, c + kPosition) - LF(base, ship + kPosition),
                      dy = y - LF(base, ship + kPosition + 4),
                      dz = LF(base, c + kPosition + 8) - LF(base, ship + kPosition + 8);
          ship_m = std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f;
          clear = std::isfinite(ship_m) && double(ship_m) >= need;
        }
      }
      d.ready = d.landed && clear && Seconds(d.still_since, now) >= REXCVAR_GET(pdz_counterop_rooftops_drop_ground_s);
      ready += d.ready && c != g.current_guard;
      const uint32_t state = (d.dead ? 1u : 0u) | (d.landed ? 2u : 0u) | (d.ready ? 4u : 0u) | (hidden ? 8u : 0u) |
                             (Ptr(parent) ? 16u : 0u) | ((f44 & kOnShip44) ? 32u : 0u) |
                             ((f40 & kInShip40) ? 64u : 0u) | ((w186c & kLanded186C) ? 128u : 0u) |
                             (c == g.current_guard ? 256u : 0u) | (clear ? 0u : 512u);
      if (state != d.told_state && (log || (d.ready && !d.told_ready) || (d.landed && !clear)) && g.drop_lines < 400)
      {
        d.told_state = state;
        d.told_ready = d.told_ready || d.ready;
        ++g.drop_lines;
        REXLOG_INFO("pdz_counterop: rooftops: dropped guard {:08X} ('{}', {:.1f} s after it was made): {} at ({:.0f}, "
                    "{:.0f}, {:.0f}); +0x40 {:08X} (0x02000000 = made in the ship), +0x44 {:08X} (0x20 = on the ship), "
                    "+0x186C {:08X} (0x10 = landed), +0x187C {:08X}, parent {:08X}, health {:.0f}; {:.1f} m from the ship's centre{}",
                    c, IdOf(base, c), Seconds(d.first_seen, now),
                    d.dead     ? "DEAD"
                    : d.ready  ? "READY to be taken"
                    : d.landed ? (clear ? "landed" : "landed but still in or at Killian's ship: not ready")
                    : hidden   ? "hidden"
                               : "in the ship / in the air",
                    LF(base, c + kPosition), y, LF(base, c + kPosition + 8), f40, f44, w186c, L32(base, c + kAiWord),
                    parent, LF(base, c + kHealth), ship_m, c == g.current_guard ? "; player 2 drives him" : "");
      }
    });
    for (auto it = g.drops.begin(); it != g.drops.end();)
    {
      if (here.count(it->first))
      {
        ++it;
        continue;
      }
      // Rare deleted him: the pointer is freed memory from now on.
      if (g.gone.size() < 4096)
      {
        g.gone.insert(it->first);
      }
      if (g.drop_lines < 400 && (log || it->first == g.current_guard))
      {
        ++g.drop_lines;
        REXLOG_INFO("pdz_counterop: rooftops: dropped guard {:08X} is no longer in Rare's character list (deleted){}",
                    it->first, it->first == g.current_guard ? ": player 2 was driving him" : "");
      }
      it = g.drops.erase(it);
    }
    g.ready_drops = ready;
  }

  void Monitor(const uint8_t *base, Clock::time_point now)
  {
    const uint32_t script = g.script;
    const uint32_t request = L32(base, kJackRequest), progress = L32(base, kJackProgress);
    const uint32_t jack = L32(base, script + kScriptJack);
    const auto jack_text = [&]() {
      return Ptr(jack) ? fmt::format("jack {:08X} at ({:.0f}, {:.0f}, {:.0f}), health {:.0f}{}", jack,
                                     LF(base, jack + kPosition), LF(base, jack + kPosition + 4),
                                     LF(base, jack + kPosition + 8), LF(base, jack + kHealth),
                                     Dead(base, jack) ? ", DEAD" : "")
                       : std::string("no jack");
    };
    if ((request != g.seen_request || progress != g.seen_progress) && g.jack_lines < 120)
    {
      ++g.jack_lines;
      REXLOG_INFO("pdz_counterop: rooftops: jack: request word {:08X} -> {:08X} (bit 1 / 2 / 3 = the bff / bfm / bfb "
                  "group is done, 5-9 = the script's orders), progress word {:08X} -> {:08X} (bit 0-4 = pads 0x275, "
                  "0x59, 0xF3, 0x6FF, 0x391 done, 7 = his run is over); {} ({:.0f} s into the level)",
                  g.seen_request, request, g.seen_progress, progress, jack_text(), Seconds(g.level_at, now));
      g.seen_request = request;
      g.seen_progress = progress;
    }
    else if (now >= g.next_jack_line && g.jack_lines < 120)
    {
      g.next_jack_line = now + std::chrono::seconds(15);
      ++g.jack_lines;
      REXLOG_INFO("pdz_counterop: rooftops: jack: {}; request {:08X}, progress {:08X}; player 2 drives {:08X} '{}' "
                  "({:.0f} s into the level)",
                  jack_text(), request, progress, g.current_guard, IdOf(base, g.current_guard),
                  Seconds(g.level_at, now));
    }
    if (Ptr(jack))
    {
      // What Jack's own "may I fight now" test reads (821DB878): his seen target, [jack +0x3168 or +0x3150] +4; and
      // the task on top of his queue (82347 1D8-82347200).
      const uint32_t list = L32(base, jack + 0x316C) != 0 ? jack + 0x3168 : jack + 0x3150;
      const uint32_t target = L32(base, list + 4);
      const uint32_t q = jack + 0x18D4;
      const uint32_t top = ((L32(base, q + 0x52C + 0x28) - (q + 0x52C)) & ~3u) != 0 ? L32(base, L32(base, q + 0x554) - 4)
                                                                                  : L32(base, L32(base, q + 0x528) - 0x84);
      const uint32_t task = Ptr(top) ? L32(base, top) : 0;
      if ((target != g.seen_target || task != g.seen_task) && g.seen_lines < 150)
      {
        ++g.seen_lines;
        g.seen_target = target;
        g.seen_task = task;
        REXLOG_INFO("pdz_counterop: rooftops: jack: his seen target is now {:08X} ({}), the task on top of his queue "
                    "{:08X} (vtable {:08X}), his mode word +0x18CC {:X}; he is at ({:.0f}, {:.0f}, {:.0f}) ({:.0f} s "
                    "into the level)",
                    target,
                    !Ptr(target)                 ? "nobody"
                    : target == g.current_guard  ? "THE GUARD PLAYER 2 DRIVES (hidden)"
                    : target == PdzCounterOpPlayer2() ? "PLAYER 2"
                    : L32(base, target) == kGuardVtable
                        ? fmt::format("guard '{}'{}{}", IdOf(base, target), Dead(base, target) ? ", dead" : "",
                                      (L32(base, target + kHidden) & 0x10) ? ", hidden" : "")
                        : std::string("another object"),
                    top, task, L32(base, jack + 0x18CC), LF(base, jack + kPosition), LF(base, jack + kPosition + 4),
                    LF(base, jack + kPosition + 8), Seconds(g.level_at, now));
      }
    }
    const uint32_t words = L32(base, script + kScriptFlags);
    for (uint32_t w = 0; Ptr(words) && w < 5; ++w)
    {
      const uint32_t v = L32(base, words + 4 * w);
      if (v == g.seen_flags[w])
      {
        continue;
      }
      const uint32_t was = g.seen_flags[w] == ~0u ? 0u : g.seen_flags[w];
      std::string set;
      for (uint32_t b = 0; b < 32; ++b)
      {
        if (((v & ~was) >> b) & 1u)
        {
          set += (set.empty() ? "" : ", ") + std::to_string(w * 32 + b);
        }
      }
      g.seen_flags[w] = v;
      if (g.flag_lines < 300)
      {
        ++g.flag_lines;
        REXLOG_INFO("pdz_counterop: rooftops: script flag word {} {:08X} -> {:08X}: flags set: {} ({:.0f} s into the "
                    "level)",
                    w, was, v, set.empty() ? "none (cleared only)" : set, Seconds(g.level_at, now));
      }
    }
    const uint32_t ship = L32(base, kShipGlobal);
    const uint32_t troops = Ptr(ship) ? L32(base, ship + kShipTroops) : 0;
    if (ship != g.seen_ship || troops != g.seen_troops)
    {
      g.seen_ship = ship;
      g.seen_troops = troops;
      if (Ptr(ship))
      {
        REXLOG_INFO("pdz_counterop: rooftops: Killian's ship {:08X} (vtable {:08X}) at ({:.0f}, {:.0f}, {:.0f}): {} "
                    "troop(s) made so far, mode +0x1368 {}, health {:.0f} ({:.0f} s into the level)",
                    ship, L32(base, ship), LF(base, ship + kPosition), LF(base, ship + kPosition + 4),
                    LF(base, ship + kPosition + 8), troops, int32_t(L32(base, ship + 0x1368)),
                    LF(base, ship + kHealth), Seconds(g.level_at, now));
      }
    }
  }

  void TestTool(PPCContext &ctx, uint8_t *base, uint32_t agent, Clock::time_point now)
  {
    const std::string cmd = REXCVAR_GET(pdz_counterop_test_rooftops);
    if (cmd != g.last_test)
    {
      g.last_test = cmd;
      int n = 0;
      char ids[96] = {};
      if (std::sscanf(cmd.c_str(), "flag:%d", &n) == 1 && n >= 0 && n < 160)
      {
        const uint32_t words = L32(base, g.script + kScriptFlags);
        if (Ptr(words))
        {
          const uint32_t at = words + 4 * uint32_t(n / 32), was = L32(base, at);
          S32(base, at, was | (1u << (n % 32)));
          REXLOG_INFO("pdz_counterop: rooftops: TEST TOOL: script flag {} set (word {} {:08X} -> {:08X}); Rare's flag "
                      "handler 8234B2F8 sees the change on its next call",
                      n, n / 32, was, L32(base, at));
        }
      }
      else if (std::sscanf(cmd.c_str(), "kill:%95s", ids) == 1)
      {
        const std::string list = std::string(",") + ids + ",";
        std::vector<uint32_t> victims;
        EachGuard(base, [&](uint32_t c) {
          const std::string id = IdOf(base, c);
          if (!id.empty() && !IsDropClone(base, c) && !Dead(base, c) && c != g.current_guard &&
              list.find("," + id + ",") != std::string::npos)
          {
            victims.push_back(c);
          }
        });
        for (const uint32_t c : victims)
        {
          const float before = LF(base, c + kHealth);
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = c;
          frame.ctx.r4.u64 = agent;
          frame.ctx.r5.u64 = 5;
          frame.ctx.r6.u64 = 1;
          sub_8252DE88(frame.ctx, base);
          REXLOG_INFO("pdz_counterop: rooftops: TEST TOOL: guard '{}' {:08X} killed through Rare's kill (credited to "
                      "the agent): health {:.0f} -> {:.0f}, dead word +0x186C {:08X}",
                      IdOf(base, c), c, before, LF(base, c + kHealth), L32(base, c + kDeadWord));
        }
        REXLOG_INFO("pdz_counterop: rooftops: TEST TOOL: 'kill:{}': {} guard(s) killed (the guard player 2 drives is "
                    "never one of them)",
                    ids, victims.size());
      }
      else if (std::sscanf(cmd.c_str(), "p2kill:%d", &n) == 1)
      {
        g.test_kills_left = n;
        REXLOG_INFO("pdz_counterop: rooftops: TEST TOOL: up to {} time(s), when a dropped guard is ready and player 2 "
                    "is not one already, pdz_counterop_test_kill_p2 goes up by one",
                    n);
      }
      else if (!cmd.empty())
      {
        REXLOG_INFO("pdz_counterop: rooftops: TEST TOOL: command '{}' not understood (flag:N | kill:id,id | p2kill:N)",
                    cmd);
      }
    }
    if (g.test_kills_left > 0 && g.ready_drops > 0 && !g.out && Ptr(g.current_guard) &&
        !IsDropClone(base, g.current_guard) && Seconds(g.last_test_kill, now) >= 8.0)
    {
      --g.test_kills_left;
      g.last_test_kill = now;
      const int32_t next = REXCVAR_GET(pdz_counterop_test_kill_p2) + 1;
      REXCVAR_SET(pdz_counterop_test_kill_p2, next);
      REXLOG_INFO("pdz_counterop: rooftops: TEST TOOL: {} dropped guard(s) ready and player 2 drives '{}': "
                  "pdz_counterop_test_kill_p2 -> {}",
                  g.ready_drops, IdOf(base, g.current_guard), next);
    }
  }
} // namespace

// The level tick (counter_op.cpp, every frame of a running Counter-Op game).
void PdzCounterOpRooftopsTick(PPCContext &ctx, uint8_t *base, uint32_t p1c, uint32_t current_guard, bool out,
                              bool cutscene)
{
  const uint32_t script = ScriptOf(base);
  if (script != g.script)
  {
    g = State{};
    g.script = script;
    g.level_at = Clock::now();
    if (script)
    {
      REXLOG_INFO("pdz_counterop: rooftops: level script {:08X}: Jack's rule {} (pdz_counterop_rooftops_jack), "
                  "Killian's dropped guards {} (pdz_counterop_rooftops_killian_drops)",
                  script, REXCVAR_GET(pdz_counterop_rooftops_jack) ? "ON" : "off",
                  REXCVAR_GET(pdz_counterop_rooftops_killian_drops) ? "ON" : "off");
    }
  }
  if (!script)
  {
    return;
  }
  g.current_guard = current_guard;
  g.out = out;
  const auto now = Clock::now();
  const bool log = REXCVAR_GET(pdz_counterop_test_rooftops_log);
  const bool drops = REXCVAR_GET(pdz_counterop_rooftops_killian_drops);
  if (now >= g.next_scan)
  {
    g.next_scan = now + std::chrono::milliseconds(250);
    EachGuard(base, [&](uint32_t c) { g.ever.insert(c); });
    if (drops || log)
    {
      ScanDrops(base, now, log);
    }
    if (log)
    {
      Monitor(base, now);
    }
    if (drops && !g.told_fight && LastFight(base, script))
    {
      g.told_fight = true;
      REXLOG_INFO("pdz_counterop: rooftops: the last fight has begun (script flag 58): from now until Killian's ship "
                  "is down (flag 22) player 2 is never out for good, and a guard the ship drops may be taken once he "
                  "has stood on the ground for {:.1f} s",
                  REXCVAR_GET(pdz_counterop_rooftops_drop_ground_s));
    }
  }
  if (!cutscene)
  {
    TestTool(ctx, base, p1c, now);
  }
}

// At a level boundary (counter_op.cpp's reset): nothing of the old level is kept.
void PdzCounterOpRooftopsReset() { g = State{}; }

// counter_op_guards.cpp Takeable(): `character` is a clone made after the opening (a reinforcement).
bool PdzCounterOpRooftopsDropTakeable(uint32_t character)
{
  if (!g.script || !REXCVAR_GET(pdz_counterop_rooftops_killian_drops))
  {
    return false;
  }
  const auto it = g.drops.find(character);
  return it != g.drops.end() && it->second.ready && !it->second.dead;
}

// counter_op_guards.cpp StillExists(): true = this guard was in Rare's character list earlier on this level and is
// no longer there: Rare has deleted it and the pointer is freed memory (never touch it again). Rooftops deletes
// guards under a running game twice: the first 29 placed guards when Killian arrives (script flag 37: the flag
// handler 8234C6A8-8234C7B8 sets +0x40 bit 0x8000 on bff1 .. top3, and the world update 8251B978 deletes an object
// with that bit; a hidden guard is not in the world list, so it goes at the moment it is put back), and dropped
// guards. Asked before counter_op.cpp restores or kills the guard player 2 drove, and by the "originals left" count.
bool PdzCounterOpRooftopsDropGone(const uint8_t *base, uint32_t character)
{
  if (!g.script || !Ptr(character) || !g.ever.count(character))
  {
    return false;
  }
  bool found = false;
  EachGuard(base, [&](uint32_t c) { found = found || c == character; });
  if (!found)
  {
    g.drops.erase(character);
    g.gone.insert(character);
    if (g.told_gone.size() < 80 && g.told_gone.insert(character).second)
    {
      REXLOG_INFO("pdz_counterop: rooftops: guard {:08X} was deleted by Rare (no longer in its character list): the "
                  "pointer is dropped, nothing of his is touched",
                  character);
    }
  }
  return !found;
}

// counter_op_guards.cpp Takeable(), first line: -1 = not a dropped guard (the usual rules decide), 0 = a dropped
// guard that may not be taken (yet), 1 = one that may. Asked first because a dropped guard can be built at the address
// of a deleted placed guard, whose old record in counter_op_guards would speak for him.
int PdzCounterOpRooftopsDropVerdict(uint32_t character)
{
  if (!g.script)
  {
    return -1;
  }
  const auto it = g.drops.find(character);
  if (it == g.drops.end())
  {
    // The ten guards of Jack's three waits (lists A, B, C: 8-byte handles, the guard at +4) stay the game's: his
    // run is then exactly Rare's (pdz_counterop_rooftops_jack_groups_closed).
    if (character && REXCVAR_GET(pdz_counterop_rooftops_jack_groups_closed))
    {
      const uint8_t *base = rex::Runtime::instance()->virtual_membase();
      static constexpr struct { uint32_t list; uint32_t count; } kLists[] = {{kJackListA, 3}, {kJackListB, 3}, {kJackListC, 4}};
      for (const auto &l : kLists)
      {
        for (uint32_t i = 0; base && i < l.count; ++i)
        {
          if (L32(base, l.list + 8 * i + 4) == character)
          {
            if (g.closed_told.insert(character).second)
            {
              REXLOG_INFO("pdz_counterop: rooftops: jack: guard '{}' ({:08X}) is one of the guards Jack's run waits "
                          "for: never given to player 2 (pdz_counterop_rooftops_jack_groups_closed)",
                          IdOf(base, character), character);
            }
            return 0;
          }
        }
      }
    }
    return -1;
  }
  return REXCVAR_GET(pdz_counterop_rooftops_killian_drops) && it->second.ready && !it->second.dead ? 1 : 0;
}

// counter_op_wake.cpp AreaAllows(): a dropped guard that may be taken is in the agent's fight, whatever the area table
// says of the template's setup record.
bool PdzCounterOpRooftopsDropAreaOk(uint32_t guard, std::string &note)
{
  if (!PdzCounterOpRooftopsDropTakeable(guard))
  {
    return false;
  }
  note = fmt::format("area rule: guard {:08X} is a guard Killian's ship dropped into the agent's fight: allowed "
                     "(pdz_counterop_rooftops_killian_drops)",
                     guard);
  return true;
}

// counter_op.cpp EligibleLeft() and the spectator's way back: 1 while Rooftops' last fight runs (the ship can still
// drop him a body), else 0.
uint32_t PdzCounterOpRooftopsDropsLeft()
{
  if (!g.script || !REXCVAR_GET(pdz_counterop_rooftops_killian_drops) || !PdzCounterOpRunning())
  {
    return 0;
  }
  const uint8_t *base = rex::Runtime::instance()->virtual_membase();
  return base && LastFight(base, g.script) ? 1u : 0u;
}

// counter_op_unheard.cpp's seen-list iterator hook, after Rare's call (as PdzCounterOpAllySeenSkip does for player 2
// himself): the guard player 2 drives is hidden, but a viewer who saw him before keeps the pair's record. For an
// ally of the agent (Jack) that record is stepped over, so the hidden guard is nobody's target any more.
void PdzCounterOpRooftopsSeenSkip(PPCContext &ctx, uint8_t *base, uint32_t it, uint32_t out)
{
  const uint32_t his = g.current_guard;
  if (!his || ctx.r3.u32 != his || !g.script || !REXCVAR_GET(pdz_counterop_rooftops_jack) || !Ptr(it) ||
      !PdzCounterOpRunning())
  {
    return;
  }
  constexpr uint32_t kSlots = 0x82E5D590, kSlotSize = 0x50, kSlotCount = 170, kSlotOwner = 0x04; // perception slots
  const uint32_t slot = L32(base, it + 4);
  const uint32_t viewer = slot < kSlotCount ? L32(base, kSlots + slot * kSlotSize + kSlotOwner) : 0;
  if (!PdzCounterOpIsAgentAlly(base, viewer))
  {
    return;
  }
  for (int i = 0; i < 4 && ctx.r3.u32 == his; ++i)
  {
    if (++g.seen_skips == 1)
    {
      REXLOG_INFO("pdz_counterop: rooftops: the hidden guard player 2 drives ('{}', {:08X}) is left out of the seen "
                  "list of {:08X}, an ally of the agent (Rare's iterator 8218B498; the first this level)",
                  IdOf(base, his), his, viewer);
    }
    ctx.r3.u64 = it;
    ctx.r4.u64 = out;
    __imp__sub_8218B498(ctx, base);
  }
}

// JACKL3's "is this guard group done" (list r3: 8-byte handles, the guard at +4; count r4; jack r5). Called up to
// three times a frame by the Rooftops script's Update (through 821DA6D0) until each group is done.
REX_HOOK_RAW(sub_821DB990)
{
  const uint32_t list = ctx.r3.u32, his = g.current_guard;
  const int32_t count = ctx.r4.s32;
  if (!his || !g.script || !REXCVAR_GET(pdz_counterop_rooftops_jack) || !PdzCounterOpRunning() || !Ptr(list) ||
      count <= 0 || count > 8)
  {
    __imp__sub_821DB990(ctx, base);
    return;
  }
  uint32_t at = 0;
  for (int32_t i = 0; i < count && !at; ++i)
  {
    if (L32(base, list + 8 * uint32_t(i) + 4) == his)
    {
      at = list + 8 * uint32_t(i) + 4;
    }
  }
  if (!at)
  {
    __imp__sub_821DB990(ctx, base);
    return;
  }
  // The guard player 2 drives reads as "not there" for this one call (Rare's own loops step over an empty handle):
  // not waited for, and not killed by the time-out.
  S32(base, at, 0);
  __imp__sub_821DB990(ctx, base);
  if (L32(base, at) == 0)
  {
    S32(base, at, his);
  }
  const bool done = (ctx.r3.u32 & 0xFF) != 0;
  const uint64_t key = (uint64_t(list) << 32) | his;
  if (g.hook_lines < 40 && (done || g.told_list.insert(key).second))
  {
    ++g.hook_lines;
    REXLOG_INFO("pdz_counterop: rooftops: jack: his guard group {} holds '{}' ({:08X}), the guard player 2 drives: "
                "not counted and not killed by Rare's time-out; Rare's answer for the rest of the group: {}",
                list == kJackListA   ? "A (bff1, bff2, bff4)"
                : list == kJackListB ? "B (bfm1-3)"
                : list == kJackListC ? "C (bfb1-4)"
                                     : "?",
                IdOf(base, his), his, done ? "DONE: Jack moves on" : "not done yet (others alive)");
  }
}
