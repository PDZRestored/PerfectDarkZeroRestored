  // Stealth layer, overnight Mon 16 / Tue 17 Nov (items 4, 5, 6b). Included by lab_stealth.cpp inside its anonymous
  // namespace, after lab_stealth_r5_A_Mon16Nov.inc. Log prefix "pdz_stealth_r6:".
  //
  // ITEM 4, THE NIGHTCLUB'S BOUNCERS UNDER THE LAYER, AND RARE'S UNARMED TRUCE (pdz_stealth_truce).
  // Cause (the code): the Nightclub row listed the two front-door bouncers s02a / s02b in not_ids ("Rare's"), so the
  // layer never tracked them: Rare's own knowledge of Jo drove them (hand test Mon 16 Nov 22:06, no line of the layer
  // about them). They stand OUTSIDE the club: the row now lists them with the dock guards (the user's decision 22:09).
  // What must not break: Rare's unarmed route. The level script gives the bouncers +12512 = 2 (GREET) at level start;
  // Rare's target test sub_8252EF68 refuses a target whose hands are empty (target +4084 == 0) for a character with
  // +12512 = 1, 2 or 3 unless his input +6260 0x10 ("provoked") is up (read from that routine: 8252EFD4, 8252EFE8-
  // 8252F02C), and the front-door warning runs from the level script. The layer's meter would otherwise fill on an
  // empty-handed Jo walking up to the door, and its "alert now" at the confirmed sighting would start his COMBAT.
  // So: a guard of the layer for whom that truce holds NOW (passive, unprovoked, Jo's hands empty) and who is calm
  // for the layer is left to Rare, exactly as a dormant guard is (R6TruceTick, from the sight update hook: no meter,
  // no layer rule, Rare's own sight answer). The moment it ends (a weapon or gadget in her hand, the script's
  // "ignored the warning" setting +12512 to 0, a provocation) he is an ordinary guard of the layer: the meter from
  // where it stood, no steering by an unseen Jo, no blind combat. One line when the truce starts and when it ends.
  //
  // ITEM 5, WHO SENT "ALERTED BY A FRIEND" (pdz_stealth_friend_alert_log; a log line, nothing else).
  // Rare's stimulus word 0008 is built by sub_821D1888 from four inputs (read from that routine): +6260 0x40 ("alert
  // now": what a squad mate's COMBAT start sets on the friends in its radius, 821CBFA0-821CC094; ALSO what the layer
  // itself raises on a guard whose sighting its meter has just confirmed, SightNowTick, so that his COMBAT starts at
  // once), +6260 0x80, +6276 0x1000 (cleared by the builder), and +6260 0x20 for a character who is not passive.
  // None of them carries the sender. So the sender is noted where it is known: the COMBAT start hook notes, for every
  // character who gets 0x40 from that start, who started it; SightNowTick notes "the layer itself". At a COMBAT START
  // whose last word holds 0008 one line says which input raised it, who sent it, where he was, and what the layer has
  // on that friend (his state, when his sighting was confirmed, when he last saw Jo himself).
  // (The hand test's case, s19b 22:10:30: his own sighting was confirmed by the meter a moment before, "FIRST SHOT
  // 0.99 s after his sighting was confirmed": the 0008 was the layer's own alert-now, not a friend's.)
  //
  // ITEM 6b, CAMERA WORDS: R6CameraWords (the setup name or the model name against pdz_stealth_camera_models); the
  // rule itself is in lab_stealth_r5_C_Mon16Nov.inc.

  constexpr uint32_t kR6Unarmed = 12512; // character: Rare's unarmed reaction (template +124): 1, 2, 3 = passive
  constexpr uint32_t kR6Hands = 4084;    // character: the count of items in the hands (0 = empty hands)
  constexpr uint32_t kR6Flags = 6276;    // character: 0x1000 = one of the four inputs of the word 0008

  struct R6Alert
  {
    uint32_t source = 0; // the friend whose COMBAT start raised "alert now" on him (0: the layer itself)
    int kind = 0;        // 1 a friend's COMBAT start, 2 the layer's own confirmed sighting, 3 as 1 but taken off again
    float at = -1e9f;
    float source_pos[3] = {};
    float d = 0.0f;
    // Rare's inputs at his last word with 0008
    uint32_t inputs = 0, flags = 0;
    float word_at = -1e9f;
  };
  std::unordered_map<uint32_t, R6Alert> g_r6_alerts;
  int g_r6_alert_lines = 0, g_r6_truce_lines = 0;

  void R6CameraReset(); // (below)
  void R6Reset()
  {
    g_r6_alerts.clear();
    g_r6_alert_lines = 0;
    g_r6_truce_lines = 0;
    R6CameraReset();
  }

  // Does a text hold one of the comma-separated words (any case)?
  bool R6HoldsWord(const char *text, const std::string &words)
  {
    if (!text || !text[0])
    {
      return false;
    }
    size_t at = 0;
    while (at < words.size())
    {
      size_t end = words.find_first_of(", ;", at);
      if (end == std::string::npos)
      {
        end = words.size();
      }
      const size_t n = end - at;
      if (n >= 3) // (a word under three letters would match half the level)
      {
        for (int i = 0; text[i]; ++i)
        {
          size_t k = 0;
          while (k < n && text[i + k] && (text[i + k] | 0x20) == (words[at + k] | 0x20))
          {
            ++k;
          }
          if (k == n)
          {
            return true;
          }
        }
      }
      at = end + 1;
    }
    return false;
  }

  // Item 6b: a security camera by its setup name or its model name (a bullet hit on a prop: not a hot path).
  bool R6CameraWords(const char *name, const char *model)
  {
    const std::string words = REXCVAR_GET(pdz_stealth_camera_models);
    return R6HoldsWord(name, words) || R6HoldsWord(model, words);
  }

  // Item 4: Rare's unarmed truce holds for him now (three loads).
  bool R6Truce(const uint8_t *base, uint32_t c, uint32_t jo)
  {
    if (!Ptr(c) || !Ptr(jo))
    {
      return false;
    }
    const uint32_t passive = Load32(base, c + kR6Unarmed);
    return passive >= 1 && passive <= 3 && !(Load32(base, c + kStimuli) & 0x10) && Load32(base, jo + kR6Hands) == 0;
  }

  // Item 4, from the sight update hook (under the lock) for every tracked guard: true = he is Rare's own this frame.
  // Only a guard who is calm for the layer: one the layer already has wary, searching or in combat stays the layer's.
  bool R6TruceTick(const uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now)
  {
    (void)now;
    const bool truce = ZoneLevel() && REXCVAR_GET(pdz_stealth_truce) && !g.scientist && !g.dead && g.st == St::Unaware &&
                       g.combat_script == 0 && R6Truce(base, c, jo);
    if (truce != g.r6_truce)
    {
      g.r6_truce = truce;
      if (g_r6_truce_lines < 40)
      {
        ++g_r6_truce_lines;
        const uint32_t passive = Load32(base, c + kR6Unarmed);
        if (truce)
        {
          REXLOG_INFO("pdz_stealth_r6: TRUCE: {} is passive towards an empty-handed Jo (Rare's unarmed reaction +12512 = {}, "
                      "not provoked, her hands empty, {:.0f} cm away): he is Rare's own while that holds (no meter, no rule "
                      "of the layer; pdz_stealth_truce)",
                      Id(base, c), passive, Ptr(jo) ? Dist(base, c, jo) : -1.0f);
        }
        else
        {
          REXLOG_INFO("pdz_stealth_r6: TRUCE OVER: {} (+12512 = {}, provoked {}, items in Jo's hands {}, his state {}): the "
                      "layer's rules apply to him from now (the meter, no steering by an unseen Jo, no blind combat)",
                      Id(base, c), passive, (Load32(base, c + kStimuli) & 0x10) ? "yes" : "no",
                      Ptr(jo) ? int32_t(Load32(base, jo + kR6Hands)) : -1, StName(g.st));
        }
      }
    }
    return truce;
  }

  // Item 5: `source`'s COMBAT start raised "alert now" on c (kind 1; 3 = and the layer took it off again), or the
  // layer itself raised it at his confirmed sighting (kind 2, source 0). Off the hot path: a COMBAT start, or the 1.5 s
  // after a confirmed sighting.
  void R6AlertNote(const uint8_t *base, uint32_t c, uint32_t source, int kind, float now)
  {
    if (!REXCVAR_GET(pdz_stealth_friend_alert_log))
    {
      return;
    }
    if (g_r6_alerts.size() > 512)
    {
      g_r6_alerts.clear();
    }
    R6Alert &a = g_r6_alerts[c];
    if (kind == 2 && a.kind != 2 && a.at > -1e8f && now >= a.at && now - a.at < 2.0f)
    {
      return; // (a friend's alert of the last 2 s is the older cause: kept)
    }
    a.source = source;
    a.kind = kind;
    a.at = now;
    a.d = 0.0f;
    if (Ptr(source))
    {
      PosOf(base, source, a.source_pos);
      a.d = Dist(base, c, source);
    }
  }

  // Item 5: Rare's word for him holds 0008: which inputs were up before the builder ran.
  void R6AlertWord(const uint8_t *base, uint32_t c, uint32_t word, uint32_t inputs_before, uint32_t flags_before, float now)
  {
    (void)base;
    (void)word;
    if (!REXCVAR_GET(pdz_stealth_friend_alert_log))
    {
      return;
    }
    if (g_r6_alerts.size() > 512)
    {
      g_r6_alerts.clear();
    }
    R6Alert &a = g_r6_alerts[c];
    a.inputs = inputs_before;
    a.flags = flags_before;
    a.word_at = now;
  }

  // What the layer has on a character, for the line.
  std::string R6LayerOn(const uint8_t *base, uint32_t who, float now)
  {
    const auto it = g_guards.find(who);
    if (it == g_guards.end())
    {
      return InLayer(base, who) ? std::string("under the layer, no record yet") : std::string("NOT under the layer (Rare's own knowledge)");
    }
    const Guard &f = it->second;
    const std::string spotted = f.spotted_at > -1e8f && now >= f.spotted_at
                                    ? fmt::format("his sighting of Jo was confirmed {:.1f} s ago", now - f.spotted_at)
                                    : std::string("NO confirmed sighting of Jo on record");
    const std::string seen = f.last_visible > -1e8f && now >= f.last_visible
                                 ? fmt::format("Rare's sight test last showed him Jo {:.1f} s ago", now - f.last_visible)
                                 : std::string("Rare's sight test never showed him Jo");
    return fmt::format("state {}, meter {:.0f}, {}, {}", StName(f.st), f.meter, spotted, seen);
  }

  // Item 5, from the COMBAT start hook right after the COMBAT START line (under the lock): one line when his last
  // word held 0008. Nothing is changed.
  void R6FriendAlertLog(const uint8_t *base, uint32_t c, const Guard &g, float now)
  {
    if (!REXCVAR_GET(pdz_stealth_friend_alert_log) || g_r6_alert_lines >= 300)
    {
      return;
    }
    const auto w = g_r4a.find(c);
    if (w == g_r4a.end() || !(w->second.word & 0x8) || !(now >= w->second.word_at && now - w->second.word_at < 1.0f))
    {
      return;
    }
    ++g_r6_alert_lines;
    const auto it = g_r6_alerts.find(c);
    const R6Alert *a = it != g_r6_alerts.end() ? &it->second : nullptr;
    std::string raised = "Rare's inputs before the word were not recorded";
    if (a && a->word_at > -1e8f && now >= a->word_at && now - a->word_at < 1.0f)
    {
      raised = fmt::format("inputs +6260 {:08X} (alert now 0x40 {}, 0x80 {}, 0x20 {}), flags +6276 {:08X} (0x1000 {})", a->inputs,
                           (a->inputs & 0x40) ? "UP" : "no", (a->inputs & 0x80) ? "UP" : "no", (a->inputs & 0x20) ? "UP" : "no",
                           a->flags, (a->flags & 0x1000) ? "UP" : "no");
    }
    float me[3];
    PosOf(base, c, me);
    std::string sender;
    if (a && a->at > -1e8f && now >= a->at && now - a->at < 2.5f && a->kind == 2)
    {
      sender = fmt::format("NO FRIEND: the layer itself raised Rare's \"alert now\" on him {:.2f} s ago, because its meter had "
                           "just confirmed HIS OWN sighting of Jo ({}), so that his fight starts at once",
                           now - a->at,
                           g.spotted_at > -1e8f && now >= g.spotted_at ? fmt::format("confirmed {:.2f} s ago", now - g.spotted_at)
                                                                       : std::string("no time on record"));
    }
    else if (a && a->at > -1e8f && now >= a->at && now - a->at < 2.5f && Ptr(a->source))
    {
      sender = fmt::format("the friend is {} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm from him: his COMBAT start {:.2f} s ago raised "
                           "Rare's \"alert now\" on him{} | the layer on that friend: {}",
                           Id(base, a->source), a->source_pos[0], a->source_pos[1], a->source_pos[2], a->d, now - a->at,
                           a->kind == 3 ? " (the layer took it off again: no squad knowledge; this word came another way)" : "",
                           R6LayerOn(base, a->source, now));
    }
    else
    {
      // No COMBAT start on record: the nearest characters in Rare's COMBAT, as candidates.
      struct Near
      {
        uint32_t c = 0;
        float d = 1e9f;
      } closest[3];
      for (const auto &[other, og] : g_guards)
      {
        if (other == c || !Ptr(other) || og.dead || og.st == St::Dead || !(og.combat_script != 0 || og.st == St::Combat))
        {
          continue;
        }
        const float d = Dist(base, c, other);
        for (int i = 0; i < 3; ++i)
        {
          if (d < closest[i].d)
          {
            for (int k = 2; k > i; --k)
            {
              closest[k] = closest[k - 1];
            }
            closest[i].c = other;
            closest[i].d = d;
            break;
          }
        }
      }
      sender = "no friend's COMBAT start reached him in the last 2.5 s and the layer did not raise it: sent by one of Rare's "
               "other inputs above (their senders are not traced)";
      std::string list;
      for (const Near &n : closest)
      {
        if (n.c)
        {
          float p[3];
          PosOf(base, n.c, p);
          list += fmt::format("{}{} at ({:.0f} {:.0f} {:.0f}), {:.0f} cm away [{}]", list.empty() ? "" : "; ", Id(base, n.c), p[0], p[1],
                              p[2], n.d, R6LayerOn(base, n.c, now));
        }
      }
      sender += list.empty() ? "; no tracked guard is in COMBAT" : "; tracked guards in COMBAT, nearest first: " + list;
    }
    REXLOG_INFO("pdz_stealth_r6: FRIEND ALERT: {} at ({:.0f} {:.0f} {:.0f}) started COMBAT on Rare's 0008 (alerted by a friend) | "
                "raised by: {} | {} | nothing is changed (log only)",
                Id(base, c), me[0], me[1], me[2], raised, sender);
  }

  // ITEMS 6b / 6c, THE REAL SECURITY CAMERAS (corrected in the night of Mon 16 / Tue 17 Nov: the first version took
  // the sentry gun's class for the camera). A security camera is the prop class DP_SecurityCamera, type index 10,
  // vtable 820C9028 (built by sub_825BDE50; update sub_825BE5D0, hit handler sub_825BF4A8). The vtable 820BE9A8 is
  // DP_AutoGun, the sentry gun: not a camera. The five "..._static_StaticNNN" props (class 820CA100, health 5 -> 0) of
  // the hand-test log are plain Static props and the log holds no line for a prop with "camera" in its name: a
  // camera's bullet hits go to its own hit handler, whose damage call is sub_825EF0A0 on the part at prop +1048, not
  // the handler sub_825EE9D0 the layer's bullet-hit hook sits on (read in the code; whether sub_825EF0A0 reaches
  // sub_825EE9D0 further down was not read).
  // Both events come from camera_sounds_Tue17Nov.cpp, which hooks the camera's break routine sub_825BFCA0:
  //   - LOUD (the explosion and the fall: the fifth hit, or a blast): pdz::camsound::SetDestroyedListener ->
  //     R6CameraDown makes the loud event of lab_stealth_r5_C_Mon16Nov.inc at the camera (heard within 20 m, halved
  //     per wall or floor, or seen).
  //   - QUIET (one shot to the lens: the hit handler sets prop +984 and puts the camera out with Rare's glass effect):
  //     pdz::camsound::SetLensHitListener -> R6CameraLens: nobody hears it; a guard looking at the camera reacts.
  constexpr uint32_t kR6CameraVtable = 0x820C9028; // DP_SecurityCamera
  int g_r6_cam_lines = 0;
  void R6CameraPush(uint32_t prop, const float pos[3], bool quiet, float now)
  {
    if (g_r5c_cams.size() >= 16)
    {
      g_r5c_cams.erase(g_r5c_cams.begin());
    }
    R5CCam cam;
    std::memcpy(cam.pos, pos, sizeof(cam.pos));
    cam.at = now;
    cam.prop = prop;
    cam.quiet = quiet;
    g_r5c_cams.push_back(cam);
  }

  // From the destroyed-camera listener (the game thread, under the lock): the loud event.
  void R6CameraDown(const uint8_t *base, uint32_t prop, const float pos[3])
  {
    const float now = Clock(base);
    const bool rule = REXCVAR_GET(pdz_stealth_camera_noise) && RarePlus();
    for (const R5CCam &c : g_r5c_cams)
    {
      if (c.prop == prop && !c.quiet && now >= c.at && now - c.at < 5.0f)
      {
        return; // (the bullet-hit path has it already)
      }
    }
    if (g_r6_cam_lines < 80)
    {
      ++g_r6_cam_lines;
      REXLOG_INFO("pdz_stealth_r6: CAMERA DOWN: the security camera {:08X} at ({:.0f} {:.0f} {:.0f}) was destroyed (its "
                  "explosion and fall): {}",
                  prop, pos[0], pos[1], pos[2],
                  rule ? fmt::format("a LOUD noise there: calm guards within {:.0f} m hear it (halved per wall or floor), or "
                                     "see it go within {:.0f} m in front of them: a what-was-that line, wary, the first two "
                                     "within 30 m walk there to look, the others look round (pdz_stealth_camera_noise)",
                                     kR5CCamHearCm / 100.0f, kR5CCamSeeCm / 100.0f)
                       : std::string("the camera noise rule is off: nothing of the layer's follows"));
    }
    if (rule)
    {
      R6CameraPush(prop, pos, false, now);
    }
  }

  // From the lens-hit listener (the game thread, under the lock): the quiet event.
  void R6CameraLens(const uint8_t *base, uint32_t prop, const float pos[3])
  {
    const float now = Clock(base);
    const bool rule = REXCVAR_GET(pdz_stealth_camera_noise) && REXCVAR_GET(pdz_stealth_camera_lens_quiet) && RarePlus();
    bool fresh = true;
    for (const R5CCam &c : g_r5c_cams)
    {
      fresh = fresh && !(c.prop == prop && now >= c.at && now - c.at < 5.0f);
    }
    if (g_r6_cam_lines < 80)
    {
      ++g_r6_cam_lines;
      REXLOG_INFO("pdz_stealth_r6: CAMERA LENS: the security camera {:08X} at ({:.0f} {:.0f} {:.0f}) was shot in the lens "
                  "(put out, not brought down): QUIET (nobody hears it){}",
                  prop, pos[0], pos[1], pos[2],
                  !rule   ? "; the lens rule is off: nothing of the layer's follows"
                  : fresh ? ": a guard who is looking at the camera now reacts (a line, wary, a look round)"
                          : "; the same camera within 5 s: already judged");
    }
    if (rule && fresh)
    {
      R6CameraPush(prop, pos, true, now);
    }
  }
  void R6CameraReset()
  {
    g_r6_cam_lines = 0;
  }
