// Stealth layer round 3c, Sun 15 Nov night, helper C (fix 4: witness tuning). Included by lab_stealth.cpp inside its
// anonymous namespace, after lab_stealth_r3b_Sun15Nov.inc. Runs only with the layer on (pdz_lab_guard_stealth with
// pdz_lab_rareplus). Log prefix "pdz_stealth_r3c:".
//
// (a) WHO IS A WITNESS (pdz_stealth_witness_range_cm, 2000; a negative value switches the whole fix off). One test,
//     R3cCSees, for guards (Rare's own "a friend died in view", round 2's any-squad raise) and scientists
//     (StaffWitness), reached through round 2's R2WitnessSees:
//       - Rare's OWN SIGHT CONE for that guard. Rare's viewer / target sight test sub_82189F18 is asked with
//         f1 = range squared, f2 and f5 = how far the cone's apex sits behind his eye (f2 x f5), f3 = 1 / cos(half
//         angle), f4 = the up / down slope limit, r3 = "the cone is required", r4 = "close up no cone is needed" (then
//         required only from the distance whose square is the float at 820EBC58). The cone itself is sub_8225BEA0
//         (v = point - apex; t = dot(facing, v) x f3; inside when t > 0 and t x t >= |v|^2), then |dy| / ground distance
//         <= f4. The sight hook keeps these arguments per guard from his own tests of Jo (R3cCSightArgs); the witness
//         test repeats the same arithmetic for the kill's spot with his eye and facing from his last sight test. A
//         guard with no test on record yet uses the newest arguments of any guard (the cone is a property of the
//         guard type), else round 2's "in front" (75 degrees).
//       - THE LIGHT: within the distance at which he would see Jo at that spot: his own sight range (the square root
//         of Rare's f1, 30 m at most: the range the layer's light scale has always been measured against) times the
//         light meter's range scale at the kill (lab_dark::RangeScaleAt, the scale the body sight and round 2 use).
//       - THE CAP: never beyond pdz_stealth_witness_range_cm (20 m).
//       - a clear line (Rare's sight ray), as before.
//     Rare's own 0010 for a kill of Jo's that fails the test is taken from his word (it was let through before), and
//     the body of such a kill is not noticed by him either (Rare's "a body", 0040) until it passes the same test
//     (pdz_stealth_witness_body_test).
// (b) LOOK FIRST (pdz_stealth_witness_look_first). A witness who has not seen Jo (no valid target of Rare's, no
//     sighting or hit in the last 3 s) does not get Rare's reaction: the 0010 is taken, and so are the other quiet
//     starts (a sound 1000, a body 0040) until the hold ends. He says his line (FRIEND_DIES_UNAWARE_TEAM), is wary and
//     alert, and walks to the body (helper H's walk, claimed here; radio users walk too). The hold ends:
//       - he sees Jo (Rare's valid target / his meter full): Rare's sighting reaction, at once ("escalated: saw Jo");
//       - his walk is over at the body with nothing found: his held reaction is released: "a friend died in view"
//         (+6260 0x800) is raised until his word shows it and let past every rule, so Rare's role dispatch runs: a
//         radio user's call, an alarm user's run (both announced by the existing code). A guard with neither role has
//         nothing to pass on: wary, back to his post, his give-up line;
//       - his walk ended away from the body (no route, the 25 s cap, his task stack stayed busy): released the same way
//         ("could not reach the body");
//       - he is killed: nothing passed on;
//       - a real stimulus of Rare's reaches him (a hit, a near miss, a sighting, gunfire with a target, the alarm, a
//         friend's "alert now"), the alarm sounds, Rare has him in COMBAT, or the layer's own glimpse rules took him
//         (suspicious / searching): theirs from there, nothing released;
//       - a time limit (the search cap + 23 s) in any other case: released.
//     Guards who stay put (the row's no_look / no_walk, Rare's never-investigates flag) say the line and look from
//     where they stand for kR3cCStayS; then the same release. A witness who is busy (not calm or wary) or saw Jo gets
//     Rare's reaction at once, as before.
//     Scientists do not walk: a scientist who saw the kill but not Jo says an alarmed line and keeps his own meter
//     (he is no longer treated as having seen her); when the hesitation is over with no hostile in Rare's sight,
//     Rare's alerted flag cowers him (the existing StaffWitnessTick). His alarm run starts only if his meter fills.
// (c) THE REPORT. One line per witness reaction or refusal:
//       pdz_stealth_r3c: WITNESS REPORT <id> victim <id> d <m> light <x> cone <in/out> saw_jo <y/n> -> <outcome>
//     and one when a look-first reaction ends:
//       pdz_stealth_r3c: WITNESS OUTCOME <id> victim <id>: <outcome> (<s> after the kill was seen)

// FOLLOW-UP 1 (after the first Mansion smoke run; it overrides the text above where they differ):
//   - the light scales the layer's fixed 30 m, not Rare's f1 (f1 is his range for the state he is in: 12.5 m for a calm
//     Mansion trooper, longer once he has a contact: sub_8218A210 picks it from his perception profile +28 or a constant);
//   - Rare's no-cone rule is only passed for a guard with a contact, so the witness test does not use it; round 2's
//     "3 m right beside him, any facing" is kept instead;
//   - one kill, one answer: a guard refused for a kill is not tested again at Rare's repeats (the body can still be
//     noticed later: BODY SIGHT);
//   - R3cCSoundOnce: one search per sound (Rare restarted a search at every bounce of the body);
//   - R3cCSlowStart: a witness walk gets 8 s to set off before the no-progress rule looks at it;
//   - test tool pdz_test_stealth_witness_when (the kill_at tools do not really move a guard).
constexpr float kR3cCStayS = 6.0f;      // s: a witness who stays put looks from where he stands this long
constexpr float kR3cCRaiseS = 3.0f;     // s: the released reaction is held up this long for Rare's word to show it
constexpr float kR3cCReachCm = 600.0f;  // "at the body": this near it on the ground plan when his walk is over
constexpr float kR3cCHoldS = 90.0f;     // s: follow-up 2: the hold's last-resort limit (a walk that still gets nearer runs on)
constexpr float kR3cCStoodS = 8.0f;     // s: his plain walk has not moved him 1 m for this long: the body cannot be reached
constexpr float kR3cCNoNearerS = 12.0f; // s: ... or has brought him no nearer to its pad for this long
constexpr float kR3cCStartS = 10.0f;    // s: ... or no walk could be started for this long

struct R3cCCone
{
  bool have = false;
  float range2 = 0.0f, back = 0.0f, k = 0.0f, slope = 0.0f, back_scale = 0.0f;
  bool need_cone = false, near_flag = false;
};
enum : int
{
  kR3cCNone = 0,
  kR3cCLook = 1,  // his walk to the body is due or runs
  kR3cCStay = 2,  // he stays put and looks from where he stands
  kR3cCRaise = 3, // released: "a friend died in view" is held up until his word shows it
};
struct R3cCSeen
{
  uint32_t victim = 0;
  float at = 0.0f; // the AI clock of the death
  bool refused = false;
  float body_log = -1e9f;
};
struct R3cCState
{
  float born = -2e9f;
  std::vector<R3cCSeen> seen; // the kills he has been judged for (a witness, or refused)
  int phase = kR3cCNone;
  int kill = -1;
  uint32_t victim = 0;
  std::string victim_id;
  float pos[3] = {};
  float from = -1e9f, until = -1e9f, raise_until = -1e9f;
  bool line_due = false;
  // Follow-up 2: the walk stages of a look-first hold: 0 his plain walk is due, 1 it runs, 2 he is at the body's pad and
  // helper H's look round there is claimed or runs.
  int stage = 0;
  uint32_t pad = 0, walk_task = 0;
  float walk_try = -1e9f, walk_at = -1e9f, best_d = 0.0f, best_at = -1e9f, mv_at = -1e9f, handed_at = -1e9f;
  float mv_pos[3] = {};
  int walk_fails = 0;
  bool search_cut = false;
  float soft_at = -1e9f; // a scientist's soft witness reaction began then (his staff_witness_at)
  bool soft_open = false;
  bool r4_afar = false;  // stealth fixes Mon 16 Nov (fix 4): the body could not be reached: he looks at it from afar
};
// Stealth fixes Mon 16 Nov evening (helper B, fix 4; lab_stealth_r4_B_Mon16Nov.inc).
bool R4BLookFromAfar(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, R3cCState &s, float now, const std::string &why);
void R4BBodyLineTick(uint8_t *base, uint32_t c, Guard &g, float now); // (fix 2)
std::unordered_map<uint32_t, R3cCCone> g_r3c_cone;
R3cCCone g_r3c_cone_last;
std::unordered_map<uint32_t, R3cCState> g_r3c_c;
std::vector<uint32_t> g_r3c_active; // guards with a hold or a soft reaction running
float g_r3c_sweep = -1e9f;
bool g_r3c_last_in_cone = true;
bool g_r3c_cone_logged = false;

// The layer's per-level memory of this file (helper A's R3cClearLayerMemory calls it at every level start).
void R3cCReset()
{
  g_r3c_cone.clear();
  g_r3c_cone_last = R3cCCone();
  g_r3c_c.clear();
  g_r3c_active.clear();
  g_r3c_sweep = -1e9f;
  g_r3c_last_in_cone = true;
  g_r3c_cone_logged = false;
}

bool R3cCOn()
{
  return REXCVAR_GET(pdz_lab_guard_stealth) && RarePlus() && REXCVAR_GET(pdz_stealth_witness_range_cm) >= 0.0;
}

R3cCState &R3cCOf(uint32_t c, const Guard &g)
{
  if (g_r3c_c.size() > 512)
  {
    g_r3c_c.clear();
    g_r3c_active.clear();
  }
  R3cCState &s = g_r3c_c[c];
  if (s.born != g.r2_born)
  {
    s = R3cCState();
    s.born = g.r2_born;
  }
  return s;
}

// From the sight hook (sub_82189F18, a guard's test of Jo, under the lock): Rare's own arguments for him. No logging.
void R3cCSightArgs(uint32_t viewer, float range2, float back, float k, float slope, float back_scale, bool need_cone, bool near_flag,
                   float now)
{
  (void)now;
  if (g_r3c_cone.size() > 1024)
  {
    g_r3c_cone.clear();
  }
  R3cCCone &a = g_r3c_cone[viewer];
  a.have = true;
  a.range2 = range2;
  a.back = back;
  a.k = k;
  a.slope = slope;
  a.back_scale = back_scale;
  a.need_cone = need_cone;
  a.near_flag = near_flag;
  g_r3c_cone_last = a;
}

// (a) The witness test: could `c` have seen Jo make a kill at `body` (where the victim fell; the point tested is 90 cm
// above it)? nullptr = yes, else one of four reasons (the report's words). Out: the distance eye to that point, the
// light's range scale there, "close up, no cone needed" (Rare's own rule), inside his cone.
const char *R3cCSees(PPCContext &ctx, uint8_t *base, uint32_t c, const Guard &g, const float body[3], float now, float eye_up,
                     float cap_cm, float &d, float &scale, bool &beside, bool &in_cone)
{
  float me[3];
  PosOf(base, c, me);
  const float at[3] = {body[0], body[1] + 90.0f, body[2]};
  float eye[3] = {me[0], me[1] + eye_up, me[2]};
  float dir[3] = {g.dir[0], g.dir[1], g.dir[2]};
  const bool fresh = now >= g.eye_at && now - g.eye_at < 3.0f;
  if (fresh)
  {
    std::memcpy(eye, g.eye, sizeof(eye));
  }
  float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
  if (!fresh || dl < 1e-3f)
  {
    const float yaw = LoadF(base, c + 0x1670);
    dir[0] = std::sin(yaw);
    dir[1] = 0.0f;
    dir[2] = std::cos(yaw);
    dl = 1.0f;
  }
  for (float &v : dir)
  {
    v /= dl;
  }
  d = Dist3(eye, at);
  const auto own = g_r3c_cone.find(c);
  const R3cCCone &cone = own != g_r3c_cone.end() ? own->second : g_r3c_cone_last;
  // The range the light scales (follow-up 1): the layer's 30 m, the range round 2's witness test, the body sight and
  // every "light range x.." line have always used. NOT Rare's f1: that is his sight range for the state he was in at
  // his last test (a calm Mansion trooper: 12.5 m; once he is aware Rare passes a longer one), so it changed from one
  // test of the same kill to the next and refused lit kills at 12 m.
  const float rare_range = 3000.0f;
  scale = LightTerm() ? lab_dark::RangeScaleAt(base, at, rare_range) : 1.0f;
  const float dx = at[0] - eye[0], dy = at[1] - eye[1], dz = at[2] - eye[2];
  const float ground = std::sqrt(dx * dx + dz * dz);
  // Right beside him (round 2's 3 m, any facing): kept. Rare's own test of a CALM guard always asks the cone (r3 = 1);
  // its no-cone rule (r3 clear, r4 set: no cone within the distance whose square is at 820EBC58, 65 m) is only passed
  // for a guard who already has a contact (sub_8218A210: profile +12 / +72), so it is not a witness's rule.
  beside = d <= 300.0f;
  in_cone = false;
  if (cone.have)
  {
    if (cone.back == 0.0f)
    {
      in_cone = true; // (Rare asks no cone of him)
    }
    else
    {
      const float set_back = cone.back * cone.back_scale;
      const float vx = at[0] - (eye[0] - dir[0] * set_back), vy = at[1] - (eye[1] - dir[1] * set_back),
                  vz = at[2] - (eye[2] - dir[2] * set_back);
      const float t = (dir[0] * vx + dir[1] * vy + dir[2] * vz) * cone.k;
      in_cone = t > 0.0f && t * t >= vx * vx + vy * vy + vz * vz;
      if (in_cone && cone.slope != 0.0f && dy != 0.0f)
      {
        in_cone = ground > 0.0f && std::fabs(dy / ground) <= cone.slope;
      }
    }
  }
  else
  {
    // No sight test of Rare's on record in this level yet: round 2's rule (75 degrees on the ground plan).
    const float fl = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
    in_cone = ground <= 100.0f || (fl > 1e-4f && (dx * dir[0] + dz * dir[2]) / (ground * fl) >= 0.26f);
  }
  g_r3c_last_in_cone = in_cone || beside;
  if (!g_r3c_cone_logged && cone.have && R2Log())
  {
    g_r3c_cone_logged = true; // once per level load
    REXLOG_INFO("pdz_stealth_r3c: WITNESS CONE (Rare's own sight test arguments, first use in this level, for {}{}): cone half "
                "angle {:.0f} degrees (1/cos {:.3f}) with its apex {:.0f} cm behind his eye, up/down slope limit {:.2f} (0 = "
                "none): used by the witness test, with 3 m right beside him at any facing. Not used: Rare's range in that "
                "test, {:.0f} cm (his range for the state he was in: calm guards short, aware guards longer; the witness "
                "test scales the layer's {:.0f} cm by the light instead), and its flags (cone required {}; Rare's no-cone "
                "rule for a guard with a contact {}: no cone within {:.0f} cm); the witness range cap is {:.0f} cm",
                Id(base, c), own != g_r3c_cone.end() ? "" : ", borrowed from the last guard tested",
                cone.k >= 1.0f ? std::acos(1.0f / cone.k) * 57.2958f : 180.0f, cone.k, cone.back * cone.back_scale, cone.slope,
                cone.range2 > 0.0f ? std::sqrt(cone.range2) : 0.0f, rare_range,
                cone.need_cone ? "yes" : "no", cone.near_flag ? "yes" : "no", std::sqrt(std::max(0.0f, LoadF(base, 0x820EBC58))),
                cap_cm);
  }
  return cap_cm > 0.0f && d > cap_cm                    ? "beyond 20 m"
         : d > rare_range * scale                       ? "beyond light range"
         : !beside && !in_cone                          ? "outside cone"
         : !beside && !SightClear(ctx, base, at, eye)   ? "no clear line"
                                                        : nullptr;
}

R3cCSeen *R3cCSeenOf(R3cCState &s, uint32_t victim, float at)
{
  for (R3cCSeen &e : s.seen)
  {
    if (e.victim == victim && std::fabs(e.at - at) < 0.01f)
    {
      return &e;
    }
  }
  return nullptr;
}

void R3cCReport(const uint8_t *base, uint32_t c, const std::string &victim, float d, float scale, bool in_cone, bool saw_jo,
                const std::string &outcome)
{
  if (R2Log())
  {
    REXLOG_INFO("pdz_stealth_r3c: WITNESS REPORT {} victim {} d {:.1f} m light {:.2f} cone {} saw_jo {} -> {}", Id(base, c), victim,
                d / 100.0f, scale, in_cone ? "in" : "out", saw_jo ? "y" : "n", outcome);
  }
}

// A refusal, once per guard and body (also from round 2's witness rule and the scientists' one).
void R3cCRefused(const uint8_t *base, uint32_t c, Guard &g, uint32_t victim, const std::string &who, float kill_at, float d,
                 float scale, const char *why)
{
  if (!R3cCOn())
  {
    return;
  }
  R3cCState &s = R3cCOf(c, g);
  if (R3cCSeenOf(s, victim, kill_at))
  {
    return;
  }
  if (s.seen.size() >= 32)
  {
    s.seen.erase(s.seen.begin());
  }
  R3cCSeen e;
  e.victim = victim;
  e.at = kill_at;
  e.refused = true;
  s.seen.push_back(e);
  R3cCReport(base, c, who, d, scale, g_r3c_last_in_cone, false, fmt::format("refused: {}", why));
}

bool R3cCSawJo(const uint8_t *base, uint32_t c, const Guard &g, float now, float within)
{
  return R2Seen(g, now, within) || (Load32(base, c + kSight) & kValidTarget) != 0 || g.st == St::Combat || g.st == St::Lost;
}

void R3cCActive(uint32_t c)
{
  if (std::find(g_r3c_active.begin(), g_r3c_active.end(), c) == g_r3c_active.end())
  {
    g_r3c_active.push_back(c);
  }
}

void R3cCOutcome(const uint8_t *base, uint32_t c, const R3cCState &s, float now, const std::string &what)
{
  REXLOG_INFO("pdz_stealth_r3c: WITNESS OUTCOME {} victim {}: {} ({:.1f} s after the kill was seen)", Id(base, c), s.victim_id, what,
              now - s.from);
}

// The hold is over: H's claim on a walk that has not started yet is given back.
void R3cCEnd(uint32_t c, R3cCState &s)
{
  if (s.phase == kR3cCLook)
  {
    const auto h = g_r3h.find(c);
    if (h != g_r3h.end() && h->second.walk_kill == s.kill)
    {
      h->second.walk_kill = -1;
      h->second.take_over = false;
    }
  }
  s.phase = kR3cCNone;
  s.line_due = false;
  if (!s.soft_open)
  {
    g_r3c_active.erase(std::remove(g_r3c_active.begin(), g_r3c_active.end(), c), g_r3c_active.end());
  }
}

// (b) His look is over with nothing found: his held reaction is released. True: Rare's "a friend died in view" is
// raised for his radio call / alarm run (no give-up line); false: he has nothing to pass on.
bool R3cCRelease(uint8_t *base, uint32_t c, R3cCState &s, float now, const std::string &why)
{
  const uint32_t data = Load32(base, c + kData);
  const uint8_t role = Ptr(data) ? base[data + 0xFD] : 0;
  const bool radio = RadioReady(base, c);
  const bool alarm = role == kRoleAlarmUser && !AlarmOn(base);
  if (!radio && !alarm)
  {
    R3cCOutcome(base, c, s, now,
                fmt::format("{}: he has no radio call or alarm run to make (role {}{}): nothing passed on; wary, back to his post, "
                            "his give-up line",
                            why, int(role), role == 3 ? ", the radio cannot call now" : role == kRoleAlarmUser ? ", the alarm sounds" : ""));
    R3cCEnd(c, s);
    return false;
  }
  R3cCOutcome(base, c, s, now,
              fmt::format("escalated: {}: his held reaction is released: Rare's \"a friend died in view\" (+6260 0x800) is raised now "
                          "and let past every rule: Rare's own {} (announced by the existing code)",
                          why, radio ? "RADIO CALL" : "ALARM RUN"));
  R3cCEnd(c, s); // (H's claim, if the walk never started)
  s.phase = kR3cCRaise;
  s.raise_until = now + kR3cCRaiseS;
  R3cCActive(c);
  Store32(base, c + kStimuli, Load32(base, c + kStimuli) | 0x800u);
  return true;
}

// Follow-up 1 (Mansion smoke 00:40:36-38: fre3 and fre2, refused as witnesses, got Rare's "heard a sound" (1000) for
// the falling body four times in 1.4 s, each one starting a new SEARCH beside their DEFAULT script: round 3b's SEARCH
// RESTARTS warning). The restarts are Rare's: every sound event in his reach (a body bounces several times) puts 1000
// in his word, and his reaction starts a new search each time. Before 3c the witness walk's rule took them for these
// guards; a guard refused as a witness has no such rule. So, for any calm or wary guard under the layer: while a
// search of his that began from a sound at a pad is running (within the search cap of its start), another 1000 from
// the same pad is taken from Rare's word. A sound from another pad, or after that search, is a new one (Rare's).
bool R3cCSoundOnce(const uint8_t *base, uint32_t c, Guard &g, float now)
{
  if (!(g.st == St::Unaware || g.st == St::Wary || g.st == St::Investigating))
  {
    return false;
  }
  const auto it = g_r3b.find(c);
  if (it == g_r3b.end() || it->second.born != g.r2_born)
  {
    return false;
  }
  R3BState &b = it->second;
  const uint32_t profile = ProfileOf(base, c);
  const uint32_t pad = profile ? Load32(base, profile + kHeardPad) : 0;
  const float window = std::max(8.0f, float(R2CapS()));
  const bool searching = (Load32(base, c + kConfig) & kSearching) != 0 || (now >= g.search_seen && now - g.search_seen < 1.0f);
  if (!Ptr(pad) || b.start_cause != R3BKey(kR3BCauseSound, pad) || now < b.start_at || now - b.start_at > window || !searching)
  {
    return false;
  }
  static std::unordered_map<uint32_t, float> said;
  if (said.size() > 512)
  {
    said.clear();
  }
  float &last = said[c];
  if (R2Log() && (last == 0.0f || now - last >= 5.0f || now < last))
  {
    last = now;
    REXLOG_INFO("pdz_stealth_r3c: SOUND ONCE: {} Rare's \"heard a sound\" (1000) again from pad {:08X}, {:.1f} s after his search for "
                "that sound began (state {}): taken from Rare's word: one search per sound (no restart)",
                Id(base, c), pad, now - b.start_at, StName(g.st));
  }
  return true;
}

// Follow-up 1 (fre3, 00:40:39-43: his walk to the body was ended 4 s in by the no-progress rule before he had taken a
// step; the same 5 s end in round 3b's run). Rare's SEARCH can stand a guard for up to 6 s before he sets off (the
// reaction, turn and start animations: round 7's note at kStuckStart, which round 11 cut from 8 s to 4 s). A witness
// walk (helper H's item 6) gets 8 s before the no-progress rule looks at it. From the SEARCH update hook.
bool R3cCSlowStart(uint32_t c, const Guard &g, double age)
{
  if (!(age < 8.0))
  {
    return false;
  }
  const auto it = g_r3h.find(c);
  return it != g_r3h.end() && it->second.born == g.r2_born && it->second.inv_item == 6;
}

// From the stimulus word hook (pdz_lab_rareplus), before round 3's rules. Returns the bits taken from Rare's word;
// `keep`: bits no later rule may take (the released reaction).
uint32_t R3cCStimulus(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now, uint32_t &keep)
{
  keep = 0;
  if (!R3cCOn() || g.scientist || !LayerGuard(g) || g.dead || g.st == St::Dead)
  {
    return 0;
  }
  const auto alone = [word](uint32_t drop) {
    // (the WARNER bit 0004 goes too when nothing else is left in the word)
    return drop && (word & 0x4) && !(word & ~(drop | 0x4u)) ? drop | 0x4u : drop;
  };
  const auto found = g_r3c_c.find(c);
  if (found != g_r3c_c.end() && found->second.born == g.r2_born && found->second.phase != kR3cCNone)
  {
    R3cCState &s = found->second;
    if (s.phase == kR3cCRaise)
    {
      if (word & 0x10)
      {
        keep = 0x10;
        REXLOG_INFO("pdz_stealth_r3c: WITNESS RELEASE: {} Rare's word {:04X} shows \"a friend died in view\" ({:.1f} s after the "
                    "release): let past every rule: Rare's reaction (his role's call) runs now | {}",
                    Id(base, c), word, now - (s.raise_until - kR3cCRaiseS), StackText(base, c));
        R3cCEnd(c, s);
      }
      return 0;
    }
    // A hold runs. A real stimulus of Rare's ends it; the quiet starts are taken.
    if (const uint32_t real = word & 0x78Au)
    {
      R3cCOutcome(base, c, s, now,
                  fmt::format("Rare's own stimulus {:04X} reached him ({}): Rare's reaction from here, the hold is over", real,
                              (real & 0x100) ? "he saw a target" : (real & 0x2) ? "he was hit" : (real & 0x80) ? "a near miss"
                              : (real & 0x400) ? "gunfire with a target" : (real & 0x200) ? "the alarm" : "a friend's alert"));
      R3cCEnd(c, s);
      return 0;
    }
    return alone(word & 0x1050u);
  }
  // Follow-up 1: one search per sound (R3cCSoundOnce below).
  const uint32_t once = (word & 0x1000) && !(word & 0x78Au) && R3cCSoundOnce(base, c, g, now) ? 0x1000u : 0u;
  if (!(word & 0x10))
  {
    return alone(once);
  }
  // A new "a friend died in view": the kills of Jo's it can be about (as round 3's once-per-body rule picks them).
  const bool towers = REXCVAR_GET(pdz_stealth_witness_any_squad);
  const float cap_cm = float(REXCVAR_GET(pdz_stealth_witness_range_cm));
  float me[3];
  PosOf(base, c, me);
  R3cCState &s = R3cCOf(c, g);
  int pass = -1, fail = -1;
  float pd = 0.0f, ps = 1.0f, fd = 0.0f, fs = 1.0f;
  bool pc = true, fc = true;
  const char *fwhy = nullptr;
  for (int i = int(g_kills.size()) - 1; i >= 0 && pass < 0; --i)
  {
    const Kill &k = g_kills[size_t(i)];
    if (k.victim == c || now < k.at || now - k.at > kR3WOnceWindow || Dist3(me, k.pos) > kR3WOnceRange)
    {
      continue;
    }
    if (towers && R3WTowerVictim(k) && !R3WMate(g, k.victim))
    {
      continue; // (round 3 item 4 takes it)
    }
    const R3cCSeen *e = R3cCSeenOf(s, k.victim, k.at);
    if (R3WReacted(g, k) || (e && !e->refused))
    {
      continue; // (he has reacted to it: round 3's rule for the repeats)
    }
    if (e && e->refused)
    {
      // Follow-up 1: one kill, one answer. He was judged for this kill and refused: Rare's repeats of the 0010 are
      // taken without a new test. He can still notice the BODY later (R3cCBodyHidden, logged as BODY SIGHT).
      fail = fail < 0 ? i : fail;
      fwhy = fwhy ? fwhy : "refused before";
      continue;
    }
    float d = 0.0f, scale = 1.0f;
    bool beside = false, in_cone = false;
    const char *no = R3cCSees(ctx, base, c, g, k.pos, now, 150.0f, cap_cm, d, scale, beside, in_cone);
    if (!no)
    {
      pass = i;
      pd = d;
      ps = scale;
      pc = in_cone || beside;
    }
    else if (fail < 0)
    {
      fail = i;
      fd = d;
      fs = scale;
      fc = in_cone || beside;
      fwhy = no;
    }
  }
  if (pass < 0)
  {
    if (fail < 0)
    {
      return alone(once); // (no new kill of Jo's on record near him: round 3's rules, Rare's)
    }
    // (a) He could not have seen it: taken from Rare's word. Reported once per body.
    const Kill &k = g_kills[size_t(fail)];
    g_r3c_last_in_cone = fc;
    R3cCRefused(base, c, g, k.victim, k.who, k.at, fd, fs, fwhy);
    return alone(0x10u | once);
  }
  return R3cCBegin(base, c, g, pass, pd, ps, pc, now) ? alone(word & 0x1050u) : 0u;
}

// He passed the witness test for the kill g_kills[pass]: the report, and (b) the look-first hold. True: the hold has
// begun (Rare's reaction is held: the caller takes 0010 and the quiet starts, or does not raise the input at all);
// false: Rare's reaction at once. Follow-up 3: called from BOTH entries: Rare's own "a friend died in view" in his
// stimulus word (R3cCStimulus) and round 2's witness rule the moment its test passes (R2Witness), a squad mate
// included. Before, R2Witness only raised Rare's input and waited for his word; when the word did not come in the same
// frame (Mansion 04:53:59.42, pat3 / pat2), helper H's old claim on round 2's raise started its SEARCH leg first and
// nothing of round 3c ran.
bool R3cCBegin(uint8_t *base, uint32_t c, Guard &g, int pass, float pd, float ps, bool pc, float now)
{
  if (!R3cCOn() || g.scientist || !LayerGuard(g) || g.dead || g.st == St::Dead)
  {
    return false;
  }
  R3cCState &s = R3cCOf(c, g);
  if (s.phase != kR3cCNone)
  {
    return true; // (a hold of his runs already: this kill is held with it)
  }
  const Kill &k = g_kills[size_t(pass)];
  if (R3cCSeenOf(s, k.victim, k.at))
  {
    return false; // (judged for this kill already: nothing new)
  }
  {
    if (s.seen.size() >= 32)
    {
      s.seen.erase(s.seen.begin());
    }
    R3cCSeen fresh;
    fresh.victim = k.victim;
    fresh.at = k.at;
    s.seen.push_back(fresh);
  }
  const bool saw = R3cCSawJo(base, c, g, now, 3.0f);
  const bool calm = (g.st == St::Unaware || g.st == St::Wary) && g.combat_script == 0 && !g.run_pending && !g.run_committed &&
                    !CombatOnStack(base, c);
  const char *at_once = saw                                            ? "he saw Jo herself (his meter confirmed): Rare's reaction at once"
                        : !REXCVAR_GET(pdz_stealth_witness_look_first) ? "pdz_stealth_witness_look_first is off: Rare's reaction at once"
                        : AlarmOn(base)                                ? "the alarm sounds: Rare's reaction at once"
                        : !calm                                        ? "he is busy (not calm or wary): Rare's reaction at once"
                                                                       : nullptr;
  if (at_once)
  {
    R3cCReport(base, c, k.who, pd, ps, pc, saw, at_once);
    return false; // (round 3's once-per-body rule records it as his first reaction)
  }
  if (R10SecondBody(base, c, g, k.victim, k.who, now))
  {
    // Thu 19 Nov: he knew of a body already (or was sure someone is here): a second one makes him Rare's at once.
    R3cCReport(base, c, k.who, pd, ps, pc, saw, "a SECOND BODY: Rare's guard at once, Rare's own reaction (pdz_stealth_second_body)");
    return false;
  }
  // (b) He goes to look first. Round 3's record of his reaction to this body (the repeats and the body's 0040 are
  // taken by rounds 3 / 3b from here on).
  g.r3_once_victim = k.victim;
  std::memcpy(g.r3_once_pos, k.pos, sizeof(g.r3_once_pos));
  g.r3_once_has_pos = true;
  g.r3_once_at = now;
  g.r3_once_killed_at = k.at;
  g.r3_once_done.emplace_back(k.victim, k.at);
  g.r3_once_repeats = 0;
  s.kill = pass;
  s.victim = k.victim;
  s.victim_id = k.who;
  std::memcpy(s.pos, k.pos, sizeof(s.pos));
  s.from = now;
  s.line_due = true;
  s.r4_afar = false; // (stealth fixes Mon 16 Nov, fix 4)
  // (stealth fixes Mon 16 Nov, fix 3: the row's no_look keeps a witness in place only while his own talk is the objective)
  const char *stay = R4BNoLook(g.who)   ? "his row's no_look (his own talk is the objective): he stays where he is"
                     : R4BNoWalk(g.who) ? "his row's no_walk (a tower sniper): he stays where he is"
                     : (Load32(base, c + kConfig) & kNeverInvestigates) ? "Rare's never-investigates flag: he stays where he is"
                     : !REXCVAR_GET(pdz_stealth_witness_walk)           ? "pdz_stealth_witness_walk is off: he stays where he is"
                                                                        : nullptr;
  if (stay)
  {
    s.phase = kR3cCStay;
    s.until = now + kR3cCStayS;
  }
  else
  {
    // Follow-up 2: he walks to the body with Rare's PLAIN go-to-pad walk first (R3cCTick, the walk stages); helper H's
    // walk (its look round at the body) is claimed when he is there. Marked here as his body so that H claims nothing.
    R3HState &h = R3HOf(c, g);
    s.stage = 0;
    s.pad = 0;
    s.walk_task = 0;
    s.walk_try = -1e9f;
    s.walk_fails = 0;
    s.search_cut = false;
    if (std::find(h.walked.begin(), h.walked.end(), pass) == h.walked.end())
    {
      h.walked.push_back(pass);
    }
    h.drop_logged = false;
    h.take_over = false;
    h.blind_logged = false;
    h.first_seen = std::max(h.first_seen, now);
    g.r2_blind_at = -1e9f;
    s.phase = kR3cCLook;
    s.until = now + kR3cCHoldS;
  }
  R3cCActive(c);
  R3cCReport(base, c, k.who, pd, ps, pc, false,
             stay ? fmt::format("looks first from where he stands ({}): his line, {:.0f} s, then his held reaction is released", stay,
                                kR3cCStayS)
                  : std::string("looks first: his line, then he walks to the body; Rare's reaction (stimulus 0010, and the quiet "
                                "starts 1000 / 0040) is held until he sees Jo or has looked at the body"));
  return true;
}

// Once a frame per tracked guard (before R3HTick), pdz_lab_rareplus on.
void R3cCTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
{
  (void)in_search;
  R4BBodyLineTick(base, c, g, now); // (stealth fixes Mon 16 Nov, fix 2: a body line said a second ago and nothing followed)
  if (g_r3c_active.empty())
  {
    return;
  }
  // Once per AI clock step: a witness killed (or gone) while he looked: nothing passed on.
  if (now != g_r3c_sweep)
  {
    g_r3c_sweep = now;
    for (size_t i = g_r3c_active.size(); i-- > 0;)
    {
      const uint32_t o = g_r3c_active[i];
      const auto it = g_r3c_c.find(o);
      const Guard *og = Find(o);
      const bool gone = it == g_r3c_c.end() || !og || og->r2_born != it->second.born;
      const bool dead = !gone && (og->dead || og->st == St::Dead || !(LoadF(base, o + kHealth) > 0.0f));
      if (!gone && !dead)
      {
        continue;
      }
      if (it != g_r3c_c.end())
      {
        R3cCState &os = it->second;
        if (os.phase != kR3cCNone || os.soft_open)
        {
          REXLOG_INFO("pdz_stealth_r3c: WITNESS OUTCOME {} victim {}: {} ({:.1f} s after the kill was seen)",
                      !gone ? Id(base, o) : fmt::format("{:08X}", o), os.victim_id,
                      dead ? "killed on the way: nothing passed on" : "he is gone (his entry was dropped): nothing passed on",
                      now - os.from);
        }
        os.soft_open = false;
        os.phase = kR3cCNone;
        os.line_due = false;
      }
      g_r3c_active.erase(g_r3c_active.begin() + std::ptrdiff_t(i));
    }
  }
  const auto found = g_r3c_c.find(c);
  if (found == g_r3c_c.end() || found->second.born != g.r2_born)
  {
    return;
  }
  R3cCState &s = found->second;
  if (s.soft_open)
  {
    // A scientist's soft reaction: how it ended.
    const bool saw = (Load32(base, c + kSight) & kValidTarget) != 0 || g.spotted || g.run_pending || g.run_committed ||
                     g.meter >= Threshold(g);
    if (saw || now - s.soft_at > 12.0f || now < s.soft_at)
    {
      s.soft_open = false;
      REXLOG_INFO("pdz_stealth_r3c: WITNESS OUTCOME {} victim {}: {} ({:.1f} s after the kill was seen)", Id(base, c), s.victim_id,
                  saw ? "escalated: saw Jo (his own meter filled): his sighting line, then Rare's flight or alarm run"
                      : "saw nothing more: he cowers (Rare's alerted flag), nothing passed on",
                  now - s.from);
      if (s.phase == kR3cCNone)
      {
        g_r3c_active.erase(std::remove(g_r3c_active.begin(), g_r3c_active.end(), c), g_r3c_active.end());
      }
    }
    return;
  }
  if (s.phase == kR3cCNone)
  {
    return;
  }
  if (s.phase == kR3cCRaise)
  {
    if (now >= s.raise_until || now < s.raise_until - kR3cCRaiseS - 1.0f)
    {
      REXLOG_INFO("pdz_stealth_r3c: WITNESS RELEASE: {} Rare's word did not show \"a friend died in view\" within {:.0f} s (his "
                  "DEFAULT script was not the one running): no longer raised | {}",
                  Id(base, c), kR3cCRaiseS, StackText(base, c));
      R3cCEnd(c, s);
    }
    else
    {
      Store32(base, c + kStimuli, Load32(base, c + kStimuli) | 0x800u); // held up until Rare's word has shown it
    }
    return;
  }
  // A hold runs.
  if (s.line_due)
  {
    s.line_due = false;
    static uint32_t ev = 0;
    if (!ev)
    {
      ev = EventIndex(base, "FRIEND_DIES_UNAWARE_TEAM");
    }
    const bool said = ev && Say(ctx, base, c, ev, false, "witness of a kill: he goes to look first");
    if (!said)
    {
      Warn(ctx, base, c, g, kEvHearUnknown, now, "witness of a kill: he goes to look first (pdz_stealth_witness_look_first)");
    }
    FillFaster(base, c, g, now, "saw a friend killed, not Jo: he goes to look first (pdz_stealth_witness_look_first)");
    if (g.st == St::Unaware)
    {
      Set(base, c, g, St::Wary, now, "saw a friend killed, not Jo: wary, he goes to look first");
    }
    if (R2Log())
    {
      REXLOG_INFO("pdz_stealth_r3c: WITNESS LOOK FIRST: {} saw {} killed and has not seen Jo: his line {}; wary and alert; {} | {}",
                  Id(base, c), s.victim_id,
                  said ? "FRIEND_DIES_UNAWARE_TEAM asked for" : "HEAR_UNKNOWN (no friend-dies line for his voice, or it was refused)",
                  s.phase == kR3cCStay ? "he stays where he is and looks" : "his walk to the body is due", StackText(base, c));
    }
  }
  if (R3cCSawJo(base, c, g, now, 0.5f))
  {
    R3cCOutcome(base, c, s, now, "escalated: saw Jo (Rare's valid target, his meter confirmed, or she hit him): Rare's sighting reaction");
    R3cCEnd(c, s);
    return;
  }
  if (AlarmOn(base))
  {
    R3cCOutcome(base, c, s, now, "the alarm sounds: Rare's from here, the hold is over");
    R3cCEnd(c, s);
    return;
  }
  if (in_combat && now - s.from > 1.0f)
  {
    R3cCOutcome(base, c, s, now, "Rare has him in COMBAT (another cause): Rare's from here, the hold is over");
    R3cCEnd(c, s);
    return;
  }
  if (s.phase == kR3cCStay)
  {
    if (now >= s.until || now < s.from)
    {
      const bool afar = s.r4_afar; // (stealth fixes Mon 16 Nov, fix 4)
      s.r4_afar = false;
      R3cCRelease(base, c, s, now,
                  afar ? "body found, nothing else (it cannot be reached: he looked at it from where he could get to)"
                       : "he looked from where he stood and saw nothing");
    }
    return;
  }
  // kR3cCLook. Follow-up 2 (Mansion 01:17:26-44: Rare's SEARCH at the pad beside the body walked mgp1 33 m the other
  // way along the walkway, to the end of his patrol, where the no-progress rule ended it; helper D saw the same with the
  // reinforcements): stages 0 and 1: he goes to the pad beside the body with Rare's PLAIN go-to-pad walk (WalkToPad,
  // the "back to his post" mover: his ordinary walk, no search behaviour), watched here. No cap on it while it brings
  // him nearer. At the pad: stage 2, helper H's look round at the body. "Cannot be reached" (the release) only when no
  // pad beside the body can be found, no walk can be started for kR3cCStartS, Rare's walk ends short of the pad twice,
  // he stands for kR3cCStoodS or comes no nearer for kR3cCNoNearerS, or the hold's last-resort limit.
  if (s.stage < 2)
  {
    R3HState &hh = R3HOf(c, g);
    hh.react_at = now;   // (no heard-react walk of H's on top of this one)
    hh.shout_done = now; // (a heard alarm shout does not turn him round: he walks on)
    g.post_due = -1e9f;  // (no walk back to his post meanwhile)
    g.r2_blind_at = -1e9f;
    if (!(g.st == St::Unaware || g.st == St::Wary))
    {
      R3cCOutcome(base, c, s, now,
                  fmt::format("the layer's own rules have him (state {}: a glimpse of Jo, a check of ours): theirs from here, "
                              "nothing passed on, the hold is over",
                              StName(g.st)));
      R3cCEnd(c, s);
      return;
    }
    if (now >= s.until || now < s.from)
    {
      R3cCRelease(base, c, s, now, "the body cannot be reached (the hold's last-resort time limit)");
      return;
    }
    float me[3], pp[3] = {};
    PosOf(base, c, me);
    if (!Ptr(s.pad))
    {
      s.pad = R3HSpotPad(base, c, s.pos);
      if (!Ptr(s.pad) || !PadPos(base, s.pad, pp))
      {
        if (!R4BLookFromAfar(ctx, base, c, g, s, now, "the body cannot be reached (no pad beside it that he can get to)"))
        {
          R3cCRelease(base, c, s, now, "the body cannot be reached (no pad beside it that he can get to)");
        }
        return;
      }
    }
    PadPos(base, s.pad, pp);
    const float d = Dist3(me, pp);
    const auto hand_over = [&]() {
      // He is at the body's pad: helper H's walk is claimed (its hand-over waits for his DEFAULT script to be on top,
      // then our noise check there: he stops and looks; its end calls R3cCWalkOver).
      hh.walk_kill = s.kill;
      hh.walk_from = now;
      hh.walk_try = -1e9f;
      hh.walk_pad = s.pad;
      std::memcpy(hh.walk_pos, s.pos, sizeof(hh.walk_pos));
      hh.drop_logged = false;
      hh.take_over = false;
      hh.blind_logged = false;
      s.stage = 2;
      s.handed_at = now;
      REXLOG_INFO("pdz_stealth_r3c: WITNESS WALK: {} ARRIVED at the body of {} (pad {:08X}, {:.0f} cm from it; {:.1f} s after the kill "
                  "was seen): he looks round there now (helper H's look, ended by the search cap from here) | {}",
                  Id(base, c), s.victim_id, s.pad, d, now - s.from, StackText(base, c));
    };
    if (s.stage == 0)
    {
      if (d < 400.0f)
      {
        hand_over();
        return;
      }
      if (now - s.walk_try < 0.3f && now >= s.walk_try)
      {
        return;
      }
      s.walk_try = now;
      uint32_t task = 0;
      if (in_combat)
      {
        // (Rare's COMBAT in the first second: the hold's own rule above ends it after 1 s)
      }
      else if (in_search || (Load32(base, c + kConfig) & kSearching) != 0)
      {
        if (!s.search_cut)
        {
          s.search_cut = true;
          g.end_search = true; // (Rare's own end of search at its next update, never early in a walk)
          g.end_search_at = now;
          REXLOG_INFO("pdz_stealth_r3c: WITNESS WALK: {} Rare has him in a SEARCH of his own: ended by Rare's own end of search, then "
                      "his walk to the body | {}",
                      Id(base, c), StackText(base, c));
        }
      }
      else if (R2TopClass(base, c) == 0x82092A8Cu)
      {
        task = WalkToPad(ctx, base, c, s.pad);
      }
      if (!task)
      {
        if (now - (s.walk_at > -1e8f ? s.walk_at : s.from) > kR3cCStartS)
        {
          if (!R4BLookFromAfar(ctx, base, c, g, s, now, "the body cannot be reached (no walk to it could be started: his task stack stayed busy)"))
          {
            R3cCRelease(base, c, s, now, "the body cannot be reached (no walk to it could be started: his task stack stayed busy)");
          }
        }
        return;
      }
      s.walk_task = task;
      s.stage = 1;
      s.walk_at = now;
      s.best_d = d;
      s.best_at = now;
      s.mv_at = now;
      std::memcpy(s.mv_pos, me, sizeof(me));
      REXLOG_INFO("pdz_stealth_r3c: WITNESS WALK: {} WALKS TO THE BODY of {} by Rare's PLAIN WALK (go-to-pad task {:08X}) to the pad "
                  "{:08X} at ({:.0f} {:.0f} {:.0f}) beside it, {:.0f} cm from him (try {}); no cap while he gets nearer; a heard "
                  "shout or sound does not stop him | {}",
                  Id(base, c), s.victim_id, task, s.pad, pp[0], pp[1], pp[2], d, s.walk_fails + 1, StackText(base, c));
      return;
    }
    // Stage 1: the plain walk runs.
    if (d < s.best_d - 100.0f)
    {
      s.best_d = d;
      s.best_at = now;
    }
    if (Dist3(me, s.mv_pos) > 100.0f)
    {
      std::memcpy(s.mv_pos, me, sizeof(me));
      s.mv_at = now;
    }
    if (d < 400.0f)
    {
      hand_over();
      return;
    }
    const bool live = TaskLive(base, c, s.walk_task);
    const char *stuck = now - s.mv_at > kR3cCStoodS         ? "he has stood still for 8 s"
                        : now - s.best_at > kR3cCNoNearerS  ? "he has come no nearer to it for 12 s"
                                                            : nullptr;
    if (live && !stuck)
    {
      return;
    }
    if (!live && !stuck && ++s.walk_fails < 2)
    {
      REXLOG_INFO("pdz_stealth_r3c: WITNESS WALK: {} Rare's plain walk ended {:.0f} cm short of the pad beside the body (at ({:.0f} "
                  "{:.0f} {:.0f})): one more walk",
                  Id(base, c), d, me[0], me[1], me[2]);
      s.stage = 0;
      s.walk_task = 0;
      s.walk_at = now;
      return;
    }
    {
      // (stealth fixes Mon 16 Nov, fix 4: he looks at it from where his walk ended, when he has a clear view)
      const std::string cannot =
          fmt::format("the body cannot be reached ({}; his walk ended {:.0f} cm from the pad beside it, nearest {:.0f} cm)",
                      stuck ? stuck : "Rare's walk ended short of the pad twice", d, s.best_d);
      if (!R4BLookFromAfar(ctx, base, c, g, s, now, cannot))
      {
        R3cCRelease(base, c, s, now, cannot);
      }
    }
    return;
  }
  // Stage 2: helper H's look round at the body. The walk is gone without H's end-of-walk call (it never started: his
  // task stack stayed busy; or it ended while he was not calm), or the time limit.
  const auto h = g_r3h.find(c);
  const bool walking = h != g_r3h.end() && h->second.born == g.r2_born &&
                       (h->second.walk_kill == s.kill || (h->second.inv_item == 6 && h->second.inv_kill == s.kill));
  if (!walking && now - s.handed_at > 0.2f)
  {
    if (!(g.st == St::Unaware || g.st == St::Wary || g.st == St::Investigating))
    {
      R3cCOutcome(base, c, s, now,
                  fmt::format("the layer's own rules have him (state {}: a glimpse of Jo, a search): theirs from here, nothing "
                              "passed on, the hold is over",
                              StName(g.st)));
      R3cCEnd(c, s);
    }
    else
    {
      R3cCRelease(base, c, s, now, "body found, nothing else (he is at it; his look round there could not be started)");
    }
  }
  else if (now - s.handed_at > std::max(8.0f, float(R2CapS())) + 23.0f || now < s.from)
  {
    R3cCRelease(base, c, s, now, "body found, nothing else (he is at it; the time limit of his look)");
  }
}

// From helper H's tick: his walk to the body `kill` is over and he is calm. True: his held reaction was released (his
// radio call / alarm run follows: no give-up line).
bool R3cCWalkOver(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, int kill)
{
  (void)ctx;
  const auto found = g_r3c_c.find(c);
  if (found == g_r3c_c.end() || found->second.born != g.r2_born || found->second.phase != kR3cCLook || found->second.kill != kill)
  {
    return false;
  }
  R3cCState &s = found->second;
  float me[3];
  PosOf(base, c, me);
  const float dx = me[0] - s.pos[0], dz = me[2] - s.pos[2];
  const float plan = std::sqrt(dx * dx + dz * dz);
  // (Follow-up 2: his look only starts once the plain walk has him at the pad beside the body, so he is there.)
  float pp[3] = {};
  const bool at_pad = Ptr(s.pad) && PadPos(base, s.pad, pp) && Dist3(me, pp) <= 500.0f;
  const bool at_body = at_pad || (plan <= kR3cCReachCm && std::fabs(me[1] - s.pos[1]) <= 300.0f);
  return R3cCRelease(base, c, s, now,
                     at_body ? std::string("body found, nothing else (he looked round it and saw nobody)")
                             : fmt::format("body found, nothing else (his look round it ended {:.0f} cm from it)", plan));
}

// (a) for Rare's "a body" (0040), from round 3b's body rule at his first reaction to it: the body of a kill he was
// refused as a witness of stays unnoticed until it passes the witness test. True: take the 0040.
bool R3cCBodyHidden(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t victim, const float *where, float now)
{
  if (!R3cCOn() || !REXCVAR_GET(pdz_stealth_witness_body_test) || g.scientist)
  {
    return false;
  }
  const auto found = g_r3c_c.find(c);
  if (found == g_r3c_c.end() || found->second.born != g.r2_born)
  {
    return false;
  }
  R3cCSeen *e = nullptr;
  for (R3cCSeen &o : found->second.seen)
  {
    if (o.victim == victim && o.refused)
    {
      e = &o;
    }
  }
  if (!e)
  {
    return false;
  }
  float spot[3] = {};
  std::string who = "?";
  bool have = where != nullptr;
  if (where)
  {
    std::memcpy(spot, where, sizeof(spot));
  }
  for (auto it = g_kills.rbegin(); it != g_kills.rend(); ++it)
  {
    if (it->victim == victim && std::fabs(it->at - e->at) < 0.01f)
    {
      who = it->who;
      if (!have)
      {
        std::memcpy(spot, it->pos, sizeof(spot));
        have = true;
      }
      break;
    }
  }
  if (!have)
  {
    return false;
  }
  spot[1] -= 60.0f; // (the test looks 90 cm above the point: a lying body is looked at 30 cm up)
  float d = 0.0f, scale = 1.0f;
  bool beside = false, in_cone = false;
  const char *no = R3cCSees(ctx, base, c, g, spot, now, 150.0f, float(REXCVAR_GET(pdz_stealth_witness_range_cm)), d, scale, beside,
                            in_cone);
  if (!no)
  {
    e->refused = false;
    if (R2Log())
    {
      REXLOG_INFO("pdz_stealth_r3c: BODY SIGHT: {} now has the body of {} in view ({:.1f} m, light {:.2f}, inside his cone, a clear "
                  "line): Rare's \"a body\" (0040) is let through: Rare's own reaction to a body found",
                  Id(base, c), who, d / 100.0f, scale);
    }
    return false;
  }
  if (now - e->body_log >= 10.0f || now < e->body_log) // (Tue 17 Nov night, fix2, item 4: always, not only with the debug log)
  {
    e->body_log = now;
    const float eye_h = R8EyeHeight(base, c, g, now);
    REXLOG_INFO("pdz_stealth_r3c: BODY SIGHT: {} Rare's \"a body\" (0040) for the body of {} ({:.1f} m, light {:.2f}): taken: {} (he "
                "was refused as a witness of this death; the body is noticed once it passes the same test; his eye {} above his "
                "feet{})",
                Id(base, c), who, d / 100.0f, scale, no, eye_h >= 0.0f ? fmt::format("{:.0f} cm", eye_h) : std::string("(not known)"),
                eye_h >= 0.0f && eye_h < 120.0f ? ", SEATED" : "");
  }
  return true;
}

// (b) for scientists, from StaffWitness (he passed the witness test). True: the soft reaction (he did not see Jo).
bool R3cCStaffSoft(const uint8_t *base, uint32_t c, Guard &g, const std::string &victim, float d, float scale, float now)
{
  if (!R3cCOn())
  {
    return false;
  }
  const bool saw = R3cCSawJo(base, c, g, now, 3.0f) || g.spotted;
  if (saw || !REXCVAR_GET(pdz_stealth_witness_look_first))
  {
    R3cCReport(base, c, victim, d, scale, true, saw,
               saw ? "scientist: he saw Jo herself: his sighting line, then Rare's flight or alarm run, at once"
                   : "scientist: pdz_stealth_witness_look_first is off: treated as having seen Jo, at once");
    return false;
  }
  R3cCState &s = R3cCOf(c, g);
  s.soft_at = now;
  s.soft_open = true;
  s.from = now;
  s.victim_id = victim;
  R3cCActive(c);
  R3cCReport(base, c, victim, d, scale, true, false,
             "scientist: looks first from where he stands: an alarmed line, his own meter (faster); no alarm run unless it "
             "fills; else he cowers when his hesitation is over");
  FillFaster(base, c, g, now, "scientist: saw a colleague killed, not Jo (pdz_stealth_witness_look_first)");
  return true;
}

// Testing: pdz_test_stealth_witness "victim,witness,cm[,deg]": the victim is put `cm` in front of the witness (along the
// facing Rare's sight test uses for him, turned by `deg` to his right) for a second, then killed there with Jo as the
// killer: round 3b's pdz_test_stealth_kill_at with the spot worked out from the witness. From the per-guard test spot.
std::string g_r3c_c_test_done;
void R3cCTestWitness(const uint8_t *base, float now)
{
  const std::string want = REXCVAR_GET(pdz_test_stealth_witness);
  if (want == g_r3c_c_test_done)
  {
    return;
  }
  g_r3c_c_test_done = want;
  if (want.empty())
  {
    return;
  }
  std::string text = want;
  std::replace(text.begin(), text.end(), ',', ' ');
  const size_t s1 = text.find(' '), s2 = s1 == std::string::npos ? s1 : text.find(' ', s1 + 1);
  if (s2 == std::string::npos)
  {
    REXLOG_INFO("pdz_lab_stealth: test witness: \"{}\" is not \"victim,witness,cm[,deg]\"", want);
    return;
  }
  const std::string victim = text.substr(0, s1), witness = text.substr(s1 + 1, s2 - s1 - 1);
  char *end = nullptr;
  const char *p = text.c_str() + s2;
  const float cm = std::strtof(p, &end);
  const float deg = end != p ? std::strtof(end, nullptr) : 0.0f;
  if (end == p || !(cm > 0.0f))
  {
    REXLOG_INFO("pdz_lab_stealth: test witness: \"{}\" is not \"victim,witness,cm[,deg]\"", want);
    return;
  }
  uint32_t vc = 0, wc = 0;
  for (const auto &[c, g] : g_guards)
  {
    if (g.st == St::Dead || g.dead || !(LoadF(base, c + kHealth) > 0.0f))
    {
      continue;
    }
    const std::string id = Id(base, c);
    vc = id == victim ? c : vc;
    wc = id == witness ? c : wc;
  }
  const Guard *wg = wc ? Find(wc) : nullptr;
  if (!vc || !wg)
  {
    REXLOG_INFO("pdz_lab_stealth: test witness: no live tracked guard {}", !vc ? victim : witness);
    return;
  }
  float wp[3], vp[3];
  PosOf(base, wc, wp);
  PosOf(base, vc, vp);
  float fx = wg->dir[0], fz = wg->dir[2];
  if (!(now >= wg->eye_at && now - wg->eye_at < 3.0f) || fx * fx + fz * fz < 1e-6f)
  {
    const float yaw = LoadF(base, wc + 0x1670);
    fx = std::sin(yaw);
    fz = std::cos(yaw);
  }
  const float fl = std::sqrt(fx * fx + fz * fz), a = deg * 0.01745329f;
  fx /= fl;
  fz /= fl;
  const float rx = fx * std::cos(a) + fz * std::sin(a), rz = fz * std::cos(a) - fx * std::sin(a);
  g_r3b_kill_at = R3BKillAt();
  g_r3b_kill_at.c = vc;
  g_r3b_kill_at.x = wp[0] + rx * cm;
  g_r3b_kill_at.z = wp[2] + rz * cm;
  g_r3b_kill_at.until = now + 1.0f;
  g_r3b_kill_at.id = victim;
  REXLOG_INFO("pdz_lab_stealth: test witness: {} moved from ({:.0f} {:.0f} {:.0f}) to ({:.0f} {:.0f} {:.0f}), {:.0f} cm in front of {} "
              "(at ({:.0f} {:.0f} {:.0f}), facing ({:.2f} {:.2f}), turned {:.0f} deg; height difference {:+.0f} cm, left to the "
              "game); Rare's Kill on him there in 1 s",
              victim, vp[0], vp[1], vp[2], g_r3b_kill_at.x, vp[1], g_r3b_kill_at.z, cm, witness, wp[0], wp[1], wp[2], fx, fz, deg,
              vp[1] - wp[1]);
}

// Testing (follow-up 1): pdz_test_stealth_witness_when "victim,witness,min_cm,max_cm". pdz_test_stealth_witness (and
// round 3b's kill_at) only write the position copy at character +48: the game's own body, sounds and perception stay
// where he really stood (Mansion smoke 00:40:36: fre1 "moved" 12 m in front of fre3, his corpse and its sounds were
// at his old place 4.6 m away). This one moves nobody: armed, it waits until the victim really is that far from the
// witness, inside his cone and in clear view (the witness test without the light and the cap), then kills him there.
std::string g_r3c_c_when_done;
uint32_t g_r3c_c_when_v = 0, g_r3c_c_when_w = 0;
float g_r3c_c_when_min = 0.0f, g_r3c_c_when_max = 0.0f, g_r3c_c_when_at = -1e9f, g_r3c_c_when_step = -1e9f;
void R3cCTestWhen(PPCContext &ctx, uint8_t *base, float now)
{
  const std::string want = REXCVAR_GET(pdz_test_stealth_witness_when);
  if (want != g_r3c_c_when_done)
  {
    g_r3c_c_when_done = want;
    g_r3c_c_when_v = g_r3c_c_when_w = 0;
    if (want.empty())
    {
      return;
    }
    std::string text = want;
    std::replace(text.begin(), text.end(), ',', ' ');
    const size_t s1 = text.find(' '), s2 = s1 == std::string::npos ? s1 : text.find(' ', s1 + 1);
    char *end = nullptr;
    const char *p = s2 == std::string::npos ? "" : text.c_str() + s2;
    const float lo = std::strtof(p, &end);
    const float hi = end != p ? std::strtof(end, nullptr) : 0.0f;
    if (s2 == std::string::npos || end == p || !(hi > lo))
    {
      REXLOG_INFO("pdz_lab_stealth: test witness when: \"{}\" is not \"victim,witness,min_cm,max_cm\"", want);
      return;
    }
    const std::string victim = text.substr(0, s1), witness = text.substr(s1 + 1, s2 - s1 - 1);
    for (const auto &[c, g] : g_guards)
    {
      if (g.st == St::Dead || g.dead || !(LoadF(base, c + kHealth) > 0.0f))
      {
        continue;
      }
      const std::string id = Id(base, c);
      g_r3c_c_when_v = id == victim ? c : g_r3c_c_when_v;
      g_r3c_c_when_w = id == witness ? c : g_r3c_c_when_w;
    }
    if (!g_r3c_c_when_v || !g_r3c_c_when_w)
    {
      REXLOG_INFO("pdz_lab_stealth: test witness when: no live tracked guard {}", !g_r3c_c_when_v ? victim : witness);
      g_r3c_c_when_v = g_r3c_c_when_w = 0;
      return;
    }
    g_r3c_c_when_min = lo;
    g_r3c_c_when_max = hi;
    g_r3c_c_when_at = now;
    REXLOG_INFO("pdz_lab_stealth: test witness when: armed: {} is killed as soon as he is {:.0f} to {:.0f} cm from {}, inside his "
                "cone and in clear view",
                victim, lo, hi, witness);
  }
  if (!g_r3c_c_when_v || now == g_r3c_c_when_step)
  {
    return;
  }
  g_r3c_c_when_step = now; // (once per AI clock step)
  const Guard *vg = Find(g_r3c_c_when_v), *wg = Find(g_r3c_c_when_w);
  if (!vg || !wg || vg->dead || wg->dead || vg->st == St::Dead || wg->st == St::Dead || now < g_r3c_c_when_at ||
      !(LoadF(base, g_r3c_c_when_v + kHealth) > 0.0f))
  {
    REXLOG_INFO("pdz_lab_stealth: test witness when: given up (one of the two is dead or gone, or a new level)");
    g_r3c_c_when_v = g_r3c_c_when_w = 0;
    return;
  }
  float vp[3];
  PosOf(base, g_r3c_c_when_v, vp);
  float d = 0.0f, scale = 1.0f;
  bool beside = false, in_cone = false;
  const char *no = R3cCSees(ctx, base, g_r3c_c_when_w, *wg, vp, now, 150.0f, 0.0f, d, scale, beside, in_cone);
  const bool fits = d >= g_r3c_c_when_min && d <= g_r3c_c_when_max && in_cone && (!no || std::strcmp(no, "beyond light range") == 0);
  if (!fits)
  {
    return;
  }
  REXLOG_INFO("pdz_lab_stealth: test witness when: {} is {:.0f} cm from {} (light {:.2f}, inside his cone), {:.1f} s after it was "
              "armed: Rare's Kill on him where he stands",
              Id(base, g_r3c_c_when_v), d, Id(base, g_r3c_c_when_w), scale, now - g_r3c_c_when_at);
  g_r3b_kill_at = R3BKillAt();
  g_r3b_kill_at.c = g_r3c_c_when_v;
  g_r3b_kill_at.x = vp[0];
  g_r3b_kill_at.z = vp[2];
  g_r3b_kill_at.until = now;
  g_r3b_kill_at.id = Id(base, g_r3c_c_when_v);
  g_r3c_c_when_v = g_r3c_c_when_w = 0;
}

bool R3cCStaffIsSoft(uint32_t c, const Guard &g)
{
  const auto found = g_r3c_c.find(c);
  return found != g_r3c_c.end() && found->second.born == g.r2_born && found->second.soft_at == g.staff_witness_at;
}
