// Stealth layer round 3, Sun 15 Nov, helper H (items 1, 6 and 8). Included by lab_stealth.cpp inside its anonymous
// namespace, after lab_stealth_r3_L_Sun15Nov.inc. Everything here runs only with pdz_lab_rareplus on, for a guard under
// the layer, and each item has its own switch. With a switch off nothing of that item runs. Log prefix pdz_stealth_r3:.
//
// Item 1, HEARD MEANS A REACTION (pdz_stealth_heard_react). Our wall check in Rare's hearing loop (sub_8218AAC8: the
// shooters' gunfire and Rare's sound events, each heard within its radius halved per wall) says a calm or wary guard
// heard something, and explosions are checked the same way here (the blasts on record, their radius halved per wall).
// When no reaction of Rare's follows within kR3HWait (no stimulus word of his with a heard / alerted / hit bit, no
// SEARCH, no COMBAT, no look round, no check of ours), the layer starts one:
//   - his line: gunfire -> his HEAR_GUNFIRE line (GunfireLineTick, at most once in 20 s); an explosion -> HEAR_UNKNOWN
//     ("Whoa, what was that?" / "Shit! What was that?"); a sound or a silenced shot -> HEAR_UNKNOWN ("What's that
//     noise?" / "Where'd that come from?" ...). He is wary and alert (his meter fills faster).
//   - he moves: a LOUD sound (unsilenced gunfire, an explosion) within kR3HLoudWalkCm: he goes to look where it came
//     from (the place of the sound, never Jo's present position): our noise check, the legs of the lamp check (Rare's
//     SEARCH at the pad beside the spot, his look there), ended at pdz_stealth_search_cap_s. At most kR3HWalkersPerSound
//     guards walk to one sound. Otherwise (a quiet sound, a far loud one, the walkers taken): he stops and looks round
//     where he stands (Rare's SEARCH at his own pad, ended after pdz_lab_stealth_alert_look_s). Both are Rare's own
//     SEARCH handed over in place of his DEFAULT script (GiveSearch), the hand-over Rare's own noise reaction makes:
//     a seated guard is given the same task Rare gives him when Rare reacts.
//   - no_look / no_walk guards (objective talks, Rare's WARNER, Mansion towers), radio users who can still call, and
//     guards with Rare's never-investigates flag say their line and stay. The round 2 restart rule (R2NoRestart) can
//     refuse the walk and the look (his line still plays). Nothing while the alarm sounds.
//   - A lamp breaking is not handled here: the lamp code already reacts to a lamp heard (its line, its walk).
//
// Item 6, A WITNESS SEARCH WALKS TO THE BODY (pdz_stealth_witness_walk). Round 2's blind combat after a witnessed kill
// (R2BlindCombat, "the kill he saw") used to end in Rare's SEARCH where he stood: Rare's DEFAULT restarted a SEARCH
// every second from the sounds and the body in view, and he never left the spot (Mansion 18:36:56-18:37:19, fre3).
// Here that hand-over is taken over: from the blind combat start until our walk is over, the bits that start Rare's
// search on the spot are taken from his stimulus word (0x1000 a sound, 0x40 a body, 0x10 a friend died in view; a hit,
// a sighting, gunfire with a target, the alarm and "alerted" stay Rare's), and once his COMBAT is over he walks to the
// pad beside the body on its floor with our noise check's legs, his look there, ended at pdz_stealth_search_cap_s.
// Once per body and guard. Not for no_look / no_walk guards or radio users who can still call (round 2's hand-over
// stays for them).
//
// Item 8, AN ALARM SHOUT IS HEARD (pdz_stealth_shout_heard). A shout that really played (Rare's WARN_TEAM, our
// announced alarm shout, a scientist's call for security, the Mansion radio call 158 / 159) is heard by calm or wary
// guards within kR3HShoutCm of the shouter, halved per wall or floor (the wall-muffle rule). Each says an acknowledging
// line (troopers: Rare's HEAR_ALARM_TEAM takes 5 "Red alert!" and 8 "Alert! Let's go!", asked for as HEAR_UNKNOWN with
// the cue swapped, as the glimpse lines are; other voices: their own HEAR_UNKNOWN), is wary and alert (his meter fills
// faster), and turns to the shouter (Rare's go-to-object / look-at-object parts with the shouter as the object, the
// walk ending where he stands). He learns nothing of Jo: no target, no pad of hers. no_look / no_walk guards and radio
// users who can still call say the line only.

  constexpr float kR3HWait = 0.5f;          // s: Rare's own reaction to a sound may take this long
  constexpr float kR3HLoudWalkCm = 3000.0f; // a loud sound further than this: he looks round where he stands
  constexpr int kR3HWalkersPerSound = 2;    // guards who walk to one sound; the others look round where they stand
  constexpr float kR3HShoutCm = 2000.0f;    // an alarm shout is heard within this distance (halved per wall)
  constexpr float kR3HCooldown = 10.0f;     // s: one reaction of ours per guard (a loud sound may follow a quiet one)

  enum : int
  {
    kR3HNone = 0,
    kR3HGunfire = 1,  // an unsilenced shot (loud)
    kR3HSound = 2,    // one of Rare's sound events (a body falling, a prop...)
    kR3HBlast = 3,    // an explosion (loud)
    kR3HSilenced = 4, // a silenced shot heard (quiet)
  };
  bool R3HLoud(int kind)
  {
    return kind == kR3HGunfire || kind == kR3HBlast;
  }
  const char *R3HKindName(int kind)
  {
    return kind == kR3HGunfire ? "gunfire" : kind == kR3HBlast ? "an explosion" : kind == kR3HSilenced ? "a silenced shot"
                                                                                                        : "a sound";
  }

  struct R3HSound
  {
    float at = -1e9f; // heard then (-1e9: nothing waiting)
    int kind = kR3HNone;
    uint32_t source = 0; // the shooter, the sound event, or the blast's key
    float pos[3] = {};
    float d = 0.0f;
    int walls = -1;
  };
  struct R3HState
  {
    float born = -2e9f; // his Guard's r2_born when this was made (a new guard at the same address starts afresh)
    // Item 1.
    R3HSound wait;            // a sound he heard, waiting for Rare's reaction
    float rare_at = -1e9f;    // a stimulus word of Rare's with a reaction bit came then
    float rare_log = -1e9f;
    float react_at = -1e9f;   // our reaction began then
    float react_pos[3] = {};
    int react_kind = kR3HNone;
    bool react_walk = false;  // ... it walks him to the sound
    float check_at = -1e9f;   // ... his stand-up / move check is due then
    float check_pos[3] = {};
    float blast_seen = -1e9f; // the newest explosion looked at
    // Ours running: inv_item 1 (item 1's walk) or 6 (item 6's walk); look_from: item 1's look round.
    int inv_item = 0;
    int inv_kill = -1; // ... item 6: the kill he walks to
    float inv_from = -1e9f;
    bool cap_logged = false;
    float look_from = -1e9f;
    // Item 6.
    int walk_kill = -1; // claimed, not started yet: the kill
    float walk_from = -1e9f, walk_try = -1e9f;
    uint32_t walk_pad = 0;
    float walk_pos[3] = {};
    std::vector<int> walked; // kills he has walked to (or was sent to)
    bool drop_logged = false;
    // Round 3b, Sun 15 Nov (fix 3): the walk is claimed at his first witness reaction to a body; a SEARCH of Rare's
    // running then is taken over (ended by R3BTakeOver in the SEARCH update hook) before the walk is handed over.
    float first_seen = -1e9f; // the newest first reaction looked at (round 3's witness-once record / round 2's raise)
    bool take_over = false;
    float take_log = -1e9f;
    bool blind_logged = false;
    // Item 8.
    float shout_done = -1e9f; // the start of the last shout checked for him
  };
  std::unordered_map<uint32_t, R3HState> g_r3h;
  struct R3HShout
  {
    uint32_t c = 0;
    float at = 0.0f;
    float pos[3] = {};
    const char *what = "";
  };
  std::vector<R3HShout> g_r3h_shouts;
  float g_r3h_scan = -1e9f;

  R3HState &R3HOf(uint32_t c, const Guard &g)
  {
    if (g_r3h.size() > 512)
    {
      g_r3h.clear();
    }
    R3HState &s = g_r3h[c];
    if (s.born != g.r2_born)
    {
      s = R3HState();
      s.born = g.r2_born;
    }
    return s;
  }

  // Item 1 and 6: an investigation of ours is running for him (RarePlusTick lets InvTick run it, as the lamp check).
  bool R3HInvOurs(const Guard &g, uint32_t c)
  {
    if (g.st != St::Investigating || g.inv != Inv::Noise)
    {
      return false;
    }
    const auto it = g_r3h.find(c);
    return it != g_r3h.end() && it->second.inv_item != 0 && it->second.born == g.r2_born;
  }

  // Item 1: a sound our wall check lets him hear (or an explosion in reach): kept until Rare's reaction comes or not.
  void R3HConsider(R3HState &s, int kind, uint32_t source, const float pos[3], float d, int walls, float now)
  {
    const bool loud = R3HLoud(kind);
    if (now >= s.react_at && now - s.react_at < kR3HCooldown && (!loud || R3HLoud(s.react_kind)))
    {
      return; // (he reacted a moment ago; only a loud sound may follow a quiet one)
    }
    if (s.wait.at > -1e8f && now >= s.wait.at && now - s.wait.at < 1.5f && !(loud && !R3HLoud(s.wait.kind)))
    {
      return; // one waits already (the first time it was heard is kept); a louder one replaces it
    }
    s.wait.at = now;
    s.wait.kind = kind;
    s.wait.source = source;
    std::memcpy(s.wait.pos, pos, sizeof(s.wait.pos));
    s.wait.d = d;
    s.wait.walls = walls;
  }

  // Item 1, from Rare's hearing loop (sub_8218AAC8 hook): our wall check says he hears this one. `what` 1: the gunfire
  // of the character `source` (a perception profile's owner); 2: the sound event at `source`. No logging, no syscall.
  void R3HHeard(const uint8_t *base, uint32_t c, int what, uint32_t source, const float src[3], float d, float now)
  {
    (void)base;
    if (!REXCVAR_GET(pdz_stealth_heard_react) || !RarePlus() || !(d > 100.0f))
    {
      return;
    }
    Guard *g = Find(c);
    if (!g || g->dead || g->st == St::Dead || g->scientist || !LayerGuard(*g) || g->run_committed ||
        !(g->st == St::Unaware || g->st == St::Wary))
    {
      return;
    }
    int kind = kR3HNone;
    if (what == 1)
    {
      for (const Shot &sh : g_shots)
      {
        if (sh.shooter == source && now >= sh.at && now - sh.at <= 1.0f)
        {
          kind = sh.silenced ? kR3HSilenced : kR3HGunfire;
        }
      }
      if (kind == kR3HNone)
      {
        return; // (no shot of his on record in the last second: the profile's radius is left over from before)
      }
    }
    else
    {
      kind = kR3HSound;
      for (const Blast &b : g_blasts)
      {
        if (now >= b.at && now - b.at < 2.0f && Dist3(b.pos, src) < 400.0f)
        {
          kind = kR3HBlast; // (the sound event of an explosion on record)
        }
      }
    }
    const auto h = g_heard.find((uint64_t(c) << 32) | source);
    R3HConsider(R3HOf(c, *g), kind, source, src, d, h != g_heard.end() ? h->second.walls : -1, now);
  }

  // Items 1 and 6, from the stimulus word hook (pdz_lab_rareplus): Rare's reaction has come; and item 6's bits.
  uint32_t R3HStimulus(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
  {
    R3HState &s = R3HOf(c, g);
    // heard a sound, gunfire with a target, the alarm, saw a target, near miss, alerted, damaged
    if (word & 0x178Au)
    {
      s.rare_at = now;
    }
    if (!REXCVAR_GET(pdz_stealth_witness_walk) || !(s.walk_kill >= 0 || s.inv_item == 6))
    {
      return 0;
    }
    const uint32_t drop = word & 0x1050u;
    if (drop && !s.drop_logged)
    {
      s.drop_logged = true;
      REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} Rare's stimulus {:04X}: {:04X} taken from it (a sound, a body, a friend "
                  "died in view: the starts of Rare's search on the spot) while his walk to the body is due or runs",
                  Id(base, c), word, drop);
    }
    return drop;
  }

  // Item 1: walkers of ours already on their way to (or at) a sound within 6 m of this one.
  int R3HWalkers(const float pos[3], float now)
  {
    int n = 0;
    for (const auto &[o, s] : g_r3h)
    {
      const Guard *og = Find(o);
      n += (og && s.inv_item == 1 && s.born == og->r2_born && og->st == St::Investigating && now - s.react_at < 40.0f &&
            Dist3(s.react_pos, pos) < 600.0f)
               ? 1
               : 0;
    }
    return n;
  }

  // Why he stays where he is (line only), or nullptr.
  const char *R3HStays(const uint8_t *base, uint32_t c, const Guard &g)
  {
    return NoLook(g.who)    ? "his row's no_look (his talk is the objective, or Rare's WARNER): he stays, his line only"
           : NoWalk(g.who)  ? "his row's no_walk (a tower sniper): he stays, his line only"
           : RadioReady(base, c) ? "a radio user who can still call: Rare's DEFAULT reaction stays ready, his line only"
           : (Load32(base, c + kConfig) & kNeverInvestigates) ? "Rare's never-investigates flag: his line only"
                                                              : nullptr;
  }

  // A look round where he stands: Rare's SEARCH at his own pad, ended after pdz_lab_stealth_alert_look_s by the SEARCH
  // update hook (Looking). False with the reason.
  bool R3HLookRound(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const char *&why)
  {
    const float look_s = float(REXCVAR_GET(pdz_lab_stealth_alert_look_s));
    const uint32_t pad = Load32(base, c + kPad);
    why = !(look_s > 0.0f)                                                  ? "pdz_lab_stealth_alert_look_s is 0"
          : !(now - g.search_seen > 1.0f || now < g.search_seen)            ? "a search of Rare's ran a moment ago"
          : g.look_started > -1e8f && now >= g.look_started && now - g.look_started <= look_s + 6.0f
              ? "he looked round a moment ago"
          : (Load32(base, c + kConfig) & (kSearching | kNeverInvestigates)) ? "Rare has him searching"
          : g.combat_script != 0                                             ? "Rare's combat script runs"
          : !Ptr(pad)                                                        ? "he has no pad"
                                                                             : nullptr;
    if (why)
    {
      return false;
    }
    if (!GiveSearch(ctx, base, c, pad))
    {
      why = "his task stack is busy (no SEARCH could be handed over)";
      return false;
    }
    g.look_started = now;
    g.look_pad = pad;
    return true;
  }

  // Item 8: he turns to an object (the shouter): Rare's go-to-object / look-at-object parts, as the scientists' turn
  // to Jo (SciReact) and Rare's walk to a body build them; the walk ends where he stands. False: his stack is busy.
  bool R3HTurnTo(PPCContext &ctx, uint8_t *base, uint32_t c, uint32_t obj)
  {
    if (R7Refuse(base, c, "turn him to a shout or a body")) // (Tue 17 Nov: he is Rare's COMBAT's)
    {
      return false;
    }
    bool top_is_search = false;
    const int depth = StackDepth(base, c), waiting = StackWaiting(base, c);
    if (!Ptr(obj) || depth < 1 || depth >= kGiveDepth || waiting != 0 || StackSearches(base, c, top_is_search) > 0 ||
        (Load32(base, c + kConfig) & (kSearching | kDormant)) != 0)
    {
      return false;
    }
    const float stop = Dist(base, c, obj) + 200.0f;
    const uint32_t composite = PoolTask(ctx, base, 96);
    if (!composite)
    {
      return false;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = composite;
      frame.ctx.r4.u64 = 0;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      sub_821CA7E0(frame.ctx, base);
      Store32(base, composite, 0x820922A4);
    }
    uint32_t look = 0, go = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = 80;
      sub_821784F0(frame.ctx, base);
      look = Ptr(frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
    }
    if (look)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = look;
      frame.ctx.r4.u64 = obj;
      frame.ctx.r5.u64 = 1;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = 0;
      sub_821A1538(frame.ctx, base);
      look = frame.ctx.r3.u32;
    }
    go = PoolTask(ctx, base, 144);
    if (go)
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = go;
      frame.ctx.r4.u64 = obj;
      frame.ctx.r5.u64 = 1;
      frame.ctx.r6.u64 = 1;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      frame.ctx.r9.u64 = 0;
      frame.ctx.r10.u64 = 0;
      frame.ctx.f1.f64 = double(stop);
      sub_821D5678(frame.ctx, base);
      Store32(base, go, 0x82092E88);
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = composite;
      frame.ctx.r4.u64 = go;
      frame.ctx.r5.u64 = look;
      frame.ctx.r6.u64 = 0;
      frame.ctx.r7.u64 = 0;
      frame.ctx.r8.u64 = 0;
      frame.ctx.r9.u64 = 0;
      frame.ctx.r10.u64 = 0;
      sub_821CA8C0(frame.ctx, base);
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c + kScheduler;
    frame.ctx.r4.u64 = composite;
    sub_82171210(frame.ctx, base);
    return true;
  }

  // Rare's state of him in one text, for the logs of a reaction of ours (the seated-guard question).
  std::string R3HRareText(const uint8_t *base, uint32_t c)
  {
    const uint32_t profile = ProfileOf(base, c);
    return fmt::format("Rare's inputs +6260 {:08X}, +6268 {:08X}, his perception flags {:08X}; {}", Load32(base, c + kStimuli),
                       Load32(base, c + kSight), profile ? Load32(base, profile + 0x44) : 0u, StackText(base, c));
  }

  // The pad beside a spot: on its floor within 8 m, else the nearest pad to it he can reach.
  uint32_t R3HSpotPad(const uint8_t *base, uint32_t c, const float pos[3])
  {
    const uint32_t own = Load32(base, c + kPad);
    if (!Ptr(own))
    {
      return 0;
    }
    float d = 0.0f;
    const uint32_t floor = FloorPadAt(base, own, pos, 800.0f, d);
    return Ptr(floor) ? floor : NearestPadTo(base, own, pos, d);
  }

  // An investigation of ours (items 1 and 6): our noise check at `pos`, its first leg the pad beside it, capped.
  bool R3HGoLook(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, R3HState &s, const float pos[3], uint32_t pad,
                 int item, float now, const std::string &why)
  {
    g_next_brief = false;
    const R8BodyWalkScope body_walk(item == 6); // (Tue 17 Nov night, fix2: his walk to a body is the body check itself)
    StartInvestigation(ctx, base, c, g, Inv::Noise, pos, -1, now, why, pad);
    if (g.st != St::Investigating || g.inv != Inv::Noise)
    {
      return false;
    }
    s.inv_item = item;
    s.inv_from = now;
    s.cap_logged = false;
    const double cap = R2CapS();
    if (cap > 0.0)
    {
      g.inv_until = std::min(g.inv_until, now + float(cap));
    }
    return true;
  }

  // Item 1: Rare's reaction did not come: ours.
  void R3HReact(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, R3HState &s, float now)
  {
    const R3HSound w = s.wait;
    s.wait.at = -1e9f;
    s.react_at = now;
    s.react_kind = w.kind;
    std::memcpy(s.react_pos, w.pos, sizeof(s.react_pos));
    s.react_walk = false;
    const bool loud = R3HLoud(w.kind);
    const bool trooper = g.voice >= 17 && g.voice <= 19;
    // His line.
    std::string line;
    if (w.kind == kR3HGunfire)
    {
      if (now - g.gunfire_line_said > 20.0f || now < g.gunfire_line_said)
      {
        g.gunfire_line_at = now; // (GunfireLineTick: HEAR_GUNFIRE, a warning line)
        line = "HEAR_GUNFIRE (his gunfire line)";
      }
      else
      {
        line = "none (his gunfire line was said less than 20 s ago)";
      }
    }
    else if (w.kind == kR3HBlast)
    {
      // Troopers: HEAR_UNKNOWN_SOLO take 5 "Whoa, what was that?"; _TEAM takes 4 "Shit! What was that?" and 7 "What's
      // that? Scared the shit out of me!".
      Warn(ctx, base, c, g, kEvHearUnknown, now, "heard an explosion (pdz_stealth_heard_react)", trooper ? 0x10u : 0u,
           trooper ? "HEAR_UNKNOWN_TEAM" : nullptr, trooper ? 0x48u : 0u);
      line = "HEAR_UNKNOWN (a what-was-that take)";
    }
    else
    {
      // Troopers: HEAR_UNKNOWN_SOLO takes 1 "Anybody there?" 3 "What's that noise?" 5 "Whoa, what was that?"; _TEAM
      // takes 1 "Thought I heard a noise." 3 "I could've sworn I heard something." 5 "Where'd that come from?" 6
      // "Wonder what that was?" 8 "You guys hear that?".
      Warn(ctx, base, c, g, kEvHearUnknown, now, "heard a sound (pdz_stealth_heard_react)", trooper ? 0x15u : 0u,
           trooper ? "HEAR_UNKNOWN_TEAM" : nullptr, trooper ? 0xB5u : 0u);
      line = "HEAR_UNKNOWN (a heard-something take)";
    }
    FillFaster(base, c, g, now, "heard something no reaction of Rare's followed (pdz_stealth_heard_react)");
    if (g.st == St::Unaware)
    {
      Set(base, c, g, St::Wary, now, "heard something: wary (pdz_stealth_heard_react)");
    }
    // He moves.
    std::string does;
    const char *stay = R3HStays(base, c, g);
    const uint32_t jo = Load32(base, kPlayerOne);
    if (stay)
    {
      does = stay;
    }
    else if (R2NoRestart(base, c, g, jo, now, "a sound he heard"))
    {
      does = "the search cap's one restart is used up: his line only";
    }
    else
    {
      const int walkers = loud ? R3HWalkers(w.pos, now) : 0;
      const uint32_t pad = loud && w.d <= kR3HLoudWalkCm && walkers < kR3HWalkersPerSound ? R3HSpotPad(base, c, w.pos) : 0;
      if (Ptr(pad) &&
          R3HGoLook(ctx, base, c, g, s, w.pos, pad, 1, now,
                    fmt::format("heard {} {:.0f} cm away and no reaction of Rare's came (pdz_stealth_heard_react): he goes "
                                "to look where it came from",
                                R3HKindName(w.kind), w.d)))
      {
        s.react_walk = true;
        float pp[3] = {};
        PadPos(base, pad, pp);
        does = fmt::format("he goes to look where it came from: our noise check, its first leg the pad {:08X} at ({:.0f} {:.0f} "
                           "{:.0f}) beside it, {:.0f} s at most (pdz_stealth_search_cap_s)",
                           pad, pp[0], pp[1], pp[2], std::max(0.0, R2CapS()));
      }
      else
      {
        const char *no = nullptr;
        if (R3HLookRound(ctx, base, c, g, now, no))
        {
          s.look_from = now;
        }
        const char *why_not_walk = !loud                        ? "a quiet sound"
                                   : w.d > kR3HLoudWalkCm        ? "a loud sound over 30 m away"
                                   : walkers >= kR3HWalkersPerSound ? "two guards already walk to it"
                                   : Ptr(pad)                    ? "no leg could start"
                                                                 : "no pad beside it he can reach";
        does = no ? fmt::format("{}: no walk; no look round either ({})", why_not_walk, no)
                  : fmt::format("{}: he stops and looks round where he stands (Rare's SEARCH at his own pad)", why_not_walk);
      }
    }
    PosOf(base, c, s.check_pos);
    s.check_at = now + 3.0f;
    REXLOG_INFO("pdz_stealth_r3: HEARD REACT: {} heard {} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm away, {} (our wall check), {:.1f} s "
                "ago, and no reaction of Rare's followed: his line {}; {}; state {}, meter {:.0f} | {}",
                Id(base, c), R3HKindName(w.kind), w.pos[0], w.pos[1], w.pos[2], w.d,
                w.walls < 0 ? std::string("walls not counted") : fmt::format("{} wall(s) or floor(s)", w.walls), now - w.at, line,
                does, StName(g.st), g.meter, R3HRareText(base, c));
  }

  // Item 8: the alarm shouts that really started in the last second (once per AI clock step for every guard).
  void R3HShoutScan(const uint8_t *base, float now)
  {
    if (now == g_r3h_scan)
    {
      return;
    }
    g_r3h_scan = now;
    g_r3h_shouts.erase(std::remove_if(g_r3h_shouts.begin(), g_r3h_shouts.end(),
                                      [now](const R3HShout &s) { return now < s.at || now - s.at > 3.0f; }),
                       g_r3h_shouts.end());
    static uint32_t warn = 0;
    if (!warn)
    {
      warn = EventIndex(base, "WARN_TEAM");
    }
    const auto add = [&](uint32_t c, float at, const char *what) {
      if (g_r3h_shouts.size() >= 32 || std::any_of(g_r3h_shouts.begin(), g_r3h_shouts.end(), [&](const R3HShout &s) {
            return s.c == c && std::fabs(s.at - at) < 1.0f;
          }))
      {
        return;
      }
      R3HShout s;
      s.c = c;
      s.at = at;
      s.what = what;
      PosOf(base, c, s.pos);
      g_r3h_shouts.push_back(s);
    };
    for (const Line &l : g_lines)
    {
      // (start: a trooper's line, its real start; any other voice's, when it was asked for: up to a second or so earlier)
      if (l.played && Ptr(l.c) && now >= l.start && now - l.start <= 2.0f && ((warn && l.ev == warn) || RadioEvent(l.ev)))
      {
        add(l.c, l.start, RadioEvent(l.ev) ? "his radio call" : "WARN_TEAM (an alarm shout)");
      }
    }
    for (const auto &[o, og] : g_guards)
    {
      if (og.announced_at > -1e8f && now >= og.announced_at && now - og.announced_at <= 1.0f && !og.dead)
      {
        add(o, og.announced_at, og.scientist ? "a scientist's call for security" : "his announced alarm shout");
      }
    }
  }

  // Item 8 for one listener.
  void R3HShoutTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, R3HState &s, float now, bool in_combat, bool in_search)
  {
    if (g_r3h_shouts.empty())
    {
      return;
    }
    for (const R3HShout &sh : g_r3h_shouts)
    {
      if (sh.c == c || now < sh.at || now - sh.at > 2.5f || sh.at <= s.shout_done)
      {
        continue;
      }
      s.shout_done = sh.at; // (each shout is judged once for him)
      if (g.dead || g.st == St::Dead || g.scientist || !LayerGuard(g) || !(g.st == St::Unaware || g.st == St::Wary) || in_combat ||
          in_search || g.run_committed || AlarmOn(base) || FightsNonPlayer(base, c) || FightsNonPlayer(base, sh.c))
      {
        continue;
      }
      float me[3];
      PosOf(base, c, me);
      const float d = Dist3(me, sh.pos);
      if (!(d < kR3HShoutCm))
      {
        continue;
      }
      const float ears[3] = {me[0], me[1] + 60.0f, me[2]}, mouth[3] = {sh.pos[0], sh.pos[1] + 60.0f, sh.pos[2]};
      int walls = 0;
      float within = kR3HShoutCm;
      if (REXCVAR_GET(pdz_stealth_wall_muffle))
      {
        walls = SoundWalls(ctx, base, mouth, ears);
        within = Muffled(kR3HShoutCm, walls);
      }
      else if (!SegmentClear(ctx, base, ears, mouth, 0x40) || !SegmentClear(ctx, base, mouth, ears, 0x40))
      {
        walls = 1;
        within = 0.0f;
      }
      if (!(d < within))
      {
        if (R2Log())
        {
          REXLOG_INFO("pdz_stealth_r3: SHOUT HEARD: {} does NOT hear {}'s {} ({:.0f} cm away, {} wall(s) or floor(s): heard within "
                      "{:.0f} cm)",
                      Id(base, c), Id(base, sh.c), sh.what, d, walls, within);
        }
        continue;
      }
      // His acknowledging line: troopers Rare's HEAR_ALARM_TEAM takes 5 "Red alert!" and 8 "Alert! Let's go!" (asked
      // for as HEAR_UNKNOWN, the cue swapped); any other voice his own HEAR_UNKNOWN.
      const bool trooper = g.voice >= 17 && g.voice <= 19;
      if (trooper)
      {
        g_next_swap = "HEAR_ALARM_TEAM";
        g_next_swap_takes = 0x90u;
      }
      Warn(ctx, base, c, g, kEvHearUnknown, now, "heard an alarm shout (pdz_stealth_shout_heard)");
      g_next_swap = nullptr;
      g_next_swap_takes = 0;
      FillFaster(base, c, g, now, "heard an alarm shout (pdz_stealth_shout_heard)");
      if (g.st == St::Unaware)
      {
        Set(base, c, g, St::Wary, now, "heard an alarm shout: wary (pdz_stealth_shout_heard)");
      }
      const char *stay = R3HStays(base, c, g);
      const bool turned = !stay && R3HTurnTo(ctx, base, c, sh.c);
      REXLOG_INFO("pdz_stealth_r3: SHOUT HEARD: {} heard {}'s {} ({:.0f} cm away, {} wall(s) or floor(s), heard within {:.0f} cm): "
                  "his line {}; wary and alert; {}; he learns nothing of Jo (no target, no pad of hers) | {}",
                  Id(base, c), Id(base, sh.c), sh.what, d, walls, within,
                  trooper ? "HEAR_ALARM_TEAM take 5 or 8 (\"Red alert!\" / \"Alert! Let's go!\")" : "his own HEAR_UNKNOWN",
                  stay ? std::string(stay)
                  : turned ? fmt::format("he turns to {} (Rare's go-to-object / look-at-object parts, the walk ending where he stands)",
                                         Id(base, sh.c))
                           : std::string("no turn (his task stack is busy)"),
                  StackText(base, c));
      return;
    }
  }

  // Round 3b, Sun 15 Nov (fix 3; Mansion 20:01:27, fre3 saw Jo kill fre1 4.6 m in front of him, Rare only started its
  // own search on the spot, and no walk ran because Rare's COMBAT never began): the walk is claimed at a guard's FIRST
  // witness reaction to a body (no sighting of Jo), whatever Rare's response: Rare's own first "a friend died in view"
  // for it (round 3's witness-once record, r3_once_*) or round 2's witness raise (r2_witness_*).
  void R3HFirstReaction(uint8_t *base, uint32_t c, Guard &g, R3HState &s, float now)
  {
    int ki = -1;
    const char *how = nullptr;
    float newest = s.first_seen;
    if (g.r3_once_victim && g.r3_once_has_pos && g.r3_once_at > s.first_seen && now >= g.r3_once_at && now - g.r3_once_at < 1.0f)
    {
      newest = std::max(newest, g.r3_once_at);
      for (size_t i = g_kills.size(); i-- > 0;)
      {
        if (g_kills[i].victim == g.r3_once_victim && std::fabs(g_kills[i].at - g.r3_once_killed_at) < 0.01f)
        {
          ki = int(i);
          how = "Rare's own \"a friend died in view\" (0010), his first for this body";
          break;
        }
      }
    }
    if (g.r2_witness_at > s.first_seen && now >= g.r2_witness_at && now - g.r2_witness_at < 1.0f)
    {
      newest = std::max(newest, g.r2_witness_at);
      for (size_t i = g_kills.size(); ki < 0 && i-- > 0;)
      {
        if (Dist3(g_kills[i].pos, g.r2_witness_pos) < 1.0f)
        {
          ki = int(i);
          how = "the witness test (round 2): he saw the kill";
        }
      }
    }
    s.first_seen = newest;
    if (ki < 0 || std::find(s.walked.begin(), s.walked.end(), ki) != s.walked.end() || g.dead || g.st == St::Dead || g.scientist ||
        !LayerGuard(g))
    {
      return; // (none new; or once per body: his walk to it is due, runs or is over)
    }
    s.walked.push_back(ki);
    const bool blind = g.r2_blind_at > -1e8f && now >= g.r2_blind_at && now - g.r2_blind_at < 1.0f;
    const std::string who = g_kills[size_t(ki)].who;
    const char *no = R2Seen(g, now, 1.0f) || (Load32(base, c + kSight) & kValidTarget) != 0 ? "he sees Jo (or she hit him): Rare's combat"
                     : AlarmOn(base)                                                        ? "the alarm sounds: Rare's"
                     : !(g.st == St::Unaware || g.st == St::Wary || blind)                  ? "he is busy (not calm or wary)"
                                                                                            : R4BWitnessStays(base, c, g); // (fix 3, Mon 16 Nov)
    if (no)
    {
      REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} first reaction to the body of {} ({}): no walk to it: {} (state {})", Id(base, c),
                  who, how, no, StName(g.st));
      return;
    }
    const bool rare_search = (Load32(base, c + kConfig) & kSearching) != 0 || now - g.search_seen < 0.5f;
    s.walk_kill = ki;
    s.walk_from = now;
    s.walk_try = -1e9f;
    s.walk_pad = 0;
    std::memcpy(s.walk_pos, g_kills[size_t(ki)].pos, sizeof(s.walk_pos));
    s.drop_logged = false;
    s.take_over = false;
    s.blind_logged = false;
    if (blind)
    {
      g.r2_blind_at = -1e9f; // (round 2's hand-over, Rare's SEARCH at a pad, is not made: the walk below is)
    }
    REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} FIRST REACTION to the body of {} ({}; Rare's response so far: {}): he walks to the "
                "body at ({:.0f} {:.0f} {:.0f}) whatever Rare does, his look there ended by the search cap {:.0f} s from now; until "
                "the walk starts Rare's search on the spot is not started (stimulus 1000 / 0040 / 0010 taken)",
                Id(base, c), who, how, blind ? "a blind COMBAT (round 2's hand-over not made)" : rare_search ? "a SEARCH of his own"
                                                                                                             : "nothing yet",
                s.walk_pos[0], s.walk_pos[1], s.walk_pos[2], std::max(0.0, R2CapS()));
  }

  // Item 6 for one guard: claim round 2's blind-combat search of the kill he saw, then walk him to the body.
  void R3HWitnessTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, R3HState &s, float now, bool in_combat, bool in_search)
  {
    if (!REXCVAR_GET(pdz_stealth_witness_walk))
    {
      s.walk_kill = -1;
      s.take_over = false;
      return;
    }
    R5BKillSync(base, now);               // (round 5 B: Jo's place and shot of a new kill, taken once)
    R3HFirstReaction(base, c, g, s, now); // (round 3b, fix 3)
    // The claim: round 2's blind combat chose "the kill he saw" (R2BlindCombat) and its hand-over has not run yet.
    if (g.r2_blind_at > -1e8f && now >= g.r2_blind_at && now - g.r2_blind_at < 1.0f && g.r2_blind_where &&
        std::strcmp(g.r2_blind_where, "the kill he saw") == 0 && !g.dead && LayerGuard(g) && !g.scientist)
    {
      int ki = -1;
      for (size_t i = g_kills.size(); i-- > 0;)
      {
        if (Dist3(g_kills[i].pos, g.r2_witness_pos) < 1.0f)
        {
          ki = int(i);
          break;
        }
      }
      const bool on_way = ki >= 0 && (s.walk_kill == ki || (s.inv_item == 6 && s.inv_kill == ki));
      const bool before = ki >= 0 && std::find(s.walked.begin(), s.walked.end(), ki) != s.walked.end();
      const char *stay = R3HStays(base, c, g);
      if (ki < 0 || stay || (before && !on_way))
      {
        if (R2Log())
        {
          REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} not sent to the body: {}: round 2's search hand-over stays", Id(base, c),
                      ki < 0 ? "the kill is not on record" : stay ? stay : "his walk to this body is over (once per body)");
        }
      }
      else
      {
        g.r2_blind_at = -1e9f; // (round 2's hand-over, Rare's SEARCH at the kill pad, is not made: the walk below is)
        if (on_way)
        {
          if (R2Log())
          {
            REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} blind combat again for the same body: his walk to it is due or runs "
                        "already: no new one",
                        Id(base, c));
          }
        }
        else
        {
          s.walk_kill = ki;
          s.walk_from = now;
          s.walk_try = -1e9f;
          s.walk_pad = g.r2_blind_pad;
          std::memcpy(s.walk_pos, g_kills[size_t(ki)].pos, sizeof(s.walk_pos));
          s.walked.push_back(ki);
          s.drop_logged = false;
          s.take_over = false;
          s.blind_logged = false;
          REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} saw Jo kill {} and Rare's COMBAT began blind: round 2's search of the kill "
                      "is taken over: when his COMBAT is over he walks to the body at ({:.0f} {:.0f} {:.0f}) and looks round there "
                      "(our noise check's legs, {:.0f} s at most); until then Rare's search on the spot is not started",
                      Id(base, c), g_kills[size_t(ki)].who, s.walk_pos[0], s.walk_pos[1], s.walk_pos[2], std::max(0.0, R2CapS()));
        }
      }
    }
    if (s.walk_kill < 0)
    {
      return;
    }
    // Round 3b (fix 3): while his walk is due, no hand-over of round 2's blind combat (any place) is made on top of it.
    if (g.r2_blind_at > -1e8f)
    {
      g.r2_blind_at = -1e9f;
      if (!s.blind_logged)
      {
        s.blind_logged = true;
        REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} a blind COMBAT while his walk to the body is due: round 2's search hand-over is "
                    "not made (the walk is)",
                    Id(base, c));
      }
    }
    // Waiting for his COMBAT to end (8 s at most), or for Rare's own SEARCH to be taken over (round 3b: no longer an end
    // of the walk; R3BTakeOver ends it in the SEARCH update hook once he stands; the search cap at most), then the walk.
    bool top_is_search = false;
    const bool rare_search = !in_combat && (in_search || (Load32(base, c + kConfig) & kSearching) != 0 ||
                                            StackSearches(base, c, top_is_search) > 0);
    const float wait_s = in_combat ? 8.0f : std::max(8.0f, float(R2CapS()));
    const char *off = g.dead || g.st == St::Dead ? "he is dead"
                      : R2Seen(g, now, 0.5f)    ? "he sees her now (or she hit him): Rare's combat"
                      : AlarmOn(base)           ? "the alarm sounds: Rare's"
                      : now - s.walk_from > wait_s
                          ? (in_combat     ? "Rare kept him in COMBAT for 8 s"
                             : rare_search ? "Rare's SEARCH could not be taken over (he never stood still): Rare's, capped"
                                           : "his task stack stayed busy")
                          : nullptr;
    if (off)
    {
      if (!g.dead && g.st != St::Dead)
      {
        REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} no walk to the body: {} ({:.0f} s after it was claimed)", Id(base, c), off,
                    now - s.walk_from);
      }
      s.walk_kill = -1;
      s.take_over = false;
      return;
    }
    if (rare_search)
    {
      if (!s.take_over)
      {
        s.take_over = true;
        REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} Rare has him in a SEARCH of his own: it is TAKEN OVER (ended by Rare's own end of "
                    "search as soon as he stands or is at its pad, never early in a walk), then his walk to the body; no second "
                    "search is stacked | {}",
                    Id(base, c), StackText(base, c));
      }
      return;
    }
    if (in_combat || now - s.walk_try < 0.3f || R2TopClass(base, c) != 0x82092A8Cu)
    {
      return; // (handed over only in place of Rare's DEFAULT script, as Rare starts a SEARCH)
    }
    s.walk_try = now;
    R5BShotFrom(base, c, g, s.walk_kill, s.walk_pos, now); // (round 5 B: shot dead beside him: towards the shooter)
    uint32_t pad = R3HSpotPad(base, c, s.walk_pos);
    if (!Ptr(pad))
    {
      pad = s.walk_pad;
    }
    const int ki = s.walk_kill;
    const std::string who = size_t(ki) < g_kills.size() ? g_kills[size_t(ki)].who : std::string("?");
    if (Ptr(pad) && R3HGoLook(ctx, base, c, g, s, s.walk_pos, pad, 6, now,
                              fmt::format("witness walk (pdz_stealth_witness_walk): he saw Jo kill {}: he goes to the body", who)))
    {
      float me[3], pp[3] = {};
      PosOf(base, c, me);
      PadPos(base, pad, pp);
      s.inv_kill = ki;
      s.walk_kill = -1;
      s.take_over = false;
      s.react_at = now; // (no item 1 reaction on top of it)
      PosOf(base, c, s.check_pos);
      s.check_at = now + 3.0f;
      // Round 3b (fix 3): the 25 s cap counts from his first reaction (the claim), not from the hand-over.
      const double cap = R2CapS();
      if (cap > 0.0)
      {
        s.inv_from = s.walk_from;
        g.inv_until = std::min(g.inv_until, s.walk_from + float(cap));
      }
      REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} WALKS TO THE BODY of {}: our noise check's first leg, the pad {:08X} at ({:.0f} "
                  "{:.0f} {:.0f}) beside it ({:.0f} cm from him), {:.1f} s after the kill was claimed; the cap ends it {:.0f} s from "
                  "now ({:.0f} s after his first reaction) | {}",
                  Id(base, c), who, pad, pp[0], pp[1], pp[2], Dist3(me, pp), now - s.walk_from,
                  cap > 0.0 ? std::max(0.0f, s.walk_from + float(cap) - now) : 180.0f, std::max(0.0, cap), StackText(base, c));
    }
  }

  // Once a frame per tracked guard (After, before R2Tick), pdz_lab_rareplus on.
  void R3HTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
  {
    R3HState &s = R3HOf(c, g);
    // Ours running: the cap, and its end (L's give-up line). (Before item 6: a blind COMBAT ends a walk of ours.)
    if (s.inv_item)
    {
      if (g.st != St::Investigating || g.inv != Inv::Noise)
      {
        const bool calm = !g.dead && g.st != St::Dead && (g.st == St::Wary || g.st == St::Unaware) && !in_combat;
        REXLOG_INFO("pdz_stealth_r3: {}: {}'s {} is over after {:.0f} s (state {}){}", s.inv_item == 6 ? "WITNESS WALK" : "HEARD REACT",
                    Id(base, c), s.inv_item == 6 ? "walk to the body" : "look at where the sound came from", now - s.inv_from,
                    StName(g.st), calm ? ": his give-up line (R3EndLine)" : "");
        // Round 3c (helper C, fix 4b): a witness who went to look first: his walk is over: he calls it in now (true: his
        // radio call / alarm run is released, so no give-up line).
        if (calm && !(s.inv_item == 6 && R3cCWalkOver(ctx, base, c, g, now, s.inv_kill)))
        {
          R3EndLine(ctx, base, c, g, now,
                    s.inv_item == 6 ? "witness walk: he looked at the body, nothing found" : "heard a sound, went to look: nothing found");
        }
        s.inv_item = 0;
        s.inv_kill = -1;
      }
      else if (const double cap = R2CapS(); cap > 0.0 && now - s.inv_from >= float(cap))
      {
        g.inv_until = std::min(g.inv_until, now); // (the look's own end at his arrival can lie past the cap)
        g.inv_end_leg = true;
        if (!s.cap_logged)
        {
          s.cap_logged = true;
          REXLOG_INFO("pdz_stealth_r3: {}: {} {:.0f} s cap (pdz_stealth_search_cap_s): his leg ends now",
                      s.inv_item == 6 ? "WITNESS WALK" : "HEARD REACT", Id(base, c), cap);
        }
      }
    }
    // Item 6.
    R3HWitnessTick(ctx, base, c, g, s, now, in_combat, in_search);
    if (s.look_from > -1e8f && (now < s.look_from || now - s.look_from >= float(REXCVAR_GET(pdz_lab_stealth_alert_look_s)) + 0.5f) &&
        !in_search)
    {
      if (!in_combat && !g.dead && (g.st == St::Wary || g.st == St::Unaware))
      {
        R3EndLine(ctx, base, c, g, now, "heard a sound, looked round where he stood: nothing found");
      }
      s.look_from = -1e9f;
    }
    if (s.check_at > -1e8f && now >= s.check_at)
    {
      s.check_at = -1e9f;
      float me[3];
      PosOf(base, c, me);
      const float dx = me[0] - s.check_pos[0], dz = me[2] - s.check_pos[2];
      REXLOG_INFO("pdz_stealth_r3: HEARD REACT: {} 3 s on: moved {:.0f} cm on the ground plan, height {:+.0f} cm (a seated guard "
                  "who stands up rises), state {} | {}",
                  Id(base, c), std::sqrt(dx * dx + dz * dz), me[1] - s.check_pos[1], StName(g.st), StackText(base, c));
    }
    // Item 1.
    if (REXCVAR_GET(pdz_stealth_heard_react))
    {
      // Explosions (the blasts on record), checked here: Rare's hearing loop may not list them as sound events.
      if (!g_blasts.empty() && (g.st == St::Unaware || g.st == St::Wary) && !in_combat && !g.dead && LayerGuard(g) && !g.scientist)
      {
        uint32_t index = 0;
        float me[3];
        PosOf(base, c, me);
        for (const Blast &b : g_blasts)
        {
          ++index;
          if (b.at <= s.blast_seen || now < b.at || now - b.at > 1.0f)
          {
            continue;
          }
          s.blast_seen = b.at;
          const float d = Dist3(me, b.pos);
          if (d < b.radius && !SoundBlocked(ctx, base, c, 0xB1A50000u + index, b.pos, 0, now, nullptr, b.radius))
          {
            const auto h = g_heard.find((uint64_t(c) << 32) | (0xB1A50000u + index));
            R3HConsider(s, kR3HBlast, 0xB1A50000u + index, b.pos, d, h != g_heard.end() ? h->second.walls : -1, now);
          }
        }
      }
      if (s.wait.at > -1e8f)
      {
        const float t = now - s.wait.at;
        const char *rare = s.rare_at >= s.wait.at - 0.2f ? "a stimulus word of Rare's (heard / alerted / hit)"
                           : in_combat                     ? "Rare's COMBAT"
                           : in_search || (Load32(base, c + kConfig) & kSearching) ? "a search of Rare's"
                           : Looking(g, now)               ? "a look round where he stands"
                           : g.st == St::Investigating     ? "a check of ours (a lamp, a sound)"
                           : g.run_pending                 ? "his alarm shout"
                                                           : nullptr;
        if (rare || g.dead || g.st == St::Dead || t < 0.0f || t > 3.0f || !(g.st == St::Unaware || g.st == St::Wary) ||
            AlarmOn(base) || s.inv_item)
        {
          if (rare && R2Log() && (now - s.rare_log > 5.0f || now < s.rare_log))
          {
            s.rare_log = now;
            REXLOG_INFO("pdz_stealth_r3: HEARD REACT: {} heard {} {:.0f} cm away: a reaction came ({}): nothing of ours",
                        Id(base, c), R3HKindName(s.wait.kind), s.wait.d, rare);
          }
          s.wait.at = -1e9f;
        }
        else if (t >= kR3HWait)
        {
          R3HReact(ctx, base, c, g, s, now);
        }
      }
    }
    else
    {
      s.wait.at = -1e9f;
    }
    // Item 8.
    if (REXCVAR_GET(pdz_stealth_shout_heard))
    {
      R3HShoutScan(base, now);
      R3HShoutTick(ctx, base, c, g, s, now, in_combat, in_search);
    }
  }
