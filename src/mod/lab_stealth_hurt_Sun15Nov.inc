// Stealth layer follow-up, Sun 15 Nov (items 11 and 16). Included by lab_stealth.cpp inside its anonymous namespace,
// after FillFaster (it uses Guard, Shot, g_shots, g_guards, Id, StName, Logging, Dist, Find and the cvars
// pdz_stealth_hurt_full / pdz_stealth_staff_witness defined in lab_stealth.cpp).

// Item 16 (Lab 11:36:43-50: Jo hit s6s5 six times from 37 m; his meter stayed at 0-26 and kept answering Rare's sight
// test "not yet", so Rare never turned him to fight; then he said the gentle "I know someone's here"). The moment Jo
// hurts a guard or a scientist (any damage from her, a near miss Rare counts on him, an explosion), his meter is FULL
// and stays out of the way (HurtKnown: his sight tests are not metered any more, the frame update's `metered`): Rare's
// own sight decides whether he sees her, and Rare's own reaction to being shot runs. A warning line still waiting is
// dropped, and none is queued after (WarnOver). A scientist's hesitation is skipped (SciHold / SciTick).
void HurtFull(const uint8_t *base, uint32_t c, Guard &g, float now, const std::string &why)
{
  if (!REXCVAR_GET(pdz_stealth_hurt_full) || g.dead || g.st == St::Dead)
  {
    return;
  }
  const bool first = g.hurt_full_at < -1e8f;
  g.hurt_full_at = now;
  const float meter_was = g.meter;
  g.meter = 100.0f;
  g.metered = false;
  g.warn_free_at = -1e9f; // (no meter hold for a warning line)
  g.hold_since = -1e9f;
  if (g.warn_ev && !g.run_pending && g.warn_queued < -1e8f)
  {
    // (A line already asked of Rare's speech is settled by WarnTick, which drops it with the same reason.)
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} warning line ({}) DROPPED: he was hurt by Jo (no warning line after that)", Id(base, c),
                  g.warn_what);
    }
    g.warn_ev = 0;
  }
  if (first && Logging())
  {
    REXLOG_INFO("pdz_lab_stealth: {} HURT BY JO ({}): his meter goes straight to full ({:.0f} -> 100, state {}): from now on "
                "Rare's own sight decides whether he sees her; no warning lines; Rare's hurt lines are not held back by our "
                "repeat rule",
                Id(base, c), why, meter_was, StName(g.st));
  }
}

// Item 16: Rare's stimulus word has the near-miss bit (0x80) for him and Jo fired within the last second: a near
// miss Rare counted on him. (Damage itself comes through the hit handler: lab_stealth::HitAfter.)
void NearMissHurt(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
{
  if (!(word & 0x80) || HurtKnown(g))
  {
    return;
  }
  const uint32_t jo_c = Load32(base, kPlayerOne);
  if (!Ptr(jo_c))
  {
    return;
  }
  for (const Shot &sh : g_shots)
  {
    if (sh.shooter == jo_c && now >= sh.at && now - sh.at < 1.0f)
    {
      HurtFull(base, c, g, now, "a near miss of hers that Rare counted on him (stimulus 0080)");
      return;
    }
  }
}

// Item 11 (Lab 11:29:12.4: Jo killed scientist s4l4 right beside s4l5, 1.3 m from her; s4l5 did and said nothing).
// What Rare has: a scientist runs the Laboratory's scientist task (820A3190, update sub_82319B18) on top of his
// stack, so the DEFAULT script's reactions ("a friend died in view", stimulus 0010: a look / go-to) never run for
// him. The task itself reacts to three things only (82319E60-82319FAC): a hostile he SEES (he surrenders if aimed at
// within reach, else flees and warns: 8231A2EC / 8231A500), and, with no hostile in sight, unsilenced gunfire in
// range (+6260 0x2), a near miss (+6260 0x100) or his alerted flag (+0x186C 0x10): he cowers (82319E88). A friend's
// death is none of them. In Rare's game s4l5 would have SEEN Jo at 1.3 m and fled; here the meter kept her hidden.
// So, the minimum: a calm scientist who has a colleague killed in his view (within 15 m, in front of him, a clear
// line) or right beside him (3 m, any facing) is treated as having seen Jo: his sighting line ("Someone call
// security!" kind) at once, without warnings 1 and 2, his meter full, and the existing hesitation; then Rare's
// sight takes over (his flight / alarm run, Rare's). If Rare's sight has no hostile for him when the hesitation
// ends (she is behind him or gone), he gets Rare's alerted flag, and Rare's task makes him cower (Rare's own start).
void StaffWitness(PPCContext &ctx, uint8_t *base, uint32_t victim, float now)
{
  if (!REXCVAR_GET(pdz_stealth_staff_witness) || !REXCVAR_GET(pdz_lab_scientist_warn) || !Ptr(victim))
  {
    return;
  }
  const uint32_t jo = Load32(base, kPlayerOne);
  if (!Ptr(jo))
  {
    return;
  }
  float body[3], jp[3];
  PosOf(base, victim, body);
  PosOf(base, jo, jp);
  // Jo's kill: her hit handler counted it, or she is within 15 m of the victim (a kill that does not come through
  // the hit handler with her as the attacker: Lab 11:29:12 had no "Jo killed s4l4" line).
  const bool hers = Dist3(body, jp) <= 1500.0f ||
                    std::any_of(g_kills.begin(), g_kills.end(), [victim](const Kill &k) { return k.victim == victim; });
  if (!hers)
  {
    return;
  }
  // Round 3b, Sun 15 Nov (fix 1; Lab 19:52:57: s2s1 "saw" s1m5 killed 14 m away on a walkway in deep dark): the
  // guards' witness test (round 2's R2WitnessSees: in front or right beside, a clear line, and the light at the body
  // for that distance), one event per body (round 3's record, r3_once_done). The scientist's 15 m reach stays.
  float killed_at = now;
  for (auto it = g_kills.rbegin(); it != g_kills.rend(); ++it)
  {
    if (it->victim == victim)
    {
      killed_at = it->at;
      break;
    }
  }
  const std::string victim_id = Id(base, victim);
  for (auto &[c, o] : g_guards)
  {
    if (c == victim || !o.scientist || (o.voice != 15 && o.voice != 16) || o.dead || o.st == St::Dead ||
        !(o.st == St::Unaware || o.st == St::Wary) || o.combat_script != 0 || o.run_pending || o.run_committed ||
        o.sci_stage >= 3 || o.spotted || HurtKnown(o) || !(LoadF(base, c + kHealth) > 0.0f))
    {
      continue;
    }
    float me[3];
    PosOf(base, c, me);
    if (Dist3(me, body) > 1500.0f)
    {
      continue;
    }
    if (std::any_of(o.r3_once_done.begin(), o.r3_once_done.end(), [victim, killed_at](const std::pair<uint32_t, float> &e) {
          return e.first == victim && std::fabs(e.second - killed_at) < 0.01f;
        }))
    {
      continue; // (one witness event per body)
    }
    float d = 0.0f, scale = 1.0f;
    bool beside = false;
    if (const char *no = R2WitnessSees(ctx, base, c, o, body, now, 90.0f, d, scale, beside))
    {
      if (Logging() || StealthLog())
      {
        REXLOG_INFO("pdz_stealth_r2: WITNESS: {} (scientist) did NOT see Jo kill {} ({:.0f} cm from him: {}; light range x{:.2f}): "
                    "no reaction (the guards' witness test)",
                    Id(base, c), victim_id, d, no, scale);
      }
      R3cCRefused(base, c, o, victim, victim_id, killed_at, d, scale, no); // (round 3c, C: the WITNESS REPORT line, once per body)
      continue;
    }
    o.r3_once_done.emplace_back(victim, killed_at);
    REXLOG_INFO("pdz_stealth_r2: WITNESS: {} (scientist) SAW Jo kill {} ({:.0f} cm from him, {}, light range x{:.2f}; the guards' "
                "witness test): his sighting line, then Rare's flight or alarm run",
                Id(base, c), victim_id, d, beside ? "right beside him" : "in front of him, a clear line", scale);
    const float hes = std::max(0.0f, float(REXCVAR_GET(pdz_lab_scientist_hesitate_s)));
    // Round 3c (helper C, fix 4b, pdz_stealth_witness_look_first): a scientist who saw the kill but not Jo is not
    // treated as having seen her: an alarmed line, he looks from where he stands (his meter fills faster), and with no
    // hostile in Rare's sight after the hesitation Rare's alerted flag cowers him (StaffWitnessTick). No alarm run
    // unless his own meter then fills. (R3cCStaffSoft also writes the WITNESS REPORT line.)
    if (R3cCStaffSoft(base, c, o, victim_id, d, scale, now))
    {
      o.staff_witness_at = now;
      o.sci_hold_until = now + hes + 3.0f;
      SciSay(base, o, "HEAR_UNKNOWN_TEAM", 0x4, 1.3f, now, "scientist witness: a colleague killed in his view, Jo not seen: alarmed line",
             false);
      continue;
    }
    o.staff_witness_at = now;
    o.meter = std::max(o.meter, Threshold(o));
    o.sci_stage = 3;
    o.sci_full_at = now;
    o.sci_hold_until = hes > 0.0f ? now + hes + 3.0f : now;
    SciSay(base, o, "WARN_TEAM", 0xF, 2.4f, now, "scientist witness: a colleague killed in his view: his sighting line", true);
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} (scientist) SAW A COLLEAGUE KILLED ({} at {:.0f} cm, {}; Jo {:.0f} cm from him): Rare's "
                  "scientist task has no reaction to a friend's death, so he is treated as having seen Jo: his sighting line "
                  "WARN_TEAM now (no warnings 1 and 2), his meter full, {:.1f} s of hesitation, then Rare's sight (his flight "
                  "or alarm run); with no hostile in Rare's sight then, Rare's alerted flag: he cowers",
                  Id(base, c), Id(base, victim), d, beside ? "right beside him" : "in front of him, a clear line",
                  Dist3(me, jp), hes);
    }
  }
}

// Item 11, afterwards: his meter stays full while he hesitates (he knows), and when the hesitation is over and Rare's
// sight has given him no hostile for a second, he gets Rare's alerted flag (+0x186C bit 0x10, the flag Rare's own
// flee-and-warn sets on the characters near a fleeing scientist): the scientist task then cowers him.
void StaffWitnessTick(uint8_t *base, uint32_t c, Guard &g, float now)
{
  if (g.staff_witness_at < -1e8f)
  {
    return;
  }
  if (g.dead || g.st == St::Dead || now < g.staff_witness_at || now - g.staff_witness_at > 30.0f || g.combat_script != 0 ||
      g.run_committed || lab_cut::AlarmRunning(base, c))
  {
    g.staff_witness_at = -1e9f; // (over: dead, fighting, or Rare has him running for an alarm)
    return;
  }
  if (now < g.sci_hold_until + 1.0f)
  {
    if (!R3cCStaffIsSoft(c, g)) // (round 3c, C: a witness who did not see Jo keeps his own meter)
    {
      g.meter = std::max(g.meter, Threshold(g));
    }
    return;
  }
  const uint32_t task = lab_cut::SciTaskOf(base, c);
  const bool cowering = task && base[task + 0x4C] != 0;
  const bool target = (Load32(base, c + kSight) & kValidTarget) != 0;
  const uint32_t latch = Load32(base, c + 6252); // (+0x186C: bit 0x10 = alerted)
  if (!cowering && !target && !(latch & 0x10))
  {
    Store32(base, c + 6252, latch | 0x10);
    if (Logging())
    {
      REXLOG_INFO("pdz_lab_stealth: {} (scientist witness): the hesitation is over and Rare's sight has no hostile for him: "
                  "Rare's alerted flag is set{}",
                  Id(base, c), task ? ": Rare's scientist task cowers him" : " (he has no scientist task: nothing more)");
    }
  }
  else if (Logging())
  {
    REXLOG_INFO("pdz_lab_stealth: {} (scientist witness): the hesitation is over: {}", Id(base, c),
                cowering ? "he is cowering already" : target ? "Rare's sight has Jo: his flight / alarm run is Rare's" : "he is alerted already");
  }
  g.staff_witness_at = -1e9f;
}

// Item 16: an explosion of the last seconds whose blast reaches `pos` (the explosions on record are the ones whose
// sound event Rare raised: g_blasts). Used by the hit handler for damage that has no character as its attacker.
bool BlastNear(const float pos[3], float now)
{
  for (const Blast &b : g_blasts)
  {
    const float age = now - b.at;
    const float dx = b.pos[0] - pos[0], dy = b.pos[1] - pos[1], dz = b.pos[2] - pos[2];
    if (age >= 0.0f && age < 2.0f && std::sqrt(dx * dx + dy * dy + dz * dz) < std::max(b.radius, 1500.0f))
    {
      return true;
    }
  }
  return false;
}
