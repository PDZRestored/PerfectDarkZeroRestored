// Stealth layer round 3, Sun 15 Nov, helper L (item 3: every search end has a line; item 9: the Laboratory's
// lights-out conversation). Included by lab_stealth.cpp inside its anonymous namespace, after
// lab_stealth_round2_Sun15Nov.inc. Log prefix "pdz_stealth_r3: END LINE:" / "pdz_stealth_r3: LIGHTS OUT:".
//
// ITEM 3, EVERY SEARCH END HAS A LINE (pdz_stealth_end_line, on by default; pdz_lab_rareplus and a guard under the
// layer). Whenever a search, a look round where he stands or a check of a lamp (noise, body) of his ends with nothing
// found, the player hears one give-up line from him.
//   Why the line was missing in the evening logs (Subway 18:26:42 saf3, Lab 18:46:16 s3g4#2, 18:46:39 s3g7): Rare's
//   say-event (sub_8262AA40) refuses a line by (1) the speaker's own gap (speech +100 against the time of his last
//   line, +144) and category mask (+196) in sub_8262B378, skipped when r6 != 0; (2) the event's group mask (+188,
//   sub_8262BC00), never skipped; (3) a request already pending (+216: sub_8262B488 drops the new one). Then the
//   squad's speech arbiter (sub_82152E20) can hold a queued line for its group's squad-wide gap (20-30 s) until the
//   speech task gives up after 2 s. In all three cases Rare's SEARCH_ABANDON came 1-2 s after his SEARCH_START, so
//   (1) refused it. And two rules of OURS made it worse: the say hook records Rare's own line in his repeat table
//   (said_at) before Rare's speech refuses it, and gives the time back only for a line of ours, so round 2's second
//   ask 1.5 s later was refused by our repeat rule ("he said this line less than pdz_lab_stealth_repeat_s ago");
//   and round 2 took Rare's request ("just said") for a line that played. Lab 18:41:39 s1m5 (a lamp found dark):
//   round 11b gave a brief lamp check no "never mind" at all. Lab 18:42:28 s1m5: the say hook refuses Rare's
//   SEARCH_ABANDON during a look round where he stands (round 5), and nothing said one at its end.
//   What R3EndLine does: it only notes that his line is due (safe from any hook; once per ended search: a second
//   report of the same end within 10 s of the last line is ignored). R3EndTick (once a frame per tracked guard,
//   after R2Tick) then: (a) counts the line as heard when a give-up line of his REALLY started (g_lines, played)
//   from 2 s before the end on: Rare's own SEARCH_ABANDON (41 / 42) or our "never mind"; (b) waits while his speech
//   is busy (a request pending at +216, a speech task at +64, a line of his followed in g_said_lines, a held alert
//   line, a line of his playing); (c) else asks: SEARCH_ABANDON or the "never mind" (Rare's recorded
//   HEAR_NOISE_NOT_INVESTIGATE_TEAM, asked for as HEAR_UNKNOWN_TEAM with the cue name swapped, as round 4's
//   NeverMind), the two in turn, with Rare's own "skip the speaker's gap and category rules" (r6 = 1), our repeat
//   rule's entry dropped when that line never played, and the line marked strong so the squad's arbiter does not
//   hold it; (d) gives up after 6 asks or 15 s ("NOT HEARD", logged). Never a stream: one line, then done.
//   Not when: he is dead, he sees Jo now (Rare's valid target, or a real sighting / hit in the last 1.5 s), he is
//   fighting (state combat / lost, or Rare's COMBAT that did not begin blind), a new check of ours began, a
//   scientist (no search lines), a voice with no give-up lines (WOMAN1 / WOMAN2).
//   Which line first: a lamp or noise check (why "lamp..." / "noise...") the "never mind"; a search SEARCH_ABANDON,
//   except for a voice with no take table (the GANGSTA voices: Rare's SEARCH_ABANDON takes are mostly "she's gone"
//   lines and cannot be steered) when he has not seen Jo: the "never mind" first there too.
//   The takes: SEARCH_ABANDON by the say hook's take rules (troopers: never "she" before his own sighting, so
//   SOLO takes 3 "I got no time for this." / 4 "Tango must have bugged out. Fun's over."). The "never mind": round
//   4's masks for the swapped cue (PrimeBegin, nevermind_at): TROOPER1 takes 1 2 3 5 ("Nevermind." / "Probably
//   just the wind." / "Maybe a bird or something." / "Don't know, don't care."), TROOPER2 1 3 5 ("Never mind,
//   you're just jumpy." / "...a bird, or a rat..." / "I don't know, and I care even less."), TROOPER3 1 2 ("Never
//   mind." / "Could be anything, don't worry."); GANGSTA1-3 XACT's pick of all five (every take is a plain give-up:
//   "I guess it was nothing.", "Just the noise, I guess.", "Like I care!", "Whatever, it's not my problem!",
//   "It's cool, don't worry!" and the like).

bool R3EndOn()
{
  return RarePlus() && REXCVAR_GET(pdz_stealth_end_line);
}

bool R3Log()
{
  return Logging() || StealthLog();
}

// A voice whose "never mind" can be played (the cue swap knows its set: ArmSwap's own rule).
bool R3CanNeverMind(uint32_t voice)
{
  return (voice >= 17 && voice <= 19) || (ZoneLevel() && GuardVoice(voice));
}

// A give-up line of his that really started at or after `from` (its event in *ev).
bool R3Heard(const uint8_t *base, uint32_t c, const Guard &g, float from, uint32_t *ev)
{
  const uint32_t nm = EventIndex(base, "HEAR_UNKNOWN_TEAM");
  for (const Line &l : g_lines)
  {
    if (l.c == c && l.played && l.start >= from &&
        (l.ev == kEvAbandon || l.ev == kEvAbandon + 1 || (nm && l.ev == nm && g.r3l_ev == nm)))
    {
      if (ev)
      {
        *ev = l.ev;
      }
      return true;
    }
  }
  return false;
}

// His speech is busy: a line of his is queued, waiting, being followed or playing (a new ask now would be dropped by
// Rare's speech, or would come on top of a line that may still be his give-up line).
const char *R3Busy(const uint8_t *base, uint32_t c, const Guard &g, float now)
{
  const uint32_t voice = c + kVoice;
  if (Load32(base, voice + 216) != 0)
  {
    return "a request of his is pending in Rare's speech";
  }
  if (Ptr(Load32(base, voice + 64)))
  {
    return "his speech task is running (a line waiting or playing)";
  }
  if (g.hold_ev)
  {
    return "an alert line of his is held";
  }
  for (const Said &s : g_said_lines)
  {
    if (s.c == c)
    {
      return "a line of his is still followed to its start";
    }
  }
  for (const Line &l : g_lines)
  {
    if (l.c == c && l.played && l.end > now && now >= l.start)
    {
      return "a line of his is playing";
    }
  }
  return nullptr;
}

// Why he gets no give-up line now (nullptr: he may have one).
const char *R3NoLine(const uint8_t *base, uint32_t c, const Guard &g, float now, bool in_combat)
{
  if (g.dead || g.st == St::Dead || !(LoadF(base, c + kHealth) > 0.0f))
  {
    return "he is dead";
  }
  if (((Load32(base, c + kSight) & kValidTarget) != 0 && !FightsNonPlayer(base, c)) || R2Seen(g, now, 1.5f))
  {
    return "he sees Jo now (a real sighting or a hit)";
  }
  if (g.st == St::Combat || g.st == St::Lost || (in_combat && g.r2_blind_at < -1e8f))
  {
    return "he is fighting (real combat)";
  }
  return nullptr;
}

// ITEM 3: the one entry point. A search / look round / investigation of his has ended; `why` says which (a "lamp...",
// "noise..." or "sound..." why gets the "never mind" first). Notes the line as due; R3EndTick asks for it.
void R3EndLine(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, const char *why)
{
  (void)ctx;
  if (!R3EndOn() || !Ptr(c) || g.scientist || !LayerGuard(g))
  {
    return;
  }
  const char *what = why ? why : "";
  if (R10NoGiveUpAfterBody(base, c, g, now, what))
  {
    return; // (Thu 19 Nov: never a give-up line of ours after a kill: Rare's own follow-up instead)
  }
  if (g.r3l_at > -1e8f && now >= g.r3l_at && now - g.r3l_at <= 15.0f)
  {
    return; // his line is already due (the same end reported by another end point)
  }
  if (g.r3l_done > -1e8f && now >= g.r3l_done && now - g.r3l_done < 10.0f)
  {
    return; // he had (or was given up on) a give-up line less than 10 s ago: the same end, once
  }
  const char *no = R3NoLine(base, c, g, now, false);
  const char *vs = VoiceSetName(g.voice);
  if (!no && vs && std::strncmp(vs, "WOMAN", 5) == 0)
  {
    no = "his voice set has no give-up lines (WOMAN1 / WOMAN2 recorded none)";
  }
  if (no)
  {
    if (R3Log() && std::strcmp(no, "he is dead") != 0 && (now - g.r3l_log > 5.0f || now < g.r3l_log))
    {
      g.r3l_log = now;
      REXLOG_INFO("pdz_stealth_r3: END LINE: {} {}: no give-up line: {} (state {})", Id(base, c), what, no, StName(g.st));
    }
    return;
  }
  uint32_t ev = 0;
  if (R3Heard(base, c, g, now - 2.0f, &ev))
  {
    g.r3l_done = now;
    if (R3Log())
    {
      REXLOG_INFO("pdz_stealth_r3: END LINE: {} {}: his give-up line {} already played (state {})", Id(base, c), what,
                  EventName(base, ev), StName(g.st));
    }
    return;
  }
  g.r3l_at = now;
  g.r3l_next = now;
  g.r3l_tries = 0;
  g.r3l_ev = 0;
  g.r3l_inv = std::strncmp(what, "lamp", 4) == 0 || std::strncmp(what, "noise", 5) == 0 || std::strncmp(what, "sound", 5) == 0;
  g.r3l_why = what;
  if (R3Log())
  {
    REXLOG_INFO("pdz_stealth_r3: END LINE: {} {} (state {}, voice set {}): his give-up line is due (followed until it really "
                "plays, 15 s at most)",
                Id(base, c), what, StName(g.st), vs ? vs : "?");
  }
}

// ITEM 3, from the say hook: Rare asks for SEARCH_ABANDON (Rare's own end of a search, or of his look round where
// he stands). A leg of our lamp check, a sweep leg of a hunt and a fight are not the end of his search.
void R3RareAbandon(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now)
{
  if (!R3EndOn() || !(g.st == St::Unaware || g.st == St::Wary || g.st == St::Suspicious || g.st == St::Searching ||
                      g.st == St::LostSearch))
  {
    return;
  }
  R3EndLine(ctx, base, c, g, now,
            Looking(g, now) ? "his look round where he stands ended (Rare's SEARCH_ABANDON asked for)"
                            : "Rare's own end of his search (Rare's SEARCH_ABANDON asked for)");
}

// ITEM 3: once a frame per tracked guard (After, after R2Tick).
void R3EndTick(PPCContext &ctx, uint8_t *base, uint32_t c, Guard &g, float now, bool in_combat)
{
  if (g.r3l_at < -1e8f)
  {
    return;
  }
  if (now < g.r3l_at || !R3EndOn())
  {
    g.r3l_at = -1e9f; // (a new level: the clock went back; or the switch went off)
    return;
  }
  uint32_t heard_ev = 0;
  if (R3Heard(base, c, g, g.r3l_at - 2.0f, &heard_ev))
  {
    if (R3Log())
    {
      REXLOG_INFO("pdz_stealth_r3: END LINE: {} HEARD: {} really played {:.1f} s after the end ({}; {} ask(s) of ours)",
                  Id(base, c), EventName(base, heard_ev), now - g.r3l_at, g.r3l_why, g.r3l_tries);
    }
    g.r3l_at = -1e9f;
    g.r3l_done = now;
    return;
  }
  const char *stop = R3NoLine(base, c, g, now, in_combat);
  if (!stop && g.st == St::Investigating && g.inv_start > g.r3l_at + 0.5f)
  {
    stop = "a new check of his began";
  }
  if (!stop && (g.st == St::Hunting || g.st == St::HuntPause))
  {
    stop = "he hunts her now (a confirmed contact)";
  }
  if (!stop && now - g.r3l_at > 15.0f)
  {
    stop = "NOT HEARD within 15 s";
  }
  if (!stop && g.r3l_tries >= 6)
  {
    stop = "NOT HEARD after 6 asks";
  }
  if (stop)
  {
    if (R3Log() && std::strcmp(stop, "he is dead") != 0)
    {
      REXLOG_INFO("pdz_stealth_r3: END LINE: {} no give-up line after all: {} ({}; {} ask(s), the last {})", Id(base, c), stop,
                  g.r3l_why, g.r3l_tries, g.r3l_ev ? EventName(base, g.r3l_ev) : std::string("none"));
    }
    g.r3l_at = -1e9f;
    g.r3l_done = now;
    return;
  }
  if (now < g.r3l_next || R3Busy(base, c, g, now))
  {
    return;
  }
  const uint32_t nm = EventIndex(base, "HEAR_UNKNOWN_TEAM");
  const bool can_nm = nm && R3CanNeverMind(g.voice);
  const bool may_she = g.seen_jo || AfterContact(g);
  const bool abandon_first = !g.r3l_inv && (Takes(base, g.voice, kEvAbandon) != nullptr || may_she);
  const bool even = (g.r3l_tries & 1) == 0;
  const bool use_nm = can_nm && (even ? !abandon_first : abandon_first);
  const uint32_t ev = use_nm ? nm : kEvAbandon;
  ++g.r3l_tries;
  // Our repeat rule counts a line of his only when it played: an entry of a line that never started (Rare's speech
  // refused it; the say hook keeps the entry of Rare's own lines) is dropped. A played one stays in g_lines for 15 s.
  if (const auto it = g.said_at.find(ev); it != g.said_at.end() && now >= it->second && now - it->second < 15.0f)
  {
    bool played = false;
    for (const Line &l : g_lines)
    {
      played = played || (l.c == c && l.ev == ev && l.played);
    }
    if (!played)
    {
      g.said_at.erase(it);
    }
  }
  if (use_nm)
  {
    g.nevermind_at = now; // (PrimeBegin: the takes that work said to nobody in particular)
    ArmSwap(base, c, g.voice, "HEAR_NOISE_NOT_INVESTIGATE_TEAM", now);
  }
  const uint32_t admit_before = g_admit_serial;
  const bool force_before = g_say_force;
  g_say_force = true; // Rare's own r6: skip the speaker's gap and category rules (the group rule stays)
  const bool said = Say(ctx, base, c, ev, false, use_nm ? "give-up line: never mind (round 3)" : "give-up line (round 3)");
  g_say_force = force_before;
  if (!said && use_nm)
  {
    DisarmSwap();
    g.nevermind_at = -1e9f;
  }
  g.r3l_ev = ev;
  if (said)
  {
    // Strong: the squad's speech arbiter (its group's squad-wide gap) does not hold it.
    for (auto it = g_said_lines.rbegin(); it != g_said_lines.rend(); ++it)
    {
      if (it->c == c && it->ev == ev)
      {
        it->strong = true;
        break;
      }
    }
    g.r3l_next = now + 0.3f; // (then R3Busy holds the next ask until it started or was lost)
  }
  else
  {
    g.r3l_next = now + 0.5f;
  }
  // A refusal by the layer's own line rules (story speech playing, mostly) is a wait, not one of the 6 asks: the
  // line is asked for again every 0.5 s until it may be said (the 15 s limit stays). Found in the Lab smoke run.
  const bool layer_wait = !said && g_admit_serial == admit_before;
  if (layer_wait)
  {
    --g.r3l_tries;
  }
  if (R3Log() && (!layer_wait || static_cast<int>((now - g.r3l_at) * 2.0f) % 6 == 0)) // (a waiting line: one log line per 3 s)
  {
    REXLOG_INFO("pdz_stealth_r3: END LINE: {} ask {} of 6: {}{} {} ({}, {:.1f} s after the end)", Id(base, c), g.r3l_tries + (layer_wait ? 1 : 0),
                use_nm ? "the \"never mind\" (HEAR_NOISE_NOT_INVESTIGATE_TEAM as " : "", EventName(base, ev),
                said ? (use_nm ? ") queued: followed to its real start" : "queued: followed to its real start")
                     : g_admit_serial != admit_before ? (use_nm ? ") refused by Rare's speech" : "refused by Rare's speech")
                                                      : (use_nm ? ") refused by the layer's line rules" : "refused by the layer's line rules"),
                g.r3l_why, now - g.r3l_at);
  }
}

// ITEM 9 (a): the layer's wall test for restorations.cpp's lights-out conversation (its "no wall between" rule).
// The function is part of the lab_stealth interface (lab_stealth.h), so the anonymous namespace this file is
// included in is closed for it and opened again after it.
} // namespace
namespace lab_stealth
{
  bool SightLineClear(PPCContext &ctx, uint8_t *base, const float a[3], const float b[3])
  {
    return Ptr(Load32(base, kPlayerOne)) && SightClear(ctx, base, a, b); // (a level is running: Rare's collision exists)
  }
} // namespace lab_stealth
namespace
{
