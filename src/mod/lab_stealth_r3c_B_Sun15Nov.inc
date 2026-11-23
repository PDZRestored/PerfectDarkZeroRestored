  // Round 3c, Sun 15 Nov night, helper B (fixes 2 and 3): only lights that really go out count; Jo's shots.
  //
  // Fix 2. Rare's light-surface routine sub_825F3978 picks the best-scoring scene light for ANY hit on a surface with
  // the light material (sub_825F3CD8: the best 1 - d^2 / R^2 over every type 0 / 2 light with the shoot-out flag,
  // however far the hit is from the fitting) and asks sub_82602648(light, off, fade) for it. Until this round the lamp
  // lure took "Rare's pick was on" as "a lamp went out" (hand test log 005, 20:44:34 and 20:49:23: a hit on glass on
  // the Mansion grounds picked the candle at (2697 -828 7636), and guards walked to a lit candle). Now:
  //   - the hook (lab_light_shot_log.cpp) reads the picked light's real state after Rare's call: still on = nothing;
  //   - a light that is not on after the call is WATCHED here: it counts as out (lab_stealth::LightIncident: the lamp
  //     lure, the lamp lines, the lamp investigation, the dead-lamp memory) only once its real state is off (0), or
  //     fading out (3) with less than half its brightness (+0x598) left, or still fading out after
  //     pdz_stealth_lamp_check_s; back on (1 / 2): it did not go out, nothing happens;
  //   - flame lights (the Mansion's candles and lanterns: the orange lights that sit on Rare's flame emitters) are
  //     never counted (pdz_stealth_candles_never_out); what their light really did is logged 2 s later.
  // The light meter reads Rare's real light list (lab_dark_patches.cpp Sample), so it never took a lamp for out that
  // was not; the wrong records were the layer's own (g_lamps_out, g_dead_lamps), which now only real ones reach.
  //
  // Fix 3. Jo's shots: Rare's noise writer for a hand-held gun (sub_825882E8, hook in lab_stealth.cpp) already takes
  // only the weapon's owner; its log line was written at most once in 5 s per shooter, so shots within 5 s of a logged
  // one left no line (log 005: tow3#2 hurt at 20:48:26.557 and killed at 20:48:29.740, both by the hit handler with
  // Jo as the attacker, within 5 s of the logged shot at 20:48:25.198). Now every shot of Jo's is counted and logged
  // (number, mode, the calling fire routine), a hit of hers says how many shots of hers came in the 1.5 s before it
  // (none = damage Rare credits to her without a shot), and a light-surface hit says how long ago her last shot was.
  struct R3cBWatch
  {
    uint32_t light = 0;
    float pos[3] = {0.0f, 0.0f, 0.0f};
    float at = 0.0f;
    int lamps = 1;
    bool flame = false; // never counted: only its real state is logged
    std::string how;
  };
  std::vector<R3cBWatch> g_r3cb_watch;
  float g_r3cb_shots[16] = {-1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f,
                            -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f, -1e9f}; // Clock() of Jo's last shots (a ring)
  uint32_t g_r3cb_shot_n = 0;   // Jo's shots in this run of the level
  std::string g_r3cb_test_done; // pdz_test_stealth_light_shot

  constexpr uint32_t kR3cBLightArray = 0x82E19AE0, kR3cBLightCount = 0x82E3C5F4, kR3cBStateIndex = 0x82E1BA20;

  void R3cBReset()
  {
    g_r3cb_watch.clear();
    for (float &t : g_r3cb_shots)
    {
      t = -1e9f;
    }
    g_r3cb_shot_n = 0;
  }

  uint32_t R3cBState(const uint8_t *base, uint32_t light)
  {
    return Load32(base, light + 0x590 + 4 * (Load32(base, kR3cBStateIndex) & 1));
  }
  float R3cBBright(const uint8_t *base, uint32_t light)
  {
    const float b = LoadF(base, light + 0x598);
    return std::isfinite(b) ? b : 0.0f;
  }
  // The light is still in Rare's light list (a restart or a reload frees the lights).
  bool R3cBListed(const uint8_t *base, uint32_t light)
  {
    const uint32_t count = std::min(Load32(base, kR3cBLightCount), 1024u);
    for (uint32_t i = 0; i < count; ++i)
    {
      if (Load32(base, kR3cBLightArray + 4 * i) == light)
      {
        return true;
      }
    }
    return false;
  }
  // A flame light: the Mansion's candles and lanterns (colour (2.0, 1.15, 0.3) at +0x2C0, on Rare's
  // aid_xml_emitter_level_4fire1 emitters: survey m 5.1). Only on the row with the radio (the Mansion grounds): the
  // other levels' lamps are judged by their real state alone.
  bool R3cBFlame(const uint8_t *base, uint32_t light)
  {
    if (!REXCVAR_GET(pdz_stealth_candles_never_out) || !RadioRow())
    {
      return false;
    }
    // Lead, Sun 15 Nov night, after two runs: the light Rare picks at the candle (2697 -828 7636) reads type 2, colour
    // (0.45 0.75 0.74) in the running game (not the data's flame colour), Rare's routine does take it to state 0, and
    // the candle's glow on the wall is unchanged in the picture (native and emulated captures before / after:
    // r3c\candle_before_after_native_top_emulated_bottom_Sun15Nov.png). So on the Mansion grounds no scene light that
    // Rare's shot path "puts out" shows as out: none of them counts, whatever its colour (the grounds have no lamp props).
    (void)base;
    (void)light;
    return true;
  }
  int R3cBShotsWithin(float now, float secs)
  {
    int n = 0;
    for (const float t : g_r3cb_shots)
    {
      n += (now >= t && now - t <= secs) ? 1 : 0;
    }
    return n;
  }
  float R3cBLastShotAgo(float now)
  {
    float best = 1e9f;
    for (const float t : g_r3cb_shots)
    {
      if (now >= t)
      {
        best = std::min(best, now - t);
      }
    }
    return best;
  }

  // Fix 3: a shot of Jo's (Rare's noise writer, the weapon's owner is player one). Under g_lock.
  void R3cBJoShot(const uint8_t *base, uint32_t weapon, uint32_t mode, uint32_t caller, bool silenced, float now)
  {
    g_r3cb_shots[g_r3cb_shot_n % 16] = now;
    ++g_r3cb_shot_n;
    if (Logging() || StealthLog())
    {
      const uint32_t jo = Load32(base, kPlayerOne);
      REXLOG_INFO("pdz_stealth_r3c: JO SHOT #{}: {} (mode {}), weapon {:08X}, Rare's fire routine returns to {:08X}, Jo at "
                  "({:.0f} {:.0f} {:.0f}); {} shot(s) of hers in the last 5 s",
                  g_r3cb_shot_n, silenced ? "silenced" : "unsilenced", mode, weapon, caller, LoadF(base, jo + kPosition),
                  LoadF(base, jo + kPosition + 4), LoadF(base, jo + kPosition + 8), R3cBShotsWithin(now, 5.0f));
    }
  }

  // Fix 3: a hit the hit handler gives Jo as its attacker took health off a tracked guard. Under g_lock.
  void R3cBJoHit(const uint8_t *base, uint32_t victim, float before, float after, float now)
  {
    if (!Logging() && !StealthLog())
    {
      return;
    }
    const int n = R3cBShotsWithin(now, 1.5f);
    const float ago = R3cBLastShotAgo(now);
    REXLOG_INFO("pdz_stealth_r3c: JO HIT: {} health {:.0f} -> {:.0f}, Rare's hit handler names Jo as the attacker; {}",
                Id(base, victim), before, after,
                n > 0 ? fmt::format("{} shot(s) of hers in the 1.5 s before it (the last {:.2f} s ago)", n, ago)
                      : fmt::format("NO SHOT OF HERS in the 1.5 s before it ({}): damage Rare credits to her without a "
                                    "hand-held shot (a thrown or dropped object, a body, a blast, a turret)",
                                    ago < 1e8f ? fmt::format("her last was {:.1f} s ago", ago) : std::string("none in this run")));
  }

  // Fix 2: Rare's light-surface routine has run for a hit at `hit`; `light` is the light it picked (or the prop's
  // linked light), `before` / `after` its real state around Rare's call. Called by lab_stealth::LampShot (the shot
  // hook, game thread) under g_lock. True: the light is on its way out and is watched from here.
  bool R3cBLampShot(const uint8_t *base, uint32_t light, const float hit[3], uint32_t prop, uint32_t caller, uint32_t before,
                    uint32_t after, int lamps)
  {
    const float now = Clock(base);
    float pos[3] = {hit[0], hit[1], hit[2]};
    for (int k = 0; k < 3; ++k)
    {
      const float v = LoadF(base, light + 0x2D0 + 4 * k);
      pos[k] = std::isfinite(v) ? v : pos[k];
    }
    const bool flame = R3cBFlame(base, light);
    const bool was_on = before == 1 || before == 2, now_on = after == 1 || after == 2;
    const float ago = R3cBLastShotAgo(now);
    const std::string how =
        fmt::format("a hit on a light surface at ({:.0f} {:.0f} {:.0f}), {:.0f} cm from the light{}, routine called from "
                    "{:08X}; Jo's last shot {}",
                    hit[0], hit[1], hit[2], Dist3(hit, pos), prop ? fmt::format(", prop {:08X}", prop) : std::string(), caller,
                    ago < 1e8f ? fmt::format("{:.2f} s ago", ago) : std::string("none in this run (not a shot of hers)"));
    if (StealthLog() || Logging())
    {
      REXLOG_INFO("pdz_stealth_r3c: LIGHT SHOT: {}: Rare's pick is the light {:08X} at ({:.0f} {:.0f} {:.0f}) (type {}, "
                  "colour {:.2f} {:.2f} {:.2f}{}), state {} -> {} (1 / 2 on, 3 fading out, 0 off), brightness {:.2f}: {}",
                  how, light, pos[0], pos[1], pos[2], Load32(base, light + 0x2A8), LoadF(base, light + 0x2C0),
                  LoadF(base, light + 0x2C4), LoadF(base, light + 0x2C8), flame ? ", a flame light" : "", before, after,
                  R3cBBright(base, light),
                  !was_on ? "it was not on: nothing went out"
                  : now_on ? "Rare left it ON: nothing went out, NOT counted"
                  : flame  ? "a candle: Rare's game never shows a candle out: NOT counted (pdz_stealth_candles_never_out); its "
                             "real state is logged in 2 s"
                           : "watched: it counts once it is really off or under half its brightness");
    }
    if (!was_on || now_on)
    {
      return false;
    }
    if (g_r3cb_watch.size() >= 16)
    {
      g_r3cb_watch.erase(g_r3cb_watch.begin());
    }
    R3cBWatch w;
    w.light = light;
    std::memcpy(w.pos, pos, sizeof(w.pos));
    w.at = now;
    w.lamps = lamps;
    w.flame = flame;
    w.how = how;
    g_r3cb_watch.push_back(w);
    return !flame;
  }

  // Fix 2: the watched lights, every frame (game thread, under g_lock). A light that is really out is the incident.
  void R3cBLampTick(PPCContext &ctx, uint8_t *base, float now)
  {
    if (const std::string want = REXCVAR_GET(pdz_test_stealth_light_shot); want != g_r3cb_test_done)
    {
      // Testing tool: Rare's own light-surface routine for a hit at the scene light nearest to "x,y,z" (the hit point
      // is that light's own position, so Rare's pick is that light).
      g_r3cb_test_done = want;
      float p[3] = {};
      if (std::sscanf(want.c_str(), "%f,%f,%f", &p[0], &p[1], &p[2]) == 3)
      {
        const uint32_t count = std::min(Load32(base, kR3cBLightCount), 1024u);
        uint32_t best = 0;
        float best_d = 1e9f;
        for (uint32_t i = 0; i < count; ++i)
        {
          const uint32_t a = Load32(base, kR3cBLightArray + 4 * i);
          const uint32_t type = Ptr(a) ? Load32(base, a + 0x2A8) : 1;
          if (!Ptr(a) || (type != 0 && type != 2) || Load32(base, a + 0x338) != 0)
          {
            continue;
          }
          const float q[3] = {LoadF(base, a + 0x2D0), LoadF(base, a + 0x2D4), LoadF(base, a + 0x2D8)};
          const float d = Dist3(p, q);
          if (d < best_d)
          {
            best_d = d;
            best = a;
          }
        }
        if (best)
        {
          REXLOG_INFO("pdz_stealth_r3c: test: a light-surface hit at the scene light nearest to ({:.0f} {:.0f} {:.0f}): light "
                      "{:08X} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm from the point, shoot-out flag {}, state {}: Rare's own "
                      "routine 825F3978 runs for it",
                      p[0], p[1], p[2], best, LoadF(base, best + 0x2D0), LoadF(base, best + 0x2D4), LoadF(base, best + 0x2D8), best_d,
                      Load32(base, best + 0x32C) == 0 ? "set" : "not set", R3cBState(base, best));
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = best + 0x2D0;
          frame.ctx.r4.u64 = 0;
          sub_825F3978(frame.ctx, base);
        }
        else
        {
          REXLOG_INFO("pdz_stealth_r3c: test: no scene light found for pdz_test_stealth_light_shot '{}'", want);
        }
      }
    }
    if (g_r3cb_watch.empty())
    {
      return;
    }
    const float limit = float(REXCVAR_GET(pdz_stealth_lamp_check_s));
    for (size_t i = 0; i < g_r3cb_watch.size();)
    {
      const R3cBWatch w = g_r3cb_watch[i];
      const float age = now - w.at;
      if (age < 0.0f || !R3cBListed(base, w.light))
      {
        g_r3cb_watch.erase(g_r3cb_watch.begin() + i); // (the level's lights are gone: a restart)
        continue;
      }
      const uint32_t state = R3cBState(base, w.light);
      const float bright = R3cBBright(base, w.light);
      const bool on = state == 1 || state == 2;
      const bool out = state == 0 || (state == 3 && bright < 0.5f);
      if (w.flame)
      {
        if (age < 2.0f)
        {
          ++i;
          continue;
        }
        if (StealthLog() || Logging())
        {
          REXLOG_INFO("pdz_stealth_r3c: LIGHT SHOT: the candle at ({:.0f} {:.0f} {:.0f}) 2 s after the hit: state {}, brightness "
                      "{:.2f}: {}; never counted (no lamp lure, no lamp line, no lamp investigation)",
                      w.pos[0], w.pos[1], w.pos[2], state, bright,
                      on ? "Rare has it ON again" : out ? "Rare's light itself is off (the flame stays on screen)" : "still fading");
        }
        g_r3cb_watch.erase(g_r3cb_watch.begin() + i);
        continue;
      }
      if (on)
      {
        if (StealthLog() || Logging())
        {
          REXLOG_INFO("pdz_stealth_r3c: LIGHT DID NOT GO OUT at ({:.0f} {:.0f} {:.0f}): {:.2f} s after the hit Rare has it on again "
                      "(state {}, brightness {:.2f}): NOT counted ({})",
                      w.pos[0], w.pos[1], w.pos[2], age, state, bright, w.how);
        }
        g_r3cb_watch.erase(g_r3cb_watch.begin() + i);
        continue;
      }
      if (out || age > limit)
      {
        // (Still fading out at the time limit: Rare is putting it out and has not relit it: it counts.)
        g_r3cb_watch.erase(g_r3cb_watch.begin() + i);
        if (StealthLog() || Logging())
        {
          REXLOG_INFO("pdz_stealth_r3c: LIGHT REALLY OUT at ({:.0f} {:.0f} {:.0f}) {:.2f} s after the hit (state {}, brightness "
                      "{:.2f}{}): it counts: the lamp lure / lamp lines / lamp investigation for those who notice it",
                      w.pos[0], w.pos[1], w.pos[2], age, state, bright, out ? "" : "; still fading at pdz_stealth_lamp_check_s");
        }
        g_r10_lamp_age = age; // (Thu 19 Nov: how long after the glass broke; hearing counts only while it is fresh)
        lab_stealth::LightIncident(ctx, base, w.pos, w.lamps);
        g_r10_lamp_age = 0.0f;
        continue; // (the incident never touches this list)
      }
      ++i;
    }
  }
