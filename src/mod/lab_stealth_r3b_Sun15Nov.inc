// Stealth layer round 3b, Sun 15 Nov (helper B): three fixes from the hand test of 19:52-20:06. Included by
// lab_stealth.cpp inside its anonymous namespace, after lab_stealth_r3_H_Sun15Nov.inc. Log prefix "pdz_stealth_r3:"
// (fix 1 logs as round 2's witness test does, "pdz_stealth_r2: WITNESS:"). No new switch: each fix belongs to an
// existing one.
//
// Fix 1, the scientist witness uses the guard test (pdz_stealth_staff_witness). StaffWitness (the follow-up .inc) now
// calls round 2's witness test R2WitnessSees (the round 2 .inc: in front of him or right beside him, a clear line, and
// the light at the body for that distance, the light meter's range scaling), the one test R2Witness uses too, and
// keeps one witness event per body in the record round 3 keeps for guards (Guard::r3_once_done). The scientist's own
// reach (15 m) is kept. And the say hook's "says" line names the cue really asked for (R3BSaysName): a scientist's
// line is asked for as Rare's LIGHT_SHOT_OUT event with its cue swapped (WarnTick), a glimpse line as HEAR_UNKNOWN
// with its cue swapped.
//
// Fix 2, one reaction per body found (pdz_stealth_witness_once) and restarts do not reset the cap (the round 2 cap,
// pdz_stealth_search_cap_s).
//   (a) Rare's "a body" stimulus 0040 is +6260 0x1000, raised by Rare's perception scan sub_82580508 at every pass
//       (about once a second) while a corpse object (type 43) is in his perception (82580980-825809A0); nothing of
//       ours raises it. Each one re-runs his DEFAULT reaction, which starts a new SEARCH beside his DEFAULT script
//       (Mansion 20:00:20-42, fre2: a new task every second). Rare's reaction acts on the nearest body in his view
//       (sub_8218B340 with type 43, its SEE_BODY pick 821D12D4), so that body is what the 0040 is about: the body is
//       keyed by its victim (the corpse constructor's record g_corpses), else by the corpse object. His first 0040
//       for a body is Rare's (let through, recorded); a later one for a body he has found, or for a body whose death
//       he reacted to as a witness (r3_once_done), is taken from Rare's word (with the WARNER bit 0004 when nothing
//       else came with it).
//   (b) The cause of each SEARCH start is noted (the body of the 0040 / 0010 that came with it, else the pad of the
//       sound of a 1000); a search that restarts more than 3 s after the last one, for the same cause, within the cap
//       of the first start, keeps the first start time (R3BEpisode, from R2SearchCap). A search restarted after the
//       cap within the same episode (Rare restarts a new task within 3 s of the ended one: Mansion 20:00:28) is ended
//       too (R3BCappedRestart), with round 2's safety rule (only the top task or the one beside his DEFAULT script,
//       never early in a walk), at most 10 times per episode; a NEW cause after the cap is a new search, round 2's
//       one restart.
//   (c) A warning (REXLOG_WARN) when a guard's SEARCH restarts more than twice in 5 s (R3BSearchStart, from the SEARCH
//       start hook), not while the alarm sounds (Rare starts one every frame then).
//
// Fix 3, a witness always walks to the body (pdz_stealth_witness_walk). Helper H's walk (lab_stealth_r3_H_Sun15Nov.inc)
// is now claimed at a guard's FIRST witness reaction to a body (Rare's own first 0010 for it, round 3's witness-once
// record, or round 2's witness raise), whatever Rare's response is; and when Rare already has him in a SEARCH (Mansion
// 20:01:28, fre3: Rare's search on the spot from the body's fall, stimulus 1000), that search is TAKEN OVER: ended
// here (R3BTakeOver, from the SEARCH update hook, the way round 2's cap ends one: Rare's own end of search, its
// what-next handler taken off when it runs beside his DEFAULT script) so that his walk is handed over in place of his
// DEFAULT script and no second search is stacked.
//
// Testing: pdz_test_stealth_kill_at "id,x,y,z": the guard is put at (x, z) on his own floor (within 2 m of y) for a
// second, then killed there as pdz_test_stealth_kill does (R3BTestKillAt).

// ---- Fix 1 --------------------------------------------------------------------------------------------------------

// The cue a "says" line names: the one really asked for (`warn`: the say is WarnTick's warning line).
std::string R3BSaysName(const uint8_t *base, const Guard &g, uint32_t event, bool warn)
{
  if (warn && event == g.warn_ev)
  {
    if (g.scientist && !g.sci_cue.empty() && g.sci_cue != "LIGHT_SHOT_OUT")
    {
      return fmt::format("{}_{} (asked for as Rare's {} event {}, its cue swapped)", g.sci_bank, g.sci_cue, EventName(base, event),
                         event);
    }
    if (!g.scientist && g.warn_swap && VoiceSetName(g.voice))
    {
      return fmt::format("{}_{} (asked for as Rare's {} event {}, its cue swapped)", VoiceSetName(g.voice), g.warn_swap,
                         EventName(base, event), event);
    }
  }
  return fmt::format("{} ({})", EventName(base, event), event);
}

// ---- Fix 2 --------------------------------------------------------------------------------------------------------

constexpr float kR3BRestartWindow = 5.0f; // s: (c) more than two restarts in this long is a warning
constexpr int kR3BCappedEnds = 10;        // (b) restarts after the cap ended per episode, then Rare's

enum : uint32_t
{
  kR3BCauseBody = 1,   // id: the victim (a character)
  kR3BCauseSound = 2,  // id: the pad of the sound he heard
  kR3BCauseCorpse = 3, // id: a corpse object whose victim is not on record
};

struct R3BState
{
  float born = -2e9f; // his Guard's r2_born when this was made (a new guard at the same address starts afresh)
  // (a)
  std::vector<std::pair<uint64_t, float>> found; // bodies he has reacted to (key, AI clock of his reaction)
  uint64_t look_key = 0;                         // the body of the last 0040 (key) ...
  float look_at = -1e9f;                         // ... looked up then
  float first_at = -1e9f;                        // his last first reaction to a body (a second build in the frame)
  uint64_t first_key = 0;
  int repeats = 0;
  float repeat_log = -1e9f, none_log = -1e9f;
  // (b)
  uint64_t cause = 0; // the cause of his last stimulus word that can start a search ...
  float cause_at = -1e9f;
  uint64_t start_cause = 0; // ... of his last SEARCH start
  float start_at = -1e9f;
  uint32_t start_task = 0;
  uint64_t ep_cause = 0; // ... of the search episode round 2's cap counts
  int ep_ended = 0;      // restarts after the cap ended in this episode
  bool ep_left = false;
  // (c)
  std::vector<float> starts;
  float warn_log = -1e9f;
};
std::unordered_map<uint32_t, R3BState> g_r3b;

R3BState &R3BOf(uint32_t c, const Guard &g)
{
  if (g_r3b.size() > 512)
  {
    g_r3b.clear();
  }
  R3BState &s = g_r3b[c];
  if (s.born != g.r2_born)
  {
    s = R3BState();
    s.born = g.r2_born;
  }
  return s;
}

uint64_t R3BKey(uint32_t kind, uint32_t id)
{
  return (uint64_t(kind) << 32) | id;
}

std::string R3BCauseText(const uint8_t *base, uint64_t key)
{
  const uint32_t kind = uint32_t(key >> 32), id = uint32_t(key);
  return kind == kR3BCauseBody     ? fmt::format("the body of {}", Ptr(id) ? Id(base, id) : std::string("?"))
         : kind == kR3BCauseCorpse ? fmt::format("a body (corpse object {:08X})", id)
         : kind == kR3BCauseSound  ? fmt::format("a sound at pad {:08X}", id)
                                   : std::string("no cause on record");
}

// (a), from the stimulus hook (pdz_lab_rareplus), before the other rules: the bits to take from Rare's word.
uint32_t R3BBodyDrop(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
{
  if (!(word & 0x40) || !REXCVAR_GET(pdz_stealth_witness_once) || !REXCVAR_GET(pdz_lab_guard_stealth) || !RarePlus() ||
      g.scientist || !LayerGuard(g) || g.dead || g.st == St::Dead)
  {
    return 0;
  }
  R3BState &s = R3BOf(c, g);
  // The body Rare's reaction picks: the nearest corpse in his own view list.
  uint32_t object = 0;
  const int32_t slot = int32_t(Load32(base, c + kProfileSlot));
  if (slot >= 0 && uint32_t(slot) < kProfileSlots)
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = uint32_t(slot);
    frame.ctx.r4.u64 = kCorpseType;
    frame.ctx.r5.u64 = 0;
    sub_8218B340(frame.ctx, base);
    object = IsCorpse(base, frame.ctx.r3.u32) ? frame.ctx.r3.u32 : 0;
  }
  if (!object)
  {
    if (now - s.none_log >= 10.0f || now < s.none_log)
    {
      s.none_log = now;
      REXLOG_INFO("pdz_stealth_r3: BODY ONCE: {} Rare's \"a body\" (0040) with no corpse in his view list (state {}): Rare's, let "
                  "through",
                  Id(base, c), StName(g.st));
    }
    return 0;
  }
  uint32_t victim = 0;
  for (auto it = g_corpses.rbegin(); it != g_corpses.rend(); ++it)
  {
    if (it->object == object)
    {
      victim = it->victim;
      break;
    }
  }
  const uint64_t key = victim ? R3BKey(kR3BCauseBody, victim) : R3BKey(kR3BCauseCorpse, object);
  s.look_key = key;
  s.look_at = now;
  float where[3] = {}, me[3];
  const bool have = ObjectPos(ctx, base, object, where);
  PosOf(base, c, me);
  // Found before: by an 0040 of his, or as a witness of the death (round 3's record: Rare's 0010 or our raise).
  const auto known = std::find_if(s.found.begin(), s.found.end(), [key](const auto &f) { return f.first == key; });
  bool witnessed = false;
  for (const auto &[v, at] : g.r3_once_done)
  {
    (void)at;
    witnessed = witnessed || (victim && v == victim);
  }
  if (known == s.found.end() && !witnessed)
  {
    // Round 3c (helper C, fix 4a): a body whose death he could not have seen (refused as a witness) is not noticed
    // either until it passes the same sight test (pdz_stealth_witness_body_test).
    if (victim && R3cCBodyHidden(ctx, base, c, g, victim, have ? where : nullptr, now))
    {
      return (word & 0x4) && !(word & ~0x44u) ? 0x44u : 0x40u;
    }
    // His first reaction to this body: Rare's own.
    if (s.found.size() >= 64)
    {
      s.found.erase(s.found.begin());
    }
    s.found.emplace_back(key, now);
    s.first_at = now;
    s.first_key = key;
    s.repeats = 0;
    REXLOG_INFO("pdz_stealth_r3: BODY ONCE: {} Rare's \"a body\" (0040) for {} (corpse {:08X}{}): his first reaction to this body, "
                "Rare's own; recorded (state {})",
                Id(base, c), R3BCauseText(base, key), object,
                have ? fmt::format(" at ({:.0f} {:.0f} {:.0f}), {:.0f} cm from him", where[0], where[1], where[2], Dist3(me, where))
                     : std::string(),
                StName(g.st));
    return 0;
  }
  if (key == s.first_key && now >= s.first_at && now - s.first_at < 0.1f)
  {
    return 0; // (the same pass's word, built again in this frame: still his first reaction)
  }
  if (R9BodyFollowUp(base, c, g, victim, known != s.found.end(), now))
  {
    // Wed 18 Nov night (rule 5b): his one reaction was the layer's own look: Rare's own follow-up, once.
    if (s.found.size() >= 64)
    {
      s.found.erase(s.found.begin());
    }
    s.found.emplace_back(key, now);
    s.first_at = now;
    s.first_key = key;
    return 0;
  }
  if (R10BodyStill(base, c, g, victim, now))
  {
    // Thu 19 Nov (no freezing): he knows of this body and has stood still for 8 s: Rare's own follow-up again.
    s.first_at = now;
    s.first_key = key;
    return 0;
  }
  ++s.repeats;
  if (s.repeats == 1 || now - s.repeat_log >= 5.0f || now < s.repeat_log)
  {
    s.repeat_log = now;
    const float since = known != s.found.end() ? now - known->second : -1.0f;
    REXLOG_INFO("pdz_stealth_r3: BODY ONCE: {} Rare's \"a body\" (0040) again for {} ({}, repeat {}, state {}): taken from Rare's "
                "word: one reaction per body (no new search)",
                Id(base, c), R3BCauseText(base, key),
                since >= 0.0f ? fmt::format("he found it {:.1f} s ago", since) : std::string("he reacted to this death as a witness"),
                s.repeats, StName(g.st));
  }
  uint32_t drop = 0x40;
  if ((word & 0x4) && !(word & ~0x44u))
  {
    drop |= 0x4; // (the WARNER bit came with that 0040 alone)
  }
  return drop;
}

// (b), from the stimulus hook after all the rules: the cause of what is left of his word (a search may start from it).
void R3BCause(const uint8_t *base, uint32_t c, Guard &g, uint32_t word, float now)
{
  if (!(word & 0x1050) || g.scientist || !LayerGuard(g))
  {
    return;
  }
  R3BState &s = R3BOf(c, g);
  uint64_t cause = 0;
  if ((word & 0x40) && s.look_key && now >= s.look_at && now - s.look_at < 0.1f)
  {
    cause = s.look_key;
  }
  else if ((word & 0x10) && g.r3_once_victim && now >= g.r3_once_at && now - g.r3_once_at < 0.5f)
  {
    cause = R3BKey(kR3BCauseBody, g.r3_once_victim);
  }
  else if (word & 0x1000)
  {
    const bool body_cause = (s.cause >> 32) == kR3BCauseBody || (s.cause >> 32) == kR3BCauseCorpse;
    if (body_cause && now >= s.cause_at && now - s.cause_at < 2.0f)
    {
      return; // (the sound of that body falling: the body stays the cause)
    }
    const uint32_t profile = ProfileOf(base, c);
    const uint32_t pad = profile ? Load32(base, profile + kHeardPad) : 0;
    cause = Ptr(pad) ? R3BKey(kR3BCauseSound, pad) : 0;
  }
  if (cause)
  {
    s.cause = cause;
    s.cause_at = now;
  }
}

// (c) and (b), from the SEARCH start hook: the restart count, and the cause of this start.
void R3BSearchStart(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now)
{
  R3BState &s = R3BOf(c, g);
  s.start_cause = now >= s.cause_at && now - s.cause_at <= 1.5f ? s.cause : 0;
  s.start_at = now;
  s.start_task = task;
  s.starts.erase(std::remove_if(s.starts.begin(), s.starts.end(),
                                [now](float t) { return now < t || now - t > kR3BRestartWindow; }),
                 s.starts.end());
  if (s.starts.size() < 32)
  {
    s.starts.push_back(now);
  }
  const int restarts = int(s.starts.size()) - 1;
  // (round 3c D, fix 6: not for a no_walk guard (the Mansion's tower snipers): R3cDTowerRestart logs what it is)
  if (restarts > 2 && !AlarmOn(base) && !g.dead && !R3cDTowerRestart(base, c, g, restarts, now) &&
      (now - s.warn_log >= 10.0f || now < s.warn_log))
  {
    s.warn_log = now;
    REXLOG_WARN("pdz_stealth_r3: SEARCH RESTARTS: WARNING: {}'s SEARCH restarted {} times in {:.0f} s (task {:08X}, state {}, cause {}): "
                "something starts a new SEARCH for him again and again | {}",
                Id(base, c), restarts, kR3BRestartWindow, task, StName(g.st), R3BCauseText(base, s.start_cause), StackText(base, c));
  }
}

// (b), from R2SearchCap: 0 = round 2's own episode rule, 1 = the same episode although more than 3 s passed (the same
// cause within the cap of the first start: the first start time is kept), 2 = a new episode although less than 3 s
// passed (a new cause after the cap: a new search, round 2's one restart).
int R3BEpisode(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now, double age)
{
  const double cap = R2CapS();
  if (!(cap > 0.0))
  {
    return 0;
  }
  R3BState &s = R3BOf(c, g);
  const float start = now - float(std::clamp(age, 0.0, 180.0));
  const uint64_t cause = s.start_task == task && std::fabs(s.start_at - start) < 1.0f ? s.start_cause : 0;
  const bool gap = now - g.r2_search_last > 3.0f || now < g.r2_search_last;
  if (gap)
  {
    if (cause && cause == s.ep_cause && g.r2_search_unconfirmed && !g.r2_search_capped && now >= g.r2_search_from &&
        now - g.r2_search_from < float(cap) && !R2Seen(g, now, 1.0f))
    {
      REXLOG_INFO("pdz_stealth_r3: SEARCH SAME CAUSE: {} search restarted (task {:08X}, state {}) for the same cause ({}) {:.0f} s after "
                  "the first start: the cap counts from the first start ({:.0f} s left)",
                  Id(base, c), task, StName(g.st), R3BCauseText(base, cause), now - g.r2_search_from,
                  float(cap) - (now - g.r2_search_from));
      return 1;
    }
    s.ep_cause = cause;
    s.ep_ended = 0;
    s.ep_left = false;
    return 0;
  }
  if (g.r2_search_capped && cause && s.ep_cause && cause != s.ep_cause)
  {
    REXLOG_INFO("pdz_stealth_r3: SEARCH SAME CAUSE: {} a search for a NEW cause ({}; his capped search was for {}) right after the cap "
                "(task {:08X}): a new search, round 2's restart rule",
                Id(base, c), R3BCauseText(base, cause), R3BCauseText(base, s.ep_cause), task);
    s.ep_cause = cause;
    s.ep_ended = 0;
    s.ep_left = false;
    return 2;
  }
  return 0;
}

// (b), from R2SearchCap: his search was capped, and Rare has started a new one in the same episode (within 3 s of the
// last: the same cause or none known). true = end it too (the caller moves its start time back).
bool R3BCappedRestart(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now, double age, bool is_top, bool settled)
{
  if (!(R2CapS() > 0.0) || !g.r2_search_unconfirmed || AlarmOn(base) || !(age < 180.0) || !(age >= 0.0))
  {
    return false;
  }
  if (!(now - float(age) > g.r2_cap_end + 0.05f))
  {
    return false; // (the search the cap ended, or one older)
  }
  if (!is_top || !(settled || age > 20.0))
  {
    return false; // (round 2's rule: only the top task or the one beside his DEFAULT script, never early in a walk)
  }
  R3BState &s = R3BOf(c, g);
  if (s.ep_ended >= kR3BCappedEnds)
  {
    if (!s.ep_left)
    {
      s.ep_left = true;
      REXLOG_INFO("pdz_stealth_r3: SEARCH SAME CAUSE: {} Rare restarted his capped search {} times: left to Rare from here (task {:08X})",
                  Id(base, c), s.ep_ended, task);
    }
    return false;
  }
  ++s.ep_ended;
  g.r2_cap_post = true; // (round 2: back to his post, wary, with the give-up line)
  REXLOG_INFO("pdz_stealth_r3: SEARCH SAME CAUSE: {} search restarted after his capped search (task {:08X}, {:.1f} s old, cause {}; "
              "{:.0f} s after the first start): ended too: restarts never reset the cap ({} of {})",
              Id(base, c), task, age, R3BCauseText(base, s.start_task == task ? s.start_cause : 0), now - g.r2_search_from,
              s.ep_ended, kR3BCappedEnds);
  return true;
}

// ---- Fix 3 --------------------------------------------------------------------------------------------------------

// From the SEARCH update hook (after R2SearchCap): Rare's search of a guard whose walk to a body is due is taken over.
// true = end it now (the caller moves its start time back and, for one beside his DEFAULT script, takes off its
// what-next handler after Rare's update).
bool R3BTakeOver(const uint8_t *base, uint32_t c, Guard &g, uint32_t task, float now, double age, bool is_top, bool settled)
{
  if (!REXCVAR_GET(pdz_stealth_witness_walk) || !RarePlus() || g.dead || g.st == St::Dead || g.st == St::Investigating ||
      !(age < 180.0))
  {
    return false;
  }
  const auto it = g_r3h.find(c);
  if (it == g_r3h.end() || it->second.born != g.r2_born)
  {
    return false;
  }
  R3HState &s = it->second;
  if (s.walk_kill < 0 || !s.take_over)
  {
    return false;
  }
  if (!is_top || !(settled || age > 20.0))
  {
    if (now - s.take_log >= 3.0f || now < s.take_log)
    {
      s.take_log = now;
      if (R2Log())
      {
        REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} Rare's SEARCH {:08X} ({:.1f} s old) not ended yet: {} | {}", Id(base, c), task, age,
                    !is_top ? "it is not the top task (or the one beside his DEFAULT script)" : "he is walking (never early in a walk)",
                    StackText(base, c));
      }
    }
    return false;
  }
  REXLOG_INFO("pdz_stealth_r3: WITNESS WALK: {} Rare's SEARCH {:08X} ({:.1f} s old, {:.1f} s after his first reaction to the body) "
              "TAKEN OVER: Rare's own end of search runs now; his walk to the body follows in place of his DEFAULT script | {}",
              Id(base, c), task, age, now - s.walk_from, StackText(base, c));
  return true;
}

// ---- Testing ------------------------------------------------------------------------------------------------------

std::string g_r3b_kill_at_done;
struct R3BKillAt
{
  uint32_t c = 0;
  float x = 0.0f, z = 0.0f;
  float until = -1e9f;
  std::string id;
};
R3BKillAt g_r3b_kill_at;

// pdz_test_stealth_kill_at "id,x,y,z": put him at (x, z) on his floor for a second, then Rare's Kill with Jo as the
// killer (as pdz_test_stealth_kill). From the per-guard test spot (every tracked guard's frame).
void R3BTestKillAt(PPCContext &ctx, uint8_t *base, uint32_t jo, float now)
{
  const std::string want = REXCVAR_GET(pdz_test_stealth_kill_at);
  if (want != g_r3b_kill_at_done)
  {
    g_r3b_kill_at_done = want;
    g_r3b_kill_at = R3BKillAt();
    if (want.empty())
    {
      return;
    }
    std::string text = want;
    std::replace(text.begin(), text.end(), ',', ' ');
    const size_t sp = text.find(' ');
    const std::string id = text.substr(0, sp);
    float v[3] = {};
    int n = 0;
    const char *p = sp == std::string::npos ? "" : text.c_str() + sp;
    for (; n < 3; ++n)
    {
      char *end = nullptr;
      v[n] = std::strtof(p, &end);
      if (end == p)
      {
        break;
      }
      p = end;
    }
    if (n < 3)
    {
      REXLOG_INFO("pdz_lab_stealth: test kill at: \"{}\" is not \"id,x,y,z\"", want);
      return;
    }
    for (auto &[c, g] : g_guards)
    {
      if (g.st == St::Dead || g.dead || Id(base, c) != id || !(LoadF(base, c + kHealth) > 0.0f))
      {
        continue;
      }
      float me[3];
      PosOf(base, c, me);
      if (std::fabs(me[1] - v[1]) > 200.0f)
      {
        REXLOG_INFO("pdz_lab_stealth: test kill at: {} is at ({:.0f} {:.0f} {:.0f}), not on the floor of ({:.0f} {:.0f} {:.0f}) (over 2 m "
                    "up or down): not moved, not killed",
                    id, me[0], me[1], me[2], v[0], v[1], v[2]);
        return;
      }
      g_r3b_kill_at.c = c;
      g_r3b_kill_at.x = v[0];
      g_r3b_kill_at.z = v[2];
      g_r3b_kill_at.until = now + 1.0f;
      g_r3b_kill_at.id = id;
      REXLOG_INFO("pdz_lab_stealth: test kill at: {} moved from ({:.0f} {:.0f} {:.0f}) to ({:.0f} {:.0f} {:.0f}) (his height left to the "
                  "game); Rare's Kill on him there in 1 s",
                  id, me[0], me[1], me[2], v[0], me[1], v[2]);
      break;
    }
    if (!g_r3b_kill_at.c)
    {
      REXLOG_INFO("pdz_lab_stealth: test kill at: no live tracked guard {}", id);
      return;
    }
  }
  const uint32_t c = g_r3b_kill_at.c;
  if (!Ptr(c))
  {
    return;
  }
  const Guard *g = Find(c);
  if (!g || g->dead || g->st == St::Dead || !(LoadF(base, c + kHealth) > 0.0f) || now + 5.0f < g_r3b_kill_at.until)
  {
    g_r3b_kill_at.c = 0;
    return;
  }
  const float xz[2] = {g_r3b_kill_at.x, g_r3b_kill_at.z};
  for (int i = 0; i < 2; ++i)
  {
    uint32_t bits;
    std::memcpy(&bits, &xz[i], 4);
    Store32(base, c + kPosition + 8 * uint32_t(i), bits); // x (+0) and z (+8)
  }
  if (now < g_r3b_kill_at.until)
  {
    return;
  }
  g_r3b_kill_at.c = 0;
  const uint32_t vt = Load32(base, c);
  if (!Ptr(vt) || Load32(base, vt + 0x2E4) != 0x8252DE88 || !Ptr(jo))
  {
    REXLOG_INFO("pdz_lab_stealth: test kill at: {} is not a guard with Rare's Kill (vtable +0x2E4)", g_r3b_kill_at.id);
    return;
  }
  float me[3];
  PosOf(base, c, me);
  REXLOG_INFO("pdz_lab_stealth: test kill at: Rare's Kill on {} ({:08X}) at ({:.0f} {:.0f} {:.0f}) with Jo as the killer", g_r3b_kill_at.id,
              c, me[0], me[1], me[2]);
  const float before = lab_stealth::HitBefore(base, c, jo);
  {
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = c;
    frame.ctx.r4.u64 = jo;
    frame.ctx.r5.u64 = 5;
    frame.ctx.r6.u64 = 1;
    sub_8252DE88(frame.ctx, base);
  }
  lab_stealth::HitAfter(base, c, jo, before);
}
