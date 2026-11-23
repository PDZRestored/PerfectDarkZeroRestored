// Stealth layer, round 5, Mon 16 Nov evening, helper A (fix 6: melee downs). Included by lab_stealth.cpp inside its
// anonymous namespace, right after lab_stealth_r4_A_Mon16Nov.inc. Log prefix pdz_stealth_r5:.
//
// Hand test 19:15 (Lab start): a melee takedown from behind left no KILL line, so no body record of ours either.
// What Rare's code does: the melee routine (sub_82158590, r3 = the attacker) works out a damage amount and gives it to
// the victim through the victim's own damage entry (vtable +212: r3 victim, r4 attacker, f1 damage, r9 the gun type),
// then makes the 5 m melee noise (8218AD40) and the victim's line (speech 129). It is ordinary damage: there is no
// knocked-out state in that routine; a victim whose health reaches 0 is dead like any other. That entry does not come
// through the character hit handler (82549040) with Jo as its attacker, which is the only place the layer counted her
// kills.
//   - MELEE: every swing of Jo's that takes health from a tracked character is logged (always on while the layer
//     runs), and the layer's hit bookkeeping (HitBefore / HitAfter) is run for it as the hit handler would: hurt by
//     Jo, and for a kill the KILL line, the body record (g_kills) and the witness rule.
//   - DOWN: a tracked character whose health is found at 0 with no kill of Jo's counted (any other path of Rare's) is
//     logged; with Jo within 3.5 m of him it is counted as her kill the same way (a body for the witness / body rules).

  struct R5AMeleeSnap
  {
    uint32_t c;
    float hp, d;
  };
  thread_local std::vector<R5AMeleeSnap> t_r5a_melee;
  float g_r5a_melee_at = -1e9f; // the AI clock of Jo's last melee swing

  // From the melee routine's hook, before Rare's routine: the health of everyone within her reach.
  void R5AMeleeBefore(const uint8_t *base, uint32_t attacker)
  {
    t_r5a_melee.clear();
    if (!g_lab || !REXCVAR_GET(pdz_lab_guard_stealth) || !Ptr(attacker) || attacker != Load32(base, kPlayerOne))
    {
      return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    g_r5a_melee_at = Clock(base);
    for (const auto &[c, g] : g_guards)
    {
      if (!Ptr(c) || c == attacker || g.dead || g.st == St::Dead)
      {
        continue;
      }
      const float d = Dist(base, c, attacker);
      const float hp = LoadF(base, c + kHealth);
      if (d <= 500.0f && std::isfinite(hp) && hp > 0.0f)
      {
        t_r5a_melee.push_back({c, hp, d});
      }
    }
  }

  // ... and after it: who lost health.
  void R5AMeleeAfter(const uint8_t *base, uint32_t attacker)
  {
    if (t_r5a_melee.empty())
    {
      return;
    }
    std::lock_guard<std::recursive_mutex> hold(g_lock);
    const float now = Clock(base);
    for (const R5AMeleeSnap &s : t_r5a_melee)
    {
      if (!Ptr(s.c))
      {
        continue;
      }
      const float after = LoadF(base, s.c + kHealth);
      if (!(after < s.hp))
      {
        continue;
      }
      const Guard *g = Find(s.c);
      const bool down = !(after > 0.0f);
      const bool counted = std::any_of(g_kills.begin(), g_kills.end(), [&s](const Kill &k) { return k.victim == s.c; }) ||
                           (g && g->hurt_at == now);
      REXLOG_INFO("pdz_stealth_r5: MELEE: Jo's melee hit {} ({:.0f} cm from her, health {:.2f} -> {:.2f}, state {}): {} | {}",
                  Id(base, s.c), s.d, s.hp, after, g ? StName(g->st) : "not tracked",
                  down ? "DEAD (his health is 0; Rare's melee is ordinary damage, there is no knocked-out state): a body for the "
                         "witness / body rules"
                       : "hurt, still up",
                  counted ? "Jo's hit handler has counted it already"
                          : "Jo's hit handler did not see it: counted here as her hit (for a kill the KILL line follows)");
      if (!counted)
      {
        (void)lab_stealth::HitBefore(base, s.c, attacker); // (as the hit handler: clears an earlier hit's explosion mark)
        lab_stealth::HitAfter(base, s.c, attacker, s.hp);
      }
    }
    t_r5a_melee.clear();
  }

  // From the guard update hook, the moment a tracked character is found dead with no kill of Jo's counted for him
  // (before his stealth state ends). One line, always on.
  void R5ADown(const uint8_t *base, uint32_t c, Guard &g, float now, float hp)
  {
    if (!g_lab || std::any_of(g_kills.begin(), g_kills.end(), [c](const Kill &k) { return k.victim == c; }))
    {
      return;
    }
    const uint32_t jo = Load32(base, kPlayerOne);
    const float d = Ptr(jo) ? Dist(base, c, jo) : 1e9f;
    const bool melee = now >= g_r5a_melee_at && now - g_r5a_melee_at < 2.0f;
    const bool hers = Ptr(jo) && d <= 350.0f;
    REXLOG_INFO("pdz_stealth_r5: DOWN: {} is down (health {:.2f}, state {}) with no hit of Jo's counted | Jo {} | her last melee "
                "swing {} | {}",
                Id(base, c), hp, StName(g.st), Ptr(jo) ? fmt::format("{:.0f} cm from him", d) : std::string("not in the level"),
                melee ? fmt::format("{:.2f} s before", now - g_r5a_melee_at) : std::string("not in the last 2 s"),
                hers ? "she is within 3.5 m: counted as her kill (the KILL line follows): a body for the witness / body rules"
                     : "not hers (someone or something else): no body record of ours, Rare's own body reactions only");
    if (!hers)
    {
      return;
    }
    // (HitAfter's kill part, without its hurt part: he is dead already)
    Kill k;
    k.victim = c;
    k.at = now;
    for (int i = 0; i < 3; ++i)
    {
      k.pos[i] = LoadF(base, c + kPosition + 4 * i);
    }
    k.who = Id(base, c);
    R4AKillLog(base, k, now);
    g.dead = true;
    if (g_kills.size() < 64)
    {
      g_kills.push_back(k);
    }
    for (auto &[other, o] : g_guards)
    {
      if (other == c || o.dead || o.st == St::Dead)
      {
        continue;
      }
      const float d_jo = Dist(base, other, jo);
      const bool saw_jo = now >= o.last_visible && now - o.last_visible < 1.5f && d_jo <= 3000.0f;
      const bool fighting = (Load32(base, other + kSight) & kValidTarget) != 0;
      if (saw_jo || fighting)
      {
        Confirm(base, other, o, kCfMateKilled,
                fmt::format("saw Jo kill {}{} ({}, {:.0f} cm from her)", k.who, (g.squad && o.squad == g.squad) ? " (squad mate)" : "",
                            fighting ? "he is fighting her" : "his sight test saw her", d_jo));
      }
    }
  }
