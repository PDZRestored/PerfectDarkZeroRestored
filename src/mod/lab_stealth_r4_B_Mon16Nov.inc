// Stealth fixes, Mon 16 Nov evening (helper B): fixes 2, 3 and 4 of the quick job. Everything here runs only under
// the stealth layer (R3cCOn: the master switch and the layer's rules); Gameplay = Original stays the game's own.
//
// Fix 2 (pdz_stealth_body_line_acts; Laboratory start, 16:55 and 17:04:25: the friend of the guard killed outside the
//   lab door said a body line and walked on, and the layer logged nothing). A calm guard of the layer who says one of
//   the game's own body / dead-friend lines (SEE_BODY_*, FRIEND_DIES_*) now acts on it whatever path raised the line:
//   the nearest kill of Jo's within 25 m becomes his body: the look-first hold of round 3c begins (he stops, walks
//   to the body, looks round there, then the witness / body rules: his call, or nothing passed on). One log line per
//   such line, always on (BODY LINE), also for a speaker the layer does not track, so the caller of the line can be
//   read back.
// Fix 3 (pdz_stealth_witness_everyone; Nightclub s01b, 17:16:26). The row's no_look exception no longer keeps a
//   witness where he stands: no carve-outs. The Nightclub's ringleader talks (rin1 + s01b, rin2 + r2bu, rin3), Rare's
//   WARNER s04a and the Subway's door kickers war1 / fac3 all follow the common rule, as in Rare's game (a disturbed
//   talk may fail its objective, as on the Xbox 360). The meter-line look round and the lamp walk keep the row's rule.
// Fix 4 (pdz_stealth_body_look_from_afar; Nightclub s05d, 17:20:22: his walk to the body ended 13 m short twice and he
//   gave up). When the body cannot be reached, he looks at it from where his walk ended (the nearest point the game's
//   walk could bring him to), if there is a clear line to it within 30 m: he turns to it, looks for 5 s, then the same
//   release as at the body (his call, or nothing passed on). No clear line from there: as before.
// The three switches (pdz_stealth_body_line_acts, pdz_stealth_witness_everyone, pdz_stealth_body_look_from_afar) are
// defined in lab_stealth.cpp beside pdz_stealth_witness_look_first.
constexpr float kR4BLineRangeCm = 2500.0f; // a body line is about a kill of Jo's this near the speaker
constexpr float kR4BAfarCm = 3000.0f;      // he can look at a body from this far (the layer's sight range)
constexpr float kR4BAfarS = 5.0f;          // s: he looks at it this long from where he stands
constexpr float kR4BLineWaitS = 1.0f;      // s: Rare's own reaction to a body gets this long to start after the line
struct R4BPending
{
  uint32_t c = 0, victim = 0;
  float born = 0.0f, kill_at = 0.0f, at = 0.0f, d = 0.0f;
};
std::vector<R4BPending> g_r4b_pending; // body lines waiting for their decision (fix 2)

// Fix 3. True: the row's no_look exception still keeps this guard where he is as a witness.
bool R4BNoLook(const std::string &who)
{
  if (!NoLook(who))
  {
    return false;
  }
  if (!REXCVAR_GET(pdz_stealth_witness_everyone))
  {
    return true;
  }
  // (correction Mon 16 Nov evening: no exception at all. The ringleader / enforcer pairs move like everyone else, as in
  // Rare's game; disturbing their talk and failing that objective is a legitimate way to fail.)
  return false;
}

// Fix 3: helper H's "why he stays" for a WITNESS (R3HStays with the no_look rule above).
const char *R4BWitnessStays(const uint8_t *base, uint32_t c, const Guard &g)
{
  if (NoLook(g.who))
  {
    if (R4BNoLook(g.who))
    {
      return "his own talk is the objective and nobody of it has been killed (the row's no_look): he stays, his line only";
    }
    return R4BNoWalk(g.who)      ? "his row's no_walk (a tower sniper): he stays, his line only"
           : RadioReady(base, c) ? "a radio user who can still call: Rare's DEFAULT reaction stays ready, his line only"
           : (Load32(base, c + kConfig) & kNeverInvestigates) ? "Rare's never-investigates flag: his line only"
                                                              : nullptr;
  }
  return R3HStays(base, c, g);
}

// Fix 2: is this speech event one of Rare's body / dead-friend lines? Cached per event number (no string work after
// the first time an event is seen).
bool R4BBodyEvent(const uint8_t *base, uint32_t event)
{
  static int8_t known[512] = {};
  if (event >= 512)
  {
    return false;
  }
  if (!known[event])
  {
    const std::string n = EventName(base, event);
    known[event] = (n.find("SEE_BODY") != std::string::npos || n.find("FRIEND_DIES") != std::string::npos) ? 1 : -1;
  }
  return known[event] > 0;
}

// Fix 2. From the say-event hook (under the lock), before the layer's own rules for the line: `g` may be nullptr (a
// speaker the layer does not track). Changes nothing of the line itself.
void R4BBodyLine(uint8_t *base, uint32_t c, Guard *g, uint32_t event, float now, bool ours, uint32_t caller)
{
  if (ours || !REXCVAR_GET(pdz_stealth_body_line_acts) || !R3cCOn() || !R4BBodyEvent(base, event))
  {
    return;
  }
  static std::unordered_map<uint32_t, float> logged;
  if (logged.size() > 512)
  {
    logged.clear();
  }
  float &last = logged[c];
  const bool do_log = last == 0.0f || now - last >= 3.0f || now < last;
  const auto say = [&](const std::string &outcome) {
    if (do_log)
    {
      last = now;
      REXLOG_INFO("pdz_stealth_r4: BODY LINE: {} said Rare's {} ({}, caller {:08X}), state {}: {}",
                  g ? Id(base, c) : fmt::format("character {:08X}", c), EventName(base, event), event, caller,
                  g ? StName(g->st) : "not tracked", outcome);
    }
  };
  if (!g)
  {
    say("not a character the layer tracks: Rare's line only");
    return;
  }
  if (g->scientist || !LayerGuard(*g) || g->dead || g->st == St::Dead || !(LoadF(base, c + kHealth) > 0.0f))
  {
    return; // (scientists have their own soft reaction; the dead say nothing)
  }
  float me[3];
  PosOf(base, c, me);
  int ki = -1;
  float kd = kR4BLineRangeCm;
  for (size_t i = 0; i < g_kills.size(); ++i)
  {
    const Kill &k = g_kills[i];
    const float d = Dist3(me, k.pos);
    if (k.victim != c && now >= k.at && d <= kd)
    {
      ki = int(i);
      kd = d;
    }
  }
  if (ki < 0)
  {
    say(fmt::format("no kill of Jo's on record within {:.0f} cm of him: Rare's line only", kR4BLineRangeCm));
    return;
  }
  const Kill &k = g_kills[size_t(ki)];
  R3cCState &s = R3cCOf(c, *g);
  if (s.phase != kR3cCNone)
  {
    say(fmt::format("the body of {} ({:.0f} cm away): a look-first hold of his runs already (victim {})", k.who, kd, s.victim_id));
    return;
  }
  if (R3WReacted(*g, k))
  {
    say(fmt::format("the body of {} ({:.0f} cm away): he has reacted to this body before: nothing new", k.who, kd));
    return;
  }
  // (a kill he was refused for as a witness: he could not see it then; now he has seen the body)
  for (size_t i = s.seen.size(); i-- > 0;)
  {
    if (s.seen[i].victim == k.victim && std::fabs(s.seen[i].at - k.at) < 0.01f)
    {
      if (!s.seen[i].refused)
      {
        say(fmt::format("the body of {} ({:.0f} cm away): he was a witness of this kill before: nothing new", k.who, kd));
        return;
      }
      s.seen.erase(s.seen.begin() + std::ptrdiff_t(i));
    }
  }
  // Decided kR4BLineWaitS later (R4BBodyLineTick): Rare's own reaction to the body, when it runs, is left alone.
  for (const R4BPending &p : g_r4b_pending)
  {
    if (p.c == c)
    {
      return; // (one is waiting for him already)
    }
  }
  if (g_r4b_pending.size() >= 32)
  {
    g_r4b_pending.erase(g_r4b_pending.begin());
  }
  R4BPending p;
  p.c = c;
  p.born = g->r2_born;
  p.victim = k.victim;
  p.kill_at = k.at;
  p.at = now;
  p.d = kd;
  g_r4b_pending.push_back(p);
  say(fmt::format("the body of {} ({:.0f} cm away, killed {:.0f} s ago): he must act on it: checked in {:.1f} s (Rare's own "
                  "reaction, if it runs, is left alone) | {}",
                  k.who, kd, now - k.at, kR4BLineWaitS, StackText(base, c)));
}

// Fix 2, once a frame per tracked guard (from R3cCTick's first line): the decision for a body line said
// kR4BLineWaitS ago. Nothing waiting: one test.
void R4BBodyLineTick(uint8_t *base, uint32_t c, Guard &g, float now)
{
  if (g_r4b_pending.empty())
  {
    return;
  }
  size_t at = g_r4b_pending.size();
  for (size_t i = 0; i < g_r4b_pending.size(); ++i)
  {
    if (g_r4b_pending[i].c == c)
    {
      at = i;
      break;
    }
  }
  if (at == g_r4b_pending.size())
  {
    return;
  }
  const R4BPending p = g_r4b_pending[at];
  if (now >= p.at && now - p.at < kR4BLineWaitS && p.born == g.r2_born)
  {
    return;
  }
  g_r4b_pending.erase(g_r4b_pending.begin() + std::ptrdiff_t(at));
  if (p.born != g.r2_born || now < p.at || now - p.at > 10.0f || !REXCVAR_GET(pdz_stealth_body_line_acts) || !R3cCOn() ||
      g.dead || g.st == St::Dead || !(LoadF(base, c + kHealth) > 0.0f))
  {
    return;
  }
  int ki = -1;
  for (size_t i = g_kills.size(); i-- > 0;)
  {
    if (g_kills[i].victim == p.victim && std::fabs(g_kills[i].at - p.kill_at) < 0.01f)
    {
      ki = int(i);
      break;
    }
  }
  if (ki < 0)
  {
    return;
  }
  const Kill &k = g_kills[size_t(ki)];
  const bool searching = (Load32(base, c + kConfig) & kSearching) != 0 || (now >= g.search_seen && now - g.search_seen < 1.0f);
  const int depth = StackDepth(base, c);
  const char *rare = !(g.st == St::Unaware || g.st == St::Wary) ? "the layer or Rare has him already (not calm or wary)"
                     : searching                                ? "Rare's own SEARCH runs"
                     : CombatOnStack(base, c)                   ? "Rare's COMBAT runs"
                     : (depth > 1 && !g.post_task)              ? "a task of Rare's runs on top of his DEFAULT script (his walk to the body)"
                                                                : nullptr;
  R3cCState &s = R3cCOf(c, g);
  if (!rare && s.phase != kR3cCNone)
  {
    rare = "a look-first hold of his runs already";
  }
  if (!rare && R3WReacted(g, k))
  {
    rare = "he has reacted to this body meanwhile";
  }
  if (rare)
  {
    REXLOG_INFO("pdz_stealth_r4: BODY LINE: {} {:.1f} s after his line about the body of {}: {}: left to it (state {}) | {}", Id(base, c),
                now - p.at, k.who, rare, StName(g.st), StackText(base, c));
    return;
  }
  const bool held = R3cCBegin(base, c, g, ki, p.d, 1.0f, true, now);
  if (held)
  {
    R3cCState &hs = R3cCOf(c, g);
    hs.line_due = false; // (he has said his line)
    if (g.st == St::Unaware)
    {
      Set(base, c, g, St::Wary, now, "said a body line and nothing of Rare's followed: wary, he goes to look at the body");
    }
    g.body_pending = false; // (the hold decides, not the end of a walk of Rare's)
  }
  REXLOG_INFO("pdz_stealth_r4: BODY LINE: {} {:.1f} s after his line about the body of {} nothing of Rare's has followed (he "
              "walked on): {} (state {}{}) | {}",
              Id(base, c), now - p.at, k.who,
              held ? "HE ACTS ON IT: he stops, walks to the body and looks round there, then the witness / body rules (round "
                     "3c's look-first hold)"
                   : "not held (he saw Jo, the alarm sounds, or look-first is off): Rare's reaction as it is",
              StName(g.st), g.post_task ? ", on his walk back to his post: the walk to the body starts when that walk is over" : "",
              StackText(base, c));
}

// Fix 4. From the look-first hold's walk stages when the body "cannot be reached": true = he looks at it from where
// he stands instead (the hold goes on as a stay-and-look; its end is the usual release).
bool R4BLookFromAfar(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, R3cCState &s, float now, const std::string &why)
{
  if (!REXCVAR_GET(pdz_stealth_body_look_from_afar) || s.r4_afar)
  {
    return false;
  }
  float me[3];
  PosOf(base, c, me);
  const bool fresh = now >= g.eye_at && now - g.eye_at < 3.0f;
  float eye[3] = {me[0], me[1] + 150.0f, me[2]};
  if (fresh)
  {
    std::memcpy(eye, g.eye, sizeof(eye));
  }
  const float chest[3] = {s.pos[0], s.pos[1] + 90.0f, s.pos[2]}, low[3] = {s.pos[0], s.pos[1] + 30.0f, s.pos[2]};
  const float d = Dist3(eye, chest);
  const bool clear = d <= kR4BAfarCm && (SightClear(ctx, base, chest, eye) || SightClear(ctx, base, low, eye));
  if (!clear)
  {
    REXLOG_INFO("pdz_stealth_r4: LOOK FROM AFAR: {} cannot reach the body of {} ({}) and has no clear view of it from where he "
                "stands ({:.0f} cm away, limit {:.0f}): he gives up as before",
                Id(base, c), s.victim_id, why, d, kR4BAfarCm);
    return false;
  }
  // He turns to the body (Rare's look-at-object parts; the walk in them ends where he stands).
  bool turned = false;
  const char *how = "no corpse object found";
  if (s.kill >= 0 && size_t(s.kill) < g_kills.size())
  {
    float where[3] = {};
    const uint32_t corpse = CorpseOf(ctx, base, c, g_kills[size_t(s.kill)], where, how);
    turned = Ptr(corpse) && R3HTurnTo(ctx, base, c, corpse);
  }
  // Helper H's claim on a walk to this body is given back (as R3cCEnd does): he stays here.
  const auto h = g_r3h.find(c);
  if (h != g_r3h.end() && h->second.walk_kill == s.kill)
  {
    h->second.walk_kill = -1;
    h->second.take_over = false;
  }
  s.r4_afar = true;
  s.phase = kR3cCStay;
  s.until = now + kR4BAfarS;
  REXLOG_INFO("pdz_stealth_r4: LOOK FROM AFAR: {} cannot reach the body of {} ({}): he has a clear view of it from where he stands "
              "({:.0f} cm away): he {} and looks for {:.0f} s, then his held reaction is released as at the body | {}",
              Id(base, c), s.victim_id, why, d, turned ? "turns to it" : "could not be turned to it (his task stack is busy, or no corpse object)",
              kR4BAfarS, StackText(base, c));
  return true;
}
