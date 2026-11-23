// Stealth layer, Wed 18 Nov night: THE REWORK. Included by lab_stealth.cpp inside its anonymous namespace, after
// lab_stealth_r8_B_Tue17Nov.inc. Log prefix "pdz_stealth_r9:".
//
// The decision (hand test of Wed 18 Nov, Nightclub then Subway): the stealth layer is an optional layer for the QUIET
// part only. The moment the player is seen or goes loud, a guard is the game's own guard again.
//
//   RULE 2, SEEN = RARE'S GUARD (pdz_stealth_seen_is_rares). The moment Rare's own sight has Jo for a guard, he is
//   Rare's, whatever our meter says:
//     - Rare's sight word shows "valid target" / "sees a hostile" (+6268 0x800000 / 0x400000), or his word carries
//       "saw a valid target" (0x100);
//     - PLAIN VIEW (pdz_stealth_plain_view): Rare's own sight test of her passes and she is close (within
//       pdz_stealth_plain_view_cm of his eye) or lit (the light measure at her at least pdz_stealth_plain_view_light,
//       or the level has no light measure). Our meter then no longer answers "not visible". The meter is left with the
//       one thing it is for: how quickly a calm guard notices a glimpse in the dark at range;
//     - the meter itself fills (a glimpse in the dark at range, noticed in the end).
//   RULE 3, LOUD = RARE'S GUARD (pdz_stealth_loud_is_rares). Rare's own hearing and alert words: damaged (0x2),
//   alerted by a friend (0x8), a near miss (0x80), the alarm (0x200), gunfire with a target (0x400), a heard sound
//   (0x1000) that was an unsilenced shot or an explosion; the alarm sounding; a hit of Jo's on him; Rare's COMBAT
//   running for him; unsilenced gunfire or an explosion our own hearing rules let him hear; Killian's escape line; a
//   security camera shot down with an unsilenced gun.
//   RULE 4, CALM GUARDS ONLY. While a guard is Rare's: no rule of the layer runs for him. His sight tests of Jo get
//   Rare's own answer, nothing is taken from Rare's word, no blind-combat-to-search, no search cap, no target
//   emptying, no witness rule, no walk, look or check of ours (the ones running end: R9Drop), and his COMBAT start
//   alerts his friends as Rare has it. He stays Rare's for the rest of that alert: he comes back to the layer's rules
//   only when Rare's own behaviour has had him calm (his DEFAULT script alone, no COMBAT, no SEARCH, no target, the
//   alarm off) for pdz_stealth_rares_calm_s.
//   One line each time: "pdz_stealth_r9: RARE'S GUARD: <id> ... (rule 2 SEEN | rule 3 LOUD): <why>", and
//   "... CALM AGAIN" when the layer's rules may apply to him again.
//
// THE QUIET PART (rule 5):
//   (a) a body seen later (pdz_stealth_late_body_any): fix2's rule only looked at bodies whose kill the guard had
//       been judged for at that moment (within 30 m of it, not looking). A guard who was further off, or busy, or
//       created later, had no record and was never tested: he walked past bodies. Now every body of this level is
//       offered to every calm guard (also one in a search of Rare's) with the same sight test, once per body and guard.
//   (b) no freezing (pdz_stealth_body_no_freeze): a guard whose one reaction to a body was the layer's own (the
//       witness look-first) never had Rare's own body reaction; Rare's repeats of "a body" were all taken and he
//       stood beside it. Once his look is over, the next "a body" of Rare's for that body is let through ONCE: Rare's
//       own follow-up (his search, his alarm run for his role). The later repeats are still taken.
//   (c) a lamp heard breaking close by (pdz_stealth_lamp_heard_walk): when no spot under the lamp with a view of it
//       is in his reach (it is round a corner or behind a door), he walks to the pad nearest the lamp instead.
//   (d) a witness walking to a body stops the moment he is Rare's (rules 2 and 3): R9Drop ends the hold, and the plain
//       walk of ours is ended as Rare ends an arrived walk (R9EndWalk).

constexpr uint32_t kR9CompositeVtable = 0x820922A4; // the composite task our plain walks are queued as (WalkToPad)
constexpr uint32_t kR9GoToPadVtable = 0x820915F8;   // Rare's go-to-pad task (sub_821A3438)
constexpr uint32_t kR9DefaultVtable = 0x82092A8C;   // Rare's DEFAULT script
constexpr uint32_t kR9LoudWord = 0x68Au;            // damaged, alerted by a friend, near miss, the alarm, gunfire with a target

bool R9On()
{
  return REXCVAR_GET(pdz_stealth_rares_guard) && RarePlus();
}

// Our plain walks (WalkToPad / WalkToBody note them here): only a walk of ours is ever ended.
struct R9Walk
{
  uint32_t c = 0, task = 0;
  float at = -1e9f;
};
std::vector<R9Walk> g_r9_walks;
void R9WalkNote(const uint8_t *base, uint32_t c, uint32_t task)
{
  if (!Ptr(task))
  {
    return;
  }
  const float now = Clock(base);
  g_r9_walks.erase(std::remove_if(g_r9_walks.begin(), g_r9_walks.end(),
                                  [&](const R9Walk &w) { return w.c == c || now < w.at || now - w.at > 180.0f; }),
                   g_r9_walks.end());
  g_r9_walks.push_back({c, task, now});
}
void R9Reset()
{
  g_r9_walks.clear();
}

// (d) His plain walk of ours ends now, the way Rare ends a go-to-pad task that has arrived (821A3780-821A37AC: the
// character's vfunc +248, task +32 = 1, status +12 = 3): the composite sees its part done, ends, and his DEFAULT
// script (and with it Rare's reaction to what he saw or heard) runs again in the next update. Only the top task of
// his stack, only a composite we queued, only while its running part is a go-to-pad task. True: ended.
bool R9EndWalk(PPCContext &ctx, uint8_t *base, uint32_t c, const Guard &g, float now, std::string &what)
{
  (void)g;
  if (!REXCVAR_GET(pdz_stealth_rares_ends_walk))
  {
    what = "pdz_stealth_rares_ends_walk is off";
    return false;
  }
  const int depth = StackDepth(base, c);
  if (depth < 2 || StackWaiting(base, c) != 0)
  {
    what = "no walk of ours on top of his stack";
    return false;
  }
  const uint32_t top = Load32(base, c + kScheduler + kStackEntry * uint32_t(depth - 1));
  if (!Ptr(top) || Load32(base, top) != kR9CompositeVtable)
  {
    what = "no walk of ours on top of his stack";
    return false;
  }
  const bool ours = std::any_of(g_r9_walks.begin(), g_r9_walks.end(), [&](const R9Walk &w) {
    return w.c == c && w.task == top && now >= w.at && now - w.at < 180.0f;
  });
  if (!ours)
  {
    what = "the walk on top of his stack is not one of ours (Rare's own: left alone)";
    return false;
  }
  const uint32_t index = Load32(base, top + 84), count = Load32(base, top + 80);
  if (index >= count || count > 3)
  {
    what = "our walk has no running part";
    return false;
  }
  const uint32_t part = Load32(base, top + 60 + 8 * index);
  if (!Ptr(part) || Load32(base, part) != kR9GoToPadVtable || Load32(base, part + 12) != 1)
  {
    what = "our walk's running part is not a go-to-pad walk under way (left to end by itself)";
    return false;
  }
  // Sun 22 Nov night (audit): his vtable read only from mapped memory and inside the game image (GuestFunction refuses
  // _purecall).
  const uint32_t vt = pdz::guestmem::Ok(base, c, 4) ? Load32(base, c) : 0;
  PPCFunc *stop = vt >= 0x82000000u && vt < 0x83000000u
                      ? pdz::GuestFunction(Load32(base, vt + 248), "the character's stop-moving slot (+248)")
                      : nullptr;
  if (!stop)
  {
    what = "the character's stop slot is not code";
    return false;
  }
  {
    // (Thu 19 Nov, crash in test far_2 at 08:17:44, a read of guest 0x3B inside this call: Rare's own call at 821A3790
    // passes r3 = the character (task +16) AND r4 = the walk's pad (task +76); r4 was left unset here.)
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c;
    frame.ctx.r4.u64 = Load32(base, part + 76);
    stop(frame.ctx, base);
  }
  Store32(base, part + 32, 1);
  Store32(base, part + 12, 3);
  what = "our plain walk is ended as an arrived walk (Rare's own end of a go-to-pad task)";
  return true;
}

// Everything of ours running for him ends (under the lock). No guest call here: it may run inside Rare's perception
// tick (the sight test hook); the walk is ended at his own next tick (R9Tick).
void R9Drop(const uint8_t *base, uint32_t c, Guard &g, float now, const char *rule)
{
  g.metered = false;
  if (g.inv != Inv::None)
  {
    if (g.inv_body >= 0 && size_t(g.inv_body) < g_kills.size() && g_kills[size_t(g.inv_body)].finder == c)
    {
      g_kills[size_t(g.inv_body)].finder = 0;
    }
    g.inv = Inv::None;
    g.inv_pad = 0;
  }
  g.inv_walk = 0;
  g.inv_walk_wait = g.inv_walk_cut = false;
  g.inv_end_leg = false;
  g.end_search = g.end_leg = false;
  g.calm_req = g.calm_after_combat = false;
  g.want_body = -1;
  g.body_pending = g.need_line = false;
  g.noise_used = g.noise_at;
  g.post_due = -1e9f;
  g.run_pending = false;
  g.warn_ev = 0;
  g.sight_now_until = -1e9f;
  g.r2_blind_at = -1e9f;
  g.r2_cap_post = false;
  g.r2_witness_until = -1e9f;
  g.redirect_pad = g.leg_pad = 0;
  g.next_bark = 0.0f;
  g.rp_stage = 0;
  if (const auto it = g_r3c_c.find(c); it != g_r3c_c.end() && it->second.born == g.r2_born && it->second.phase != kR3cCNone)
  {
    R3cCOutcome(base, c, it->second, now, fmt::format("he is Rare's guard now ({}): the body check ends at once", rule));
    R3cCEnd(c, it->second);
  }
  if (const auto it = g_r3h.find(c); it != g_r3h.end() && it->second.born == g.r2_born)
  {
    R3HState &h = it->second;
    h.wait.at = -1e9f;
    h.inv_item = 0;
    h.inv_kill = -1;
    h.walk_kill = -1;
    h.take_over = false;
    h.look_from = -1e9f;
    h.check_at = -1e9f;
  }
  if (const auto it = g_r8_blind.find(c); it != g_r8_blind.end())
  {
    it->second.hold_until = -1e9f;
  }
  g.r9_end_walk = true;
}

// A guard becomes Rare's (rule 2: seen, rule 3: loud). Under the lock. True: he is Rare's from this call.
bool R9Make(const uint8_t *base, uint32_t c, Guard &g, int rule, const std::string &why, float now)
{
  if (!R9On() || g.r9_rares || g.dead || g.st == St::Dead || g.scientist || !LayerGuard(g) || g.run_committed || g.r6_truce ||
      !REXCVAR_GET(pdz_lab_guard_stealth))
  {
    return false;
  }
  if (rule == 2 ? !REXCVAR_GET(pdz_stealth_seen_is_rares) : !REXCVAR_GET(pdz_stealth_loud_is_rares))
  {
    return false;
  }
  const St was = g.st;
  const float meter = g.meter;
  const Inv inv = g.inv;
  const uint32_t jo = Load32(base, kPlayerOne);
  g.r9_rares = true;
  g.r9_rule = uint8_t(rule);
  g.r9_at = now;
  g.r9_calm_from = -1e9f;
  ++g.r9_n;
  R9Drop(base, c, g, now, rule == 2 ? "rule 2, seen" : "rule 3, loud");
  if (rule == 2)
  {
    g.spotted = true;
    g.spotted_at = now;
    g.seen_jo = true;
    g.stale = false;
  }
  if (g.st != St::Combat && g.st != St::Wary)
  {
    Set(base, c, g, St::Wary, now, "Rare's guard now: no state of the layer's for him");
  }
  REXLOG_INFO("pdz_stealth_r9: RARE'S GUARD: {} is Rare's from now ({}): {} | before: state {}, meter {:.0f}{}; Jo {:.0f} cm "
              "away; Rare's sight word {:08X} | every rule of the layer is off for him until Rare has had him calm for "
              "{:.0f} s (time {} for him) | {}",
              Id(base, c), rule == 2 ? "rule 2 SEEN" : "rule 3 LOUD", why, StName(was), meter,
              inv != Inv::None ? fmt::format(", a {} check of ours (ended)", InvName(inv)) : std::string(),
              Ptr(jo) ? Dist(base, c, jo) : -1.0f, Load32(base, c + kSight), float(REXCVAR_GET(pdz_stealth_rares_calm_s)), g.r9_n,
              StackText(base, c));
  return true;
}

// PLAIN VIEW, from the sight test hook after Rare's own test passed for a calm (metered) guard: is she close or lit?
// nullptr = no (the meter decides: a glimpse in the dark at range), else why.
const char *R9PlainView(const float eye[3], const float at[3], float &dist)
{
  const float dx = at[0] - eye[0], dy = at[1] - eye[1], dz = at[2] - eye[2];
  dist = std::sqrt(dx * dx + dy * dy + dz * dz);
  if (!R9On() || !REXCVAR_GET(pdz_stealth_seen_is_rares) || !REXCVAR_GET(pdz_stealth_plain_view))
  {
    return nullptr;
  }
  if (dist <= float(REXCVAR_GET(pdz_stealth_plain_view_cm)))
  {
    return "plain view: Rare's own sight test passed and she is close";
  }
  if (!LightTerm() || lab_dark::RareBlackout())
  {
    return "plain view: Rare's own sight test passed (no light measure here)";
  }
  if (lab_dark::MeterFactor() >= float(REXCVAR_GET(pdz_stealth_plain_view_light)))
  {
    return "plain view: Rare's own sight test passed and she stands in the light";
  }
  return nullptr;
}

// What a "heard a sound" of Rare's was, for rule 3: an unsilenced shot or an explosion in his reach (no wall test:
// Rare's own hearing has already let it through), or Rare's gunfire input. nullptr = a quiet sound.
const char *R9HeardLoud(const uint8_t *base, uint32_t c, float now)
{
  const uint32_t inputs = Load32(base, c + kStimuli);
  const uint32_t data = Load32(base, c + kData);
  const float factor = Ptr(data) ? std::max(1.0f, LoadF(base, data + 0x48)) : 1.0f;
  float me[3];
  PosOf(base, c, me);
  for (const Shot &sh : g_shots)
  {
    if (sh.silenced || now < sh.at || now - sh.at > 2.5f || sh.shooter == c || !Ptr(sh.shooter))
    {
      continue;
    }
    const int32_t slot = int32_t(Load32(base, sh.shooter + kProfileSlot));
    const float radius = slot >= 0 && slot < int32_t(kProfileSlots) ? LoadF(base, kProfileTable + 0x50 * uint32_t(slot) + 0x30) : 0.0f;
    float src[3];
    PosOf(base, sh.shooter, src);
    if (Dist3(me, src) < std::max(std::isfinite(radius) ? radius : 0.0f, 1500.0f) * factor)
    {
      return sh.shooter == Load32(base, kPlayerOne) ? "an unsilenced shot of Jo's" : "unsilenced gunfire near him";
    }
  }
  for (const Blast &b : g_blasts)
  {
    const float age = now - b.at;
    if (age >= 0.0f && age < 1.5f + b.radius / 1500.0f && Dist3(me, b.pos) < b.radius)
    {
      return "an explosion";
    }
  }
  return (inputs & 0x2) ? "gunfire (Rare's gunfire input)" : nullptr;
}

// From the stimulus word hook, before any rule of ours. True: he is Rare's (now or already): the word is left
// exactly as Rare built it.
bool R9Stimulus(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
{
  if (!R9On())
  {
    return false;
  }
  if (g.r9_rares)
  {
    return true;
  }
  static const std::pair<uint32_t, const char *> kBits[] = {{0x2, "damaged"},          {0x8, "alerted by a friend"},
                                                            {0x80, "a near miss"},     {0x200, "the alarm"},
                                                            {0x400, "gunfire with a target"}};
  if (word & 0x100)
  {
    return R9Make(base, c, g, 2, "Rare's word: saw a valid target (0100)", now);
  }
  if (word & kR9LoudWord)
  {
    std::string names;
    for (const auto &[bit, name] : kBits)
    {
      if (word & bit)
      {
        names += std::string(names.empty() ? "" : ", ") + name;
      }
    }
    return R9Make(base, c, g, 3, fmt::format("Rare's word {:04X}: {}", word, names), now);
  }
  if (word & 0x1000)
  {
    if (const char *loud = R9HeardLoud(base, c, now))
    {
      return R9Make(base, c, g, 3, fmt::format("Rare's word {:04X}: heard a sound, and it was {}", word, loud), now);
    }
  }
  return false;
}

// Once a tick for every live guard under the layer (After, first). Returns true while he is Rare's: After then runs
// the detectors only.
bool R9Tick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_combat, bool in_search)
{
  if (!R9On() || g.scientist || !LayerGuard(g))
  {
    g.r9_rares = false;
    g.r10_far = false; // (Thu 19 Nov)
    g.r10_stage = 0;
    return false;
  }
  if (g.r2_born < -1e8f)
  {
    g.r2_born = now; // (the layer's first tick for him: round 2's tick may not run for a guard who is Rare's at once)
  }
  const uint32_t sight = Load32(base, c + kSight);
  const bool gang = FightsNonPlayer(base, c);
  if (!g.r9_rares)
  {
    if ((sight & (kValidTarget | 0x400000u)) != 0 && !gang && TargetOf(base, c) == jo)
    {
      R9Make(base, c, g, 2, fmt::format("Rare's sight word {:08X}: {}", sight, (sight & kValidTarget) ? "a valid target" : "sees a hostile"), now);
    }
    else if (g.spotted && now >= g.spotted_at && now - g.spotted_at < 1.0f)
    {
      R9Make(base, c, g, 2, fmt::format("his meter filled ({:.0f}): a glimpse in the dark at range, noticed", g.meter), now);
    }
    else if (now >= g.hurt_at && now - g.hurt_at < 1.0f)
    {
      R9Make(base, c, g, 3, "Jo hit him", now);
    }
    else if (AlarmOn(base))
    {
      R9Make(base, c, g, 3, "the alarm sounds", now);
    }
    else if (in_combat && g.r2_blind_at < -1e8f && !gang)
    {
      R9Make(base, c, g, 3, "Rare's COMBAT runs for him", now);
    }
    else if (const auto it = g_r3h.find(c); it != g_r3h.end() && it->second.born == g.r2_born && it->second.wait.at > -1e8f &&
                                            now >= it->second.wait.at && now - it->second.wait.at < 1.0f && R3HLoud(it->second.wait.kind))
    {
      // (Our own hearing rules let him hear unsilenced gunfire or an explosion: he is Rare's; no reaction of ours.
      // The same record carries Killian's escape line and a security camera shot down: the camera counts only when
      // Jo's last shot was unsilenced; a silenced one stays a quiet noise check of the layer's.)
      const R3HSound &w = it->second.wait;
      const bool camera = (w.source & 0xFFFF0000u) == 0xCA3E0000u, killian = w.source == 0xC1114A00u;
      bool loud_shot = false;
      for (const Shot &sh : g_shots)
      {
        loud_shot = loud_shot || (sh.shooter == jo && !sh.silenced && now >= sh.at && now - sh.at < 3.0f);
      }
      // (Thu 19 Nov: another guard's gunfire is left to Rare's word, which the far-gunfire rule decides: he does not
      // learn Jo's position from it.)
      const bool guard_fire = R10FarOn() && w.kind == 1 && Ptr(w.source) && w.source != jo && !camera && !killian;
      if ((!camera || loud_shot) && !guard_fire)
      {
        R9Make(base, c, g, 3,
               killian  ? std::string("Killian's escape line")
               : camera ? fmt::format("a security camera was shot down with an unsilenced gun {:.0f} cm away", w.d)
                        : fmt::format("he heard {} {:.0f} cm away{} (Rare's hearing, our wall check)", R3HKindName(w.kind), w.d,
                                      w.walls >= 0 ? fmt::format(", {} wall(s) or floor(s)", w.walls) : std::string()),
               now);
      }
    }
  }
  if (!g.r9_rares)
  {
    return false;
  }
  // (d) a walk of ours on top of his stack ends now.
  if (g.r9_end_walk)
  {
    g.r9_end_walk = false;
    std::string what;
    const bool ended = R9EndWalk(ctx, base, c, g, now, what);
    if (ended || what[0] != 'n')
    {
      REXLOG_INFO("pdz_stealth_r9: WALK END: {} {}: {} | {}", Id(base, c), ended ? "ENDED" : "not ended", what, StackText(base, c));
    }
  }
  // Thu 19 Nov: a far guard's run to the guard who fired; a hand-over to Rare's SEARCH (lab_stealth_r10_Thu19Nov.inc).
  R10RaresTick(ctx, base, c, g, jo, now, in_combat, in_search);
  // His state for the other guards' rules and the log: combat while Rare's COMBAT runs, else wary. No meter.
  g.metered = false;
  g.sees = (sight & kValidTarget) != 0;
  if (g.sees)
  {
    g.seen_jo = true;
    g.r2_sight_at = now;
  }
  if (in_combat && g.st != St::Combat)
  {
    Set(base, c, g, St::Combat, now, "Rare's combat (Rare's guard)");
    g.fought = true;
  }
  else if (!in_combat && g.st != St::Wary)
  {
    Set(base, c, g, St::Wary, now, "Rare's guard, not in COMBAT: what he does is Rare's");
  }
  // Back to calm: Rare's own behaviour has him calm (his DEFAULT script alone, no fight, no search, no target of
  // Rare's sight, the alarm off, no hit lately).
  const int depth = StackDepth(base, c);
  const uint32_t bottom = depth == 1 ? Load32(base, c + kScheduler) : 0;
  const bool calm = !in_combat && !in_search && (Load32(base, c + kConfig) & kSearching) == 0 && !AlarmOn(base) &&
                    (sight & (kValidTarget | 0x400000u)) == 0 && depth == 1 && Ptr(bottom) && Load32(base, bottom) == kR9DefaultVtable &&
                    !(now >= g.hurt_at && now - g.hurt_at < 5.0f);
  if (!calm)
  {
    g.r9_calm_from = -1e9f;
  }
  else if (g.r9_calm_from < -1e8f || now < g.r9_calm_from)
  {
    g.r9_calm_from = now;
  }
  else if (now - g.r9_calm_from >= float(REXCVAR_GET(pdz_stealth_rares_calm_s)))
  {
    REXLOG_INFO("pdz_stealth_r9: CALM AGAIN: {} Rare's own behaviour has had him calm for {:.0f} s (his DEFAULT script alone, no "
                "COMBAT, no SEARCH, no target, the alarm off), {:.0f} s after he became Rare's ({}): the layer's rules for calm "
                "guards may apply to him again; wary | {}",
                Id(base, c), now - g.r9_calm_from, now - g.r9_at, g.r9_rule == 2 ? "rule 2 SEEN" : "rule 3 LOUD", StackText(base, c));
    g.r9_rares = false;
    g.r9_calm_from = -1e9f;
    g.r10_far = false; // (Thu 19 Nov)
    g.r10_stage = 0;
    g.spotted = false;
    g.confirmed = false;
    g.meter = std::min(g.meter, float(REXCVAR_GET(pdz_lab_stealth_wary_floor)));
    g.glimpse_used = g.glimpse_time;
    g.since = now;
    return false;
  }
  return true;
}

// Is he Rare's guard now? (Takes the lock: for the hooks outside the per-guard tick.)
bool R9IsRares(uint32_t c)
{
  std::lock_guard<std::recursive_mutex> hold(g_lock);
  const Guard *g = Ptr(c) ? Find(c) : nullptr;
  return g && g->r9_rares && !g->r10_far; // (Thu 19 Nov: a far guard does not pass on, or get, Jo's position)
}

// (a) A BODY SEEN LATER, for every body (pdz_stealth_late_body_any). Twice a second for a calm guard: every kill of
// this level he has no record for (not judged at the kill: too far, busy, not there yet) gets a "not looking then"
// record when it lies within 30 m of him now, so that fix2's test (R8LateBodyTick, right after this) looks at it.
void R9LateBodyOffer(const uint8_t *base, uint32_t c, Guard &g, float now)
{
  if (!REXCVAR_GET(pdz_stealth_late_body_any) || !REXCVAR_GET(pdz_stealth_late_body) || !R3cCOn() || g_kills.empty() ||
      (now >= g.r9_body_scan && now - g.r9_body_scan < 0.5f))
  {
    return;
  }
  g.r9_body_scan = now;
  R3cCState &s = R3cCOf(c, g);
  if (s.phase != kR3cCNone)
  {
    return;
  }
  float me[3];
  PosOf(base, c, me);
  for (const Kill &k : g_kills)
  {
    if (k.victim == c || now < k.at || now - k.at > 300.0f || now - k.at < 1.5f || Dist3(me, k.pos) > 3000.0f ||
        R3cCSeenOf(s, k.victim, k.at))
    {
      continue;
    }
    bool witnessed = false;
    for (const auto &[v, at] : g.r3_once_done)
    {
      (void)at;
      witnessed = witnessed || v == k.victim;
    }
    if (witnessed)
    {
      continue;
    }
    if (s.seen.size() >= 32)
    {
      s.seen.erase(s.seen.begin());
    }
    R3cCSeen e;
    e.victim = k.victim;
    e.at = k.at;
    e.refused = true;
    s.seen.push_back(e);
    if (R2Log())
    {
      REXLOG_INFO("pdz_stealth_r9: LATE BODY OFFER: {} had no record for the body of {} (killed {:.0f} s ago, {:.0f} cm from him "
                  "now): it is tested from now on with the sight test of a kill (pdz_stealth_late_body_any)",
                  Id(base, c), k.who, now - k.at, Dist3(me, k.pos));
    }
  }
}

// (b) NO FREEZING, from round 3b's one-reaction-per-body rule (R3BBodyDrop), for a repeat of Rare's "a body": true =
// let this one through, once per body and guard: his one reaction was the layer's own and it is over.
bool R9BodyFollowUp(const uint8_t *base, uint32_t c, Guard &g, uint32_t victim, bool found_by_rare, float now)
{
  if (!REXCVAR_GET(pdz_stealth_body_no_freeze) || found_by_rare || !victim)
  {
    return false;
  }
  if (std::find(g.r9_followed.begin(), g.r9_followed.end(), victim) != g.r9_followed.end())
  {
    return false;
  }
  // His look must be over: no hold of round 3c's, no walk or look of round 3's, no check of ours, not in a search.
  if (const auto it = g_r3c_c.find(c); it != g_r3c_c.end() && it->second.born == g.r2_born && it->second.phase != kR3cCNone)
  {
    return false;
  }
  if (const auto it = g_r3h.find(c); it != g_r3h.end() && it->second.born == g.r2_born &&
                                     (it->second.inv_item != 0 || it->second.walk_kill >= 0 || it->second.look_from > -1e8f))
  {
    return false;
  }
  if (g.st != St::Wary && g.st != St::Unaware)
  {
    return false;
  }
  if ((Load32(base, c + kConfig) & kSearching) != 0 || (now >= g.search_seen && now - g.search_seen < 1.0f) || g.combat_script != 0 ||
      g.run_pending || g.run_committed || (now >= g.r3_once_at && now - g.r3_once_at < 5.0f))
  {
    return false;
  }
  if (g.r9_followed.size() >= 32)
  {
    g.r9_followed.erase(g.r9_followed.begin());
  }
  g.r9_followed.push_back(victim);
  REXLOG_INFO("pdz_stealth_r9: BODY FOLLOW-UP: {} stands by a body he reacted to as a witness (the layer's look first, over now) "
              "and Rare raises \"a body\" for it again: let through ONCE: Rare's own follow-up (his search, his alarm run "
              "for his role); the later repeats are taken as before (pdz_stealth_body_no_freeze; state {}) | {}",
              Id(base, c), StName(g.st), StackText(base, c));
  return true;
}
