// Stealth layer, Thu 19 Nov (day job): fixes to the calm-guard part after the hand test of Thu 19 Nov (log 022,
// Nightclub then Subway). Included by lab_stealth.cpp inside its anonymous namespace, after
// lab_stealth_r9_Wed18Nov.inc. Log prefix "pdz_stealth_r10:". Seen = Rare's guard and loud = Rare's guard (r9) stay as
// they are; everything here only applies with pdz_lab_guard_stealth on, and each rule has its own switch.
//
//   1 FAR GUNFIRE (pdz_stealth_far_gunfire_to_shooter). A calm guard whose alert is ONLY another guard's gunfire
//     (Rare's 0400 "gunfire with a target") or Rare's friend alert (0008), with no sighting of Jo, no hit, no near miss,
//     no death in view, no alarm and no loud shot of Jo's, is Rare's guard for the layer (no rule of ours) but does not
//     get Jo's position: those bits are taken from his word (and the squad's target copy is not written for him) and he
//     runs (Rare's go-to-pad with Rare's own hurry byte, as the chase route passes it) to the guard who fired. He is
//     Rare's whole, with Jo's position, the moment Rare's sight has Jo, Jo is loud, he is hit, the alarm sounds, or he
//     comes within 10 m of the guard who fired while that guard still fights (Rare's own "alert now" input is raised
//     for him then, as a friend's COMBAT start raises it). He arrives and the guard who fired is dead or no longer
//     fighting: Rare's SEARCH from that spot. No route, no progress for 2 s, or 10 s without arriving: Rare's SEARCH
//     from where he is. A SEARCH that cannot be handed over in 3 s: Rare's own alert (as in the original game).
//   2 SECOND BODY (pdz_stealth_second_body). A guard who already knows of a body, or is sure someone is here (his
//     stronger line, or the meter at the far-dark hold), and then sees a friend killed or a fresh body: Rare's guard at
//     once (rule 3 LOUD, "second body"). A first kill keeps the witness look first.
//   3 NO GIVE-UP LINE AFTER A BODY (pdz_stealth_no_giveup_after_body). A check of ours that ends within 3 minutes of
//     his reaction to a body plays no give-up line: he is handed to Rare (Rare's guard, his own follow-up; Rare's
//     SEARCH at the body if Rare starts nothing in 2 s).
//   4 NO FREEZING. (a) pdz_stealth_post_release: back to his post finds no route: our walk ends and he is Rare's
//     DEFAULT again (no look round of ours). (b) pdz_stealth_body_still_followup: a guard who knows of a body and has
//     stood still for 8 s gets Rare's "a body" let through again (at most 3 times per body, 20 s apart). (c)
//     pdz_stealth_sure_moves: a sure guard (his stronger line, meter at the hold) who stands still with nothing to do
//     gets Rare's SEARCH towards his glimpse (also a no_look row, unless his talk is the objective).
//   5 NO STEER ONCE (pdz_stealth_no_steer_once). After a guard's first NO STEER PAUSE his target holders are not
//     emptied again until he is calm (unaware) again. The meter and the far-dark hold still hide the sighting.
//   6 LAMPS (pdz_stealth_lamp_fresh_hearing). Every guard who notices a lamp gets one line with the cause (in his
//     view / its light / heard: distance, walls or floors, how long after the glass broke). Hearing counts only while
//     the break is fresh (2.5 s); a lamp that takes longer to go dark is noticed by sight only.

constexpr uint32_t kR10FarBits = 0x400u | 0x8u;                                // gunfire with a target, alerted by a friend
constexpr uint32_t kR10RealBits = 0x2u | 0x10u | 0x20u | 0x80u | 0x100u | 0x200u; // hit, death / hurt friend in view, near miss, saw, alarm
constexpr float kR10JoinCm = 1000.0f;   // within this of the guard who fired, while that guard fights: he joins (Rare's whole)
constexpr float kR10ArriveCm = 300.0f;  // at the pad he ran to
constexpr float kR10LimitS = 10.0f;     // neither arrived nor saw Jo after this: Rare's SEARCH where he is
constexpr float kR10StuckS = 2.0f;      // no 50 cm of progress for this long: no route
constexpr float kR10LampFreshS = 2.5f;  // a lamp is heard only this soon after the glass broke
// (g_r10_lamp_age, set by the lamp watch around its LightIncident call, is defined with the forward declarations.)

bool R10FarOn()
{
  return R9On() && REXCVAR_GET(pdz_stealth_far_gunfire_to_shooter) && REXCVAR_GET(pdz_stealth_loud_is_rares) &&
         REXCVAR_GET(pdz_lab_guard_stealth);
}

// The guard (or Jo) at `who` is alive and in a fight (Rare's COMBAT on his stack, or the layer has him fighting).
bool R10Fighting(const uint8_t *base, uint32_t who)
{
  if (!Ptr(who) || !(LoadF(base, who + kHealth) > 0.0f))
  {
    return false;
  }
  Guard *o = Find(who);
  if (o && (o->dead || o->st == St::Dead))
  {
    return false;
  }
  return CombatOnStack(base, who) || (o && o->st == St::Combat);
}

// A loud thing of Jo's he hears now: her unsilenced shot or an explosion in his reach in the last `within` s.
const char *R10JoLoud(const uint8_t *base, uint32_t c, float now, float within)
{
  const uint32_t jo = Load32(base, kPlayerOne);
  const uint32_t data = Load32(base, c + kData);
  const float factor = Ptr(data) ? std::max(1.0f, LoadF(base, data + 0x48)) : 1.0f;
  float me[3];
  PosOf(base, c, me);
  if (Ptr(jo))
  {
    for (const Shot &sh : g_shots)
    {
      if (sh.shooter != jo || sh.silenced || now < sh.at || now - sh.at > within)
      {
        continue;
      }
      const int32_t slot = int32_t(Load32(base, jo + kProfileSlot));
      const float radius = slot >= 0 && slot < int32_t(kProfileSlots) ? LoadF(base, kProfileTable + 0x50 * uint32_t(slot) + 0x30) : 0.0f;
      float src[3];
      PosOf(base, jo, src);
      if (Dist3(me, src) < std::max(std::isfinite(radius) ? radius : 0.0f, 1500.0f) * factor)
      {
        return "an unsilenced shot of Jo's";
      }
    }
  }
  for (const Blast &b : g_blasts)
  {
    const float age = now - b.at;
    if (age >= 0.0f && age < std::min(within, 1.5f + b.radius / 1500.0f) && Dist3(me, b.pos) < b.radius)
    {
      return "an explosion";
    }
  }
  return nullptr;
}

// Who raised his far alert: the newest unsilenced shooter (not Jo, not himself) in his reach for 0400, else the nearest
// fighting guard for 0008. 0 = none found (the word is left to Rare: if in doubt, Rare's).
uint32_t R10Source(const uint8_t *base, uint32_t c, uint32_t word, float now, const char *&how)
{
  const uint32_t jo = Load32(base, kPlayerOne);
  const uint32_t data = Load32(base, c + kData);
  const float factor = Ptr(data) ? std::max(1.0f, LoadF(base, data + 0x48)) : 1.0f;
  float me[3];
  PosOf(base, c, me);
  uint32_t best = 0;
  float best_at = -1e9f;
  if (word & 0x400)
  {
    for (const Shot &sh : g_shots)
    {
      if (sh.silenced || sh.shooter == c || sh.shooter == jo || !Ptr(sh.shooter) || now < sh.at || now - sh.at > 2.5f || sh.at <= best_at)
      {
        continue;
      }
      const int32_t slot = int32_t(Load32(base, sh.shooter + kProfileSlot));
      const float radius = slot >= 0 && slot < int32_t(kProfileSlots) ? LoadF(base, kProfileTable + 0x50 * uint32_t(slot) + 0x30) : 0.0f;
      float src[3];
      PosOf(base, sh.shooter, src);
      if (Dist3(me, src) < std::max(std::isfinite(radius) ? radius : 0.0f, 1500.0f) * factor)
      {
        best = sh.shooter;
        best_at = sh.at;
      }
    }
    if (best)
    {
      how = "the gunfire of";
      return best;
    }
  }
  if (word & 0x8)
  {
    float best_d = 6000.0f;
    for (const auto &[other, o] : g_guards)
    {
      if (other == c || o.dead || o.st == St::Dead || !R10Fighting(base, other))
      {
        continue;
      }
      const float d = Dist(base, c, other);
      if (d < best_d)
      {
        best_d = d;
        best = other;
      }
    }
    if (best)
    {
      how = "the friend alert of";
    }
  }
  return best;
}

// His far alert is over. `join`: Rare's "alert now" input is raised for him (as a friend's COMBAT start raises it), so
// that Rare's own reaction (with the squad's position of Jo) follows at once. Our run on top of his stack ends at his
// next tick (R9Tick, WALK END).
void R10End(const uint8_t *base, uint32_t c, Guard &g, float now, bool join, const std::string &why)
{
  if (!g.r10_far)
  {
    return;
  }
  g.r10_far = false;
  g.r10_stage = 0;
  g.r9_end_walk = true;
  if (join && Ptr(c))
  {
    Store32(const_cast<uint8_t *>(base), c + kStimuli, Load32(base, c + kStimuli) | 0x40u);
  }
  float me[3];
  PosOf(base, c, me);
  REXLOG_INFO("pdz_stealth_r10: FAR GUNFIRE END: {} after {:.1f} s: {}{} | he is Rare's guard (whole) from here | he is at ({:.0f} "
              "{:.0f} {:.0f}), {:.0f} cm from {} | {}",
              Id(base, c), now - g.r10_at, why, join ? " (Rare's \"alert now\" raised for him: Rare's own reaction follows)" : "", me[0],
              me[1], me[2], Ptr(g.r10_by) ? Dist(base, c, g.r10_by) : -1.0f, Ptr(g.r10_by) ? Id(base, g.r10_by) : std::string("?"),
              StackText(base, c));
}

// From the stimulus word hook, before R9Stimulus. True: he is (now or already) a far guard: `drop` = the bits taken
// from Rare's word. False: nothing of this rule (R9Stimulus and the other rules decide as before).
bool R10FarStimulus(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now, uint32_t &drop)
{
  drop = 0;
  if (!R10FarOn() || g.scientist || !LayerGuard(g))
  {
    if (g.r10_far)
    {
      R10End(base, c, g, now, false, "the far-gunfire rule is off");
    }
    return false;
  }
  if (g.r10_far)
  {
    if (!g.r9_rares)
    {
      g.r10_far = false;
      return false;
    }
    const char *why = (word & 0x2)     ? "he was hit (Rare's word: damaged)"
                      : (word & 0x100) ? "he saw Jo (Rare's word: a valid target)"
                      : (word & 0x80)  ? "a near miss (Rare's word)"
                      : (word & 0x200) ? "the alarm (Rare's word)"
                      : (word & 0x30)  ? "a friend died or was hurt in his view (Rare's word)"
                                       : nullptr;
    const char *loud = why ? nullptr : R10JoLoud(base, c, now, 2.5f);
    if (why || loud)
    {
      R10End(base, c, g, now, false, why ? why : fmt::format("he heard {}", loud));
      return false; // (the word is Rare's, whole: R9Stimulus sees Rare's guard)
    }
    drop = word & (kR10FarBits | 0x1000u | 0x4u);
    return true;
  }
  // (Thu 19 Nov night, log 025 21:23:49: s01a, 21 m from s19b, already in Rare's own search after a body (Rare's guard,
  // rule 3) and with no sighting of Jo, took Rare's friend alert and with it Jo's position: the rule covered calm guards
  // only. A guard in Rare's search who has not seen Jo gets the same rule (pdz_stealth_far_gunfire_searching).)
  const bool searching = g.r9_rares && REXCVAR_GET(pdz_stealth_far_gunfire_searching) && g.r9_rule != 2 && !g.spotted &&
                         g.st != St::Combat && !CombatOnStack(base, c) &&
                         (now - g.search_seen < 0.5f || (Load32(base, c + kConfig) & kSearching) != 0);
  if ((g.r9_rares && !searching) || !(word & kR10FarBits) || (word & kR10RealBits) || AlarmOn(base) || g.dead || g.st == St::Dead)
  {
    return false;
  }
  const uint32_t jo = Load32(base, kPlayerOne);
  const uint32_t sight = Load32(base, c + kSight);
  if ((sight & (kValidTarget | 0x400000u)) != 0 || R2Seen(g, now, 10.0f) || (now >= g.hurt_at && now - g.hurt_at < 5.0f) ||
      (now >= g.r2_witness_at && now - g.r2_witness_at < 10.0f) || R10JoLoud(base, c, now, 2.5f) || FightsNonPlayer(base, c) ||
      (Ptr(jo) && Dist(base, c, jo) < 300.0f))
  {
    return false; // (he knows of Jo another way: Rare's, as before)
  }
  const char *how = "";
  const uint32_t src = R10Source(base, c, word, now, how);
  if (!Ptr(src) || FightsNonPlayer(base, src))
  {
    return false;
  }
  const float d = Dist(base, c, src);
  if (d <= kR10JoinCm && R10Fighting(base, src))
  {
    return false; // (the guard who fired is beside him and fighting: Rare's at once)
  }
  if (!searching && !R9Make(base, c, g, 3,
                            fmt::format("another guard's gunfire only ({} {}, {:.0f} cm away): NOT Jo's position: he runs to the guard "
                                        "who fired (pdz_stealth_far_gunfire_to_shooter)",
                                        how, Id(base, src), d),
                            now))
  {
    return false;
  }
  g.r10_far = true;
  g.r10_stage = 0;
  g.r10_by = src;
  PosOf(base, src, g.r10_pos);
  g.r10_at = now;
  g.r10_try = -1e9f;
  g.r10_walk = 0;
  g.r10_pad = 0;
  g.r10_legs = 0;
  g.r10_how = how;
  drop = word & (kR10FarBits | 0x1000u | 0x4u);
  REXLOG_INFO("pdz_stealth_r10: FAR GUNFIRE: {}{}: Rare's word {:04X} ({} {} {:.0f} cm away at ({:.0f} {:.0f} {:.0f}), no sighting of Jo, "
              "no hit; Jo {:.0f} cm away): bits {:04X} taken: he does NOT get Jo's position; he runs to {} as Rare's alerted guard | {}",
              Id(base, c), searching ? " (already in Rare's own search, no sighting of Jo: pdz_stealth_far_gunfire_searching)" : "", word, how,
              Id(base, src), d, g.r10_pos[0], g.r10_pos[1], g.r10_pos[2], Ptr(jo) ? Dist(base, c, jo) : -1.0f, drop, Id(base, src),
              StackText(base, c));
  return true;
}

// Rare's SEARCH is due for him (far gunfire's end, or the hand-over after a body): stage 2.
void R10SearchDue(Guard &g, float now, uint32_t pad, const std::string &why)
{
  g.r10_stage = 2;
  g.r10_search_pad = pad;
  g.r10_search_from = now;
  g.r10_search_try = -1e9f;
  g.r10_search_why = why;
}

// The stage-2 hand-over: Rare's SEARCH at the pad, retried; after 3 s Rare's own alert. True while it is pending.
bool R10SearchTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
{
  if (g.r10_stage != 2)
  {
    return false;
  }
  if (g.r10_far && !in_combat && (in_search || (Load32(base, c + kConfig) & kSearching) != 0))
  {
    g.r10_stage = 3; // (a far guard in Rare's own search: still held; R10RaresTick)
    g.r10_search_from = now;
    return false;
  }
  if (in_combat || in_search || (Load32(base, c + kConfig) & kSearching) != 0)
  {
    g.r10_stage = 0;
    if (g.r10_far)
    {
      R10End(base, c, g, now, false, g.r10_search_why + ": Rare's own " + (in_combat ? "COMBAT" : "SEARCH") + " runs for him already");
    }
    else
    {
      REXLOG_INFO("pdz_stealth_r10: HANDED TO RARE: {}: {}: Rare's own {} runs for him already | {}", Id(base, c), g.r10_search_why,
                  in_combat ? "COMBAT" : "SEARCH", StackText(base, c));
    }
    return false;
  }
  if (now >= g.r10_search_try - 5.0f && now < g.r10_search_try + 0.3f)
  {
    return true; // (also a first try set in the future: Rare's own reaction first)
  }
  g.r10_search_try = now;
  const uint32_t own = Load32(base, c + kPad);
  const uint32_t pad = Ptr(g.r10_search_pad) ? g.r10_search_pad : own;
  if (Ptr(pad) && GiveSearch(ctx, base, c, pad))
  {
    float pp[3] = {};
    PadPos(base, pad, pp);
    g.r10_stage = g.r10_far ? 3 : 0;
    const std::string what = fmt::format("{}: Rare's SEARCH handed over at pad {:08X} ({:.0f} {:.0f} {:.0f}): his own search, whole "
                                         "(no cap, no give-up line of ours)",
                                         g.r10_search_why, pad, pp[0], pp[1], pp[2]);
    if (g.r10_far)
    {
      // (Thu 19 Nov, test far_1: handed over and then the next gunfire gave him Jo's position anyway. He stays a far
      // guard through Rare's search: the gunfire bits are still taken until he sees, hears or is hit, or a fighting
      // guard is within 10 m of him.)
      REXLOG_INFO("pdz_stealth_r10: FAR GUNFIRE SEARCH: {} after {:.1f} s: {} | still no position of Jo from other guards' gunfire "
                  "while he searches | {}",
                  Id(base, c), now - g.r10_at, what, StackText(base, c));
    }
    else
    {
      REXLOG_INFO("pdz_stealth_r10: HANDED TO RARE: {}: {} | {}", Id(base, c), what, StackText(base, c));
    }
    return false;
  }
  if (now - g.r10_search_from >= 3.0f)
  {
    g.r10_stage = 0;
    if (g.r10_far)
    {
      R10End(base, c, g, now, true, g.r10_search_why + ": no SEARCH could be handed over in 3 s: Rare's own alert, as in the original game");
    }
    else
    {
      REXLOG_INFO("pdz_stealth_r10: HANDED TO RARE: {}: {}: no SEARCH could be handed over in 3 s: Rare's DEFAULT has him | {}", Id(base, c),
                  g.r10_search_why, StackText(base, c));
    }
    return false;
  }
  return true;
}

// Once a tick while he is Rare's guard (R9Tick, after its walk end): his far run, and a pending hand-over.
void R10RaresTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_combat, bool in_search)
{
  if (g.r10_stage == 2)
  {
    R10SearchTick(ctx, base, c, g, now, in_combat, in_search);
    return;
  }
  if (!g.r10_far)
  {
    return;
  }
  const uint32_t sight = Load32(base, c + kSight);
  const uint32_t src = g.r10_by;
  if (in_combat)
  {
    R10End(base, c, g, now, false, "Rare's COMBAT started for him (Rare's own)");
    return;
  }
  if ((sight & (kValidTarget | 0x400000u)) != 0 && Ptr(jo) && TargetOf(base, c) == jo && !FightsNonPlayer(base, c))
  {
    R10End(base, c, g, now, false, fmt::format("he SAW Jo (Rare's own sight word {:08X})", sight));
    return;
  }
  if (now >= g.hurt_at && now - g.hurt_at < 1.0f)
  {
    R10End(base, c, g, now, false, "he was HIT");
    return;
  }
  if (const char *loud = R10JoLoud(base, c, now, 1.0f))
  {
    R10End(base, c, g, now, false, fmt::format("he HEARD Jo ({})", loud));
    return;
  }
  if (AlarmOn(base))
  {
    R10End(base, c, g, now, false, "the alarm sounds");
    return;
  }
  if (R10Fighting(base, src) && Dist(base, c, src) <= kR10JoinCm)
  {
    R10End(base, c, g, now, true, fmt::format("JOINED THE FIGHT: within {:.0f} cm of {}, who still fights Jo: his mate shows him where she is",
                                              Dist(base, c, src), Id(base, src)));
    return;
  }
  if (g.r10_stage == 3)
  {
    // In Rare's search (ours handed over): a fighting guard within 10 m shows him where she is.
    for (const auto &[other, o] : g_guards)
    {
      if (other != c && !o.dead && o.st != St::Dead && R10Fighting(base, other) && Dist(base, c, other) <= kR10JoinCm)
      {
        R10End(base, c, g, now, true, fmt::format("JOINED THE FIGHT during his search: {} fights Jo {:.0f} cm from him", Id(base, other),
                                                  Dist(base, c, other)));
        return;
      }
    }
    if (!in_search && (Load32(base, c + kConfig) & kSearching) == 0 && now - g.r10_search_from > 3.0f &&
        StackDepth(base, c) == 1 && R2TopClass(base, c) == kR9DefaultVtable)
    {
      R10End(base, c, g, now, false, "Rare's SEARCH is over: no fight, no sighting (Rare's DEFAULT has him; he is Rare's guard until calm)");
    }
    return;
  }
  if (in_search || (Load32(base, c + kConfig) & kSearching) != 0)
  {
    g.r10_stage = 3;
    g.r10_search_from = now;
    REXLOG_INFO("pdz_stealth_r10: FAR GUNFIRE SEARCH: {} after {:.1f} s: Rare's own SEARCH started for him | still no position of Jo "
                "from other guards' gunfire while he searches | {}",
                Id(base, c), now - g.r10_at, StackText(base, c));
    return;
  }
  const float t = now - g.r10_at;
  float me[3];
  PosOf(base, c, me);
  const uint32_t own = Load32(base, c + kPad);
  if (g.r10_stage == 0)
  {
    if (now - g.r10_try < 0.25f && now >= g.r10_try)
    {
      return;
    }
    g.r10_try = now;
    uint32_t pad = 0;
    if (R10Fighting(base, src) || (Ptr(src) && LoadF(base, src + kHealth) > 0.0f))
    {
      PosOf(base, src, g.r10_pos); // (where the guard who fired is now)
      pad = Load32(base, src + kPad);
    }
    if (!Ptr(pad) && Ptr(own))
    {
      float pd = 0.0f;
      pad = NearestPadTo(base, own, g.r10_pos, pd);
    }
    const uint32_t walk = Ptr(pad) && R2TopClass(base, c) == kR9DefaultVtable && StackWaiting(base, c) == 0
                              ? WalkToPad(ctx, base, c, pad, 1)
                              : 0;
    if (walk)
    {
      float pp[3] = {};
      PadPos(base, pad, pp);
      g.r10_stage = 1;
      g.r10_walk = walk;
      g.r10_pad = pad;
      g.r10_best = Dist3(me, pp);
      g.r10_prog_at = now;
      ++g.r10_legs;
      REXLOG_INFO("pdz_stealth_r10: FAR GUNFIRE RUN: {} runs (Rare's go-to-pad, Rare's hurry byte) to pad {:08X} at ({:.0f} {:.0f} {:.0f}), "
                  "{:.0f} cm from him, by {} ({} {:.0f} cm from him), {:.1f} s after the alert | {}",
                  Id(base, c), pad, pp[0], pp[1], pp[2], g.r10_best, Id(base, src), R10Fighting(base, src) ? "fighting," : "not fighting,",
                  Ptr(src) ? Dist(base, c, src) : -1.0f, t, StackText(base, c));
    }
    else if (t >= 1.5f)
    {
      R10SearchDue(g, now, own, fmt::format("no run could start in 1.5 s ({}): Rare's SEARCH from where he is",
                                            Ptr(pad) ? "his task stack was busy" : "no pad towards the guard who fired"));
    }
    return;
  }
  // Stage 1: he runs.
  float pp[3] = {};
  PadPos(base, g.r10_pad, pp);
  const float d = Dist3(me, pp);
  if (d < g.r10_best - 50.0f)
  {
    g.r10_best = d;
    g.r10_prog_at = now;
  }
  const bool live = TaskLive(base, c, g.r10_walk);
  if (d <= kR10ArriveCm || !live)
  {
    if (R10Fighting(base, src) && Dist(base, c, src) <= 1500.0f)
    {
      R10End(base, c, g, now, true, fmt::format("JOINED THE FIGHT: arrived beside {}, who still fights Jo", Id(base, src)));
    }
    else if (d <= 2.0f * kR10ArriveCm)
    {
      R10SearchDue(g, now, g.r10_pad,
                   fmt::format("ARRIVED ({:.0f} cm from the pad) and {} {}: Rare's SEARCH from that spot", d, Id(base, src),
                               Ptr(src) && LoadF(base, src + kHealth) > 0.0f ? "is no longer fighting" : "is dead"));
    }
    else
    {
      R10SearchDue(g, now, own, fmt::format("his run ended {:.0f} cm short of the pad (no route): Rare's SEARCH from where he is", d));
    }
    return;
  }
  if (now - g.r10_prog_at >= kR10StuckS && t >= 4.5f) // (Rare's walks take up to 4 s to set off: round 7)
  {
    R10SearchDue(g, now, own, fmt::format("NO PROGRESS for {:.0f} s ({:.0f} cm from the pad): Rare's SEARCH from where he is", kR10StuckS, d));
    return;
  }
  if (t >= kR10LimitS)
  {
    R10SearchDue(g, now, own,
                 fmt::format("{:.0f} s and neither arrived ({:.0f} cm from the pad) nor saw Jo: Rare's SEARCH from where he is", kR10LimitS, d));
  }
}

// ITEM 2: from round 3c's look-first entry (R3cCBegin), for a guard who would look first. True: he is Rare's now.
bool R10SecondBody(const uint8_t *base, uint32_t c, Guard &g, uint32_t victim, const std::string &who, float now)
{
  if (!REXCVAR_GET(pdz_stealth_second_body) || !R9On() || g.r9_rares)
  {
    return false;
  }
  const char *knows = nullptr;
  for (const auto &[v, at] : g.r3_once_done)
  {
    (void)at;
    if (v != victim)
    {
      knows = "he already knows of a body";
    }
  }
  if (!knows)
  {
    const R3BState &s = R3BOf(c, g);
    if (!s.found.empty())
    {
      knows = "he already found a body";
    }
  }
  const float second = float(REXCVAR_GET(pdz_lab_rareplus_second));
  if (!knows && (g.rp_stage >= 2 || g.meter >= second - 0.5f))
  {
    knows = "he is sure someone is here (his stronger line, or his meter at the hold)";
  }
  if (!knows)
  {
    return false;
  }
  return R9Make(base, c, g, 3, fmt::format("second body: {}, and now the body of {}", knows, who), now);
}

// ITEM 3: from R3EndLine. True: no give-up line; he is handed to Rare at his next tick.
bool R10NoGiveUpAfterBody(const uint8_t *base, uint32_t c, Guard &g, float now, const char *what)
{
  if (!REXCVAR_GET(pdz_stealth_no_giveup_after_body) || !R9On())
  {
    return false;
  }
  const bool body = (g.r3_once_at > -1e8f && now >= g.r3_once_at && now - g.r3_once_at < 180.0f) ||
                    (what && (std::strstr(what, "body") || std::strstr(what, "witness")));
  if (!body)
  {
    return false;
  }
  if (g.r10_after_body < -1e8f)
  {
    g.r10_after_body = now;
    REXLOG_INFO("pdz_stealth_r10: NO GIVE-UP LINE: {}: {} ({:.0f} s after his reaction to a body): no give-up line after a kill; he is "
                "handed to Rare's own follow-up (Rare's guard; Rare's SEARCH at the body if Rare starts nothing in 2 s) | {}",
                Id(base, c), what ? what : "", g.r3_once_at > -1e8f ? now - g.r3_once_at : -1.0f, StackText(base, c));
  }
  return true;
}

// ITEM 4a: from PostTick, back to his post found no route. True: our walk is ended (Rare's DEFAULT has him).
bool R10PostNoRoute(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const std::string &why)
{
  if (!REXCVAR_GET(pdz_stealth_post_release))
  {
    return false;
  }
  std::string what;
  const bool ended = R9EndWalk(ctx, base, c, g, now, what);
  REXLOG_INFO("pdz_stealth_r10: BACK TO POST NO ROUTE: {} ({}): {} | {}", Id(base, c), why,
              ended ? "RELEASED TO RARE: our walk is ended as Rare ends an arrived walk; no task of ours, no look round: Rare's "
                      "DEFAULT has him (the body and sure rules still move him if he stands)"
                    : "our walk could not be ended (" + what + "): the old look round",
              StackText(base, c));
  return ended;
}

// ITEM 4b: from round 3b's BODY ONCE, a repeat of Rare's "a body". True: let this one through.
bool R10BodyStill(const uint8_t *base, uint32_t c, Guard &g, uint32_t victim, float now)
{
  if (!REXCVAR_GET(pdz_stealth_body_still_followup) || !victim || g.r10_still_since < -1e8f || now - g.r10_still_since < 8.0f)
  {
    return false;
  }
  if (g.st != St::Wary && g.st != St::Unaware)
  {
    return false;
  }
  if ((Load32(base, c + kConfig) & kSearching) != 0 || (now >= g.search_seen && now - g.search_seen < 1.0f) || g.combat_script != 0 ||
      g.run_pending || g.run_committed || g.inv != Inv::None)
  {
    return false;
  }
  if (const auto it = g_r3c_c.find(c); it != g_r3c_c.end() && it->second.born == g.r2_born && it->second.phase != kR3cCNone)
  {
    return false;
  }
  int n = 0;
  float last = -1e9f;
  for (const auto &[v, at] : g.r10_body_lets)
  {
    if (v == victim)
    {
      ++n;
      last = std::max(last, at);
    }
  }
  if (n >= 3 || (now >= last && now - last < 20.0f))
  {
    return false;
  }
  if (g.r10_body_lets.size() >= 48)
  {
    g.r10_body_lets.erase(g.r10_body_lets.begin());
  }
  g.r10_body_lets.emplace_back(victim, now);
  REXLOG_INFO("pdz_stealth_r10: BODY STILL: {} knows of this body and has stood still for {:.0f} s (state {}): Rare's \"a body\" is let "
              "through again ({} of 3 for this body): Rare's own follow-up (pdz_stealth_body_still_followup) | {}",
              Id(base, c), now - g.r10_still_since, StName(g.st), n + 1, StackText(base, c));
  g.r10_still_since = now; // (the next one only after he has stood still again)
  return true;
}

// Once a tick for a calm guard (After, right after R9Tick): his stillness, the hand-over after a body (item 3) and the
// sure guard who stands (item 4c).
void R10CalmTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_combat, bool in_search)
{
  (void)jo;
  float me[3];
  PosOf(base, c, me);
  if (g.r10_still_since < -1e8f || Dist3(me, g.r10_still_pos) > 100.0f || now < g.r10_still_since)
  {
    std::memcpy(g.r10_still_pos, me, sizeof(me));
    g.r10_still_since = now;
  }
  if (g.scientist || !LayerGuard(g) || g.dead || g.st == St::Dead)
  {
    return;
  }
  // Item 3: our check after a body is over: Rare's guard, Rare's follow-up.
  if (g.r10_after_body > -1e8f)
  {
    const float at = g.r10_after_body;
    g.r10_after_body = -1e9f;
    if (now >= at && now - at < 5.0f && !in_combat)
    {
      float pd = 0.0f;
      const uint32_t own = Load32(base, c + kPad);
      const uint32_t pad = g.r3_once_has_pos && Ptr(own) ? NearestPadTo(base, own, g.r3_once_pos, pd) : own;
      if (R9Make(base, c, g, 3, "after a body: our check of it is over: Rare's own follow-up, no give-up line of ours", now))
      {
        R10SearchDue(g, now + 2.0f, pad, "after a body: Rare started nothing in 2 s: Rare's SEARCH at the body");
        g.r10_search_from = now + 2.0f;
        g.r10_search_try = now + 1.7f; // (the first try 2 s from now: Rare's own reaction first)
      }
      return;
    }
  }
  // Item 4c: a sure guard who stands with nothing to do.
  if (!REXCVAR_GET(pdz_stealth_sure_moves) || in_combat || in_search || g.inv != Inv::None ||
      !(g.st == St::Wary || g.st == St::Unaware || g.st == St::Suspicious) || g.rp_stage < 2 ||
      g.meter < float(REXCVAR_GET(pdz_lab_rareplus_second)) - 0.5f || now - g.r10_still_since < 3.0f ||
      (now >= g.r10_sure_act && now - g.r10_sure_act < 30.0f) || g.run_pending || g.run_committed)
  {
    return;
  }
  if ((Load32(base, c + kConfig) & (kSearching | kNeverInvestigates)) != 0 || R2TopClass(base, c) != kR9DefaultVtable ||
      StackDepth(base, c) != 1 || StackWaiting(base, c) != 0 || R4BNoLook(g.who) || R4BNoWalk(g.who))
  {
    return;
  }
  g.r10_sure_act = now;
  const uint32_t own = Load32(base, c + kPad);
  float pd = 0.0f;
  const bool fresh = g.glimpse_time > -1e8f && now >= g.glimpse_time && now - g.glimpse_time < 30.0f;
  const uint32_t pad = fresh && Ptr(own) ? NearestPadTo(base, own, g.glimpse_pos, pd) : own;
  const bool ok = Ptr(pad) && GiveSearch(ctx, base, c, pad);
  if (ok)
  {
    g.search_pad = pad;
  }
  float pp[3] = {};
  PadPos(base, pad, pp);
  REXLOG_INFO("pdz_stealth_r10: SURE MOVES: {} is sure someone is here (meter {:.0f}, state {}) and has stood still for {:.0f} s with "
              "nothing to do: {} (pdz_stealth_sure_moves) | {}",
              Id(base, c), g.meter, StName(g.st), now - g.r10_still_since,
              ok ? fmt::format("Rare's SEARCH at pad {:08X} ({:.0f} {:.0f} {:.0f}){}", pad, pp[0], pp[1], pp[2],
                               fresh ? ", towards his glimpse" : ", where he stands (no fresh glimpse)")
                 : std::string("no SEARCH could be handed over now (tried again in 30 s)"),
              StackText(base, c));
}

// ITEM 6: from the lamp incident, for every guard who noticed it. One line with the cause.
void R10LampCause(const uint8_t *base, uint32_t c, const float lamp[3], float d, bool saw, bool heard, int walls, const char *seen_how)
{
  REXLOG_INFO("pdz_stealth_r10: LAMP NOTICE: {} noticed the lamp at ({:.0f} {:.0f} {:.0f}), {:.0f} cm from his eyes, {:.1f} s after the "
              "glass broke: {}",
              Id(base, c), lamp[0], lamp[1], lamp[2], d, g_r10_lamp_age,
              saw ? fmt::format("SEEN ({})", seen_how ? seen_how : "it was in his view")
              : heard ? fmt::format("HEARD the glass break ({} wall(s) or floor(s) between)", walls >= 0 ? std::to_string(walls) : std::string("walls not counted"))
                      : std::string("?"));
}
// ITEM 6: may he hear this lamp now? (Only while the break is fresh.) Out: why not.
bool R10LampHearFresh(const char *&why_not)
{
  if (!REXCVAR_GET(pdz_stealth_lamp_fresh_hearing) || g_r10_lamp_age <= kR10LampFreshS)
  {
    return true;
  }
  why_not = "the glass broke too long ago to count as heard now (it took long to go dark): only seeing it counts";
  return false;
}
