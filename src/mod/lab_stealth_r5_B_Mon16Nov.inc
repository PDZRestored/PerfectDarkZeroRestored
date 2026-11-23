// Stealth round 5 (Mon 16 Nov 2026 evening), helper B: fixes 2 and 4. Included by lab_stealth.cpp after the round 4
// files. Log prefix "pdz_stealth_r5:".
//
// Fix 2 (pdz_stealth_glimpse_walk). Since "Rare's rules plus" a glimpse ("thought I saw something", the meter's
//   notice level) only made a guard stop and look round where he stands. Now he goes to look: OUR noise check at the
//   place he glimpsed something (helper H's R3HGoLook: the lamp check's legs, its NO PROGRESS give-up, the search cap
//   pdz_stealth_search_cap_s, the give-up line, back to post). Never Rare's SEARCH at her pad (the old glimpse search,
//   TryInvestigate / GiveSearch, which paced between floors). Rules:
//     - only a spot he can walk to on his OWN floor: CanReachOnFloor, an open pad route of at most
//       pdz_lab_stealth_glimpse_reach_cm ending within 6 m of it. A glimpse up or down a staircase, on a balcony or
//       across a gap is refused: he looks round where he stands, as before (the Mansion stairs case);
//     - one investigation per spot: a place within kR5BSpotCm of one he has already walked to gets a look round only;
//     - not for a guard who stays (helper H's R3HStays: the row's no_look / no_walk, a radio user, Rare's
//       never-investigates), nor when the search cap's one restart is used up, nor while Rare has him in a SEARCH or
//       any task but his DEFAULT script;
//     - on the way his meter runs on: a second glimpse / a full meter escalates exactly as during any noise check.
// Fix 4 (pdz_stealth_shot_from_search). A witness whose friend is SHOT dead beside him (within kR5BBesideCm of the
//   body when his walk is handed over) does not only look round the body: his witness walk (helper H's item 6, same
//   cap, same release / give-up line at its end) goes towards where the shot came from. Jo's place is taken ONCE, in
//   the frame her kill comes on record (never afterwards):
//     - an unsilenced shot: the place the shot came from, if he can walk there on his floor; else as a silenced one;
//     - a silenced shot: only the direction of the hit. He walks at most kR5BSilencedCm from the body along it and
//       never nearer than kR5BKeepOffCm to where she stood.
//   No shot on record (melee, a thrown weapon), a spot he cannot walk to, or a witness further from the body: the
//   walk to the body, as before.
constexpr float kR5BSpotCm = 600.0f;      // fix 2: a glimpse this near a spot he has walked to is the same spot
constexpr float kR5BBesideCm = 600.0f;    // fix 4: "beside him": the witness is this near the body
constexpr float kR5BSilencedCm = 1200.0f; // fix 4: a silenced shot: he walks at most this far along the hit's direction
constexpr float kR5BKeepOffCm = 800.0f;   // fix 4: ... and stays this far from where she stood
constexpr float kR5BShotS = 2.0f;         // fix 4: a shot of hers this long before the kill is the shot that killed
struct R5BState
{
  float born = -2e9f;                         // his Guard's r2_born (a new guard at the same address starts afresh)
  std::vector<std::array<float, 3>> spots;    // fix 2: the glimpse spots he has walked to
  float refuse_log = -1e9f;
  int shot_kill = -1;                         // fix 4: the kill his walk was turned for (once per kill)
};
struct R5BKill
{
  float jo[3] = {};      // where Jo stood when the kill came on record
  bool have_jo = false;
  bool shot = false;     // a shot of hers within kR5BShotS before it
  bool silenced = false;
};
std::unordered_map<uint32_t, R5BState> g_r5b;
std::vector<R5BKill> g_r5b_kills; // parallel to g_kills

void R5BReset()
{
  g_r5b.clear();
  g_r5b_kills.clear();
}

R5BState &R5BOf(uint32_t c, const Guard &g)
{
  if (g_r5b.size() > 512)
  {
    g_r5b.clear();
  }
  R5BState &s = g_r5b[c];
  if (s.born != g.r2_born)
  {
    s = R5BState();
    s.born = g.r2_born;
  }
  return s;
}

// Fix 4, once a frame per tracked guard (helper H's witness tick): a new kill on record gets Jo's place and her shot.
// A size compare when nothing is new.
void R5BKillSync(const uint8_t *base, float now)
{
  if (g_r5b_kills.size() == g_kills.size())
  {
    return;
  }
  if (g_r5b_kills.size() > g_kills.size())
  {
    g_r5b_kills.clear(); // (the record was cleared: a level start)
  }
  const uint32_t jo = Load32(base, kPlayerOne);
  while (g_r5b_kills.size() < g_kills.size())
  {
    const Kill &k = g_kills[g_r5b_kills.size()];
    R5BKill r;
    // Only a kill that is fresh now has her place of that moment (an older one, seen late, has none: body walk).
    if (Ptr(jo) && now >= k.at && now - k.at < 0.5f)
    {
      PosOf(base, jo, r.jo);
      r.have_jo = true;
      for (const Shot &sh : g_shots)
      {
        if (sh.shooter == jo && k.at >= sh.at && k.at - sh.at < kR5BShotS)
        {
          r.shot = true;
          r.silenced = sh.silenced;
        }
      }
    }
    g_r5b_kills.push_back(r);
  }
}

// Fix 4, from helper H's witness tick just before his walk is handed over: `walk_pos` (the body) becomes the place
// he goes to look at instead. Once per guard and kill.
void R5BShotFrom(uint8_t *base, uint32_t c, Guard &g, int ki, float walk_pos[3], float now)
{
  if (!REXCVAR_GET(pdz_stealth_shot_from_search) || !REXCVAR_GET(pdz_lab_guard_stealth) || !RarePlus() || ki < 0 ||
      size_t(ki) >= g_kills.size() || size_t(ki) >= g_r5b_kills.size())
  {
    return;
  }
  (void)now;
  R5BState &s = R5BOf(c, g);
  if (s.shot_kill == ki)
  {
    return;
  }
  s.shot_kill = ki;
  const Kill &k = g_kills[size_t(ki)];
  const R5BKill &r = g_r5b_kills[size_t(ki)];
  float me[3];
  PosOf(base, c, me);
  const float beside = Dist3(me, k.pos);
  const char *no = !r.have_jo                 ? "her place at the kill is not on record"
                   : !r.shot                  ? "no shot of hers on record before it (melee, a thrown weapon)"
                   : beside > kR5BBesideCm    ? "he is not beside the body"
                                              : nullptr;
  float dx = r.jo[0] - k.pos[0], dz = r.jo[2] - k.pos[2];
  const float d = std::sqrt(dx * dx + dz * dz);
  if (!no && d < 300.0f)
  {
    no = "the shot came from right beside the body";
  }
  if (no)
  {
    REXLOG_INFO("pdz_stealth_r5: SHOT FROM: {} witness of {}: his walk stays the walk to the body: {} (he is {:.0f} cm from it)",
                Id(base, c), k.who, no, beside);
    return;
  }
  dx /= d;
  dz /= d;
  const float reach = std::max(1000.0f, float(REXCVAR_GET(pdz_lab_stealth_glimpse_reach_cm)));
  float spot[3] = {}, walk = 0.0f;
  const char *what = nullptr;
  if (!r.silenced)
  {
    std::memcpy(spot, r.jo, sizeof(spot));
    if (CanReachOnFloor(base, c, spot, reach, walk))
    {
      what = "an UNSILENCED shot: the place it came from";
    }
  }
  if (!what)
  {
    // The direction of the hit only: steps along it from the body, the furthest one he can walk to.
    const float furthest = std::min(kR5BSilencedCm, d - kR5BKeepOffCm);
    for (float step = furthest; step >= 300.0f && !what; step -= 300.0f)
    {
      spot[0] = k.pos[0] + dx * step;
      spot[1] = k.pos[1];
      spot[2] = k.pos[2] + dz * step;
      if (CanReachOnFloor(base, c, spot, reach, walk))
      {
        what = r.silenced ? "a SILENCED shot: only the direction of the hit" : "an unsilenced shot from where he cannot walk to: its direction";
      }
    }
  }
  if (!what)
  {
    REXLOG_INFO("pdz_stealth_r5: SHOT FROM: {} witness of {} ({} shot from {:.0f} cm away): no spot that way he can walk to on "
                "his floor (or she stood within {:.0f} cm): his walk stays the walk to the body",
                Id(base, c), k.who, r.silenced ? "silenced" : "unsilenced", d, kR5BKeepOffCm + 300.0f);
    return;
  }
  REXLOG_INFO("pdz_stealth_r5: SHOT FROM: {} witness of {}, {:.0f} cm from the body: {}: his witness walk goes to ({:.0f} {:.0f} "
              "{:.0f}), {:.0f} cm from the body towards the shooter (she stood {:.0f} cm from it; a pad route of {:.0f} cm), in "
              "place of the look round the body; same cap, same end (pdz_stealth_shot_from_search) | {}",
              Id(base, c), k.who, beside, what, spot[0], spot[1], spot[2],
              std::sqrt((spot[0] - k.pos[0]) * (spot[0] - k.pos[0]) + (spot[2] - k.pos[2]) * (spot[2] - k.pos[2])), d, walk,
              StackText(base, c));
  std::memcpy(walk_pos, spot, sizeof(spot));
}

// Fix 2, from RarePlusTick at the meter's notice level (his sight line has been asked for). True: our noise check
// walks him to the place of the glimpse (no look round on the spot is started by the caller).
bool R5BGlimpseWalk(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t jo, float now, bool in_search)
{
  if (!REXCVAR_GET(pdz_stealth_glimpse_walk) || !REXCVAR_GET(pdz_lab_guard_stealth) || g.scientist || g.dead || !LayerGuard(g))
  {
    return false;
  }
  R5BState &s = R5BOf(c, g);
  float spot[3];
  std::memcpy(spot, g.glimpse_pos, sizeof(spot));
  float me[3];
  PosOf(base, c, me);
  const float d = Dist3(me, spot);
  const float reach = float(REXCVAR_GET(pdz_lab_stealth_glimpse_reach_cm));
  float walk = 0.0f;
  bool same = false;
  for (const auto &p : s.spots)
  {
    same = same || Dist3(p.data(), spot) < kR5BSpotCm;
  }
  const char *no = now - g.glimpse_time > 1.0f || now < g.glimpse_time ? "the place of the glimpse is not fresh"
                   : d < 300.0f                                         ? "it is right where he stands"
                   : same                                               ? "he has looked at this spot already (one investigation per spot)"
                   : in_search || (Load32(base, c + kConfig) & kSearching) != 0 ? "Rare has him in a SEARCH"
                   : R2TopClass(base, c) != 0x82092A8Cu                 ? "Rare has him busy (not his DEFAULT script)"
                                                                        : R3HStays(base, c, g);
  if (!no && R2NoRestart(base, c, g, jo, now, "glimpse"))
  {
    no = "the search cap's one restart is used up (or a low long-range glimpse)";
  }
  if (!no && !CanReachOnFloor(base, c, spot, reach > 0.0f ? reach : 3000.0f, walk))
  {
    no = "he cannot walk there on his own floor (another floor, stairs, or no open pad route in reach)";
  }
  const uint32_t pad = no ? 0 : R3HSpotPad(base, c, spot);
  if (!no && !Ptr(pad))
  {
    no = "no pad beside it";
  }
  if (!no)
  {
    if (g.st == St::Unaware)
    {
      Set(base, c, g, St::Wary, now, "noticed something: wary (pdz_stealth_glimpse_walk)");
    }
    R3HState &h = R3HOf(c, g);
    if (R3HGoLook(ctx, base, c, g, h, spot, pad, 2, now,
                  fmt::format("glimpsed something {:.0f} cm away (pdz_stealth_glimpse_walk): he goes to look where he saw it", d)))
    {
      h.react_at = now; // (no heard reaction on top of it)
      PosOf(base, c, h.check_pos);
      h.check_at = now + 3.0f;
      if (s.spots.size() >= 16)
      {
        s.spots.erase(s.spots.begin());
      }
      s.spots.push_back({spot[0], spot[1], spot[2]});
      float pp[3] = {};
      PadPos(base, pad, pp);
      REXLOG_INFO("pdz_stealth_r5: GLIMPSE WALK: {} glimpsed something at ({:.0f} {:.0f} {:.0f}), {:.0f} cm away (meter {:.0f}): he "
                  "walks there to look: our noise check, its first leg the pad {:08X} at ({:.0f} {:.0f} {:.0f}), a pad route of "
                  "{:.0f} cm on his floor, {:.0f} s at most, then his give-up line and back to his post (spot {} of his) | {}",
                  Id(base, c), spot[0], spot[1], spot[2], d, g.meter, pad, pp[0], pp[1], pp[2], walk, std::max(0.0, R2CapS()),
                  s.spots.size(), StackText(base, c));
      return true;
    }
    no = "no leg could start";
  }
  if (now - s.refuse_log > 3.0f || now < s.refuse_log)
  {
    s.refuse_log = now;
    REXLOG_INFO("pdz_stealth_r5: GLIMPSE WALK: {} glimpsed something at ({:.0f} {:.0f} {:.0f}), {:.0f} cm away, height {:+.0f} cm "
                "(meter {:.0f}): NO walk: {}: he looks round where he stands",
                Id(base, c), spot[0], spot[1], spot[2], d, spot[1] - me[1], g.meter, no);
  }
  return false;
}
