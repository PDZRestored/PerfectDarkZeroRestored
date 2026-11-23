  // Stealth layer, Tue 17 Nov night (job fix2), part A. Included by lab_stealth.cpp inside its anonymous namespace,
  // right after lab_stealth_r7_Tue17Nov.inc. Log prefix "pdz_stealth_r8:".
  //
  // ITEM 1, ONE TRUTH FOR "HAS HE SEEN JO" (pdz_stealth_one_sight_truth). Rare's sight test of Jo (sub_82189F18) passes
  // through our hook, and Rare's concealment (sub_825A93C0, called inside that test) through another. Once the layer's
  // meter has confirmed a guard's sighting (spotted), or she hurt him, or the layer's own state for him is combat (and
  // not the "lost her" kind it meters again), the layer holds him as having seen her. For such a guard the hook now
  // returns Rare's own answer with nothing of ours in it: no Laboratory sight block, and no darkness range inside Rare's
  // test (the concealment hook leaves Rare's value alone for him). Rare's own range, cone, crouch and walls stay. When
  // the layer lets him lose her (spotted cleared, the lost state), the meter and its rules apply again. The committed
  // alarm runner's rule (round 11h) is left as it is: it is not a fight. Counter-Op has no hook of its own here.
  //
  // What the code shows for log 018 (Nightclub, s19a 21:03:56): on the Nightclub the darkness range is not applied at
  // all (lab_dark::On() is the Laboratory's script tick only) and the sight blocks are Laboratory coordinates (off on
  // zone levels), and from the frame his meter confirmed the sighting the hook handed back Rare's own answer (he was no
  // longer metered). So in that fight our hook did not veto Rare's sight. The "60 asked, 0 passed" in the earlier
  // lines was s06a's, and frozen: the record only counted tests while a guard was not metered and never rolled its
  // second over once he was metered again (the same text repeated for 7 minutes). The record below counts every test.
  //
  // ITEM 5, THE RECORD. Every test of Jo by a tracked guard: Rare's own answer, whether our hook then hid her and by
  // which rule, and the darkness range scale inside Rare's test. FACING SWINGS / NO STEER / OWNER lines carry it
  // (R7Who), and each of his last 10 frames shows how many vetoes our hook had made by then ("/v<n>").

  thread_local bool t_r8_raw_set = false;
  thread_local bool t_r8_raw = false;
  thread_local const char *t_r8_veto = nullptr;
  thread_local bool t_r8_holds = false;  // inside Rare's sight test for a guard the layer holds as having seen Jo
  thread_local float t_r8_scale = 1.0f;  // the darkness range scale the concealment hook applied in this test

  // The layer holds him as having seen Jo (under the lock).
  bool R8Holds(const Guard &g)
  {
    return REXCVAR_GET(pdz_stealth_one_sight_truth) && !g.dead && g.st != St::Dead &&
           (g.spotted || HurtKnown(g) || (g.st == St::Combat && !LostLike(g)));
  }

  // Rare's own sight test, its answer noted for the record.
  void R8RareSight(PPCContext &ctx, uint8_t *base)
  {
    __imp__sub_82189F18(ctx, base);
    t_r8_raw_set = true;
    t_r8_raw = (ctx.r3.u32 & 0xFF) != 0;
  }
  // Our hook hides Jo from Rare now (only counted when Rare's own answer was "visible").
  void R8Veto(const char *why)
  {
    t_r8_veto = why;
  }

  // One per sight hook call: on the way out, the test goes into the record (a tracked guard's test of Jo).
  struct R8SightScope
  {
    PPCContext &ctx;
    const uint8_t *base;
    uint32_t viewer = 0;
    bool jo = false;
    R8SightScope(PPCContext &c, const uint8_t *b) : ctx(c), base(b)
    {
      t_r8_raw_set = false;
      t_r8_raw = false;
      t_r8_veto = nullptr;
      t_r8_holds = false;
      t_r8_scale = 1.0f;
    }
    ~R8SightScope()
    {
      if (jo && t_r8_raw_set && Ptr(viewer))
      {
        const bool final_answer = (ctx.r3.u32 & 0xFF) != 0;
        const bool vetoed = t_r8_raw && !final_answer;
        std::lock_guard<std::recursive_mutex> hold(g_lock);
        R7SightTest(base, viewer, t_r8_raw, vetoed, vetoed ? (t_r8_veto ? t_r8_veto : "a rule without a label") : nullptr,
                    t_r8_scale);
      }
      t_r8_holds = false;
    }
  };

  // From the concealment hook: may the darkness range be applied in this test? (Not for a guard who has seen her.)
  bool R8DarkRangeAllowed()
  {
    return !t_r8_holds;
  }
  void R8DarkRangeUsed(float scale)
  {
    t_r8_scale = scale;
  }

  // ITEM 2 (pdz_stealth_blind_search_clean), the stimulus side. Log 018, s06a 20:39:03: Rare started his COMBAT on
  // "gunfire with a target" with no sighting; round 2's blind-combat rule DID catch it (BLIND COMBAT line, his COMBAT
  // ended within 0.1 s), but (a) he knew of no place, so the search was "where he stands": he never moved; (b) Rare's
  // SEARCH restarted 3 times in 5 s from the next shots (SEARCH RESTARTS warning), and the swing came in those
  // restarts. Here: Rare's last word per guard (for the place of the shot), and the hold of the restart bits while the
  // search that replaced his blind COMBAT runs.
  struct R8Blind
  {
    float born = -2e9f;
    uint32_t word = 0;
    float word_at = -1e9f;
    float hold_from = -1e9f, hold_until = -1e9f;
    int held = 0;
    float held_log = -1e9f;
  };
  std::unordered_map<uint32_t, R8Blind> g_r8_blind;
  R8Blind &R8BlindOf(uint32_t c, const Guard &g)
  {
    if (g_r8_blind.size() > 512)
    {
      g_r8_blind.clear();
    }
    R8Blind &b = g_r8_blind[c];
    if (b.born != g.r2_born)
    {
      b = R8Blind();
      b.born = g.r2_born;
    }
    return b;
  }
  void R8Reset()
  {
    g_r8_blind.clear();
  }
  // From the stimulus hook (every word of his): Rare's last word.
  void R8StimSeen(uint32_t c, const Guard &g, uint32_t word, float now)
  {
    R8Blind &b = R8BlindOf(c, g);
    b.word = word;
    b.word_at = now;
  }
  // From R2BlindCombat: the place of the shot that started it, when nothing else is known. True: `pos` is Jo's spot
  // now (she fired a moment ago: Rare's "gunfire with a target" 0400 or a near miss 0080 in this frame).
  bool R8ShotPlace(const uint8_t *base, uint32_t c, const Guard &g, float now, float pos[3])
  {
    if (!REXCVAR_GET(pdz_stealth_blind_search_clean))
    {
      return false;
    }
    const auto it = g_r8_blind.find(c);
    if (it == g_r8_blind.end() || it->second.born != g.r2_born || !(it->second.word & 0x480u) || now < it->second.word_at ||
        now - it->second.word_at > 0.5f)
    {
      return false;
    }
    const uint32_t jo = Load32(base, kPlayerOne);
    if (!Ptr(jo))
    {
      return false;
    }
    for (int i = 0; i < 3; ++i)
    {
      pos[i] = LoadF(base, jo + kPosition + 4 * i);
    }
    return true;
  }
  // From R2BlindCombat when it turns his COMBAT into a search: the restart hold begins (the search cap, 25 s at most).
  void R8BlindBegins(uint32_t c, const Guard &g, float now, float cap_s)
  {
    if (!REXCVAR_GET(pdz_stealth_blind_search_clean))
    {
      return;
    }
    R8Blind &b = R8BlindOf(c, g);
    b.hold_from = now;
    b.hold_until = now + std::clamp(cap_s, 5.0f, 25.0f);
    b.held = 0;
  }
  // From the stimulus hook (rareplus): the bits that restart Rare's SEARCH (a sound 1000, gunfire with a target 0400,
  // the heard-sound input 0004) are taken while that search runs and he has not seen her or been hurt. A near miss,
  // a hit, a body, the alarm and "alerted" stay Rare's.
  uint32_t R8BlindSearchDrop(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
  {
    if (!REXCVAR_GET(pdz_stealth_blind_search_clean) || !(word & 0x1404u))
    {
      return 0;
    }
    const auto it = g_r8_blind.find(c);
    if (it == g_r8_blind.end() || it->second.born != g.r2_born)
    {
      return 0;
    }
    R8Blind &b = it->second;
    if (!(now >= b.hold_from && now < b.hold_until))
    {
      return 0;
    }
    const bool seen = (now >= g.r2_sight_at && now - g.r2_sight_at < 0.5f) || (now >= g.hurt_at && now - g.hurt_at < 0.5f) ||
                      g.spotted || (word & 0x082u) || AlarmOn(base);
    if (seen)
    {
      b.hold_until = -1e9f; // (a sighting, a hit or the alarm: Rare's from here)
      return 0;
    }
    ++b.held;
    if (now - b.held_log >= 5.0f || now < b.held_log)
    {
      b.held_log = now;
      REXLOG_INFO("pdz_stealth_r8: BLIND SEARCH: {} a sound in Rare's word {:04X} would restart the search that replaced his blind "
                  "COMBAT {:.1f} s ago: taken ({} so far; he goes on with the one search; pdz_stealth_blind_search_clean) | {}",
                  Id(base, c), word, now - b.hold_from, b.held, StackText(base, c));
    }
    return word & 0x1404u;
  }

  // Part B (lab_stealth_r8_B_Tue17Nov.inc, after round 3c): the body check comes first (item 3), a body seen later
  // (item 4).
  bool R8BodyCheck(uint32_t c, const Guard &g);
  bool R8BodyFirstRefuses(const uint8_t *base, uint32_t c, const Guard &g, Inv kind, const std::string &why, float now);
  void R8LateBodyTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search);
  float R8EyeHeight(const uint8_t *base, uint32_t c, const Guard &g, float now);
  thread_local bool t_r8_body_walk = false; // round 3 helper H's walk to a body (item 6) is being started
  struct R8BodyWalkScope
  {
    bool prev;
    explicit R8BodyWalkScope(bool on) : prev(t_r8_body_walk) { t_r8_body_walk = on || prev; }
    ~R8BodyWalkScope() { t_r8_body_walk = prev; }
  };
