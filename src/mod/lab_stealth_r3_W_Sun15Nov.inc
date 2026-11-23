// Stealth layer round 3, Sun 15 Nov, helper W (items 2, 4, 5 and 7). Included by lab_stealth.cpp inside its anonymous
// namespace, after the round 2 include. Everything here acts only with the layer on (pdz_lab_guard_stealth with
// pdz_lab_rareplus), for guards under the layer, and each item has its own switch (item 4 is part of
// pdz_stealth_witness_any_squad). Log prefix "pdz_stealth_r3:".
//
// Item 2, explosions muffle less (pdz_stealth_explosion_muffle). pdz_stealth_wall_muffle halves the distance a sound
// is heard within for every wall or floor between it and the listener's ears. For an EXPLOSION each wall or floor
// takes pdz_stealth_explosion_muffle_wall off instead (a quarter: 1 wall 75%, 2 walls 56%), never below
// pdz_stealth_explosion_muffle_floor of Rare's range (60%). Gunfire, a breaking lamp and every other sound keep the
// halving. Rare's sound-event record (82D2D124 list, built by sub_8218AD40: +0 radius, +4 a constant, +8 position,
// +20 pad) has NO type field, so an explosion is told by where it came from: sub_8218AD40's hook already keeps every
// event raised by the three explosion callers (824BAA54 grenade / plasma impact, 825B8D24 projectile detonation,
// 825EE8C4 exploding props: barrels) in g_blasts with the event's exact position; a sound event in the hearing loop
// (sub_8218AAC8) whose position is one of those (within 1 cm, under 10 s old) is an explosion. The layer's own blast
// tests (RarePlusStimulus, the gunfire rule) pass the source keys 0xB1A50000 + index: explosions too. Only the
// distance changes (SoundBlocked's `within`): what a heard sound leads to is Rare's (and round 3 item 1's).
//
// Item 4, no cross-squad witness for tower snipers (part of pdz_stealth_witness_any_squad, no new switch). A kill of
// one of the level row's no_walk guards (the Mansion's tower snipers tow1-4) is not raised by the any-squad witness
// rule for a guard of another squad. Rare's own "friend died in view" is NOT limited to squad mates (sub_82580508,
// the perception scan, raises +6260 0x800 for any dying character that sub_8251EBD0 calls an ally: a team-mask test,
// no squad test; seen on Subway 18:24:09: arp2 got 0010 for arm1 of another squad), so a 0010 whose only new body is
// such a tower kill of another squad is taken from Rare's word too. His own squad mates still react (Rare's rule).
//
// Item 5, one witness event per body (pdz_stealth_witness_once). Rare's perception scan (sub_82580508) runs about once
// a second (its own interval, character +13012 / +13024) and raises "a friend died in view" (+6260 0x800 -> word 0010)
// at EVERY scan while a dying ally is in his perception, until Rare turns the body into a corpse object (about 3 s;
// then it is "a body", 0040). Our raised bit (round 2's witness) is held only until Rare's word has shown it once
// (at most 0.5 s), so the repeats are Rare's own input (Mansion 18:36:53.6 / 55.1 / 56.1, fre3 for fre1: each one
// restarted his blind combat -> search). The first 0010 for a body is let through and recorded per guard (the
// r3_once_* fields: H's walk to the body may rely on them); a later 0010 for a body he has already reacted to (or,
// with no body on record, any 0010 within 10 s of his last reaction) is taken from Rare's word, with the WARNER bit
// 0004 when nothing else is in the word.
//
// Item 7, limit stacked boosts (pdz_stealth_boost_limit). Two rules, both only while Jo is beyond
// pdz_stealth_boost_limit_cm (25 m) of his eye, for a guard under the layer who is unaware, wary or on his lamp check
// (never in combat, never close up):
//   - the boost cap: (the meter's rate multipliers: hunt / alert x expecting) x (his wary levels' shortcut:
//     100 / (sighting level - meter floor), 2 for a wary guard) is capped at pdz_stealth_boost_limit_cap (2): the
//     rate multipliers are cut until the product fits;
//   - far and dark: a look whose exposure (the meter's whole exposure factor, the "exposure" of the log lines) is under
//     pdz_stealth_boost_limit_exposure (0.08) cannot take his meter past min(pdz_lab_rareplus_second, his sighting
//     level - 5) (70): he still gets his first sight line and look round at the notice level and his second,
//     stronger line at 70 (and the warning hold), but never confirms from such a look. Above that level already: no
//     rise.

bool R3WOn()
{
  return REXCVAR_GET(pdz_lab_guard_stealth) && RarePlus();
}

// ---- Item 2 ------------------------------------------------------------------------------------------------------

// Is this sound (SoundBlocked's source key and position) an explosion?
bool R3WIsBlast(uint32_t source, const float src[3], float now)
{
  if ((source & 0xFFFF0000u) == 0xB1A50000u)
  {
    return true; // (the layer's own keys for g_blasts entries)
  }
  for (const Blast &b : g_blasts)
  {
    const float dx = b.pos[0] - src[0], dy = b.pos[1] - src[1], dz = b.pos[2] - src[2];
    if (dx * dx + dy * dy + dz * dz < 1.0f && now >= b.at && now - b.at < 10.0f)
    {
      return true;
    }
  }
  return false;
}

// SoundBlocked's distance for `walls` walls (pdz_stealth_wall_muffle on): the halving, or for an explosion the gentler
// rule of item 2.
float R3WWithin(const uint8_t *base, uint32_t c, uint32_t source, const float src[3], float reach, int walls, float d, float now)
{
  const float halved = Muffled(reach, walls);
  if (walls <= 0 || !REXCVAR_GET(pdz_stealth_explosion_muffle) || !R3WOn() || !R3WIsBlast(source, src, now))
  {
    return halved;
  }
  const float per = std::clamp(float(REXCVAR_GET(pdz_stealth_explosion_muffle_wall)), 0.0f, 1.0f);
  const float low = std::clamp(float(REXCVAR_GET(pdz_stealth_explosion_muffle_floor)), 0.0f, 1.0f);
  float keep = 1.0f;
  for (int i = 0; i < std::min(walls, kMuffleCap); ++i)
  {
    keep *= 1.0f - per;
  }
  const float within = std::max(reach * std::max(keep, low), halved);
  // The log: once per listener and explosion (5 s).
  static std::unordered_map<uint64_t, float> said;
  if (said.size() > 2048)
  {
    said.clear();
  }
  float &last = said[(uint64_t(c) << 32) | source];
  if (last == 0.0f || now - last >= 5.0f || now < last)
  {
    last = now;
    const bool hears = d < within;
    REXLOG_INFO("pdz_stealth_r3: EXPLOSION MUFFLE: {} {} an explosion at ({:.0f} {:.0f} {:.0f}), {:.0f} cm away, through {} wall(s) or "
                "floor(s): heard within {:.0f} cm (Rare's {:.0f} cm; {:.0f}% off per wall, never below {:.0f}%; the halving of "
                "other sounds would give {:.0f} cm){}",
                Id(base, c), hears ? "HEARS" : "does NOT hear", src[0], src[1], src[2], d, walls, within, reach, per * 100.0f,
                low * 100.0f, halved, hears && !(d < halved) ? ": he hears it only by this rule" : "");
  }
  return within;
}

// ---- Item 4 ------------------------------------------------------------------------------------------------------

// The victim is one of the level row's no_walk guards (the Mansion's tower snipers).
bool R3WTowerVictim(const Kill &k)
{
  return ZoneLevel() && g_level->no_walk && *g_level->no_walk && CsvHasText(g_level->no_walk, BaseId(k.who));
}
bool R3WMate(const Guard &g, uint32_t victim)
{
  const Guard *v = Find(victim);
  return v && v->squad && v->squad == g.squad;
}

// From R2Witness (pdz_stealth_witness_any_squad), for a kill he could see: true = not raised for him.
bool R3WTowerKill(const uint8_t *base, uint32_t c, Guard &g, const Kill &k, float d)
{
  if (!R3WOn() || !R3WTowerVictim(k) || R3WMate(g, k.victim))
  {
    return false;
  }
  REXLOG_INFO("pdz_stealth_r3: TOWER WITNESS: {} could see Jo kill tower guard {} ({:.0f} cm from him, state {}) but is not of "
              "his squad: the any-squad witness rule leaves him out (a tower sniper's death reaches only his own squad)",
              Id(base, c), k.who, d, StName(g.st));
  return true;
}

// ---- Item 5 (and item 4 for Rare's own raise) ----------------------------------------------------------------------

constexpr float kR3WOnceWindow = 10.0f;  // s: a kill this old or younger can be the body of a 0010 (Rare: about 3 s)
constexpr float kR3WOnceRange = 6000.0f; // cm: and this near him

bool R3WReacted(const Guard &g, const Kill &k)
{
  for (const auto &[victim, at] : g.r3_once_done)
  {
    if (victim == k.victim && std::fabs(at - k.at) < 0.01f)
    {
      return true;
    }
  }
  return false;
}

// From the stimulus hook (pdz_lab_rareplus): the bits to take from Rare's word.
uint32_t R3WStimulusDrop(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
{
  if (!(word & 0x10) || !R3WOn() || g.scientist || !LayerGuard(g) || g.dead || g.st == St::Dead)
  {
    return 0;
  }
  const bool once = REXCVAR_GET(pdz_stealth_witness_once);
  const bool towers = REXCVAR_GET(pdz_stealth_witness_any_squad);
  if (!once && !towers)
  {
    return 0;
  }
  float me[3];
  PosOf(base, c, me);
  int fresh = -1, tower = -1, known = -1;
  for (int i = int(g_kills.size()) - 1; i >= 0; --i)
  {
    const Kill &k = g_kills[size_t(i)];
    if (k.victim == c || now < k.at || now - k.at > kR3WOnceWindow || Dist3(me, k.pos) > kR3WOnceRange)
    {
      continue;
    }
    if (once && R3WReacted(g, k))
    {
      known = known < 0 ? i : known;
    }
    else if (towers && R3WTowerVictim(k) && !R3WMate(g, k.victim))
    {
      tower = tower < 0 ? i : tower;
    }
    else
    {
      fresh = fresh < 0 ? i : fresh;
    }
  }
  if (fresh >= 0)
  {
    if (once)
    {
      // His first reaction to this body: Rare's own. The record H's walk to the body may rely on.
      const Kill &k = g_kills[size_t(fresh)];
      g.r3_once_victim = k.victim;
      std::memcpy(g.r3_once_pos, k.pos, sizeof(g.r3_once_pos));
      g.r3_once_has_pos = true;
      g.r3_once_at = now;
      g.r3_once_killed_at = k.at;
      g.r3_once_done.emplace_back(k.victim, k.at);
      g.r3_once_repeats = 0;
      if (R2Log())
      {
        REXLOG_INFO("pdz_stealth_r3: WITNESS ONCE: {} Rare's \"a friend died in view\" (0010) for the body of {} ({:.1f} s after the kill, "
                    "at ({:.0f} {:.0f} {:.0f})): his first reaction to it, Rare's own; recorded",
                    Id(base, c), k.who, now - k.at, k.pos[0], k.pos[1], k.pos[2]);
      }
    }
    return 0;
  }
  uint32_t drop = 0;
  const bool recent = g.r3_once_at > -1e8f && now >= g.r3_once_at && now - g.r3_once_at < kR3WOnceWindow;
  if (once && (known >= 0 || recent))
  {
    if (now >= g.r3_once_at && now - g.r3_once_at < 0.1f)
    {
      return 0; // (the same scan's word, built again in this frame: still his first reaction)
    }
    drop = 0x10;
    ++g.r3_once_repeats;
    if (g.r3_once_repeats == 1 || now - g.r3_once_log >= 5.0f || now < g.r3_once_log)
    {
      g.r3_once_log = now;
      const std::string body = known >= 0 ? g_kills[size_t(known)].who : g.r3_once_victim ? Id(base, g.r3_once_victim) : std::string("(not on record)");
      REXLOG_INFO("pdz_stealth_r3: WITNESS ONCE: {} Rare's \"a friend died in view\" (0010) again for the body of {} ({:.1f} s after "
                  "he reacted to it, repeat {}, state {}): taken from Rare's word: he has already reacted to this body (no new "
                  "blind combat, no new search)",
                  Id(base, c), body, now - g.r3_once_at, g.r3_once_repeats, StName(g.st));
    }
  }
  else if (tower >= 0)
  {
    drop = 0x10;
    const Kill &k = g_kills[size_t(tower)];
    if (now - g.r3_tower_log >= 5.0f || now < g.r3_tower_log)
    {
      g.r3_tower_log = now;
      REXLOG_INFO("pdz_stealth_r3: TOWER WITNESS: {} Rare's own \"a friend died in view\" (0010) for tower guard {} ({:.1f} s after the "
                  "kill), who is not of his squad: taken from Rare's word (a tower sniper's death reaches only his own squad)",
                  Id(base, c), k.who, now - k.at);
    }
  }
  else if (once)
  {
    // No body on record (not killed by Jo, or out of the record's reach): his first reaction, Rare's own; recorded
    // without a position.
    g.r3_once_victim = 0;
    g.r3_once_has_pos = false;
    g.r3_once_at = now;
    g.r3_once_killed_at = -1e9f;
    g.r3_once_repeats = 0;
    if (R2Log())
    {
      REXLOG_INFO("pdz_stealth_r3: WITNESS ONCE: {} Rare's \"a friend died in view\" (0010) with no kill of Jo's on record near him: "
                  "his first reaction, Rare's own; repeats in the next {:.0f} s count as the same body",
                  Id(base, c), kR3WOnceWindow);
    }
  }
  if (drop && (word & 0x4) && !(word & ~0x14u))
  {
    drop |= 0x4; // (the WARNER bit came with that 0010 alone)
  }
  return drop;
}

// ---- Item 7 ------------------------------------------------------------------------------------------------------

bool R3WBoostGuard(const Guard &g)
{
  return REXCVAR_GET(pdz_stealth_boost_limit) && R3WOn() && !g.scientist && LayerGuard(g) && !g.dead &&
         (g.st == St::Unaware || g.st == St::Wary || g.st == St::Investigating) && g.combat_script == 0;
}

// From the sight hook, before the meter step: the rate (pdz_lab_stealth_rate x his multipliers) with the boost cap.
float R3WBoostCap(const uint8_t *base, uint32_t c, Guard &g, float rate, float d, float now)
{
  if (!R3WBoostGuard(g) || !(d > float(REXCVAR_GET(pdz_stealth_boost_limit_cm))))
  {
    return rate;
  }
  const float base_rate = float(REXCVAR_GET(pdz_lab_stealth_rate));
  if (!(base_rate > 0.0f) || !(rate > 0.0f))
  {
    return rate;
  }
  const float mult = rate / base_rate;
  const float span = Threshold(g) - Floor(g);
  const float shortcut = span > 1.0f ? std::max(1.0f, 100.0f / span) : 1.0f;
  const float cap = std::max(1.0f, float(REXCVAR_GET(pdz_stealth_boost_limit_cap)));
  if (mult * shortcut <= cap * 1.001f)
  {
    return rate;
  }
  const float allowed = cap / shortcut;
  if (now - g.r3_cap_log >= 5.0f || now < g.r3_cap_log)
  {
    g.r3_cap_log = now;
    REXLOG_INFO("pdz_stealth_r3: BOOST CAP: {} Jo {:.0f} cm away (beyond {:.0f}): his meter's boosts x{:.2f} on the rate x {:.2f} "
                "from his levels (floor {:.0f}, sighting {:.0f}, state {}) = x{:.2f}: capped at x{:.2f} (rate x{:.2f})",
                Id(base, c), d, float(REXCVAR_GET(pdz_stealth_boost_limit_cm)), mult, shortcut, Floor(g), Threshold(g), StName(g.st),
                mult * shortcut, cap, allowed);
  }
  return base_rate * allowed;
}

// From the sight hook, after the meter step: far away in poor visibility he can only be suspicious.
void R3WFarDark(const uint8_t *base, uint32_t c, Guard &g, float f, float d, float before, float close_gain, float now)
{
  if (!R3WBoostGuard(g) || close_gain > 0.0f || !(d > float(REXCVAR_GET(pdz_stealth_boost_limit_cm))) ||
      !(f < float(REXCVAR_GET(pdz_stealth_boost_limit_exposure))))
  {
    return;
  }
  const float threshold = Threshold(g);
  const float hold = std::min(float(REXCVAR_GET(pdz_lab_rareplus_second)), threshold - 5.0f);
  const float top = std::min(std::max(hold, before), threshold - 1.0f);
  if (!(g.meter > top))
  {
    return;
  }
  g.meter = top;
  if (now - g.r3_far_log >= 5.0f || now < g.r3_far_log)
  {
    g.r3_far_log = now;
    REXLOG_INFO("pdz_stealth_r3: FAR DARK: {} meter held at {:.0f} (his sighting level {:.0f}, state {}): Jo {:.0f} cm away (beyond "
                "{:.0f}) at exposure {:.3f} (under {:.3f}): from that far in that little light he can only be suspicious (his sight "
                "lines and look round, no sighting)",
                Id(base, c), top, threshold, StName(g.st), d, float(REXCVAR_GET(pdz_stealth_boost_limit_cm)), f,
                float(REXCVAR_GET(pdz_stealth_boost_limit_exposure)));
  }
}
