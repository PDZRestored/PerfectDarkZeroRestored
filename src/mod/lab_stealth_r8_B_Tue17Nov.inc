// Stealth layer, Tue 17 Nov night (job fix2), part B. Included by lab_stealth.cpp inside its anonymous namespace, after
// the round 3c / 4 / 5 / 6 files (it uses round 3c's witness record). Log prefix "pdz_stealth_r8:".
//
// ITEM 3, THE BODY FIRST (pdz_stealth_body_first). Log 018, 20:31:51-53: s01a said SEE_BODY for s19a, round 3c's
// witness walk sent him to the body (WITNESS WALK 20:31:52.6), and 0.7 s later the lamp rule (DarkLampTick: "finds a
// lamp dark") started its own investigation of a lamp that was out: his state became "investigating", round 3c read
// that as "the layer's own rules have him (a glimpse of Jo, a check of ours)" and dropped the body; LIGHT_SHOT_OUT
// played at 20:31:53.9. The lamp rule had no test for a body check in progress. Now, while his body check runs (round
// 3c's hold: the look first, the walk, the look round at the body), the layer's own lamp, sound and glimpse
// investigations are not started for him (logged: BODY FIRST), and the lamp rule leaves him alone (the lamp stays
// unnoticed by him, for later). A real sighting still ends the check at once (round 3c's own test), and so do Rare's
// COMBAT and the alarm.
//
// ITEM 4, A BODY SEEN LATER (pdz_stealth_late_body). Hand test 21:02: a seated guard did not react to a guard killed near
// him. With pdz_lab_rareplus on (the default) the layer's own body sight (BodySight) never runs (it is in the older
// branch); a body is found only (a) at the kill (round 2 / 3c's witness test, within 1.5 s) or (b) by Rare's own "a
// body" (0040). A guard who was within 30 m of the kill but failed the witness test then (not facing it) is marked
// "refused", and from then on Rare's own 0040 for that body is TAKEN unless the body passes the same test at that
// moment (R3cCBodyHidden); that line printed only with the debug log, so log 018 cannot show whether it happened, and
// it does not record where the guard faced at 21:02:34 (KILL line: "could see the spot: nobody"). Now: (1) the
// "taken" line prints always (rate-limited) with his eye height above his feet (a seated guard's is low); (2) every
// 0.5 s a calm guard with such a "refused" body is tested again with the same witness test; when it passes (the body
// is in his cone, in the light's range, a clear line) round 3c's look-first begins for it, as at a kill he saw: his
// line, he walks to the body and looks round there. Once per body and guard.

bool R8BodyCheck(uint32_t c, const Guard &g)
{
  if (!REXCVAR_GET(pdz_stealth_body_first) || !RarePlus())
  {
    return false;
  }
  const auto it = g_r3c_c.find(c);
  return it != g_r3c_c.end() && it->second.born == g.r2_born && it->second.phase != kR3cCNone;
}

struct R8BodyLog
{
  float born = -2e9f, at = -1e9f, late_at = -1e9f;
  int n = 0;
};
std::unordered_map<uint32_t, R8BodyLog> g_r8_body_log;

bool R8BodyFirstRefuses(const uint8_t *base, uint32_t c, const Guard &g, Inv kind, const std::string &why, float now)
{
  if ((kind != Inv::Lamp && kind != Inv::Noise) || t_r8_body_walk || g.spotted || !R8BodyCheck(c, g))
  {
    return false;
  }
  if (g_r8_body_log.size() > 512)
  {
    g_r8_body_log.clear();
  }
  R8BodyLog &l = g_r8_body_log[c];
  if (l.born != g.r2_born)
  {
    l = R8BodyLog();
    l.born = g.r2_born;
  }
  ++l.n;
  if (now - l.at >= 3.0f || now < l.at)
  {
    l.at = now;
    const auto it = g_r3c_c.find(c);
    REXLOG_INFO("pdz_stealth_r8: BODY FIRST: {} is checking the body of {} (state {}, meter {:.0f}): the layer's {} check is NOT "
                "started ({}); he finishes the body first (time {} for him; pdz_stealth_body_first) | {}",
                Id(base, c), it != g_r3c_c.end() ? it->second.victim_id : std::string("?"), StName(g.st), g.meter, InvName(kind),
                why, l.n, StackText(base, c));
  }
  return true;
}

// His eye height above his feet (cm; -1 = his sight eye is not fresh): a seated guard's is low.
float R8EyeHeight(const uint8_t *base, uint32_t c, const Guard &g, float now)
{
  if (!(now >= g.eye_at && now - g.eye_at < 3.0f))
  {
    return -1.0f;
  }
  return g.eye[1] - LoadF(base, c + kPosition + 4);
}

void R8LateBodyTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
{
  if (!REXCVAR_GET(pdz_stealth_late_body) || !R3cCOn() || g.scientist || !LayerGuard(g) || g.dead || g.st == St::Dead ||
      in_combat || g.run_committed || g.run_pending || !(g.st == St::Unaware || g.st == St::Wary) ||
      ((in_search || (Load32(base, c + kConfig) & kSearching) != 0) && !REXCVAR_GET(pdz_stealth_late_body_any)) ||
      AlarmOn(base) || !(LoadF(base, c + kHealth) > 0.0f))
  {
    return;
  }
  R9LateBodyOffer(base, c, g, now); // (Wed 18 Nov night, rule 5a: every body of the level, not only kills he was near)
  const auto found = g_r3c_c.find(c);
  if (found == g_r3c_c.end() || found->second.born != g.r2_born || found->second.phase != kR3cCNone)
  {
    return;
  }
  R3cCState &s = found->second;
  if (s.seen.empty())
  {
    return;
  }
  if (g_r8_body_log.size() > 512)
  {
    g_r8_body_log.clear();
  }
  R8BodyLog &l = g_r8_body_log[c];
  if (l.born != g.r2_born)
  {
    l = R8BodyLog();
    l.born = g.r2_born;
  }
  if (now >= l.late_at && now - l.late_at < 0.5f)
  {
    return; // (twice a second)
  }
  l.late_at = now;
  for (size_t si = 0; si < s.seen.size(); ++si)
  {
    R3cCSeen &e = s.seen[si];
    if (!e.refused || now < e.at || now - e.at > 300.0f || now - e.at < 1.5f)
    {
      continue;
    }
    int idx = -1;
    for (size_t i = g_kills.size(); i-- > 0;)
    {
      if (g_kills[i].victim == e.victim && std::fabs(g_kills[i].at - e.at) < 0.01f)
      {
        idx = int(i);
        break;
      }
    }
    if (idx < 0)
    {
      continue;
    }
    const Kill &k = g_kills[size_t(idx)];
    float spot[3] = {k.pos[0], k.pos[1], k.pos[2]};
    if (Ptr(k.victim) && Load32(base, k.victim + kData) != 0 && !(LoadF(base, k.victim + kHealth) > 0.0f))
    {
      float p[3];
      for (int i = 0; i < 3; ++i)
      {
        p[i] = LoadF(base, k.victim + kPosition + 4 * i);
      }
      if (Dist3(p, k.pos) < 600.0f)
      {
        std::memcpy(spot, p, sizeof(spot)); // where he lies now (a ragdoll may have slid)
      }
    }
    spot[1] -= 60.0f; // (as R3cCBodyHidden: the test looks 90 cm above the point; a lying body is looked at 30 cm up)
    float d = 0.0f, scale = 1.0f;
    bool beside = false, in_cone = false;
    const char *no = R3cCSees(ctx, base, c, g, spot, now, 150.0f, float(REXCVAR_GET(pdz_stealth_witness_range_cm)), d, scale,
                              beside, in_cone);
    if (no)
    {
      continue;
    }
    const float eye_h = R8EyeHeight(base, c, g, now);
    const std::string victim = k.who;
    const float kill_at = k.at;
    // Round 3c's start wants no record of this body for him: the refused entry goes, R3cCBegin writes its own.
    s.seen.erase(s.seen.begin() + std::ptrdiff_t(si));
    const bool held = R3cCBegin(base, c, g, idx, d, scale, in_cone, now);
    REXLOG_INFO("pdz_stealth_r8: LATE BODY: {} now has the body of {} in view ({:.1f} m, light {:.2f}, {}; killed {:.0f} s ago, "
                "when he was not looking that way; his eye {} above his feet{}; state {}): {} (pdz_stealth_late_body) | {}",
                Id(base, c), victim, d / 100.0f, scale, beside ? "right beside him" : "inside his cone, a clear line",
                now - kill_at, eye_h >= 0.0f ? fmt::format("{:.0f} cm", eye_h) : std::string("(not known)"),
                eye_h >= 0.0f && eye_h < 120.0f ? ", SEATED" : "", StName(g.st),
                held ? "round 3c's look first: his line, he walks to the body and looks round there"
                     : "Rare's own reaction (his \"a body\" is no longer taken)",
                StackText(base, c));
    return; // one body at a time
  }
}
