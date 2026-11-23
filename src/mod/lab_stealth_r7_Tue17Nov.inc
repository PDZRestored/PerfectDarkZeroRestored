  // Stealth layer, Tue 17 Nov evening: ONE OWNER PER GUARD (pdz_stealth_combat_owner). Included by lab_stealth.cpp
  // inside its anonymous namespace, before GiveSearch. Log prefix "pdz_stealth_r7:".
  //
  // The hand test (Tue 17 Nov 18:56, Nightclub docks): two guards whose sighting the layer's meter had just confirmed
  // were put into Rare's COMBAT by the layer's own "alert now", stood about 8 m from Jo, swung their facing 40 times a
  // second and never fired; Rare's sight word for them never showed "sees a hostile". A third stood wary while the
  // layer emptied his target holders about once a second and Rare refilled them at once.
  //
  // The rule: at any moment a guard's facing and target belong to ONE system.
  //   - RARE'S COMBAT (Rare's COMBAT script runs and the layer's own state for him is combat, with no blind-combat
  //     hand-over pending): the layer keeps its hands off. No SEARCH is handed over, no look round, no turn-to, no
  //     go-and-look, his target holders are not emptied, and nothing is taken from Rare's stimulus word. The only way
  //     out is the layer's deliberate end of the fight (lose contact, the alarm's end), which asks Rare to leave COMBAT
  //     in the same step.
  //   - THE LAYER'S SEARCH (a COMBAT that began with no sighting: round 2's blind-combat rule turns it into a search,
  //     once): Rare is asked to leave, then the layer's search runs.
  //   - RARE, CALM: the layer may hand him its own tasks. The target emptying of round 4 (NO STEER) is limited: when
  //     Rare has refilled the holders three times in 10 s the layer stops for 20 s instead of fighting Rare for them.
  // And the squad's target place is written for a guard whose sighting the layer has just confirmed (the layer used to
  // start his COMBAT and withhold the place in the same frame).
  //
  // THE RECORD, for the FACING SWINGS and NO STEER lines: every write of the layer to a guard (which rule, what, when),
  // every write it held back, the last task queued for him by anyone (its class, the caller), Rare's own sight of Jo
  // (when "sees a hostile" was last up, how many of his sight tests of her passed in the last second), and his last
  // frames (yaw, target, top task).

  enum class R7Owner : uint8_t
  {
    RareCalm = 0,   // Rare's default behaviour; the layer may hand him tasks
    RareCombat,     // Rare's COMBAT, the layer agrees: hands off
    LayerSearch,    // a blind COMBAT being turned into the layer's search
    RareCombatOdd,  // Rare's COMBAT runs but the layer's state for him is not combat
  };
  const char *R7OwnerName(R7Owner o)
  {
    return o == R7Owner::RareCombat      ? "Rare's COMBAT (the layer agrees he has seen Jo: hands off)"
           : o == R7Owner::LayerSearch   ? "the layer's search (a COMBAT with no sighting, being turned into a search)"
           : o == R7Owner::RareCombatOdd ? "Rare's COMBAT, while the layer's own state for him is NOT combat"
                                         : "Rare's calm behaviour (the layer may hand him tasks)";
  }
  struct R7Frame
  {
    float at = -1e9f, yaw = 0.0f;
    uint32_t sight = 0, top = 0;
    uint8_t target = 0; // 0 none, 1 Jo, 2 another character
    uint8_t owner = 0;
    uint16_t writes = 0; // the layer's writes to him so far
    uint16_t vetoes = 0; // (Tue 17 Nov night, fix2: our sight hook's vetoes of Rare's sight of Jo so far)
  };
  struct R7State
  {
    float born = -2e9f;
    // The layer's writes.
    const char *w_rule = nullptr, *w_what = nullptr;
    float w_at = -1e9f;
    int w_n = 0;
    // ... held back by the rule.
    const char *h_rule = nullptr, *h_what = nullptr;
    float h_at = -1e9f, h_log = -1e9f;
    int h_n = 0;
    // The last task queued for him (by anyone).
    uint32_t q_class = 0, q_caller = 0;
    const char *q_rule = nullptr;
    float q_at = -1e9f;
    int q_n = 0;
    // Rare's own sight of Jo.
    float hostile_at = -1e9f, valid_at = -1e9f; // +6268 0x400000 / 0x800000 last up
    float test_from = -1e9f, pass_at = -1e9f;
    int tests = 0, passes = 0, tests_last = -1, passes_last = 0;
    // (Tue 17 Nov night, fix2) Our sight hook's vetoes: Rare's own test passed, our answer was "not visible".
    int vetoes = 0, vetoes_last = 0, veto_total = 0;
    const char *veto_why = nullptr;
    float veto_at = -1e9f, window_end = -1e9f;
    float dark_scale = 1.0f, dark_at = -1e9f; // the Laboratory darkness range scale inside Rare's test (1 = none)
    // The NO STEER limit.
    float ns_from = -1e9f, ns_quiet = -1e9f;
    int ns_n = 0;
    bool ns_alert = false; // (Thu 19 Nov: no emptying for the rest of this alert)
    // His last frames.
    R7Frame ring[10];
    int ring_n = 0;
    R7Owner owner = R7Owner::RareCalm;
    float owner_at = -1e9f;
  };
  std::unordered_map<uint32_t, R7State> g_r7;
  thread_local const char *t_r7_rule = nullptr; // the layer rule running now (nullptr: not inside the layer)
  thread_local bool t_r7_leaving = false;       // the layer's deliberate end of a fight is running
  struct R7Rule
  {
    const char *prev;
    explicit R7Rule(const char *rule) : prev(t_r7_rule) { t_r7_rule = rule; }
    ~R7Rule() { t_r7_rule = prev; }
  };
  struct R7Leaving
  {
    bool prev;
    R7Leaving() : prev(t_r7_leaving) { t_r7_leaving = true; }
    ~R7Leaving() { t_r7_leaving = prev; }
  };

  void R7Reset()
  {
    g_r7.clear();
  }
  R7State &R7Of(uint32_t c, const Guard &g)
  {
    if (g_r7.size() > 512)
    {
      g_r7.clear();
    }
    R7State &s = g_r7[c];
    if (s.born != g.r2_born)
    {
      s = R7State();
      s.born = g.r2_born;
    }
    return s;
  }

  // Who owns him now (a few loads; under the lock).
  R7Owner R7OwnerOf(const uint8_t *base, uint32_t c, const Guard &g)
  {
    if (g.dead || g.st == St::Dead || !(g.combat_script != 0 || CombatOnStack(base, c)))
    {
      return R7Owner::RareCalm;
    }
    if (g.r2_blind_at > -1e8f)
    {
      return R7Owner::LayerSearch;
    }
    return g.st == St::Combat ? R7Owner::RareCombat : R7Owner::RareCombatOdd;
  }
  // The layer keeps its hands off him now.
  bool R7HandsOff(const uint8_t *base, uint32_t c, const Guard &g)
  {
    return REXCVAR_GET(pdz_stealth_combat_owner) && RarePlus() && !t_r7_leaving &&
           R7OwnerOf(base, c, g) == R7Owner::RareCombat;
  }
  // A write of the layer is about to be made: true = it is held back (he is Rare's). Under the lock.
  bool R7Refuse(const uint8_t *base, uint32_t c, const char *what)
  {
    Guard *g = Ptr(c) ? Find(c) : nullptr;
    if (!g || !R7HandsOff(base, c, *g))
    {
      return false;
    }
    R7State &s = R7Of(c, *g);
    const float now = Clock(base);
    s.h_rule = t_r7_rule ? t_r7_rule : "a layer rule without a label";
    s.h_what = what;
    s.h_at = now;
    ++s.h_n;
    if (now - s.h_log >= 5.0f || now < s.h_log)
    {
      s.h_log = now;
      REXLOG_INFO("pdz_stealth_r7: HANDS OFF: {} is in Rare's COMBAT and the layer agrees he has seen Jo (state {}, meter {:.0f}, "
                  "spotted {}): the layer's wish to {} ({}) is NOT carried out: Rare's combat owns his facing and his target "
                  "(time {} for him; pdz_stealth_combat_owner) | {}",
                  Id(base, c), StName(g->st), g->meter, g->spotted ? 1 : 0, what, s.h_rule, s.h_n, StackText(base, c));
    }
    return true;
  }
  // A write of the layer was made. Under the lock.
  void R7Wrote(const uint8_t *base, uint32_t c, const char *what)
  {
    Guard *g = Ptr(c) ? Find(c) : nullptr;
    if (!g)
    {
      return;
    }
    R7State &s = R7Of(c, *g);
    s.w_rule = t_r7_rule ? t_r7_rule : "a layer rule without a label";
    s.w_what = what;
    s.w_at = Clock(base);
    ++s.w_n;
  }
  // From the scheduler hook (sub_82171210): a task was queued for him, by Rare or by the layer. Under the lock.
  void R7Queued(const uint8_t *base, uint32_t c, uint32_t task, uint32_t caller)
  {
    Guard *g = Ptr(c) ? Find(c) : nullptr;
    if (!g)
    {
      return;
    }
    R7State &s = R7Of(c, *g);
    s.q_class = Ptr(task) ? Load32(base, task) : 0;
    s.q_caller = caller;
    s.q_rule = t_r7_rule;
    s.q_at = Clock(base);
    ++s.q_n;
    if (t_r7_rule)
    {
      R7Wrote(base, c, "a task queued on his stack");
    }
  }
  // From the sight test hook (sub_82189F18), EVERY test of Jo by a tracked guard (Tue 17 Nov night, fix2: it used to
  // count only while he was not metered, so a metered guard's line repeated an old second for minutes): Rare's own
  // answer, whether our hook then hid her (and why), the darkness range scale used inside Rare's test. Under the lock.
  void R7SightTest(const uint8_t *base, uint32_t viewer, bool passed, bool vetoed = false, const char *why = nullptr,
                   float dark_scale = 1.0f)
  {
    Guard *g = Ptr(viewer) ? Find(viewer) : nullptr;
    if (!g)
    {
      return;
    }
    R7State &s = R7Of(viewer, *g);
    const float now = Clock(base);
    if (now - s.test_from >= 1.0f || now < s.test_from)
    {
      s.tests_last = s.test_from > -1e8f ? s.tests : -1;
      s.passes_last = s.passes;
      s.vetoes_last = s.vetoes;
      s.window_end = now;
      s.tests = s.passes = s.vetoes = 0;
      s.test_from = now;
    }
    ++s.tests;
    if (passed)
    {
      ++s.passes;
      s.pass_at = now;
    }
    if (vetoed)
    {
      ++s.vetoes;
      ++s.veto_total;
      s.veto_why = why ? why : "a rule without a label";
      s.veto_at = now;
    }
    if (dark_scale < 0.999f)
    {
      s.dark_scale = dark_scale;
      s.dark_at = now;
    }
  }
  // The NO STEER limit: may the layer empty his target holders now? (Rare refills them at once: after three in 10 s
  // the layer stops for 20 s; the first refusal of a pause is logged.)
  bool R7NoSteerMay(const uint8_t *base, uint32_t c, const Guard &g, float now)
  {
    if (!REXCVAR_GET(pdz_stealth_combat_owner))
    {
      return true;
    }
    R7State &s = R7Of(c, g);
    // Thu 19 Nov (pdz_stealth_no_steer_once): after his first pause the layer leaves the holders to Rare for the rest
    // of this alert: until he is calm (unaware, never spotted) again.
    if (s.ns_alert)
    {
      if (g.st == St::Unaware && !g.spotted)
      {
        s.ns_alert = false;
      }
      else
      {
        return false;
      }
    }
    if (now >= s.ns_quiet - 20.0f && now < s.ns_quiet)
    {
      return false;
    }
    if (now - s.ns_from > 10.0f || now < s.ns_from)
    {
      s.ns_from = now;
      s.ns_n = 0;
    }
    if (++s.ns_n > 3)
    {
      s.ns_quiet = now + 20.0f;
      s.ns_n = 0;
      s.ns_alert = REXCVAR_GET(pdz_stealth_no_steer_once);
      REXLOG_INFO("pdz_stealth_r7: NO STEER PAUSE: {} Rare refilled his target holders with Jo three times in {:.1f} s after the "
                  "layer emptied them (state {}, meter {:.0f}): the layer stops emptying them {} instead of fighting "
                  "Rare for them (one owner: Rare's; pdz_stealth_combat_owner) | {}",
                  Id(base, c), now - s.ns_from, StName(g.st), g.meter,
                  s.ns_alert ? "for the rest of this alert (until he is unaware again; pdz_stealth_no_steer_once)" : "for 20 s",
                  StackText(base, c));
      return false;
    }
    return true;
  }

  // From the squad's target copy hook (sub_8217C540): the layer itself confirmed his sighting of Jo in the last 3 s
  // (it is the layer that starts his COMBAT then, before Rare's own "valid target" bit is up): the place is his to
  // pass on, as Rare does at a COMBAT start.
  bool R7SawForSquad(const uint8_t *base, uint32_t who)
  {
    if (!REXCVAR_GET(pdz_stealth_combat_owner))
    {
      return false;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const Guard *g = Ptr(who) ? Find(who) : nullptr;
    const float now = Clock(base);
    return g && !g->dead && g->spotted && g->spotted_at > -1e8f && now >= g->spotted_at && now - g->spotted_at < 3.0f;
  }

  // Once a tick for every guard under the layer (After, before the layer's rules): his owner and his frame record.
  void R7Tick(const uint8_t *base, uint32_t c, const Guard &g, float now)
  {
    R7State &s = R7Of(c, g);
    const uint32_t sight = Load32(base, c + kSight);
    if (sight & 0x400000u)
    {
      s.hostile_at = now;
    }
    if (sight & kValidTarget)
    {
      s.valid_at = now;
    }
    const R7Owner owner = R7OwnerOf(base, c, g);
    if (owner != s.owner)
    {
      if (REXCVAR_GET(pdz_stealth_owner_log) && (owner != R7Owner::RareCalm || s.owner != R7Owner::RareCalm))
      {
        REXLOG_INFO("pdz_stealth_r7: OWNER: {} now belongs to {} (was: {}, for {:.1f} s) | layer state {}, meter {:.0f}, spotted "
                    "{} | Rare's sight word {:08X} | {}",
                    Id(base, c), R7OwnerName(owner), R7OwnerName(s.owner), s.owner_at > -1e8f ? now - s.owner_at : 0.0f,
                    StName(g.st), g.meter, g.spotted ? 1 : 0, sight, StackText(base, c));
      }
      s.owner = owner;
      s.owner_at = now;
    }
    R7Frame &f = s.ring[s.ring_n % 10];
    ++s.ring_n;
    f.at = now;
    f.yaw = LoadF(base, c + 0x1670);
    f.sight = sight;
    const int depth = StackDepth(base, c);
    const uint32_t task = depth > 0 ? Load32(base, c + kScheduler + kStackEntry * uint32_t(depth - 1)) : 0;
    f.top = Ptr(task) ? Load32(base, task) : 0;
    const uint32_t target = TargetOf(base, c);
    f.target = target == 0 ? 0 : target == Load32(base, kPlayerOne) ? 1 : 2;
    f.owner = uint8_t(owner);
    f.writes = uint16_t(s.w_n);
    f.vetoes = uint16_t(s.veto_total);
  }

  // For the FACING SWINGS and NO STEER lines: who owns him and who wrote what.
  std::string R7Who(const uint8_t *base, uint32_t c, const Guard &g, float now)
  {
    const auto it = g_r7.find(c);
    if (it == g_r7.end() || it->second.born != g.r2_born)
    {
      return "owner record: none yet";
    }
    const R7State &s = it->second;
    const auto age = [now](float at) {
      return at > -1e8f && now >= at ? fmt::format("{:.2f} s ago", now - at) : std::string("never");
    };
    std::string frames;
    const int n = std::min(s.ring_n, 10);
    for (int i = n - 1; i >= 0; --i)
    {
      const R7Frame &f = s.ring[(s.ring_n - 1 - i) % 10];
      frames += fmt::format("{}{:.1f}/{}/{:08X}/w{}/v{}", frames.empty() ? "" : " ", f.yaw, f.target == 0 ? "none" : f.target == 1 ? "Jo" : "other",
                            f.top, f.writes, f.vetoes);
    }
    return fmt::format(
        "OWNER: {} for {:.1f} s | Rare's own sight of Jo: \"sees a hostile\" last up {}, \"valid target\" last up {}; his sight "
        "tests of her in the last full second: {} | the layer's last write to him: {} | the layer's last write held back: {} "
        "| the last task queued for him: {} | his last {} frames, oldest first (yaw deg / target / top task class / the "
        "layer's writes so far / OUR SIGHT HOOK's vetoes of Rare's sight so far): {}",
        R7OwnerName(s.owner), s.owner_at > -1e8f ? now - s.owner_at : 0.0f, age(s.hostile_at), age(s.valid_at),
        s.tests_last < 0 ? std::string("none on record yet")
                         : fmt::format("{} asked, Rare's own test passed {} (last pass {}), OUR SIGHT HOOK VETOED {} of them (last veto "
                                       "{}{}; {} in all){}; that second ended {}",
                                       s.tests_last, s.passes_last, age(s.pass_at), s.vetoes_last, age(s.veto_at),
                                       s.veto_why ? fmt::format(": {}", s.veto_why) : std::string(), s.veto_total,
                                       s.dark_at > -1e8f && now - s.dark_at < 2.0f
                                           ? fmt::format("; the Laboratory darkness range inside Rare's test x{:.2f}", s.dark_scale)
                                           : std::string(),
                                       age(s.window_end)),
        s.w_n ? fmt::format("{} ({}) {}, {} in all", s.w_what, s.w_rule, age(s.w_at), s.w_n) : std::string("none"),
        s.h_n ? fmt::format("{} ({}) {}, {} in all", s.h_what, s.h_rule, age(s.h_at), s.h_n) : std::string("none"),
        s.q_n ? fmt::format("class {:08X} {} by {} (caller {:08X}), {} in all", s.q_class, age(s.q_at),
                            s.q_rule ? s.q_rule : "Rare (or a layer hook without a label)", s.q_caller, s.q_n)
              : std::string("none"),
        n, frames);
  }
