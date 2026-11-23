  // Stealth layer round 5, Mon 16 Nov evening, helper C (fix 5). Included by lab_stealth.cpp inside its anonymous
  // namespace, after lab_stealth_r4_C_Mon16Nov.inc. Runs only with the layer on (pdz_lab_rareplus under the master
  // pdz_lab_guard_stealth). Log prefix "pdz_stealth_r5:".
  //
  // Fix 5, A SHOT-DOWN SECURITY CAMERA IS A NOISE (pdz_stealth_camera_noise). Hand test Mon 16 Nov ~19:28, Subway: a
  // camera shot down was silent to the AI: the layer had no camera event at all, and Rare's game raises none (a
  // destroyed prop tells the level script, PropMsg Level_Prop_Destroyed, never a character).
  // Where it is caught: Rare's bullet-hit handler of the prop damage component, sub_825EE9D0 (hook in
  // lab_light_shot_log.cpp; r3 = the component, [r3] = the prop). The hook reads the prop's health through the prop's
  // own virtual (vtable +332, the read sub_825EE9D0 itself starts with) before and after Rare's handler; health above
  // 0 before and not after = the prop is down. Every prop that goes down this way is logged once (PROP DOWN, with its
  // name and class vtable, so that a camera this rule misses can be read back); a prop whose name holds "camera" is a
  // camera (the setup names carry the class word; DP_SecurityCamera's vtable is not known to this code).
  // What follows, for each calm or wary guard of the layer, judged once per camera in his next tick:
  //   - he HEARS it break within kR5CCamHearCm, halved per wall or floor (the wall-muffle rule, pdz_stealth_wall_muffle);
  //   - or he SEES it go: within kR5CCamSeeCm, in front of him (75 degrees to either side) and in clear view;
  //   - then round 3's heard reaction is given the camera's place as a loud sound (as an explosion): his what-was-that
  //     line, wary, and the first two within 30 m walk there to look (our noise check, capped by
  //     pdz_stealth_search_cap_s, its give-up line, back to post); the others stop and look round where they stand.
  //     Rare's own reaction, if one comes within 0.5 s, wins. Needs pdz_stealth_heard_react (on by default).
  // Nobody learns where Jo is.
  //
  // Overnight Mon 16 / Tue 17 Nov (items 6b and 6c; hand test 22:18-22:25, Subway: five PROP DOWN lines, every one "not a
  // camera by its name": 'aid_xml_propsetup_single_ware_static_StaticNNN', class vtable 820CA100, plain Static props.
  // Those five are not of the camera class; the real cameras are the class DP_SecurityCamera (type 10, vtable 820C9028,
  // setups ..._securitycameras_securitycameraNNN; 820BE9A8 is the sentry gun), and their explosions and lens shots are
  // caught in lab_stealth_r6_Tue17Nov.inc (the two listeners of camera_sounds_Tue17Nov.cpp). This bullet-hit path
  // stays as a second way in.)
  //   - RECOGNITION here: the class vtable 820C9028, or a prop whose setup name OR model name (setup +84 -> +140, read
  //     in the hit hook) holds one of pdz_stealth_camera_models' words ("camera,cctv,seccam"; the camera models in the
  //     level packages are testmodels_securitycamera, props_electrical_camera_controller and cutscene_camera_security).
  //     PROP DOWN prints both names of every prop a bullet takes down.
  //   - THE EXPLOSION AND THE FALL ARE LOUD (the user's decision 22:23): a camera that goes DOWN is the loud event
  //     above, unchanged (heard within 20 m, halved per wall or floor; or seen).
  //   - ONE SHOT TO THE LENS IS QUIET (pdz_stealth_camera_lens_quiet): a bullet that hits a camera and does NOT bring
  //     it down is heard by nobody. Only a guard who is LOOKING at the camera at that moment (within kR5CCamSeeCm, in
  //     front of him, in clear view) reacts, as to a quiet sound at the camera: his line, wary, a look round (round
  //     3's heard reaction, the quiet kind: no walk). Once per camera in 5 s.

  constexpr float kR5CCamHearCm = 2000.0f; // the break is heard within this (halved per wall or floor)
  constexpr float kR5CCamSeeCm = 2500.0f;  // ... or seen within this, in front of him and in clear view
  constexpr float kR5CCamJudgeS = 2.5f;    // s: a camera down is judged for a guard within this of it

  struct R5CCam
  {
    float pos[3] = {};
    float at = -1e9f;
    uint32_t prop = 0;
    int heard = 0, saw = 0; // guards who reacted
    bool quiet = false;     // a hit that did not bring it down (the lens): seen only, never heard
  };
  std::vector<R5CCam> g_r5c_cams;
  std::unordered_map<uint32_t, float> g_r5c_judged; // per guard: the newest camera judged for him
  int g_r5c_props_down = 0;                         // PROP DOWN lines of this level start (capped)
  int g_r5c_cam_hits = 0;                           // CAMERA HIT lines of this level start (capped)

  // R4CReset (every level start / restart / reload) calls this.
  void R5CReset()
  {
    g_r5c_cams.clear();
    g_r5c_judged.clear();
    g_r5c_props_down = 0;
    g_r5c_cam_hits = 0;
    R6Reset(); // (overnight Tue 17 Nov: lab_stealth_r6_Tue17Nov.inc)
  }

  // From lab_stealth::PropBulletHit (the sub_825EE9D0 hook, game thread, under g_lock): a bullet hit a prop with a
  // damage component; its health before and after Rare's handler (`gone`: the object was no longer that prop after it).
  void R5CPropHit(const uint8_t *base, uint32_t prop, uint32_t vtable, const char *name, const char *model, const float pos[3],
                  float before, float after, bool gone)
  {
    const bool down = before > 0.0f && (gone || !(after > 0.0f));
    // (overnight Tue 17 Nov: the class DP_SecurityCamera, vtable 820C9028; or the setup name / the model name)
    const bool camera = vtable == 0x820C9028u || R6CameraWords(name, model);
    const float now = Clock(base);
    if (!down)
    {
      if (!camera)
      {
        return;
      }
      const bool rule = REXCVAR_GET(pdz_stealth_camera_noise) && REXCVAR_GET(pdz_stealth_camera_lens_quiet) && RarePlus();
      bool fresh = true;
      for (const R5CCam &c : g_r5c_cams)
      {
        fresh = fresh && !(c.prop == prop && now >= c.at && now - c.at < 5.0f);
      }
      if (g_r5c_cam_hits < 60)
      {
        ++g_r5c_cam_hits; // (always on, capped: what a lens shot does to the camera's health is read from this line)
        REXLOG_INFO("pdz_stealth_r5: CAMERA HIT: prop {:08X} '{}' (model '{}', class vtable {:08X}) at ({:.0f} {:.0f} {:.0f}): "
                    "health {:.1f} -> {:.1f}: not down: QUIET (nobody hears it){}",
                    prop, name, model, vtable, pos[0], pos[1], pos[2], before, after,
                    !rule   ? "; the lens rule is off: nothing of the layer's follows"
                    : fresh ? ": a guard who is looking at the camera now reacts (a line, wary, a look round)"
                            : "; the same camera within 5 s: already judged");
      }
      if (!rule || !fresh)
      {
        return;
      }
      if (g_r5c_cams.size() >= 16)
      {
        g_r5c_cams.erase(g_r5c_cams.begin());
      }
      R5CCam cam;
      std::memcpy(cam.pos, pos, sizeof(cam.pos));
      cam.at = now;
      cam.prop = prop;
      cam.quiet = true;
      g_r5c_cams.push_back(cam);
      return;
    }
    if (g_r5c_props_down < 200)
    {
      ++g_r5c_props_down;
      REXLOG_INFO("pdz_stealth_r5: PROP DOWN: a bullet took prop {:08X} '{}' (model '{}', class vtable {:08X}) at ({:.0f} "
                  "{:.0f} {:.0f}) down: health {:.1f} -> {}: {}",
                  prop, name, model, vtable, pos[0], pos[1], pos[2], before,
                  gone ? std::string("the prop is gone") : fmt::format("{:.1f}", after),
                  camera ? "a security camera"
                         : "not a camera by its setup name or model name (pdz_stealth_camera_models): nothing of the layer's "
                           "follows");
    }
    if (!camera || !REXCVAR_GET(pdz_stealth_camera_noise) || !RarePlus())
    {
      return;
    }
    for (const R5CCam &c : g_r5c_cams)
    {
      if (c.prop == prop && !c.quiet && now >= c.at && now - c.at < 5.0f)
      {
        return; // (the same camera, a second bullet of the same burst; a quiet lens hit before it does not count)
      }
    }
    if (g_r5c_cams.size() >= 16)
    {
      g_r5c_cams.erase(g_r5c_cams.begin());
    }
    R5CCam cam;
    std::memcpy(cam.pos, pos, sizeof(cam.pos));
    cam.at = now;
    cam.prop = prop;
    g_r5c_cams.push_back(cam);
    REXLOG_INFO("pdz_stealth_r5: CAMERA DOWN: the security camera '{}' (model '{}') at ({:.0f} {:.0f} {:.0f}) was shot down "
                "(its explosion and fall): a LOUD noise there: calm guards within {:.0f} m hear it (halved per wall or "
                "floor), or see it go within {:.0f} m in front of them: a what-was-that line, wary, the first two within "
                "30 m walk there to look, the others look round (pdz_stealth_camera_noise; round 3's heard reaction)",
                name, model, pos[0], pos[1], pos[2], kR5CCamHearCm / 100.0f, kR5CCamSeeCm / 100.0f);
  }

  // Once a tick for every guard under the layer (from R4CTick, before round 3's heard reaction): a camera that went
  // down in the last kR5CCamJudgeS is judged once for him. Nothing but one empty() test while no camera is down.
  void R5CTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat, bool in_search)
  {
    if (g_r5c_cams.empty())
    {
      return;
    }
    for (R5CCam &cam : g_r5c_cams)
    {
      if (now < cam.at || now - cam.at > kR5CCamJudgeS)
      {
        continue;
      }
      if (g_r5c_judged.size() > 512)
      {
        g_r5c_judged.clear();
      }
      const auto done = g_r5c_judged.find(c);
      if (done != g_r5c_judged.end() && done->second >= cam.at && done->second <= now)
      {
        continue;
      }
      g_r5c_judged[c] = cam.at;
      if (!REXCVAR_GET(pdz_stealth_camera_noise) || g.dead || g.st == St::Dead || g.scientist || !LayerGuard(g) ||
          !(g.st == St::Unaware || g.st == St::Wary) || in_combat || in_search || g.run_committed || FightsNonPlayer(base, c))
      {
        continue;
      }
      float eye[3];
      EyeOf(base, c, g, now, eye);
      const float d = Dist3(eye, cam.pos);
      if (!(d < 3000.0f))
      {
        continue;
      }
      int walls = 0;
      float within = kR5CCamHearCm;
      if (REXCVAR_GET(pdz_stealth_wall_muffle))
      {
        walls = SoundWalls(ctx, base, cam.pos, eye);
        within = Muffled(kR5CCamHearCm, walls);
      }
      else if (!SegmentClear(ctx, base, eye, cam.pos, 0x40) || !SegmentClear(ctx, base, cam.pos, eye, 0x40))
      {
        walls = 1;
        within = 0.0f;
      }
      const bool heard = !cam.quiet && d < within; // (a lens hit is heard by nobody)
      bool saw = false;
      if (!heard && d < kR5CCamSeeCm)
      {
        // In front of him on the ground plan: Rare's facing vector from his last sight test, else his body yaw (degrees).
        float fx = g.dir[0], fz = g.dir[2];
        if (!(now >= g.eye_at && now - g.eye_at < 3.0f) || fx * fx + fz * fz < 1e-6f)
        {
          const float yaw = LoadF(base, c + 0x1670) * 0.01745329f;
          fx = std::sin(yaw);
          fz = std::cos(yaw);
        }
        const float lx = cam.pos[0] - eye[0], lz = cam.pos[2] - eye[2];
        const float ll = std::sqrt(lx * lx + lz * lz), fl = std::sqrt(fx * fx + fz * fz);
        const bool in_front = ll <= 100.0f || (fl > 1e-4f && (lx * fx + lz * fz) / (ll * fl) >= 0.26f);
        saw = in_front && SightClear(ctx, base, cam.pos, eye);
      }
      if (!heard && !saw)
      {
        if (R2Log())
        {
          REXLOG_INFO("pdz_stealth_r5: CAMERA DOWN: {} neither hears nor sees it ({:.0f} cm away, {} wall(s) or floor(s): heard "
                      "within {:.0f} cm)",
                      Id(base, c), d, walls, within);
        }
        continue;
      }
      heard ? ++cam.heard : ++cam.saw;
      if (cam.quiet)
      {
        // (overnight Tue 17 Nov, item 6c) He was looking at the camera as its lens was hit: the quiet kind of round 3's
        // heard reaction at the camera's place (his line, wary, he stops and looks round; no walk).
        R3HConsider(R3HOf(c, g), kR3HSound, 0xCA3E0000u | (cam.prop & 0xFFFFu), cam.pos, std::max(d, 101.0f), 0, now);
        g.meter = std::max(g.meter, Notice(g));
        REXLOG_INFO("pdz_stealth_r5: CAMERA HIT: {} was LOOKING at the camera at ({:.0f} {:.0f} {:.0f}) as its lens was hit "
                    "({:.0f} cm away; state {}, meter {:.0f}): a quiet sound for round 3's heard reaction (his line, wary, a "
                    "look round); nobody hears it | {}",
                    Id(base, c), cam.pos[0], cam.pos[1], cam.pos[2], d, StName(g.st), g.meter, StackText(base, c));
        continue;
      }
      // Round 3's heard reaction (R3HTick, this tick or within 0.5 s): a loud sound at the camera's place.
      R3HState &h = R3HOf(c, g);
      h.wait.at = now;
      h.wait.kind = kR3HBlast;
      h.wait.source = 0xCA3E0000u | (cam.prop & 0xFFFFu);
      std::memcpy(h.wait.pos, cam.pos, sizeof(h.wait.pos));
      h.wait.d = std::max(d, 101.0f);
      h.wait.walls = heard ? walls : 0;
      REXLOG_INFO("pdz_stealth_r5: CAMERA DOWN: {} {} the camera at ({:.0f} {:.0f} {:.0f}) go ({:.0f} cm away, {} wall(s) or "
                  "floor(s); state {}, meter {:.0f}): round 3's heard reaction follows (his line; a walk there for the first "
                  "two within 30 m, else a look round) unless Rare's own reaction comes first | {}",
                  Id(base, c), heard ? "HEARS" : "SEES", cam.pos[0], cam.pos[1], cam.pos[2], d, walls, StName(g.st), g.meter,
                  StackText(base, c));
    }
  }
