// Stealth layer, stealth4 quick job, Mon 16 Nov evening, helper A (fixes 1 and 5). Included by lab_stealth.cpp inside
// its anonymous namespace, after lab_stealth_r3c_A_Sun15Nov.inc. Log prefix pdz_stealth_r4:.
//
// Fix 1, LOGGING (always on while the layer runs; one line per event):
//   - KILL: every kill by Jo that the hit hook counts: victim, place, her last shot (the only weapon record the layer
//     has), the light at the victim, and who could see the spot (light range and cone; walls are not tested here).
//   - COMBAT START: every start of Rare's COMBAT script for a tracked guard: Rare's reason as far as the layer can read
//     it (his last stimulus word 821D1888 and its age, his per-frame inputs +6260, his sight word +6268, the caller of
//     the script start), Jo's distance, in his sight cone or not, her gait, and the layer's state for him.
//
// Fix 5, SNEAK UP BEHIND HIM (pdz_stealth_sneak_behind). While Jo is behind a guard who does not know of her (outside
// his sight cone: Rare's own cone half angle for him plus 10 degrees, 75 + 10 when none is on record) and walks or
// crouches, however close:
//   - a sight test of his that Rare answers "visible" although she is more than 90 degrees off the test's own facing
//     (an all-round test of Rare's) is answered "not visible" and does not fill his meter (R4ASneakHides);
//   - "saw a valid target" (0x100) is taken from his stimulus word (R4ASneakStimulus);
//   - a COMBAT start with no sighting no longer counts as real only because she is within 3 m (R4ASneakBlind): round
//     2's blind-combat rule turns it into a search.
// RUNNING behind him within pdz_stealth_sneak_run_cm is heard: the footsteps meter (Footsteps, running only, walking
// and crouching never), then round 3's heard reaction (R3HConsider: his "what's that noise" line, wary, he stops and
// looks round). A BUMP (within pdz_stealth_sneak_bump_cm of him, any gait, any side) is heard at once and nothing
// above hides her for the next 2 s. In front of him the sight rules are unchanged. Stealth off, or the switch off:
// none of this runs.

  struct R4AState
  {
    uint32_t word = 0;        // Rare's last non-zero stimulus word for him
    float word_at = -1e9f;
    float hide_log = -1e9f, drop_log = -1e9f, blind_log = -1e9f;
    float bump_at = -1e9f;
  };
  std::unordered_map<uint32_t, R4AState> g_r4a;
  struct R4AGaitState
  {
    float at = -1e9f;     // the AI clock of the last look
    bool crouch = false;
    bool moving = false;
    float run_at = -1e9f; // she ran then
    float stick = -1.0f;
    bool away = false;    // round 5, fix 1: her weapon is put away (Rare's sprint needs that)
    int gun_state = -1;   // ... the fire state of the gun in her hand (-1: no gun in a hand)
  };
  R4AGaitState g_r4a_gait;

  // Round 5 (Mon 16 Nov, fix 1; 19:11:53: the rule called a full-stick walk with the gun out "running"). Rare's sprint:
  // the weapon is put away first (the gun's Lower: fire state 8 lowering, 13 lowered, then the switcher takes it out of
  // the hand: cut_scenarios.cpp, the gun's fire object +1280, state +4). True: no weapon is out in either hand.
  bool R5AWeaponAway(const uint8_t *base, uint32_t jo, int &state)
  {
    state = -1;
    bool out = false;
    for (const uint32_t hand : {kHand0, kHand1})
    {
      const uint32_t item = Load32(base, jo + hand);
      if (!ArmedItem(base, item))
      {
        continue;
      }
      const uint32_t fire = Load32(base, item + 1280);
      const int s = Ptr(fire) ? int(int32_t(Load32(base, fire + 4))) : 0;
      state = s;
      if (s != 8 && s != 13)
      {
        out = true;
      }
    }
    return !out;
  }

  // Round 5, fix 1: she is sprinting now (weapon put away and moving at more than a walk, not crouched), or was in the
  // last 0.4 s. With a weapon out no stick push is a sprint.
  bool R5ASprinting(float now)
  {
    return now >= g_r4a_gait.at && now - g_r4a_gait.at < 0.5f && now >= g_r4a_gait.run_at && now - g_r4a_gait.run_at < 0.4f;
  }

  R4AState &R4AOf(uint32_t c)
  {
    if (g_r4a.size() > 1024)
    {
      g_r4a.clear();
    }
    return g_r4a[c];
  }

  bool R4ASneakOn()
  {
    return REXCVAR_GET(pdz_lab_guard_stealth) && REXCVAR_GET(pdz_stealth_sneak_behind) && !lab_stealth::CounterOpGameNow();
  }

  // Once per AI frame (from Footsteps, which has the context Rare's crouch test needs): Jo's gait.
  void R4AGait(PPCContext &ctx, uint8_t *base, uint32_t jo, float now)
  {
    if (now == g_r4a_gait.at || !Ptr(jo))
    {
      return;
    }
    g_r4a_gait.at = now;
    const float stick = MoveStick(base, jo);
    g_r4a_gait.stick = stick;
    g_r4a_gait.crouch = Crouched(ctx, base, jo);
    g_r4a_gait.moving = g_jo_speed >= 30.0f || stick > 0.15f;
    g_r4a_gait.away = R5AWeaponAway(base, jo, g_r4a_gait.gun_state);
    // (round 5, fix 1: only Rare's sprint is running: the weapon put away AND more than a walk)
    const bool running = g_r4a_gait.away && !g_r4a_gait.crouch && g_r4a_gait.moving &&
                         (stick >= 0.0f ? stick > float(REXCVAR_GET(pdz_lab_footsteps_walk_stick)) + 0.02f : g_jo_speed > 300.0f);
    if (running)
    {
      g_r4a_gait.run_at = now;
    }
  }

  // Her gait is on record for this moment and she is not running (and has not run in the last 0.4 s).
  bool R4AQuiet(float now)
  {
    return now >= g_r4a_gait.at && now - g_r4a_gait.at < 0.5f && !(now >= g_r4a_gait.run_at && now - g_r4a_gait.run_at < 0.4f);
  }

  std::string R4AGaitName(float now)
  {
    if (!(now >= g_r4a_gait.at && now - g_r4a_gait.at < 0.5f))
    {
      return "gait not on record, sprint not known";
    }
    const bool sprint = now >= g_r4a_gait.run_at && now - g_r4a_gait.run_at < 0.4f;
    const char *gait = sprint              ? "sprinting"
                       : g_r4a_gait.crouch ? (g_r4a_gait.moving ? "crouched, moving" : "crouched, still")
                       : g_r4a_gait.moving ? "walking"
                                           : "standing still";
    // (round 5, fix 1: every SNEAK line says sprint yes / no and what decided it)
    return fmt::format("{}; sprint {} (weapon {}, {})", gait, sprint ? "yes" : "no",
                       !g_r4a_gait.away               ? fmt::format("out, fire state {}", g_r4a_gait.gun_state)
                       : g_r4a_gait.gun_state >= 0    ? fmt::format("put away, fire state {}", g_r4a_gait.gun_state)
                                                      : std::string("put away, none in a hand"),
                       g_r4a_gait.stick >= 0.0f ? fmt::format("stick {:.2f}", g_r4a_gait.stick)
                                                : fmt::format("stick not readable, speed {:.0f} cm/s", g_jo_speed));
  }

  // His cone's half angle (degrees): Rare's own for him when a sight test of his is on record, else 75.
  float R4AConeHalfDeg(uint32_t c)
  {
    const auto it = g_r3c_cone.find(c);
    if (it != g_r3c_cone.end() && it->second.have && it->second.back != 0.0f && it->second.k >= 1.0f)
    {
      return std::clamp(std::acos(1.0f / it->second.k) * 57.2958f, 45.0f, 100.0f);
    }
    return 75.0f;
  }

  // How far off his facing Jo is on the ground plan (degrees, 0 = straight ahead); `ground` = their ground distance.
  float R4AOffDeg(const uint8_t *base, uint32_t c, const Guard &g, uint32_t jo, float now, float &ground)
  {
    float fx = 0.0f, fz = 0.0f;
    if (now >= g.face_at && now - g.face_at < 0.5f)
    {
      fx = g.face[0];
      fz = g.face[2];
    }
    else if (now >= g.eye_at && now - g.eye_at < 0.5f)
    {
      fx = g.dir[0];
      fz = g.dir[2];
    }
    float fl = std::sqrt(fx * fx + fz * fz);
    if (fl < 1e-3f)
    {
      const float yaw = LoadF(base, c + 0x1670);
      fx = std::sin(yaw);
      fz = std::cos(yaw);
      fl = 1.0f;
    }
    float me[3], her[3];
    PosOf(base, c, me);
    PosOf(base, jo, her);
    const float dx = her[0] - me[0], dz = her[2] - me[2];
    ground = std::sqrt(dx * dx + dz * dz);
    if (ground < 1.0f)
    {
      return 0.0f;
    }
    return std::acos(std::clamp((dx * fx + dz * fz) / (ground * fl), -1.0f, 1.0f)) * 57.2958f;
  }

  bool R4ABumped(uint32_t c, float ground, float now)
  {
    if (ground <= float(REXCVAR_GET(pdz_stealth_sneak_bump_cm)))
    {
      return true;
    }
    const auto it = g_r4a.find(c);
    return it != g_r4a.end() && now >= it->second.bump_at && now - it->second.bump_at < 2.0f;
  }

  // The rule itself: he does not know of her, she is behind him, quiet, and has not bumped him.
  bool R4ASneakHolds(const uint8_t *base, uint32_t c, const Guard &g, uint32_t jo, float now, float &off, float &ground)
  {
    off = 0.0f;
    ground = 0.0f;
    if (!R4ASneakOn() || !Ptr(jo) || g.dead || g.st == St::Dead || g.spotted || g.combat_script != 0 || g.run_committed ||
        !(g.st == St::Unaware || g.st == St::Wary || g.st == St::Suspicious) || !R4AQuiet(now))
    {
      return false;
    }
    off = R4AOffDeg(base, c, g, jo, now, ground);
    return off > R4AConeHalfDeg(c) + 10.0f && !R4ABumped(c, ground, now);
  }

  // From the sight hook (a metered guard's test of Jo that Rare answered "visible", under the lock). True: answered
  // "not visible" instead, no meter. `dir` is the facing of this very test.
  bool R4ASneakHides(const uint8_t *base, uint32_t viewer, Guard &g, uint32_t jo, const float eye[3], const float dir[3],
                     const float at[3], float now)
  {
    if (!R4ASneakOn() || g.spotted || g.combat_script != 0 || !R4AQuiet(now))
    {
      return false;
    }
    const float dx = at[0] - eye[0], dz = at[2] - eye[2];
    const float ground = std::sqrt(dx * dx + dz * dz);
    const float fl = std::sqrt(dir[0] * dir[0] + dir[2] * dir[2]);
    if (ground < 1.0f || fl < 1e-3f || R4ABumped(viewer, ground, now))
    {
      return false;
    }
    const float off = std::acos(std::clamp((dx * dir[0] + dz * dir[2]) / (ground * fl), -1.0f, 1.0f)) * 57.2958f;
    if (!(off > 90.0f && off > R4AConeHalfDeg(viewer) + 10.0f))
    {
      return false;
    }
    R4AState &s = R4AOf(viewer);
    if (now - s.hide_log >= 3.0f || now < s.hide_log)
    {
      s.hide_log = now;
      REXLOG_INFO("pdz_stealth_r4: SNEAK: {} Rare's sight test saw Jo {:.0f} cm BEHIND him ({:.0f} deg off his facing, his cone "
                  "{:.0f}; she is {}): answered not visible, no meter (state {}, meter {:.0f})",
                  Id(base, viewer), ground, off, R4AConeHalfDeg(viewer), R4AGaitName(now), StName(g.st), g.meter);
    }
    (void)jo;
    return true;
  }

  // From the stimulus word hook (under the lock): record Rare's word for the COMBAT START line.
  void R4AStimSeen(uint32_t c, uint32_t word, float now)
  {
    R4AState &s = R4AOf(c);
    s.word = word;
    s.word_at = now;
  }

  // From the stimulus word hook, after everything else: the bits taken under the sneak rule.
  uint32_t R4ASneakStimulus(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
  {
    if (!(word & 0x100) || (word & 0x682u) != 0 || g.scientist || !LayerGuard(g)) // (damaged, near miss, the alarm, gunfire with a target: his)
    {
      return 0;
    }
    float off = 0.0f, ground = 0.0f;
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!R4ASneakHolds(base, c, g, jo, now, off, ground))
    {
      return 0;
    }
    R4AState &s = R4AOf(c);
    if (now - s.drop_log >= 3.0f || now < s.drop_log)
    {
      s.drop_log = now;
      REXLOG_INFO("pdz_stealth_r4: SNEAK: {} Rare's stimulus {:04X} says \"saw a valid target\" with Jo {:.0f} cm BEHIND him "
                  "({:.0f} deg off his facing, his cone {:.0f}; she is {}): 0100 taken from it (state {}, meter {:.0f})",
                  Id(base, c), word, ground, off, R4AConeHalfDeg(c), R4AGaitName(now), StName(g.st), g.meter);
    }
    return 0x100u;
  }

  // From round 2's blind-combat test: true = "she is within 3 m of him" is not a real reason for this COMBAT start.
  bool R4ASneakBlind(const uint8_t *base, uint32_t c, const Guard &g, float now)
  {
    float off = 0.0f, ground = 0.0f;
    const uint32_t jo = Load32(base, kPlayerOne);
    // (the COMBAT script has just started: the rule is asked as if it had not)
    if (!R4ASneakOn() || !Ptr(jo) || g.spotted || !(g.st == St::Unaware || g.st == St::Wary || g.st == St::Suspicious) ||
        !R4AQuiet(now))
    {
      return false;
    }
    off = R4AOffDeg(base, c, g, jo, now, ground);
    if (!(off > R4AConeHalfDeg(c) + 10.0f) || R4ABumped(c, ground, now))
    {
      return false;
    }
    R4AState &s = R4AOf(c);
    if (now - s.blind_log >= 3.0f || now < s.blind_log)
    {
      s.blind_log = now;
      REXLOG_INFO("pdz_stealth_r4: SNEAK: {} Rare started his COMBAT with no sighting while Jo is {:.0f} cm BEHIND him ({:.0f} deg "
                  "off his facing; she is {}): \"within 3 m\" does not make it a real one: round 2's blind-combat rule decides",
                  Id(base, c), ground, off, R4AGaitName(now));
    }
    return true;
  }

  // From Footsteps with only the sneak rule on (pdz_lab_footsteps off). False: her steps do not count for him now (she
  // is in front of him: the sight rules). A bump is heard whatever her gait: `heard_cm` is raised to it.
  bool R4AStepsCount(const uint8_t *base, uint32_t c, const Guard &g, uint32_t jo, float d, float now, float &heard_cm, bool &bump)
  {
    float ground = 0.0f;
    const float off = R4AOffDeg(base, c, g, jo, now, ground);
    const float bump_cm = float(REXCVAR_GET(pdz_stealth_sneak_bump_cm));
    bump = bump_cm > 0.0f && ground <= bump_cm && d <= bump_cm + 100.0f;
    if (bump)
    {
      heard_cm = std::max(heard_cm, d + 1.0f);
      return true;
    }
    return off > R4AConeHalfDeg(c) + 10.0f;
  }

  // From Footsteps, the moment his hearing meter is full under the sneak rule. True: handled here (round 3's heard
  // reaction); false: the footsteps rule's own reaction follows.
  bool R4AHeard(const uint8_t *base, uint32_t c, Guard &g, uint32_t jo, bool bump, float d, float now)
  {
    float her[3];
    PosOf(base, jo, her);
    if (bump)
    {
      R4AOf(c).bump_at = now;
    }
    const bool r3 = RarePlus() && REXCVAR_GET(pdz_stealth_heard_react);
    float ground = 0.0f;
    const float off = R4AOffDeg(base, c, g, jo, now, ground);
    REXLOG_INFO("pdz_stealth_r4: SNEAK: {} {} ({:.0f} cm away, {:.0f} deg off his facing; she is {}; state {}, meter {:.0f}): {}",
                Id(base, c), bump ? "was BUMPED by Jo" : "HEARD Jo sprinting behind him", d, off, R4AGaitName(now), StName(g.st), g.meter,
                r3 ? "a sound for round 3's heard reaction (his line, wary, he stops and looks round)"
                   : "the footsteps rule's own reaction (he goes to look where she was)");
    if (!r3)
    {
      return false;
    }
    R3HConsider(R3HOf(c, g), kR3HSound, jo, her, d, 0, now);
    g.meter = std::max(g.meter, Notice(g));
    return true;
  }

  std::string R4AWordNames(uint32_t word)
  {
    static const struct
    {
      uint32_t bit;
      const char *name;
    } kBits[] = {{0x2, "damaged"},          {0x4, "WARNER"},        {0x8, "alerted by a friend"}, {0x10, "a friend died in view"},
                 {0x20, "a hurt friend"},   {0x40, "a body"},       {0x80, "near miss"},          {0x100, "saw a valid target"},
                 {0x200, "the alarm"},      {0x400, "gunfire with a target"},                     {0x1000, "heard a sound"}};
    std::string out;
    uint32_t rest = word;
    for (const auto &b : kBits)
    {
      if (word & b.bit)
      {
        out += out.empty() ? "" : ", ";
        out += b.name;
        rest &= ~b.bit;
      }
    }
    if (rest)
    {
      out += fmt::format("{}other {:04X}", out.empty() ? "" : ", ", rest);
    }
    return out.empty() ? std::string("none") : out;
  }

  // Fix 1, from the COMBAT script start hook (after Rare's start, under the lock): one line, always on.
  void R4ACombatLog(const uint8_t *base, uint32_t c, const Guard &g, bool sighting, float now, uint32_t caller)
  {
    const uint32_t jo = Load32(base, kPlayerOne);
    float off = 0.0f, ground = 0.0f;
    const bool have = Ptr(jo);
    if (have)
    {
      off = R4AOffDeg(base, c, g, jo, now, ground);
    }
    const float half = R4AConeHalfDeg(c);
    const auto it = g_r4a.find(c);
    const bool word_known = it != g_r4a.end() && it->second.word_at > -1e8f && now >= it->second.word_at;
    const uint32_t sight = Load32(base, c + kSight);
    REXLOG_INFO("pdz_stealth_r4: COMBAT START: {} | Rare's reason: {} | his inputs +6260 {:08X}, sight +6268 {:08X} (valid target "
                "{}), script start called from {:08X} | Jo {} | our state {} meter {:.0f}, {}{}",
                Id(base, c),
                word_known ? fmt::format("his last stimulus word {:04X} ({}) {:.1f} s ago", it->second.word, R4AWordNames(it->second.word),
                                         now - it->second.word_at)
                           : std::string("no stimulus word of his on record"),
                Load32(base, c + kStimuli), sight, (sight & kValidTarget) ? "yes" : "no", caller,
                have ? fmt::format("{:.0f} cm away ({:.0f} on the ground plan), {} his sight cone ({:.0f} deg off his facing, cone half "
                                   "angle {:.0f}), {}",
                                   Dist(base, c, jo), ground, off <= half ? "IN" : "OUTSIDE", off, half, R4AGaitName(now))
                     : std::string("not in the level"),
                StName(g.st), g.meter, sighting ? "a sighting of ours (valid target, or his sight test passed in the last 0.5 s)"
                                                : "NO sighting of ours",
                g.scientist ? ", a scientist" : "");
  }

  // Fix 1, from the hit hook (a kill by Jo just counted, under the lock): one line, always on.
  void R4AKillLog(const uint8_t *base, const Kill &k, float now)
  {
    const uint32_t jo = Load32(base, kPlayerOne);
    const float chest[3] = {k.pos[0], k.pos[1] + 90.0f, k.pos[2]};
    const float scale = LightTerm() ? lab_dark::RangeScaleAt(base, chest, 3000.0f) : 1.0f;
    std::string shot = "no shot of hers on record in the last 2 s (a melee hit, a thrown weapon or something else)";
    for (const Shot &sh : g_shots)
    {
      if (sh.shooter == jo && now >= sh.at && now - sh.at < 2.0f)
      {
        shot = fmt::format("her last shot {:.2f} s before, {}", now - sh.at, sh.silenced ? "SILENCED" : "UNSILENCED");
      }
    }
    std::string could;
    int n = 0;
    for (const auto &[other, o] : g_guards)
    {
      if (other == k.victim || o.dead || o.st == St::Dead || !Ptr(other))
      {
        continue;
      }
      float me[3];
      PosOf(base, other, me);
      const float d = Dist3(me, chest);
      if (d > 3000.0f * scale)
      {
        continue;
      }
      // (his facing against the victim's spot, as R4AOffDeg does for Jo)
      float fx = 0.0f, fz = 0.0f;
      if (now >= o.eye_at && now - o.eye_at < 3.0f)
      {
        fx = o.dir[0];
        fz = o.dir[2];
      }
      float fl = std::sqrt(fx * fx + fz * fz);
      if (fl < 1e-3f)
      {
        const float yaw = LoadF(base, other + 0x1670);
        fx = std::sin(yaw);
        fz = std::cos(yaw);
        fl = 1.0f;
      }
      const float dx = chest[0] - me[0], dz = chest[2] - me[2];
      const float ground = std::sqrt(dx * dx + dz * dz);
      const float off = ground < 1.0f ? 0.0f : std::acos(std::clamp((dx * fx + dz * fz) / (ground * fl), -1.0f, 1.0f)) * 57.2958f;
      if (d > 300.0f && off > R4AConeHalfDeg(other))
      {
        continue;
      }
      if (++n <= 8)
      {
        could += fmt::format("{}{} ({:.0f} cm, {:.0f} deg off his facing, {})", could.empty() ? "" : ", ", Id(base, other), d, off,
                             StName(o.st));
      }
    }
    if (n > 8)
    {
      could += fmt::format(" and {} more", n - 8);
    }
    REXLOG_INFO("pdz_stealth_r4: KILL: Jo killed {} at ({:.0f} {:.0f} {:.0f}) in {} | Jo {} | {} | light at the victim: range x{:.2f} "
                "({:.0f} cm of 3000) | could see the spot (in that range and in his cone or within 3 m; walls not tested): {}",
                k.who, k.pos[0], k.pos[1], k.pos[2], ZoneLevel() && g_level ? g_level->name : "the Laboratory",
                Ptr(jo) ? fmt::format("{:.0f} cm from him", Dist(base, k.victim, jo)) : std::string("not in the level"), shot, scale,
                3000.0f * scale, could.empty() ? std::string("nobody") : could);
  }
