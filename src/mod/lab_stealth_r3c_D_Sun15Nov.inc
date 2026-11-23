  // Stealth layer round 3c, Sun 15 Nov night, helper D (fixes 5 and 6). Included by lab_stealth.cpp inside its anonymous
  // namespace, after lab_stealth_r3b_Sun15Nov.inc. Log prefix "pdz_stealth_r3c:".
  //
  // Fix 5, THE MANSION'S RADIO REINFORCEMENTS UNDER THE LAYER (pdz_stealth_mansion_reinforcements, Mansion only).
  // What Rare does (read from the TU3 image): a completed radio call (the message task's run sub_821A6D70 -> the level's
  // OnCommand 823326B0 type 7) counts a detection and starts a wave; the Mansion's level update sub_8232EE10 then makes
  // the clones of the templates s4s1 / s4s2 (wave 1) and s5s1 / s5s2 (wave 2) with
  // sub_82142AD8, one a second, sets their squad mode (8216E9A8 / 8216EA38), and puts each one straight on the agent:
  // sub_8252D8D8(clone, player one, 1) (calls at 8232FFA4 and 8233023C; it fills his target holders +0x3158 / +0x3168 /
  // +0x3178 and the byte +0x3188). That forced target is how they know where Jo is: they run at her and Rare's own
  // (unmetered) sight does the rest (hand test 20:45:14-18: spawned 75 m off, firing within 4 s).
  // What the layer does with the switch on, for the reinforcements of a GUARD's completed call:
  //   - where the call came from is noted when the call starts (R3cDCallStarted, from the hook of sub_821D0748): the
  //     place the caller last saw Jo if he had a real sighting in the last 10 s; else the body that made him call (the
  //     body he found, Rare's 0040, or the kill he witnessed, within the last 15 s); else the caller's own spot. It
  //     becomes the wave's place when Rare's message is sent (R3cDCallCompleted, from the hook of sub_821A6D70).
  //     Rare's message, its counter (a detection) and the wave itself are untouched.
  //   - the clones are under the layer (R3cDReinforcement, asked by DecideMember before not_ids): the meter, the
  //     layer's hearing, no squad knowledge of Jo's place.
  //   - Rare's "set on the agent" call is not made for them (the hook of sub_8252D8D8, only for calls from the
  //     Mansion's level update): no target, no knowledge of Jo.
  //   - each one starts wary and alert (his meter fills faster), and is walked to the place of the call with our noise
  //     check (Rare's SEARCH at the pad there, handed over in place of his DEFAULT script as Rare starts one): the
  //     first to the place itself, the others to spread pads of a sweep plan round it (PlanSweep: the routes out of the
  //     place). Then each checks further spread spots of that plan (pdz_stealth_mansion_reinforcements_spots), for at
  //     most pdz_stealth_mansion_reinforcements_search_s after his first arrival, and stays where he ends, wary (that
  //     spot is his post from then on), with his give-up line. A real sighting (his meter filled: Rare's valid target),
  //     a hit, or the alarm ends our part at once: Rare's combat.
  //   - a forced detection with no caller (the intercom's, a wrong tower's: message byte 3 not 0, or no call on
  //     record) keeps Rare's reinforcements exactly as Rare sends them.
  // Fix 6, THE TOWER GUARDS' SEARCH RESTARTS: see R3cDTowerRestart below.

  constexpr const char *kR3cDIds = "s4s1,s4s2,s5s1,s5s2"; // the templates' setup ids (their clones carry them)
  constexpr uint32_t kR3cDUpdateFrom = 0x8232EE10;        // the Mansion's level update (the two set-on-agent calls)
  constexpr uint32_t kR3cDUpdateTo = 0x823326B0;          // ... up to the level's OnCommand
  constexpr float kR3cDWalkS = 130.0f;                    // s: the walk to the place of the call may take this long
  constexpr float kR3cDGiveUpS = 220.0f;                  // s: never there after this long: he gives up
  // Follow-up 1 (smoke run r3c_mansion1, 23:11:41-23:14:16, s4s1): the walk's own watch, in place of the layer's
  // 2 s no-progress rule (which cut him as he paused at the front gate, 23:12:09; every new SEARCH then took another
  // way round and he went up and down the terrace).
  // Follow-up 2 (runs r3c_mansion1b / r3c_mansion2): the watch was too tight. s4s1's way out of the east yard goes
  // AWAY from the place for 30 s (north round the yard, then out by the front gate): "no nearer for 35 s" cut him when
  // he was through the gate and on his way (00:33:15, 254 cm nearer than at the start). Now: 1 m steps, 90 s.
  constexpr float kR3cDStoodS = 12.0f;    // s without moving 1 m: the leg ends
  constexpr float kR3cDNoNearerS = 90.0f; // s without coming 1 m nearer the pad (straight line): the leg ends
  constexpr int kR3cDFails = 3;           // legs ended by that watch, then he gives up and goes back
  constexpr int kR3cDWalkTries = 6;                       // legs of that walk (a leg that ends short of it is walked on)

  struct R3cDOrigin
  {
    bool valid = false;
    float at = -1e9f;    // the AI clock (call start, then the message)
    float pos[3] = {};
    uint32_t pad = 0;    // a pad beside it, reachable from the caller's own pad
    uint32_t caller = 0;
    std::string who;
    const char *why = "";
    int sent = 0;        // reinforcements sent to it
  };
  struct R3cDState
  {
    int stage = 0;       // 0 new, 1 to be sent, 2 a leg of ours runs, 8 Rare's (saw her / the alarm), 9 over
    std::string id;
    int index = 0;       // his number in the wave
    float from = -1e9f, try_at = -1e9f, wait_log = -1e9f;
    uint32_t pad = 0;    // the pad of his running (or next) leg
    bool first = true;   // ... it is his walk to the place of the call
    bool arrived = false; // ... he reached it
    int tries = 0;       // legs of the walk so far
    int spots = 0;       // spots checked after it
    float arrived_at = -1e9f;
    std::vector<uint32_t> swept;
    bool cut_asked = false;
    // Follow-up 1: the walk's watch.
    float best_d = 1e9f, best_at = -1e9f; // his nearest to the pad so far (3 m steps), and when
    float mv_pos[3] = {};                 // where he last moved 1 m from, and when
    float mv_at = -1e9f;
    const char *watch = nullptr;          // the watch ended this leg: why
    bool watch_out = false;               // ... he left the grounds
    int fails = 0;
    // Follow-up 2: a clone Rare's SEARCH does not walk (s4s1#2, the third searcher of the squad, stood on his spawn pad
    // through two legs in both runs): his walk is then Rare's plain go-to-pad task (WalkToPad), which no search rule
    // of Rare's can hold back.
    float start[3] = {};    // where this leg began
    bool plain = true;      // his walk to the place is Rare's plain go-to-pad walk (follow-up 3: from the first send;
                            // Rare's SEARCH walk took s4s1 over 160 s and then away again, run r3c_mansion1c)
    bool plain_failed = false; // ... it did not move him once: Rare's SEARCH walk is tried instead
    uint32_t walk_task = 0; // ... its task (stage 3)
  };
  std::unordered_map<uint32_t, R3cDOrigin> g_r3cd_started; // caller -> the place noted at his call's start
  R3cDOrigin g_r3cd_origin;                                // the place of the last completed call
  Hunt g_r3cd_plan;                                        // the sweep plan round it
  std::unordered_map<uint32_t, R3cDState> g_r3cd;          // the reinforcements of ours
  std::string g_r3cd_test_done;                            // pdz_test_stealth_radio
  uint32_t g_r3cd_test_c = 0;
  float g_r3cd_test_until = -1e9f;
  std::unordered_map<uint32_t, float> g_r3cd_tower_log;    // fix 6: one line per tower guard and 30 s
  std::unordered_map<uint32_t, std::array<float, 4>> g_r3cd_tower_ref; // ... his place and yaw at the first restart

  // Helper A's R3cClearLayerMemory calls this at every level start / restart / reload.
  void R3cDReset()
  {
    g_r3cd_started.clear();
    g_r3cd_origin = R3cDOrigin();
    g_r3cd_plan = Hunt();
    g_r3cd.clear();
    g_r3cd_test_c = 0;
    g_r3cd_test_until = -1e9f;
    g_r3cd_tower_log.clear();
    g_r3cd_tower_ref.clear();
    // (g_r3cd_test_done is kept: a test tool value is applied once per new value, as the other tools' are)
  }

  bool R3cDOn()
  {
    return g_level && g_level->sw == LevelSwitch::kMansion && RarePlus() && REXCVAR_GET(pdz_stealth_mansion_reinforcements);
  }

  // DecideMember's question (before not_ids): a radio reinforcement of a guard's completed call is under the layer.
  bool R3cDReinforcement(const StealthLevel &l, const std::string &id)
  {
    return l.sw == LevelSwitch::kMansion && g_level == &l && g_r3cd_origin.valid && R3cDOn() && CsvHasText(kR3cDIds, id);
  }

  // From the hook of sub_821D0748, as a guard's radio call starts: where it comes from, and why.
  void R3cDCallStarted(const uint8_t *base, uint32_t c, float now)
  {
    if (!R3cDOn() || !Ptr(c))
    {
      return;
    }
    R3cDOrigin o;
    o.valid = true;
    o.at = now;
    o.caller = c;
    o.who = Id(base, c);
    PosOf(base, c, o.pos);
    o.pad = Load32(base, c + kPad);
    o.why = "the caller's spot (he did not see Jo, no body on record for his call)";
    const uint32_t own = o.pad;
    const Guard *g = Find(c);
    const uint32_t jo = Load32(base, kPlayerOne);
    float spot[3] = {};
    bool have = false;
    if (g && Ptr(jo) && (R2Seen(*g, now, 10.0f) || (Load32(base, c + kSight) & kValidTarget) != 0))
    {
      // (a real sighting, or her hit: the last place he knew her at; Jo's place now while he still sees her)
      if ((Load32(base, c + kSight) & kValidTarget) != 0 || (now >= g->last_visible && now - g->last_visible < 2.0f))
      {
        PosOf(base, jo, spot);
      }
      else if (now >= g->glimpse_time && now - g->glimpse_time < 10.0f)
      {
        std::memcpy(spot, g->glimpse_pos, sizeof(spot));
      }
      else
      {
        PosOf(base, jo, spot);
      }
      have = true;
      o.why = "Jo's last seen place (the caller saw her)";
    }
    if (!have && g)
    {
      // The body that made him call: the kill he witnessed (round 2 / round 3's records) or the body he found (round
      // 3b's record of Rare's 0040), within the last 15 s.
      uint32_t victim = 0;
      if (now >= g->r2_witness_at && now - g->r2_witness_at < 15.0f)
      {
        std::memcpy(spot, g->r2_witness_pos, sizeof(spot));
        have = true;
      }
      else if (g->r3_once_victim && g->r3_once_has_pos && now >= g->r3_once_at && now - g->r3_once_at < 15.0f)
      {
        std::memcpy(spot, g->r3_once_pos, sizeof(spot));
        have = true;
      }
      else if (const auto it = g_r3b.find(c); it != g_r3b.end() && it->second.born == g->r2_born &&
                                              now >= it->second.first_at && now - it->second.first_at < 15.0f &&
                                              uint32_t(it->second.first_key >> 32) == kR3BCauseBody)
      {
        victim = uint32_t(it->second.first_key);
      }
      if (!have)
      {
        // (a body he found: its place from the kill record; any body of Jo's within 15 m of him when no key is known
        // and Rare's SEE_BODY was his last line)
        const bool see_body = g->last_event == kEvSeeBody && now >= g->last_event_time && now - g->last_event_time < 15.0f;
        for (size_t i = g_kills.size(); !have && i-- > 0;)
        {
          const Kill &k = g_kills[i];
          if ((victim && k.victim == victim) || (!victim && see_body && Dist3(k.pos, o.pos) < 1500.0f))
          {
            std::memcpy(spot, k.pos, sizeof(spot));
            have = true;
          }
        }
      }
      if (have)
      {
        o.why = "the body that made him call";
      }
    }
    if (have && Ptr(own))
    {
      float d = 0.0f;
      uint32_t pad = FloorPadAt(base, own, spot, 800.0f, d);
      if (!Ptr(pad))
      {
        pad = NearestPadTo(base, own, spot, d);
      }
      if (Ptr(pad) && d < 1500.0f)
      {
        o.pad = pad;
        std::memcpy(o.pos, spot, sizeof(spot));
      }
      else
      {
        o.why = "the caller's spot (no pad he can reach beside the place he calls about)";
      }
    }
    if (g_r3cd_started.size() > 32)
    {
      g_r3cd_started.clear();
    }
    g_r3cd_started[c] = o;
    REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {}'s radio call starts: if it is sent, its reinforcements go to ({:.0f} {:.0f} "
                "{:.0f}), pad {:08X}: {}",
                o.who, o.pos[0], o.pos[1], o.pos[2], o.pad, o.why);
  }

  // From the hook of sub_821A6D70, after Rare's message: `caller` is the guard whose call it was (0: none on record),
  // `forced` the message's fourth byte (not 0: the intercom's forced detection).
  void R3cDCallCompleted(const uint8_t *base, uint32_t caller, uint8_t forced, float now)
  {
    (void)base;
    if (!R3cDOn())
    {
      return;
    }
    const auto it = g_r3cd_started.find(caller);
    if (!caller || forced != 0 || it == g_r3cd_started.end())
    {
      g_r3cd_origin = R3cDOrigin();
      REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: a detection with no guard's call on record ({}): its reinforcements are "
                  "Rare's, sent at Jo as Rare sends them",
                  forced != 0 ? "a forced detection: the message's fourth byte is set" : "no caller");
      return;
    }
    g_r3cd_origin = it->second;
    g_r3cd_origin.at = now;
    g_r3cd_origin.sent = 0;
    g_r3cd_started.erase(it);
    g_r3cd_plan = Hunt(); // (planned when the first of them is sent)
    REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {}'s call is sent (Rare's detection counts): the reinforcements Rare makes now "
                "are under the layer and go to ({:.0f} {:.0f} {:.0f}), pad {:08X}: {}; they are not told where Jo is",
                g_r3cd_origin.who, g_r3cd_origin.pos[0], g_r3cd_origin.pos[1], g_r3cd_origin.pos[2], g_r3cd_origin.pad,
                g_r3cd_origin.why);
  }

  // From the hook of sub_8252D8D8 (Rare's "set this character on that target"): true = the call is not made (a radio
  // reinforcement of ours, set on the agent by the Mansion's level update).
  bool R3cDNoTarget(const uint8_t *base, uint32_t c, uint32_t target, uint32_t caller)
  {
    if (caller < kR3cDUpdateFrom || caller >= kR3cDUpdateTo || !R3cDOn() || !g_r3cd_origin.valid || !Ptr(c))
    {
      return false;
    }
    const std::string id = BaseId(Id(base, c));
    if (!CsvHasText(kR3cDIds, id) || !InLayer(base, c))
    {
      return false;
    }
    if (g_r3cd.size() > 64)
    {
      g_r3cd.clear();
    }
    R3cDState s;
    s.id = id;
    s.index = g_r3cd_origin.sent++;
    g_r3cd[c] = s;
    float me[3];
    PosOf(base, c, me);
    REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} spawned at ({:.0f} {:.0f} {:.0f}) for {}'s call (number {} of the wave): under "
                "the layer; Rare's \"set on the agent\" (sub_8252D8D8 from {:08X}, target {:08X}) is NOT made: he is not told "
                "where Jo is; he goes to ({:.0f} {:.0f} {:.0f}), {:.0f} cm away: {}",
                Id(base, c), me[0], me[1], me[2], g_r3cd_origin.who, s.index + 1, caller, target, g_r3cd_origin.pos[0],
                g_r3cd_origin.pos[1], g_r3cd_origin.pos[2], Dist3(me, g_r3cd_origin.pos), g_r3cd_origin.why);
    return true;
  }

  // RarePlusTick's question: the noise check running for him is a leg of ours.
  bool R3cDInvOurs(const Guard &g, uint32_t c)
  {
    if (g.st != St::Investigating || g.inv != Inv::Noise)
    {
      return false;
    }
    const auto it = g_r3cd.find(c);
    return it != g_r3cd.end() && it->second.stage == 2;
  }

  // The SEARCH update hook's question: his leg is the long walk to the place of the call (no 30 s limit on it).
  bool R3cDLongLeg(uint32_t c)
  {
    const auto it = g_r3cd.find(c);
    return it != g_r3cd.end() && it->second.stage == 2 && it->second.first;
  }

  // His next pad: the plan's pad farthest from the pads the other reinforcements are on (the place itself first).
  uint32_t R3cDPick(const uint8_t *base, uint32_t c, const R3cDState &s)
  {
    uint32_t best = 0;
    float best_gap = -1.0f;
    for (size_t i = 0; i < g_r3cd_plan.pads.size(); ++i)
    {
      const uint32_t pad = g_r3cd_plan.pads[i];
      float p[3];
      if (!PadPos(base, pad, p) || std::find(s.swept.begin(), s.swept.end(), pad) != s.swept.end())
      {
        continue;
      }
      // Follow-up 1: a spot must be on the place's floor and have a way BACK to the place's pad over open links (the
      // plan follows links outwards only: a drop or a one-way link would strand him).
      float cp[3];
      if (i > 0 && PadPos(base, g_r3cd_plan.centre, cp))
      {
        bool back = false;
        for (const auto &[q, hop] : PadsAround(base, pad, 4, 96))
        {
          back = back || q == g_r3cd_plan.centre;
        }
        if (!back || std::fabs(p[1] - cp[1]) > 150.0f)
        {
          continue;
        }
      }
      float gap = 1e7f - 1000.0f * float(i); // (nobody else: the plan's order, the place itself first)
      for (const auto &[o, os] : g_r3cd)
      {
        float q[3];
        if (o != c && (os.stage == 1 || os.stage == 2) && PadPos(base, os.pad, q))
        {
          gap = std::min(gap, Dist3(p, q));
        }
      }
      if (gap > best_gap)
      {
        best = pad;
        best_gap = gap;
      }
    }
    return best;
  }

  // Follow-up 2: a pad of his own beside the place for the second and third of them: reached from the place's pad
  // within 3 open links, on its floor (150 cm), with a way back to it, 2-12 m from it, 3 m from every pad another
  // reinforcement is on or going to; the nearest such pad. 0 = none (he takes the place's own pad).
  uint32_t R3cDOwnPad(const uint8_t *base, uint32_t c)
  {
    const uint32_t centre = g_r3cd_origin.pad;
    float cp[3];
    if (!Ptr(centre) || !PadPos(base, centre, cp))
    {
      return 0;
    }
    uint32_t best = 0;
    float best_d = 1e9f;
    for (const auto &[pad, hop] : PadsAround(base, centre, 3, 64))
    {
      float p[3];
      if (pad == centre || !PadPos(base, pad, p) || std::fabs(p[1] - cp[1]) > 150.0f)
      {
        continue;
      }
      const float d = Dist3(p, cp);
      if (d < 200.0f || d > 1200.0f || d >= best_d)
      {
        continue;
      }
      bool taken = false;
      for (const auto &[o, os] : g_r3cd)
      {
        float q[3];
        taken = taken || (o != c && os.stage < 8 && PadPos(base, os.pad, q) && Dist3(p, q) < 300.0f);
      }
      if (taken)
      {
        continue;
      }
      bool back = false;
      for (const auto &[q, h] : PadsAround(base, pad, 4, 96))
      {
        back = back || q == centre;
      }
      if (back)
      {
        best = pad;
        best_d = d;
      }
    }
    return best;
  }

  // His part is over (`rare`: Rare's from here).
  void R3cDEnd(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, R3cDState &s, float now, const char *why, bool rare)
  {
    float me[3];
    PosOf(base, c, me);
    REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} {} ({:.0f} s after he spawned, {} spot(s) checked, at ({:.0f} {:.0f} {:.0f}), "
                "{:.0f} cm from the place of the call; state {}){}",
                Id(base, c), why, now - s.from, s.spots, me[0], me[1], me[2], Dist3(me, g_r3cd_origin.pos), StName(g.st),
                rare ? "" : ": he stays here, wary (this is his post now)");
    s.stage = rare ? 8 : 9;
    if (!rare && !g.dead && g.st != St::Dead)
    {
      // He has no post of his own (he was made at the spawn pad): where he ends is his post.
      std::memcpy(g.post_pos, me, sizeof(me));
      g.post_pad = Ptr(Load32(base, c + kPad)) ? Load32(base, c + kPad) : 0;
      g.post_due = -1e9f;
      R3EndLine(ctx, base, c, g, now, "reinforcement: searched round the place of the call, nothing found");
    }
  }

  // Once a frame per tracked guard (After, before R3HTick), pdz_lab_rareplus on.
  void R3cDTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
  {
    if (c == g_r3cd_test_c && now < g_r3cd_test_until && !g.dead)
    {
      Store32(base, c + kStimuli, Load32(base, c + kStimuli) | 0x800u); // pdz_test_stealth_radio
    }
    if (g_r3cd.empty())
    {
      return;
    }
    const auto it = g_r3cd.find(c);
    if (it == g_r3cd.end())
    {
      return;
    }
    R3cDState &s = it->second;
    if (s.stage >= 8)
    {
      return;
    }
    if (s.stage == 0)
    {
      if (BaseId(g.who) != s.id || !R3cDOn())
      {
        g_r3cd.erase(it); // (another character at this address, or the switch went off)
        return;
      }
      s.stage = 1;
      s.from = now;
      s.pad = g_r3cd_origin.pad;
      if (g.st == St::Unaware)
      {
        Set(base, c, g, St::Wary, now, "a radio reinforcement: he starts wary (he knows there was a call, not where Jo is)");
      }
      FillFaster(base, c, g, now, "is a radio reinforcement");
    }
    if (g.dead || g.st == St::Dead || !(LoadF(base, c + kHealth) > 0.0f))
    {
      s.stage = 9;
      return;
    }
    const bool sees = R2Seen(g, now, 0.5f) || (Load32(base, c + kSight) & kValidTarget) != 0;
    if (sees || AlarmOn(base) || !R3cDOn())
    {
      const uint32_t jo = Load32(base, kPlayerOne);
      if (sees)
      {
        REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} SAW JO BY HIS OWN SIGHT (or she hit him): meter {:.0f}, {:.0f} cm from her, "
                    "{:.0f} s after he spawned",
                    Id(base, c), g.meter, Ptr(jo) ? Dist(base, c, jo) : -1.0f, now - s.from);
      }
      R3cDEnd(ctx, base, c, g, s, now, sees ? "is in Rare's combat from here" : "is Rare's from here (the alarm, or the switch is off)",
              true);
      return;
    }
    if (g.alert_until - now < 5.0f)
    {
      FillFaster(base, c, g, now, "is a radio reinforcement, searching"); // (alert for the whole of his search)
    }
    if (s.stage == 3)
    {
      // Follow-up 2: his plain walk to the pad runs (he is wary, on Rare's go-to-pad task): the same watch.
      g.post_due = -1e9f;
      g.r2_blind_at = -1e9f; // (follow-up 3: no round 2 search "where he stands" on the way)
      float me[3], pp[3] = {};
      PosOf(base, c, me);
      PadPos(base, s.pad, pp);
      const float d = Dist3(me, pp);
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
      const bool live = TaskLive(base, c, s.walk_task);
      s.watch_out = g_level && g_level->jo_hold_boxes[0] && !InJoBoxes(g_level->jo_hold_boxes, me);
      s.watch = s.watch_out                           ? "he has left the grounds (the layer's zone)"
                : now - s.mv_at > kR3cDStoodS         ? "he has stood still for 12 s"
                : now - s.best_at > kR3cDNoNearerS    ? "he has come no nearer to it for 90 s"
                                                      : nullptr;
      if (live && d >= 300.0f && !s.watch)
      {
        return;
      }
      if (d < 400.0f)
      {
        REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} ARRIVED at his first spot by the plain walk (pad {:08X}, {:.0f} cm from the "
                    "place of the call) {:.0f} s after he spawned: searching round it; Jo is found only by his own sight and "
                    "hearing",
                    Id(base, c), s.pad, Dist3(me, g_r3cd_origin.pos), now - s.from);
      }
      else
      {
        REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} his plain walk to pad {:08X} is over {:.0f} cm short of it ({}; at ({:.0f} "
                    "{:.0f} {:.0f}), nearest so far {:.0f} cm)",
                    Id(base, c), s.pad, d, s.watch ? s.watch : "Rare's walk ended", me[0], me[1], me[2], s.best_d);
        // Follow-up 3. A walk that ended by itself short of the pad (Rare took it off him: a look round, a reaction of
        // his to a sound on the way) is not a failed leg: he is sent again (kR3cDWalkTries). A walk still on his stack
        // that does not move him is taken off him as the layer's walk back to a post does it: a short look round where
        // he stands (Rare's SEARCH hand-over swaps the top task), ended by the look's own rule; a failed leg, unless he
        // never left the spot: then his next send is Rare's SEARCH walk for once (the fallback the other way round).
        if (live)
        {
          const uint32_t own = Load32(base, c + kPad);
          const bool look = Ptr(own) && GiveSearch(ctx, base, c, own);
          if (look)
          {
            g.look_started = now;
            g.look_pad = own;
          }
          s.watch = s.watch ? s.watch : "Rare's walk does not move him";
          if (!s.plain_failed && Dist3(me, s.start) < 150.0f)
          {
            s.plain_failed = true;
            s.plain = false;
            s.watch = nullptr;
          }
          REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} the plain walk is still on his stack and does not move him: {}; {}",
                      Id(base, c), look ? "a short look round where he stands takes it off him" : "no look round could be handed over",
                      s.plain ? "a failed leg" : "he never left the spot: his next send is Rare's SEARCH walk");
          if (!look && s.plain)
          {
            s.fails = kR3cDFails; // (it cannot be taken off: no second walk on top of it: the give-up)
          }
        }
      }
      s.walk_task = 0;
      s.stage = 2; // (the leg-over rules below: arrived, walks on, or gives up)
    }
    if (s.stage == 2)
    {
      if (g.st == St::Investigating && g.inv == Inv::Noise)
      {
        if (g.inv_arrived > -1e8f && !s.arrived)
        {
          s.arrived = true;
          if (s.first)
          {
            s.arrived_at = now;
            float me[3];
            PosOf(base, c, me);
            REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} ARRIVED at his first spot (pad {:08X}, {:.0f} cm from the place of the "
                        "call) {:.0f} s after he spawned: searching round it for up to {:.0f} s; Jo is found only by his own sight "
                        "and hearing",
                        Id(base, c), s.pad, Dist3(me, g_r3cd_origin.pos), now - s.from,
                        float(REXCVAR_GET(pdz_stealth_mansion_reinforcements_search_s)));
          }
        }
        // Follow-up 1: the watch on his leg (his own pace and pauses are Rare's: a gate, a turn).
        float me[3], pp[3] = {};
        PosOf(base, c, me);
        if (!s.watch && !s.arrived && PadPos(base, s.pad, pp))
        {
          const float d = Dist3(me, pp);
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
          s.watch_out = g_level && g_level->jo_hold_boxes[0] && !InJoBoxes(g_level->jo_hold_boxes, me);
          s.watch = s.watch_out                                     ? "he has left the grounds (the layer's zone)"
                    : now - s.mv_at > kR3cDStoodS                   ? "he has stood still for 12 s"
                    : s.first && now - s.best_at > kR3cDNoNearerS   ? "he has come no nearer to it for 90 s"
                                                                    : nullptr;
          if (s.watch)
          {
            g.inv_until = std::min(g.inv_until, now); // (the leg's own end: Rare's end of search, never early in a walk)
            g.inv_end_leg = true;
            REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} his walk to pad {:08X} is ended: {} (at ({:.0f} {:.0f} {:.0f}), {:.0f} cm "
                        "from it, nearest so far {:.0f} cm)",
                        Id(base, c), s.pad, s.watch, me[0], me[1], me[2], d, s.best_d);
          }
        }
        return; // (the leg runs: InvTick, the SEARCH update hook)
      }
      // The leg is over (FinishInvestigation set him wary, due back at his post, with a give-up line): not yet.
      g.post_due = -1e9f;
      g.r3l_at = -1e9f;
      s.stage = 1;
      s.try_at = now;
      if (s.first && !s.arrived)
      {
        float me[3], pp[3] = {};
        PosOf(base, c, me);
        PadPos(base, s.pad, pp);
        if (Dist3(me, pp) < 400.0f)
        {
          s.arrived = true;
          s.arrived_at = now;
        }
        else if (s.watch && !s.watch_out && !s.plain && !s.plain_failed && Dist3(me, s.start) < 150.0f)
        {
          // Follow-up 2: Rare's SEARCH never walked him (he is where the leg began): the plain walk from here on. Not a
          // failed leg.
          s.plain = true;
          REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} Rare's SEARCH gave him no walk (he has not left ({:.0f} {:.0f} {:.0f}) in "
                      "{:.0f} s): he is sent with Rare's plain go-to-pad walk instead",
                      Id(base, c), me[0], me[1], me[2], kR3cDStoodS);
        }
        else if (s.watch_out || (s.watch && ++s.fails >= kR3cDFails) || ++s.tries >= kR3cDWalkTries || now - s.from > kR3cDGiveUpS)
        {
          // Follow-up 1: a clean give-up: back to where he was made (his post), wary; no more legs.
          REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} did not get to the place of the call ({}; {:.0f} s after he spawned, "
                      "{:.0f} cm from pad {:08X}): he GIVES UP and goes back to where he was made ({:.0f} {:.0f} {:.0f}), wary",
                      Id(base, c), s.watch ? s.watch : "his legs or his time are used up", now - s.from, Dist3(me, pp), s.pad,
                      g.post_pos[0], g.post_pos[1], g.post_pos[2]);
          s.stage = 9;
          g.post_due = now; // (PostTick walks him back to his post: the spawn pad)
          g.post_tries = 0;
          if (in_search || (Load32(base, c + kConfig) & kSearching) != 0)
          {
            g.end_search = true; // (a SEARCH of the leg still running: Rare's own end of search, then the walk back)
            g.end_search_at = now;
          }
          return;
        }
        else
        {
          REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} is not there yet ({:.0f} cm from pad {:08X}, leg {} of his walk over): he "
                      "walks on",
                      Id(base, c), Dist3(me, pp), s.pad, s.tries);
        }
      }
      if (s.arrived || !s.first) // (follow-up 1: a spot he did not reach is a spot done: never the same pad again)
      {
        if (s.first)
        {
          s.first = false;
        }
        ++s.spots;
        s.swept.push_back(s.pad);
        const int spots = std::max(1, int(REXCVAR_GET(pdz_stealth_mansion_reinforcements_spots)));
        const float secs = float(REXCVAR_GET(pdz_stealth_mansion_reinforcements_search_s));
        if (s.spots >= spots || now - s.arrived_at >= secs)
        {
          R3cDEnd(ctx, base, c, g, s, now, "search over, Jo not found", false);
          return;
        }
        s.pad = R3cDPick(base, c, s);
        s.arrived = false;
        if (!Ptr(s.pad))
        {
          R3cDEnd(ctx, base, c, g, s, now, "search over (no further spot round the place of the call), Jo not found", false);
          return;
        }
      }
    }
    // Stage 1: his next leg is handed over in place of his DEFAULT script, as Rare starts a SEARCH.
    g.r2_blind_at = -1e9f; // (round 2's hand-over "where he stands" is not made for him: ours is)
    if (now - s.from > kR3cDGiveUpS && s.first)
    {
      R3cDEnd(ctx, base, c, g, s, now, "could not be sent in time (Rare kept him busy)", false);
      return;
    }
    if (in_combat || now - s.try_at < 0.3f)
    {
      return; // (a COMBAT Rare started blind: round 2 has asked Rare to leave it)
    }
    s.try_at = now;
    if (in_search || (Load32(base, c + kConfig) & kSearching) != 0)
    {
      if (!s.cut_asked)
      {
        s.cut_asked = true;
        g.end_search = true; // (Rare's own end of search at its next update, never early in a walk)
        g.end_search_at = now;
        REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} Rare has him in a SEARCH of his own: ended by Rare's own end of search, "
                    "then he is sent | {}",
                    Id(base, c), StackText(base, c));
      }
      return;
    }
    s.cut_asked = false;
    if (R2TopClass(base, c) != 0x82092A8Cu)
    {
      if (now - s.wait_log > 5.0f || now < s.wait_log)
      {
        s.wait_log = now;
        REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} not sent yet: his top task is not his DEFAULT script | {}", Id(base, c),
                    StackText(base, c));
      }
      return;
    }
    if (g_r3cd_plan.centre != g_r3cd_origin.pad && Ptr(g_r3cd_origin.pad))
    {
      PlanSweep(base, g_r3cd_plan, 0x5243u, g_r3cd_origin.pad, c,
                fmt::format("the radio reinforcements' spread round the place of {}'s call", g_r3cd_origin.who));
    }
    if (s.first && !Ptr(s.pad))
    {
      s.pad = g_r3cd_origin.pad;
    }
    // Follow-up 2: the first walks to the place's own pad (the pad the caller stood on); each of the others to a pad of
    // his own beside it (on its floor, a way back to it over open links, 3 m from the others' pads), so that no two
    // searchers of the squad are given one pad.
    if (s.first && s.index > 0 && s.pad == g_r3cd_origin.pad)
    {
      const uint32_t own = R3cDOwnPad(base, c);
      s.pad = Ptr(own) ? own : s.pad;
    }
    if (!Ptr(s.pad))
    {
      s.pad = Load32(base, c + kPad); // (no place on record: he searches from where he is)
    }
    float pp[3] = {}, me[3];
    PosOf(base, c, me);
    if (!Ptr(s.pad) || !PadPos(base, s.pad, pp))
    {
      R3cDEnd(ctx, base, c, g, s, now, "has no pad to go to", false);
      return;
    }
    if (s.first && s.plain)
    {
      // Follow-up 2: the plain walk (Rare's go-to-pad task on top of his DEFAULT script), watched here (stage 3).
      const uint32_t task = WalkToPad(ctx, base, c, s.pad);
      if (!task)
      {
        return; // (tried again in 0.3 s)
      }
      s.walk_task = task;
      s.stage = 3;
      if (const float wary = float(REXCVAR_GET(pdz_lab_stealth_wary_floor)); g.meter > wary)
      {
        g.meter = wary;
      }
      g.confirmed = false;
      s.watch = nullptr;
      s.watch_out = false;
      s.best_d = Dist3(me, pp);
      s.best_at = now;
      std::memcpy(s.mv_pos, me, sizeof(me));
      std::memcpy(s.start, me, sizeof(me));
      s.mv_at = now;
      REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} SENT TO pad {:08X} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm from him and {:.0f} cm "
                  "from the place of {}'s call, by Rare's PLAIN WALK (go-to-pad task {:08X}); Jo's own place is not used | {}",
                  Id(base, c), s.pad, pp[0], pp[1], pp[2], Dist3(me, pp), Dist3(pp, g_r3cd_origin.pos), g_r3cd_origin.who, task,
                  StackText(base, c));
      return;
    }
    g_next_brief = false;
    StartInvestigation(ctx, base, c, g, Inv::Noise, pp, -1, now,
                       s.first ? fmt::format("radio reinforcement (pdz_stealth_mansion_reinforcements): he goes to where {}'s call "
                                             "came from",
                                             g_r3cd_origin.who)
                               : std::string("radio reinforcement: his next spot round the place of the call"),
                       s.pad);
    if (g.st != St::Investigating || g.inv != Inv::Noise)
    {
      return; // (tried again in 0.3 s)
    }
    s.stage = 2;
    if (s.first)
    {
      g.inv_until = std::max(g.inv_until, now + kR3cDWalkS);
    }
    // Follow-up 1: Rare's COMBAT starts for the clone as he is made (blind: round 2 has Rare leave it at once), and
    // the layer's combat state put his meter at 100, so his first seconds had Rare's full sight. He starts as a wary
    // guard: the wary floor, nothing confirmed.
    if (const float wary = float(REXCVAR_GET(pdz_lab_stealth_wary_floor)); g.meter > wary)
    {
      g.meter = wary;
    }
    g.confirmed = false;
    s.watch = nullptr;
    s.watch_out = false;
    s.best_d = Dist3(me, pp);
    s.best_at = now;
    std::memcpy(s.mv_pos, me, sizeof(me));
    std::memcpy(s.start, me, sizeof(me));
    s.mv_at = now;
    REXLOG_INFO("pdz_stealth_r3c: REINFORCEMENTS: {} SENT TO pad {:08X} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm from him and {:.0f} cm from "
                "the place of {}'s call ({}): {}; Jo's own place is not used | {}",
                Id(base, c), s.pad, pp[0], pp[1], pp[2], Dist3(me, pp), Dist3(pp, g_r3cd_origin.pos), g_r3cd_origin.who,
                g_r3cd_origin.why,
                s.first ? std::string(s.tries ? "he walks on to it"
                                      : s.pad == g_r3cd_origin.pad ? "the place itself" : "his own pad beside the place")
                        : fmt::format("SEARCHING: spot {} of his search round it", s.spots + 1),
                StackText(base, c));
  }

  // TESTING: pdz_test_stealth_radio "<id>": Rare's "a friend died in view" input for the named live tracked guard for
  // half a second, with no body (Rare's reaction falls back to his alerted one: a radio user on Secret Agent or higher
  // starts his call). Called once a frame with the other test tools.
  void R3cDTestRadio(const uint8_t *base, float now)
  {
    const std::string want = REXCVAR_GET(pdz_test_stealth_radio);
    if (want == g_r3cd_test_done)
    {
      return;
    }
    g_r3cd_test_done = want;
    if (want.empty())
    {
      return;
    }
    for (const auto &[c, g] : g_guards)
    {
      if (g.dead || g.st == St::Dead || Id(base, c) != want || !(LoadF(base, c + kHealth) > 0.0f))
      {
        continue;
      }
      g_r3cd_test_c = c;
      g_r3cd_test_until = now + 0.5f;
      REXLOG_INFO("pdz_stealth_r3c: test: {} is given Rare's \"a friend died in view\" input (+6260 0x800) for 0.5 s, no body: {} "
                  "(difficulty {}, radio state {})",
                  want, RadioReady(base, c) ? "a radio user who can call now: Rare's radio call should start" : "he cannot call now",
                  RadioDifficulty(base), RadioState(base));
      return;
    }
    REXLOG_INFO("pdz_stealth_r3c: test: no live tracked guard {} for pdz_test_stealth_radio", want);
  }

  // Fix 6, THE TOWER GUARDS' SEARCH RESTARTS (round 3b's warning fired 8 times for tow1 / tow2 / tow4 in the r3b Mansion
  // smoke run). Cause (the log of that run, 20:39:25-20:41:40): a tower sniper who hears gunfire (there: the radio
  // reinforcements firing at Jo, every few seconds) is alerted by Rare's stimulus 0400, and while he stays alerted
  // Rare's DEFAULT script starts its on-the-spot SEARCH beside itself again about once a second (task stack "[82092A8C
  // status 1 beside 1]": each new search replaces the last; the same restart round 3b found for a body in view). On
  // his 4-pad island the search has nowhere to walk: in every 5 s sample he moved 0 cm, and his sight heading and
  // skeleton heading stayed the same (102 / 101 deg) while only the yaw value +0x1670 wandered up to 19 deg for a
  // moment. Rare's SEARCH_START line is asked for at each start and refused by the layer's repeat rule. So nothing
  // walks and nothing of ours runs: the restarts are Rare's, they cost a small task a second, and round 2's cap ends
  // the episode at 25 s (after it Rare restarted twice more, ended at once by round 3b, then stopped). For a no_walk
  // guard the warning is therefore not raised: one information line per guard and 30 s instead, with how far he moved
  // and turned since the first restart, so a run shows whether it could be seen. true = no warning for him.
  bool R3cDTowerRestart(const uint8_t *base, uint32_t c, const Guard &g, int restarts, float now)
  {
    if (!NoWalk(g.who))
    {
      return false;
    }
    float me[3];
    PosOf(base, c, me);
    const float yaw = LoadF(base, c + 0x1670);
    if (g_r3cd_tower_ref.size() > 64)
    {
      g_r3cd_tower_ref.clear();
      g_r3cd_tower_log.clear();
    }
    auto ref = g_r3cd_tower_ref.find(c);
    float &last = g_r3cd_tower_log.try_emplace(c, -1e9f).first->second;
    if (ref == g_r3cd_tower_ref.end() || now < last || now - last > 60.0f)
    {
      g_r3cd_tower_ref[c] = {me[0], me[1], me[2], yaw};
      ref = g_r3cd_tower_ref.find(c);
      last = -1e9f;
    }
    if (now - last >= 30.0f || now < last)
    {
      last = now;
      const float dx = me[0] - ref->second[0], dz = me[2] - ref->second[2];
      float turn = (yaw - ref->second[3]) * 57.29578f;
      while (turn > 180.0f)
      {
        turn -= 360.0f;
      }
      while (turn < -180.0f)
      {
        turn += 360.0f;
      }
      REXLOG_INFO("pdz_stealth_r3c: TOWER RESTARTS: {} (a no_walk guard) Rare restarted his on-the-spot SEARCH {} times in 5 s (Rare's "
                  "DEFAULT script restarts it about once a second while he stays alerted; nothing of ours); since the first of "
                  "them he moved {:.0f} cm and his yaw value changed {:+.0f} deg; no warning for a tower guard: he stays where "
                  "he is, his lines only (state {}) | {}",
                  Id(base, c), restarts, std::sqrt(dx * dx + dz * dz), turn, StName(g.st), StackText(base, c));
    }
    return true;
  }
