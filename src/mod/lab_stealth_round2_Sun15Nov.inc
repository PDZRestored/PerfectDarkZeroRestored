// Stealth layer round 2, Sun 15 Nov (items 1, 2 and 3). Included by lab_stealth.cpp inside its anonymous namespace,
// after lab_stealth_hurt_Sun15Nov.inc. Everything here runs only with pdz_lab_rareplus on, for a guard under the
// layer (his zone), and each item has its own switch. With a switch off nothing of that item runs.
//
// Item 1, the search cap (pdz_stealth_search_cap_s). Rare's SEARCH lasts 180 s and keeps choosing pads round one
// spot. A search that started without a real sighting of Jo (a glimpse, a noise, a lamp, a body, a blind combat)
// is ended once it is this old, through Rare's own end of search (the task's start time is moved back, as every
// other early end in this file does). He then goes back to his post, wary, with Rare's SEARCH_ABANDON line. One
// more search may follow from a new glimpse or noise (the restart); after it, none for 90 s. Never touched: a search
// while the alarm sounds, a search that is not the top task of his stack, a guard outside the layer (Rare's
// reinforcement waves are never tracked), and a search that follows a real sighting.
//
// Item 2, blind combat (pdz_stealth_blind_combat_search). Rare's COMBAT can start with no target position (a sound,
// a friend's death, "alerted"). With the meter hiding Jo he then runs about with nowhere to go. A COMBAT start with
// no real sighting, no hit and no alarm is given Rare's own leave-combat request (+6268 0x8, the bit Rare's COMBAT
// update ends on), and he gets Rare's SEARCH at the last place he could know about: the kill he just saw, his last
// glimpse, the pad of the sound he heard, else where he stands. Never Jo's own position. Item 1's cap applies.
//
// Item 3, a witness from any squad (pdz_stealth_witness_any_squad). Rare's "a friend died in view" is the per-frame
// input +6260 0x800 (sub_821D1888 turns it into stimulus 0010), and Rare only raises it for squad mates. A calm
// guard under the layer who has one of Jo's kills in view (in front of him, a clear line, within the range the
// light at the body allows) gets that same input: Rare's own reaction and line follow. Only Jo's kills are on
// record (the hit handler with her as the attacker), so a gang-war death on Subway never counts.

bool R2Log()
{
  return Logging() || StealthLog();
}

double R2CapS()
{
  return RarePlus() ? REXCVAR_GET(pdz_stealth_search_cap_s) : 0.0;
}

// A real sighting within the last `within` seconds: Rare's sight had Jo as his valid target (never while the meter
// still hides her), or she hurt him (a hit tells him where she is).
bool R2Seen(const Guard &g, float now, float within)
{
  return (now >= g.r2_sight_at && now - g.r2_sight_at < within) || (now >= g.hurt_at && now - g.hurt_at < within);
}

// Item 1: no further search for him now (the first capped search and its one restart are over).
bool R2NoRestart(const uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, const char *what)
{
  if (!(R2CapS() > 0.0) || g.r2_chain == 0 || now < g.r2_cap_end || now - g.r2_cap_end >= 90.0f)
  {
    return false;
  }
  const float d = Ptr(jo) ? Dist(base, c, jo) : -1.0f;
  // After the first capped search one restart is allowed, but not from a low, long-range glimpse (her at over 20 m
  // with his meter under the second line's level).
  const bool far_low = d > 2000.0f && g.meter < float(REXCVAR_GET(pdz_lab_rareplus_second));
  const bool refuse = g.r2_chain >= 2 || (what[0] == 'g' && far_low);
  if (refuse && R2Log() && (now - g.r2_drop_log > 5.0f || now < g.r2_drop_log))
  {
    g.r2_drop_log = now;
    REXLOG_INFO("pdz_stealth_r2: SEARCH CAP: {} gets no new search from {} ({}; his last capped search ended {:.0f} s ago, "
                "meter {:.0f}, Jo {:.0f} cm away): his line still plays",
                Id(base, c), what,
                g.r2_chain >= 2 ? "the one restart is used up" : "a low long-range glimpse does not restart it", now - g.r2_cap_end,
                g.meter, d);
  }
  return refuse;
}

// Item 1, from the SEARCH update hook: true = end this search now (the caller moves its start time back).
bool R2SearchCap(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now, double age, bool is_top, bool settled)
{
  const double cap = R2CapS();
  // Only a search Rare runs for a guard the layer has calm or wary. A lamp check of ours (state investigating) keeps
  // its own, shorter rules.
  if (!(cap > 0.0) || g.scientist || !LayerGuard(g) || g.dead || g.run_committed || !(g.st == St::Unaware || g.st == St::Wary))
  {
    return false;
  }
  // Round 3b, Sun 15 Nov (fix 2b, lab_stealth_r3b_Sun15Nov.inc): a restart for the same cause within the cap keeps the
  // first start (1); a new cause right after the cap is a new episode (2).
  const int r3b = R3BEpisode(base, c, g, task, now, age);
  if (r3b == 2 || (r3b == 0 && (now - g.r2_search_last > 3.0f || now < g.r2_search_last)))
  {
    // A new search episode (searches less than 3 s apart are one episode: Rare starts a new task for every pad).
    g.r2_search_from = now - float(std::clamp(age, 0.0, 180.0));
    g.r2_search_unconfirmed = !R2Seen(g, now, 60.0f);
    // A search the level itself gives him (Subway's 16-3 is created with a SEARCH as his first task: it is his walk
    // up and down the tunnel; smoke run r2_subway) is his patrol, not a reaction: never capped. Told by its start:
    // before the layer had ticked him for 5 s.
    const bool level_own = g.r2_born < -1e8f || now - float(std::clamp(age, 0.0, 180.0)) < g.r2_born + 5.0f;
    if (level_own)
    {
      g.r2_search_unconfirmed = false;
    }
    g.r2_search_capped = false;
    const bool chain_live = g.r2_chain > 0 && now >= g.r2_cap_end && now - g.r2_cap_end < 90.0f;
    if (!chain_live)
    {
      g.r2_chain = 0;
    }
    else if (g.r2_search_unconfirmed && g.r2_chain == 1)
    {
      g.r2_chain = 2; // the one restart
    }
    if (R2Log())
    {
      REXLOG_INFO("pdz_stealth_r2: SEARCH CAP: {} search START (task {:08X}, state {}, {}): {}{}", Id(base, c), task, StName(g.st),
                  Looking(g, now) ? "his look round where he stands" : "Rare's search",
                  level_own ? "the level's own search for him (it began as he was created: his patrol): Rare's, not capped"
                  : !g.r2_search_unconfirmed
                      ? "after a real sighting or a hit in the last 60 s: Rare's 180 s, not capped"
                      : AlarmOn(base) ? "no sighting, but the alarm sounds: Rare's, not capped while it does"
                                      : fmt::format("no real sighting of Jo: capped at {:.0f} s", cap),
                  g.r2_chain == 2 ? " (the one restart after a capped search)" : "");
    }
  }
  g.r2_search_last = now;
  if (R2Seen(g, now, 1.0f))
  {
    g.r2_search_unconfirmed = false; // he really saw her during it: Rare's from here
    g.r2_chain = 0;
  }
  const float ep = now - g.r2_search_from;
  if (g.r2_search_capped && R3BCappedRestart(base, c, g, task, now, age, is_top, settled))
  {
    return true; // (round 3b, fix 2b: a search restarted after the cap in the same episode ends too)
  }
  if (!g.r2_search_unconfirmed || g.r2_search_capped || ep < float(cap) || AlarmOn(base) || !(age < 180.0))
  {
    return false;
  }
  // Only the top task of his stack, and never early in a walk (round 3's crashes were a search ended 2 to 8 s into
  // its walk): he stands, is at its pad, or this task is over 20 s old.
  if (!is_top || !(settled || age > 20.0))
  {
    return false;
  }
  g.r2_search_capped = true;
  g.r2_cap_end = now;
  g.r2_cap_post = true;
  if (g.r2_chain == 0)
  {
    g.r2_chain = 1;
  }
  if (R2Log())
  {
    REXLOG_INFO("pdz_stealth_r2: SEARCH CAP: {} search END after {:.0f} s (cap {:.0f} s; this task {:.0f} s old; why: it began "
                "with no real sighting of Jo and found nothing; state {}; {}): Rare's own end of search runs now; then back to "
                "his post, wary",
                Id(base, c), ep, cap, age, StName(g.st),
                g.r2_chain >= 2 ? "this was the one restart: no further search for 90 s" : "one restart is allowed");
  }
  return true;
}

// Item 1, from the stimulus hook: the bits taken from Rare's word (a sound alone, once the restart is used up).
uint32_t R2StimulusDrop(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
{
  if (!(word & 0x1000) || (word & ~0x1004u) != 0 || g.scientist || !LayerGuard(g) || AlarmOn(base) ||
      !(g.st == St::Unaware || g.st == St::Wary) || g.combat_script != 0)
  {
    return 0;
  }
  return R2NoRestart(base, c, g, Load32(base, kPlayerOne), now, "a new noise") ? 0x1000u : 0u;
}

// Item 2, from the COMBAT start hook (after Rare's start): true = this COMBAT began blind and is being turned into
// a search.
bool R2BlindCombat(uint8_t *base, uint32_t c, Guard &g, bool sighting, float now)
{
  // (Round 5, Mon 16 Nov, fix 3: scientists too. A scientist's COMBAT is real only on a sighting now or in the last
  // 3 s, a hit, or Jo beside him: not because the alarm sounds, not for a sighting 10 s old: the Lab's s2s1 raised the
  // alarm and then came straight for Jo, 34 m behind him.)
  if (!REXCVAR_GET(pdz_stealth_blind_combat_search) || !RarePlus() || !(g.scientist || LayerGuard(g)) || g.dead ||
      g.st == St::Dead || g.run_committed || g.r9_rares) // (LayerGuard is false for every scientist; Wed 18 Nov: Rare's guard)
  {
    return false;
  }
  const uint32_t jo = Load32(base, kPlayerOne);
  const float d = Ptr(jo) ? Dist(base, c, jo) : -1.0f;
  const bool sci = g.scientist;
  const bool sci_seen = sci && ((now >= g.last_visible && now - g.last_visible < 3.0f) || R2Seen(g, now, 3.0f));
  const char *real = sighting                 ? "he sees her (Rare's valid target, or his sight test passed in the last 0.5 s)"
                     : sci_seen               ? "a scientist who saw her or was hit by her in the last 3 s"
                     : (!sci && R2Seen(g, now, 10.0f)) ? "he saw her or was hit by her in the last 10 s"
                     : HurtKnown(g)           ? "she hurt him"
                     : (!sci && AlarmOn(base)) ? "the alarm sounds"
                     : (d >= 0.0f && d < 300.0f && !R4ASneakBlind(base, c, g, now)) ? "she is within 3 m of him" // (stealth4, fix 5)
                                              : nullptr;
  if (real)
  {
    if (R2Log())
    {
      REXLOG_INFO("pdz_stealth_r2: BLIND COMBAT: {} COMBAT start is a real one ({}): full combat, Rare's", Id(base, c), real);
    }
    g.r2_blind_n = 0;
    return false;
  }
  if (now - g.r2_blind_first > 15.0f || now < g.r2_blind_first)
  {
    g.r2_blind_first = now;
    g.r2_blind_n = 0;
  }
  if (++g.r2_blind_n > 3)
  {
    // Rare keeps starting it (something of his holds him alerted): left to Rare, so that nothing loops.
    if (R2Log() && g.r2_blind_n == 4)
    {
      REXLOG_INFO("pdz_stealth_r2: BLIND COMBAT: {} Rare started his COMBAT {} times in 15 s with no sighting: left to Rare "
                  "(no further leave-combat request until 15 s have passed)",
                  Id(base, c), g.r2_blind_n);
    }
    return false;
  }
  // The last place he could know about.
  uint32_t pad = 0;
  const char *where = "where he stands (he knows of no place)";
  const uint32_t own = Load32(base, c + kPad);
  float pd = 0.0f;
  if (now >= g.r2_witness_at && now - g.r2_witness_at < 8.0f && Ptr(own))
  {
    pad = NearestPadTo(base, own, g.r2_witness_pos, pd);
    where = "the kill he saw";
  }
  if (!Ptr(pad) && now >= g.glimpse_time && now - g.glimpse_time < 20.0f && Ptr(own))
  {
    pad = NearestPadTo(base, own, g.glimpse_pos, pd);
    where = "his last glimpse";
  }
  if (!Ptr(pad))
  {
    const uint32_t profile = ProfileOf(base, c);
    const uint32_t heard = profile ? Load32(base, profile + kHeardPad) : 0;
    if (Ptr(heard))
    {
      pad = heard;
      where = "the pad of the sound he heard";
    }
  }
  if (!Ptr(pad) && Ptr(own))
  {
    // Tue 17 Nov night (fix2, item 2; log 018 s06a 20:39:03, "gunfire with a target", no place known): the place the
    // shot came from (Jo's spot as Rare's word arrived, this frame), so that he goes there and looks.
    float shot[3];
    if (R8ShotPlace(base, c, g, now, shot))
    {
      pad = NearestPadTo(base, own, shot, pd);
      where = "where the shot came from";
    }
  }
  if (!Ptr(pad))
  {
    pad = Ptr(own) ? own : 0;
  }
  g.r2_blind_at = now;
  R8BlindBegins(c, g, now, float(R2CapS())); // (fix2, item 2: further sounds do not restart this one search)
  g.r2_blind_pad = pad;
  g.r2_blind_where = where;
  g.r2_blind_try = -1e9f;
  Store32(base, c + kSight, Load32(base, c + kSight) | kLeaveCombat);
  float pp[3] = {};
  const bool have = PadPos(base, pad, pp);
  REXLOG_INFO("pdz_stealth_r2: BLIND COMBAT: {} Rare's COMBAT started with no sighting of Jo and no hit (meter {:.0f}, state {}, "
              "Jo {:.0f} cm away, time {} of 3 in 15 s): Rare's leave-combat request is set; he will search {}{} instead "
              "(capped by the search cap); no sighting line",
              Id(base, c), g.meter, StName(g.st), d, g.r2_blind_n, where,
              have ? fmt::format(" (pad {:08X} at ({:.0f} {:.0f} {:.0f}))", pad, pp[0], pp[1], pp[2]) : std::string());
  return true;
}

// Item 2, from the say hook: a sighting line of a guard whose COMBAT began blind and who still has not seen her.
bool R2BlindLine(const uint8_t *base, uint32_t c, const Guard &g, float now)
{
  return now >= g.r2_blind_line_until - 6.0f && now < g.r2_blind_line_until && !(Load32(base, c + kSight) & kValidTarget) &&
         !(now - g.last_visible < 0.5f && now >= g.last_visible);
}

// Item 3's witness test: could the viewer `c` see a death at `body` (where the victim fell)? In front of him (or right
// beside him), a clear line, and the light at the body allows that distance (the light meter's range scaling). nullptr
// = he saw it, else why not. Out: the distance eye to chest, the light's range scale, "right beside him".
// Round 3b, Sun 15 Nov (fix 1): the one test for guards (R2Witness) and scientists (StaffWitness); `eye_up` is the eye
// height used when his sight eye is not fresh (guards 150 cm, scientists 90 cm, as before).
const char *R2WitnessSees(PPCContext &ctx, uint8_t *base, uint32_t c, const Guard &g, const float body[3], float now, float eye_up,
                          float &d, float &scale, bool &beside)
{
  if (REXCVAR_GET(pdz_stealth_witness_range_cm) >= 0.0)
  {
    // Round 3c, Sun 15 Nov night (helper C, fix 4a; lab_stealth_r3c_C_Sun15Nov.inc): Rare's own sight cone for him, the
    // distance the light at the kill allows (his own sight range, light-scaled as the meter's light term), capped at
    // pdz_stealth_witness_range_cm. A negative value of that switch gives the round 2 test below.
    bool in_cone = false;
    return R3cCSees(ctx, base, c, g, body, now, eye_up, float(REXCVAR_GET(pdz_stealth_witness_range_cm)), d, scale, beside, in_cone);
  }
  float me[3];
  PosOf(base, c, me);
  const float chest[3] = {body[0], body[1] + 90.0f, body[2]};
  float eye[3] = {me[0], me[1] + eye_up, me[2]};
  float fx = g.dir[0], fz = g.dir[2];
  const bool fresh = now >= g.eye_at && now - g.eye_at < 3.0f;
  if (fresh)
  {
    std::memcpy(eye, g.eye, sizeof(eye));
  }
  if (!fresh || fx * fx + fz * fz < 1e-6f)
  {
    const float yaw = LoadF(base, c + 0x1670);
    fx = std::sin(yaw);
    fz = std::cos(yaw);
  }
  d = Dist3(eye, chest);
  const float range = 3000.0f;
  scale = LightTerm() ? lab_dark::RangeScaleAt(base, chest, range) : 1.0f;
  const float lx = chest[0] - eye[0], lz = chest[2] - eye[2];
  const float ll = std::sqrt(lx * lx + lz * lz), fl = std::sqrt(fx * fx + fz * fz);
  beside = d <= 300.0f;
  const bool front = ll <= 100.0f || (fl > 1e-4f && (lx * fx + lz * fz) / (ll * fl) >= 0.26f);
  return d > range                    ? "over 30 m away"
         : d > range * scale          ? "too dark at the body for that distance (the light meter)"
         : !beside && !front          ? "not in front of him"
         : !beside && !SightClear(ctx, base, chest, eye) ? "no clear line to it"
                                      : nullptr;
}

// Item 3: has he one of Jo's new kills in view?
void R2Witness(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat)
{
  if (g.r2_kills_n > g_kills.size())
  {
    g.r2_kills_n = 0; // (a new level load emptied the record)
  }
  const size_t from = g.r2_kills_n;
  g.r2_kills_n = g_kills.size();
  if (now < g.r2_witness_until && now >= g.r2_witness_at && !g.dead && g.st != St::Dead)
  {
    Store32(base, c + kStimuli, Load32(base, c + kStimuli) | 0x800u); // held up until Rare's word has shown it
  }
  if (from >= g_kills.size() || !REXCVAR_GET(pdz_stealth_witness_any_squad) || g.scientist || !LayerGuard(g) || g.dead ||
      g.st == St::Dead || in_combat || !(g.st == St::Unaware || g.st == St::Wary) || g.run_committed ||
      !(LoadF(base, c + kHealth) > 0.0f) || FightsNonPlayer(base, c))
  {
    return;
  }
  for (size_t i = from; i < g_kills.size(); ++i)
  {
    const Kill &k = g_kills[i];
    if (k.victim == c || now < k.at || now - k.at > 1.5f)
    {
      continue;
    }
    const float range = 3000.0f;
    float d = 0.0f, scale = 1.0f;
    bool beside = false;
    const char *no = R2WitnessSees(ctx, base, c, g, k.pos, now, 150.0f, d, scale, beside); // (round 3b: the shared test)
    const Guard *v = Find(k.victim);
    const bool mate = v && v->squad && v->squad == g.squad;
    if (no)
    {
      if (R2Log() && d <= range)
      {
        REXLOG_INFO("pdz_stealth_r2: WITNESS: {} did NOT see Jo kill {} ({:.0f} cm from him: {}; light range x{:.2f})", Id(base, c),
                    k.who, d, no, scale);
      }
      if (d <= range)
      {
        R3cCRefused(base, c, g, k.victim, k.who, k.at, d, scale, no); // (round 3c, C: the WITNESS REPORT line, once per body)
      }
      continue;
    }
    if (!mate && R3WTowerKill(base, c, g, k, d)) // (round 3 item 4: a tower sniper's death reaches only his own squad)
    {
      R3cCRefused(base, c, g, k.victim, k.who, k.at, d, scale, "a tower sniper of another squad (round 3's rule)"); // (3c: the report)
      continue;
    }
    // Round 3c (helper C, follow-up 3): every witness this rule finds, a squad mate included, goes through round 3c's
    // one start (the WITNESS REPORT, and the look-first hold for one who has not seen Jo). With the hold begun Rare's
    // input is NOT raised here (his reaction is held; Rare's own raise for a squad mate is taken in the stimulus hook).
    if (R3cCBegin(base, c, g, int(i), d, scale, true, now))
    {
      if (R2Log())
      {
        REXLOG_INFO("pdz_stealth_r2: WITNESS: {} SAW Jo kill {} ({:.0f} cm from him, {}, light range x{:.2f}; {}; state {}, meter "
                    "{:.0f}): he has not seen Jo: round 3c's look first (Rare's input is not raised; Rare's own is held)",
                    Id(base, c), k.who, d, beside ? "right beside him" : "inside his cone, a clear line", scale,
                    mate ? "a squad mate" : "another squad", StName(g.st), g.meter);
      }
      return;
    }
    g.r2_witness_at = now;
    g.r2_witness_until = now + 0.5f;
    g.r2_witness_line = now + 1.5f;
    std::memcpy(g.r2_witness_pos, k.pos, sizeof(g.r2_witness_pos));
    Store32(base, c + kStimuli, Load32(base, c + kStimuli) | 0x800u);
    FillFaster(base, c, g, now, "saw a friend killed");
    REXLOG_INFO("pdz_stealth_r2: WITNESS: {} SAW Jo kill {} ({:.0f} cm from him, {}, light range x{:.2f}; {}; state {}, meter "
                "{:.0f}): Rare's \"a friend died in view\" input (+6260 0x800) is raised for him: Rare's own reaction and line",
                Id(base, c), k.who, d, beside ? "right beside him" : "in front of him, a clear line", scale,
                mate ? "a squad mate: Rare raises it too" : "ANOTHER SQUAD: Rare would not have told him", StName(g.st), g.meter);
    return;
  }
}

// The class (vtable) of the top task of his stack, or 0.
uint32_t R2TopClass(const uint8_t *base, uint32_t c)
{
  const uint32_t sched = c + kScheduler;
  const uint32_t top = Load32(base, sched + kStackTop);
  if (top < sched + kStackEntry || top > sched + kStackTop)
  {
    return 0;
  }
  const uint32_t task = Load32(base, top - kStackEntry);
  return Ptr(task) ? Load32(base, task) : 0;
}

// Once a frame per tracked guard (After, before RarePlusTick), pdz_lab_rareplus on.
void R2Tick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
{
  if (g.r2_born < -1e8f)
  {
    g.r2_born = now; // the layer's first tick for him
  }
  if ((Load32(base, c + kSight) & kValidTarget) != 0 && !FightsNonPlayer(base, c))
  {
    g.r2_sight_at = now; // Rare's sight has Jo (the meter is full, or he is off the meter)
    g.r2_chain = 0;
  }
  // Item 1: his capped search is over: wary, back to his post, his line.
  if (g.r2_cap_post && !in_search)
  {
    g.r2_cap_post = false;
    if (!in_combat && !g.dead && (g.st == St::Unaware || g.st == St::Wary))
    {
      if (g.st != St::Wary)
      {
        Set(base, c, g, St::Wary, now, "search cap: nothing found: back to his post, wary");
      }
      g.post_due = now; // (PostTick walks him back)
      g.post_tries = 0;
      if (REXCVAR_GET(pdz_stealth_end_line))
      {
        // Round 3, Sun 15 Nov (L): his give-up line is followed until it really plays (lab_stealth_r3_L_Sun15Nov.inc);
        // the ask and the second ask below are not made.
        R3EndLine(ctx, base, c, g, now, "search cap: nothing found");
        if (R2Log())
        {
          REXLOG_INFO("pdz_stealth_r2: SEARCH CAP: {} is wary and due back at his post; his line: round 3's give-up line "
                      "(followed until it really plays)",
                      Id(base, c));
        }
      }
      else
      {
      const bool rare_said = (g.last_event == kEvAbandon || g.last_event == kEvAbandon + 1) && now >= g.last_event_time &&
                             now - g.last_event_time < 3.0f;
      const bool said = rare_said || Say(ctx, base, c, kEvAbandon, false, "search cap: nothing found");
      g.r2_cap_line = now + 1.5f; // (asked for is not heard: checked below)
      if (R2Log())
      {
        REXLOG_INFO("pdz_stealth_r2: SEARCH CAP: {} is wary and due back at his post; his line: {}", Id(base, c),
                    rare_said ? "Rare's own SEARCH_ABANDON, just said" : said ? "SEARCH_ABANDON asked for" : "SEARCH_ABANDON refused by Rare's speech");
      }
      } // (round 3: pdz_stealth_end_line off)
    }
    else if (R2Log())
    {
      REXLOG_INFO("pdz_stealth_r2: SEARCH CAP: {} no walk back (state {}{}): something else has him", Id(base, c), StName(g.st),
                  in_combat ? ", fighting" : "");
    }
  }
  // Item 1: was his give-up line really heard? Rare's speech can drop a line it was asked for (smoke run r2_lab2:
  // "Rare's speech did not queue it"). No line of his has started since the search ended: asked for once more.
  if (g.r2_cap_line > -1e8f && now >= g.r2_cap_line)
  {
    g.r2_cap_line = -1e9f;
    bool heard = false;
    for (const Line &l : g_lines)
    {
      heard = heard || (l.c == c && l.played && l.start >= g.r2_cap_end - 0.5f);
    }
    if (!heard && !in_combat && !g.dead && g.st == St::Wary)
    {
      const bool said = Say(ctx, base, c, kEvAbandon, false, "search cap: nothing found (asked again)");
      if (R2Log())
      {
        REXLOG_INFO("pdz_stealth_r2: SEARCH CAP: {} no line of his was heard after his search ended: SEARCH_ABANDON {}", Id(base, c),
                    said ? "asked for again" : "refused again by Rare's speech");
      }
    }
  }
  // Item 2: his blind COMBAT is over: Rare's SEARCH at the place he knows about.
  if (g.r2_blind_at > -1e8f)
  {
    const float t = now - g.r2_blind_at;
    if (g.dead || g.st == St::Dead || t < 0.0f || t > 8.0f || R2Seen(g, now, 0.5f))
    {
      if (R2Log() && !g.dead && g.st != St::Dead)
      {
        REXLOG_INFO("pdz_stealth_r2: BLIND COMBAT: {} no search handed over: {}", Id(base, c),
                    R2Seen(g, now, 0.5f) ? "he sees her now: Rare's combat" : in_combat ? "Rare kept him in COMBAT for 8 s"
                                                                                         : "his task stack stayed busy for 8 s");
      }
      g.r2_blind_at = -1e9f;
    }
    else if (!in_combat)
    {
      if (g.scientist)
      {
        // (round 5, fix 3: a scientist gets no SEARCH of Rare's: out of COMBAT he is back with his own script)
        REXLOG_INFO("pdz_stealth_r2: BLIND COMBAT: {} (a scientist) left COMBAT after {:.1f} s: no search for him, his own "
                    "script goes on | {}",
                    Id(base, c), t, StackText(base, c));
        g.r2_blind_at = -1e9f;
        if (g.st == St::Combat)
        {
          Set(base, c, g, St::Unaware, now, "a scientist's COMBAT without a sighting is over (round 5, fix 3)");
        }
      }
      else if (in_search || (Load32(base, c + kConfig) & kSearching) != 0)
      {
        if (R2Log())
        {
          REXLOG_INFO("pdz_stealth_r2: BLIND COMBAT: {} left COMBAT after {:.1f} s: Rare's own search follows (capped)", Id(base, c), t);
        }
        g.r2_blind_at = -1e9f;
      }
      else if (now - g.r2_blind_try >= 0.3f || now < g.r2_blind_try)
      {
        g.r2_blind_try = now;
        const uint32_t own = Load32(base, c + kPad);
        const uint32_t pad = Ptr(g.r2_blind_pad) ? g.r2_blind_pad : own;
        // Handed over only in place of Rare's DEFAULT script (class 82092A8C), as Rare itself starts a SEARCH.
        if (Ptr(pad) && R2TopClass(base, c) == 0x82092A8Cu && GiveSearch(ctx, base, c, pad))
        {
          REXLOG_INFO("pdz_stealth_r2: BLIND COMBAT: {} left COMBAT after {:.1f} s: Rare's SEARCH handed over at {} (pad {:08X}; "
                      "capped by the search cap)",
                      Id(base, c), t, g.r2_blind_where ? g.r2_blind_where : "his pad", pad);
          g.r2_blind_at = -1e9f;
        }
      }
    }
  }
  // Item 3.
  R2Witness(ctx, base, c, g, now, in_combat);
  if (g.r2_witness_line > -1e8f && now >= g.r2_witness_line)
  {
    g.r2_witness_line = -1e9f;
    const bool spoke = now >= g.last_event_time && g.last_event_time >= g.r2_witness_at - 0.2f;
    if (!spoke && !g.dead && g.st != St::Dead)
    {
      const uint32_t ev = EventIndex(base, "FRIEND_DIES_UNAWARE_TEAM");
      const bool said = ev && Say(ctx, base, c, ev, false, "witness of a kill");
      if (R2Log())
      {
        REXLOG_INFO("pdz_stealth_r2: WITNESS: {} said nothing in 1.5 s (Rare's reaction raised no line): FRIEND_DIES_UNAWARE_TEAM {}",
                    Id(base, c), said ? "asked for" : "refused");
      }
    }
    else if (R2Log() && spoke)
    {
      REXLOG_INFO("pdz_stealth_r2: WITNESS: {} spoke after it: {} ({:.1f} s after the kill)", Id(base, c),
                  EventName(base, g.last_event), g.last_event_time - g.r2_witness_at);
    }
  }
}
