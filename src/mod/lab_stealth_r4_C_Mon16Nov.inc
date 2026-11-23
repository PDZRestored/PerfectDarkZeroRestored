  // Stealth layer round 4, Mon 16 Nov evening, helper C (fixes 6 and 7). Included by lab_stealth.cpp inside its
  // anonymous namespace, after lab_stealth_r3c_D_Sun15Nov.inc. Everything here runs only with the layer on
  // (pdz_lab_rareplus under the master pdz_lab_guard_stealth), for a guard under the layer. Log prefix "pdz_stealth_r4:".
  //
  // Fix 6, AN UNSEEN JO DOES NOT STEER A GUARD (pdz_stealth_no_steer_unseen). Hand test Mon 16 Nov 17:42-17:43, Subway
  // after Killian's hangar (arp2 / arp3 / arp4): a guard stood shaking; nothing in the log. Read as: Rare's side holds
  // Jo as his target (his target holders +0x3168 / +0x3150: Rare's turn / aim at her) while the layer says he has not
  // seen her (his meter below his sighting level), and he flips between the two. Here: while the layer says a calm,
  // wary or investigating guard has not seen Jo (not spotted by his meter, no valid-target bit of Rare's, no COMBAT),
  // a target holder of his that holds Jo for over kR4CTargetS is emptied through Rare's own holder setter (ClearTarget,
  // as lost contact does). What he has left to face is what the layer gave him: his look round / his search at the
  // pad of his last glimpse. Only a real sighting (82580A78) gives him a target again.
  // THE SWING LINE (always on while the layer is on): his body yaw (+0x1670) is watched with a few float compares a
  // tick; when it swings back and forth (reversals of over kR4CSwingDeg, each within kR4CSwingGapS of the last) for
  // over 1 s, one line says so, with Rare's state (task stack, config and sight words), Rare's target and our state.
  //
  // Fix 7, KILLIAN'S ESCAPE LINE COUNTS AS SPOTTING JO (pdz_stealth_killian_alert, Subway only). Rare's script flies
  // Killian out of his hangar as soon as Jo enters, with his line "Gonna be back for ya." (cue killian_arrival, bank
  // lv02_speech_vo, subtitle 170), whatever his guards know. Here, when that cue's name is looked up (sub_82202D20,
  // through lab_stealth::CueName; one bounded compare per cue while the level is the Subway and the line has not been
  // heard yet): every calm or wary guard of the layer within kR4CKillianCm of Jo's place at that moment is alerted and
  // searches: wary, his meter fills faster, and round 3's heard reaction is given the place Killian saw her at (Jo's
  // place when the line started, never her place afterwards): up to two walk there to look (our noise check, capped
  // by pdz_stealth_search_cap_s), the others stop and look round where they stand, each with a what-was-that line.
  // Nobody gets a target or a pad of hers. Rare's escape itself is untouched. A look-up in the level's first
  // kR4CKillianLoadS seconds is taken for a bank load, logged and ignored.

  constexpr float kR4CTargetS = 0.3f;       // s: Jo held as an unseen guard's target this long: the holder is emptied
  constexpr float kR4CSwingDeg = 6.0f;      // deg: a turn back of this much is a reversal
  constexpr float kR4CSwingGapS = 0.6f;     // s: reversals closer together than this are one swinging
  constexpr float kR4CSwingS = 1.0f;        // s: swinging this long: the line
  constexpr int kR4CSwingRevs = 4;          // ... with at least this many reversals
  constexpr float kR4CKillianCm = 4500.0f;  // guards within this of Jo's place at the line (ground plan)
  constexpr float kR4CKillianUpCm = 1200.0f; // ... and this far above / below it
  constexpr float kR4CKillianLoadS = 20.0f; // s after a level start: a look-up of the cue is a load, not the line

  struct R4CState
  {
    float born = -2e9f;      // his Guard's r2_born when this was made
    // Fix 6: the target.
    float tgt_since = -1e9f; // Jo has been his target since then while unseen (-1e9: not)
    float tgt_log = -1e9f;
    int tgt_clears = 0;
    // The swing line.
    float yaw_ext = 0.0f;    // the furthest yaw of his current turn
    int yaw_dir = 0;         // its direction (0: none yet)
    float rev_at = -1e9f;    // his last reversal
    float rev_from = -1e9f;  // the first reversal of this swinging
    int revs = 0;
    float rev_span = 0.0f;   // the widest turn between two reversals (deg)
    float swing_log = -1e9f;
    // Fix 7.
    float killian = -1e9f;   // the line he was last checked for
  };
  std::unordered_map<uint32_t, R4CState> g_r4c;
  std::atomic<bool> g_r4c_killian_armed{false}; // the cue is looked for (the Subway, the layer on, not heard yet)
  std::atomic<bool> g_r4c_killian_cue{false};   // ... and its name has just been looked up
  bool g_r4c_killian_done = false;              // the line was heard on this level start
  float g_r4c_killian_at = -1e9f;               // ... then
  float g_r4c_killian_pos[3] = {};              // ... Jo's place at that moment
  int g_r4c_killian_n = 0;                      // guards alerted by it
  float g_r4c_level_at = -1e9f;                 // the first tick after a level start (the AI clock)

  // R3cClearLayerMemory calls this at every level start / restart / reload.
  void R4CReset()
  {
    g_r4c.clear();
    g_r4c_killian_armed.store(false, std::memory_order_relaxed);
    g_r4c_killian_cue.store(false, std::memory_order_relaxed);
    g_r4c_killian_done = false;
    g_r4c_killian_at = -1e9f;
    g_r4c_killian_n = 0;
    g_r4c_level_at = -1e9f;
    R5CReset(); // (round 5, fix 5: the cameras shot down)
    R7Reset();  // (Tue 17 Nov: the owner and write record)
    R8Reset();  // (Tue 17 Nov night, fix2: the blind-search record)
    R9Reset();  // (Wed 18 Nov night: our plain walks on record)
  }

  // From lab_stealth::CueName (sub_82202D20, every cue name looked up: about 2,900 a second): no lock, no log, no
  // string object; one bounded scan while armed.
  void R4CCue(const uint8_t *base, uint32_t name)
  {
    if (!g_r4c_killian_armed.load(std::memory_order_relaxed) || name < 0x10000u || name >= 0x90000000u)
    {
      return;
    }
    static const char kWant[] = "KILLIAN_ARRIVAL";
    const char *text = reinterpret_cast<const char *>(base + name);
    for (int i = 0; i < 96 && text[i]; ++i)
    {
      if ((text[i] | 0x20) != 'k')
      {
        continue;
      }
      int k = 0;
      while (kWant[k] && text[i + k] && (text[i + k] & ~0x20) == (kWant[k] & ~0x20))
      {
        ++k;
      }
      if (!kWant[k])
      {
        g_r4c_killian_cue.store(true, std::memory_order_relaxed);
        return;
      }
    }
  }

  // Once a tick for every guard under the layer (RarePlus), before round 3's heard reaction (R3HTick).
  void R4CTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
  {
    if (!g.r9_rares) // (Wed 18 Nov night: for Rare's guard only the swing line below runs)
    {
      R5CTick(ctx, base, c, g, now, in_combat, in_search); // (round 5, fix 5: a security camera shot down is a noise)
    }
    if (g_r4c.size() > 512)
    {
      g_r4c.clear();
    }
    R4CState &s = g_r4c[c];
    if (s.born != g.r2_born)
    {
      s = R4CState();
      s.born = g.r2_born;
    }
    if (g_r4c_level_at < -1e8f || now < g_r4c_level_at)
    {
      g_r4c_level_at = now;
    }
    const uint32_t jo = Load32(base, kPlayerOne);
    const bool alive = !g.dead && g.st != St::Dead;
    const uint32_t sight = Load32(base, c + kSight);

    // --- Fix 7: Killian's line ---------------------------------------------------------------------------------------
    const bool killian_on = g_level && g_level->sw == LevelSwitch::kSubway && REXCVAR_GET(pdz_stealth_killian_alert);
    const bool want = killian_on && !g_r4c_killian_done;
    if (g_r4c_killian_armed.load(std::memory_order_relaxed) != want)
    {
      g_r4c_killian_armed.store(want, std::memory_order_relaxed);
    }
    if (g_r4c_killian_cue.load(std::memory_order_relaxed) && g_r4c_killian_cue.exchange(false))
    {
      if (!want || !Ptr(jo))
      {
        // (switched off meanwhile, or no player: nothing)
      }
      else if (now - g_r4c_level_at < kR4CKillianLoadS)
      {
        REXLOG_INFO("pdz_stealth_r4: KILLIAN ALERT: the cue killian_arrival was looked up {:.1f} s after the level start: taken "
                    "for a bank load, not his line: ignored",
                    now - g_r4c_level_at);
      }
      else
      {
        g_r4c_killian_done = true;
        g_r4c_killian_armed.store(false, std::memory_order_relaxed);
        g_r4c_killian_at = now;
        g_r4c_killian_n = 0;
        PosOf(base, jo, g_r4c_killian_pos);
        REXLOG_INFO("pdz_stealth_r4: KILLIAN ALERT: Killian's escape line (cue killian_arrival) starts: it counts as spotting Jo: "
                    "the layer's calm guards within {:.0f} m of the place he saw her at ({:.0f} {:.0f} {:.0f}) are alerted and "
                    "search (that place, never her place afterwards); his escape is Rare's, unchanged",
                    kR4CKillianCm / 100.0f, g_r4c_killian_pos[0], g_r4c_killian_pos[1], g_r4c_killian_pos[2]);
      }
    }
    if (g_r4c_killian_at > -1e8f && s.killian != g_r4c_killian_at && now >= g_r4c_killian_at && now - g_r4c_killian_at < 3.0f)
    {
      s.killian = g_r4c_killian_at;
      float me[3];
      PosOf(base, c, me);
      const float dx = me[0] - g_r4c_killian_pos[0], dz = me[2] - g_r4c_killian_pos[2];
      const float d = std::sqrt(dx * dx + dz * dz);
      if (killian_on && alive && LayerGuard(g) && !g.scientist && !g.run_committed && !in_combat && !g.r9_rares &&
          (g.st == St::Unaware || g.st == St::Wary) && d <= kR4CKillianCm &&
          std::fabs(me[1] - g_r4c_killian_pos[1]) <= kR4CKillianUpCm)
      {
        ++g_r4c_killian_n;
        FillFaster(base, c, g, now, "Killian's escape line: an intruder is in the hangar (pdz_stealth_killian_alert)");
        if (g.st == St::Unaware)
        {
          Set(base, c, g, St::Wary, now, "Killian's escape line: wary (pdz_stealth_killian_alert)");
        }
        // Round 3's heard reaction (R3HTick, this tick or the next 0.5 s): a loud sound at the place Killian saw her at:
        // two walk there to look, the others look round where they stand; Rare's own reaction, if one comes, wins.
        R3HState &h = R3HOf(c, g);
        h.wait.at = now;
        h.wait.kind = kR3HBlast;
        h.wait.source = 0xC1114A00u;
        std::memcpy(h.wait.pos, g_r4c_killian_pos, sizeof(h.wait.pos));
        h.wait.d = std::max(d, 101.0f);
        h.wait.walls = -1;
        REXLOG_INFO("pdz_stealth_r4: KILLIAN ALERT: {} (guard {} of this alert, {:.0f} cm from the place Killian saw her at, state "
                    "{}, meter {:.0f}): alerted: wary, his meter fills faster, and he searches (round 3's heard reaction: a walk "
                    "to that place for the first two within 30 m, a look round where he stands for the others) | {}",
                    Id(base, c), g_r4c_killian_n, d, StName(g.st), g.meter, StackText(base, c));
      }
    }

    // --- Fix 6: an unseen Jo is not his target -----------------------------------------------------------------------
    // (Round 5, Mon 16 Nov, fix 3: scientists too and every layer state. "Unseen" is now a matter of time: no valid
    // target of Rare's, no sight test passed and no hit of hers in the last 3 s, whatever his meter once said: the Lab's
    // s2s1 had seen her 7 s before, ran for the alarm and was turned to her 26 m away by Rare's target.)
    const bool seen_now = (now >= g.last_visible && now - g.last_visible < 3.0f) || R2Seen(g, now, 3.0f);
    const bool unseen = alive && (g.scientist || LayerGuard(g)) && !in_combat && !g.run_committed && !g.r9_rares &&
                        (sight & kValidTarget) == 0 && !seen_now; // (LayerGuard is false for every scientist)
    const uint32_t target = TargetOf(base, c);
    // (After the first smoke run: Rare holds Jo as the target of every calm guard from the level's first second; that is
    // his normal state and steers nobody. For a calm Unaware guard the holder is emptied only when it matters: his
    // facing is swinging (two reversals close together), or his meter once spotted her (s2s1 on his alarm run).)
    const bool swinging = s.revs >= 2 && now >= s.rev_at && now - s.rev_at <= kR4CSwingGapS;
    const bool matters = g.st != St::Unaware || g.spotted || swinging;
    if (unseen && matters && Ptr(jo) && target == jo && REXCVAR_GET(pdz_stealth_no_steer_unseen))
    {
      if (s.tgt_since < -1e8f || now < s.tgt_since)
      {
        s.tgt_since = now;
      }
      else if (now - s.tgt_since >= kR4CTargetS && !R7NoSteerMay(base, c, g, now))
      {
        // (Tue 17 Nov: Rare refills the holders at once; after three times in 10 s the layer leaves them to Rare
        // for 20 s: one owner, no tug of war. The pause's line is R7NoSteerMay's.)
        s.tgt_since = -1e9f;
      }
      else if (now - s.tgt_since >= kR4CTargetS)
      {
        ClearTarget(ctx, base, c);
        ++s.tgt_clears;
        if (now - s.tgt_log >= 5.0f || now < s.tgt_log)
        {
          s.tgt_log = now;
          REXLOG_INFO("pdz_stealth_r4: NO STEER: {} Rare held Jo as his target for {:.1f} s while the layer says he has not seen "
                      "her (state {}, meter {:.0f}, Jo {:.0f} cm away, sight word {:08X}): his target holders are emptied "
                      "(time {} for him): he faces what the layer gave him (his look round, his last glimpse) | {} | {}",
                      Id(base, c), now - s.tgt_since, StName(g.st), g.meter, Dist(base, c, jo), sight, s.tgt_clears,
                      StackText(base, c), R7Who(base, c, g, now));
        }
        s.tgt_since = -1e9f;
      }
    }
    else
    {
      s.tgt_since = -1e9f;
    }

    // --- The swing line ----------------------------------------------------------------------------------------------
    if (!alive)
    {
      return;
    }
    const float yaw = LoadF(base, c + 0x1670); // his body's yaw, degrees (round 11l)
    if (!(yaw > -100000.0f && yaw < 100000.0f))
    {
      return;
    }
    float turn = yaw - s.yaw_ext;
    turn = turn > 180.0f ? turn - 360.0f : turn < -180.0f ? turn + 360.0f : turn;
    if (s.yaw_dir == 0)
    {
      if (turn > kR4CSwingDeg || turn < -kR4CSwingDeg)
      {
        s.yaw_dir = turn > 0.0f ? 1 : -1;
        s.yaw_ext = yaw;
      }
      else if (s.rev_at < -1e8f && s.yaw_ext == 0.0f)
      {
        s.yaw_ext = yaw; // (his first tick)
      }
      return;
    }
    if (turn * float(s.yaw_dir) >= 0.0f)
    {
      s.yaw_ext = yaw; // (he turns on the same way)
      return;
    }
    if (turn * float(s.yaw_dir) > -kR4CSwingDeg)
    {
      return;
    }
    // A reversal.
    const float span = turn < 0.0f ? -turn : turn;
    s.yaw_dir = -s.yaw_dir;
    s.yaw_ext = yaw;
    if (now >= s.rev_at && now - s.rev_at <= kR4CSwingGapS)
    {
      ++s.revs;
      s.rev_span = std::max(s.rev_span, span);
    }
    else
    {
      s.revs = 1;
      s.rev_from = now;
      s.rev_span = span;
    }
    s.rev_at = now;
    if (s.revs >= kR4CSwingRevs && now - s.rev_from >= kR4CSwingS && (now - s.swing_log >= 5.0f || now < s.swing_log))
    {
      s.swing_log = now;
      float me[3] = {}, her[3] = {};
      PosOf(base, c, me);
      if (Ptr(jo))
      {
        PosOf(base, jo, her);
      }
      const float to_jo = Ptr(jo) ? std::atan2(her[0] - me[0], her[2] - me[2]) * 57.29578f : 0.0f;
      const float to_glimpse = std::atan2(g.glimpse_pos[0] - me[0], g.glimpse_pos[2] - me[2]) * 57.29578f;
      REXLOG_INFO("pdz_stealth_r4: FACING SWINGS: {} at ({:.0f} {:.0f} {:.0f}) has swung back and forth for {:.1f} s ({} reversals, "
                  "up to {:.0f} deg; yaw now {:.0f} deg; the way to Jo {:.0f} deg, {:.0f} cm; the way to his last glimpse {:.0f} "
                  "deg, {:.1f} s old) | Rare: target {} ({:08X}), sight word {:08X}, config {:08X}, COMBAT {}, {} | ours: state "
                  "{}, meter {:.0f}, spotted {}, unseen by the layer {}, targets emptied {} time(s) | {}",
                  Id(base, c), me[0], me[1], me[2], now - s.rev_from, s.revs, s.rev_span, yaw, to_jo,
                  Ptr(jo) ? Dist3(me, her) : -1.0f, to_glimpse, now - g.glimpse_time,
                  target == 0 ? "none" : target == jo ? "Jo" : "another character", target, sight, Load32(base, c + kConfig),
                  in_combat ? 1 : 0, StackText(base, c), StName(g.st), g.meter, g.spotted ? 1 : 0, unseen ? 1 : 0, s.tgt_clears,
                  R7Who(base, c, g, now));
    }
  }
