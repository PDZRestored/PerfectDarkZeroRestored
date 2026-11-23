// Camera slides at 60 (Tue 17 Nov 2026): a diagnostic, no behaviour change (pdz_camera_blend_log, default off).
//
// The report: with single player locked to 60 on the paced path (pdz_sp_120 off, pdz_sp_60_paced on) the ladder climb's
// camera pan and the zoom slide judder; both are smooth at 120, and normal turning and walking at 60 are fine.
// Ruled out from the play log and the code (16 Nov night):
//   - the game's cadence: the log shows 60.0 Swaps a second throughout, every native frame shown;
//   - the presentation: with a guest vblank every second host vblank, each frame is latched at its own guest vblank
//     (SDK presenter, OnGuestVblankLatch) and shown once;
//   - the frame time the game steps with: sub_82140500 takes the raw CPU clock between two frames (82D6BFD8 + 120);
//   - the zoom itself: sub_825AC408 (player vtable +632, called from the player's control update sub_825A79B8 with the
//     frame's dt) is a straight line over a timer: current (+6812) = start (+6808) x timer (+6804) x k + target x the
//     rest, the timer counting down by dt; nothing in it counts frames or vblanks;
//   - our hooks on that path: zoom_steps.cpp passes dt through; fov_tuning.cpp scales a factor; nothing is rate-gated.
// Not found: what makes those two slides uneven at 60. This file measures it instead of guessing. While the log is on,
// the player's mover update (player_move.cpp's hook of sub_82697088, once per game frame with the frame's dt) calls
// OnPlayerMove, which follows two "episodes":
//   ZOOM    while the zoom timer runs or the zoom value changes: the value per frame (the camera's FOV is 58 degrees x
//           (1 - value) x pdz_fov_scale);
//   LADDER  while the mover is on a ladder or in an animated move (mover +408 / +412, mode byte +605): the view yaw
//           (character +0x1670, degrees) and the feet (mover +360).
// Each frame of an episode gets one line (the frame's dt, the real time since the previous game frame, the value and
// its change per second); the episode's end gets a summary: how even the change per second was, the frames that did
// not move while their neighbours did ("held") and the frames that moved more than 1.6 x the median ("double"). An
// even series at 60 means the game side is smooth and the judder is made later (rendering / presenting); a stepped
// one names the value that steps and its rhythm (every 2nd frame = a 30 Hz step).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include <rex/cvar.h>
#include <rex/logging.h>

#include "render/game_pace.h" // EffectiveHz

REXCVAR_DEFINE_BOOL(pdz_camera_blend_log, false, "Diagnostics",
                    "Log player 1's zoom slide and ladder / animated-move camera frame by frame, with a summary per "
                    "slide of how evenly it moved (held frames, double steps, the frame times): for the camera "
                    "judder at the 60 fps lock. No behaviour change")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  constexpr uint32_t kPlayerVtable = 0x820C7DE0; // TU3's player class (player_move.cpp)
  constexpr uint32_t kZoomTimer = 6804, kZoomValue = 6812; // sub_825AC408 (+6808 the start, +6816 the target)
  constexpr uint32_t kViewYawDegrees = 0x1670;                                                   // player_move.cpp
  constexpr uint32_t kMoverPosition = 360, kMoverLadderA = 408, kMoverLadderB = 412, kMoverMode = 605;
  constexpr int kQuietFramesToEnd = 4;
  constexpr uint32_t kMaxFrameLines = 4000, kMaxSummaries = 400;

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  float LoadF(const uint8_t *base, uint32_t a)
  {
    const uint32_t v = Load32(base, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  bool IsGuestPointer(uint32_t a) { return a >= 0x00010000u && a < 0xFFFF0000u; }

  struct Sample
  {
    float dt = 0.0f;      // the game's frame time
    float real_ms = 0.0f; // real time since the previous game frame
    float rate = 0.0f;    // the followed value's change per second of game time
  };

  struct Episode
  {
    const char *name = "";
    const char *unit = "";
    bool open = false;
    int quiet = 0;
    float first = 0.0f, last = 0.0f;
    std::vector<Sample> samples;
  };

  // Game thread only.
  uint32_t g_player = 0;
  uint64_t g_frame = 0, g_player_frame = 0;
  bool g_have_prev = false;
  std::chrono::steady_clock::time_point g_prev_time{};
  float g_prev_zoom = 0.0f, g_prev_yaw = 0.0f, g_prev_feet_y = 0.0f;
  Episode g_zoom{"ZOOM", "zoom/s"}, g_ladder{"LADDER", "cm/s up"};
  uint32_t g_frame_lines = 0, g_summaries = 0;

  float Median(std::vector<float> v)
  {
    if (v.empty())
    {
      return 0.0f;
    }
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
  }

  void Close(Episode &e)
  {
    if (!e.open)
    {
      return;
    }
    e.open = false;
    // Drop the quiet tail that ended it.
    while (!e.samples.empty() && e.samples.back().rate == 0.0f)
    {
      e.samples.pop_back();
    }
    const size_t n = e.samples.size();
    if (n < 3 || g_summaries >= kMaxSummaries)
    {
      e.samples.clear();
      return;
    }
    ++g_summaries;
    std::vector<float> mags, dts, reals;
    for (const Sample &s : e.samples)
    {
      mags.push_back(std::fabs(s.rate));
      dts.push_back(s.dt * 1000.0f);
      reals.push_back(s.real_ms);
    }
    const float median = Median(mags);
    int held = 0, doubles = 0, held_runs_of_one = 0;
    for (size_t i = 0; i < n; ++i)
    {
      if (mags[i] == 0.0f && i > 0 && i + 1 < n && (mags[i - 1] > 0.0f || mags[i + 1] > 0.0f))
      {
        ++held;
        if (mags[i - 1] > 0.0f && mags[i + 1] > 0.0f)
        {
          ++held_runs_of_one;
        }
      }
      else if (median > 0.0f && mags[i] > 1.6f * median)
      {
        ++doubles;
      }
    }
    const auto [dt_lo, dt_hi] = std::minmax_element(dts.begin(), dts.end());
    const auto [re_lo, re_hi] = std::minmax_element(reals.begin(), reals.end());
    const auto [mg_lo, mg_hi] = std::minmax_element(mags.begin(), mags.end());
    double seconds = 0.0;
    for (const Sample &s : e.samples)
    {
      seconds += s.dt;
    }
    const bool stepped = held > 0 || doubles > int(n / 8);
    const bool uneven_time = *re_hi - *re_lo > 4.0f || *dt_hi - *dt_lo > 4.0f;
    REXLOG_INFO("pdz_camera_blend: {} SUMMARY: {} frames in {:.3f} s of game time, value {:.4f} -> {:.4f} | change "
                "per second ({}) median {:.3f}, least {:.3f}, most {:.3f} | held frames (no change between moving "
                "ones) {} ({} single), double steps (over 1.6 x the median) {} | frame dt {:.2f}-{:.2f} ms, real time "
                "between frames {:.2f}-{:.2f} ms | paced {} Hz | {}",
                e.name, n, seconds, e.first, e.last, e.unit, median, *mg_lo, *mg_hi, held, held_runs_of_one, doubles,
                *dt_lo, *dt_hi, *re_lo, *re_hi, pdz::pace::EffectiveHz(),
                stepped && held * 3 >= int(n)   ? "STEPPED: the value moves on some frames only (a slower tick than "
                                                  "the frame rate)"
                : stepped                       ? "UNEVEN: some frames hold or jump"
                : uneven_time                   ? "the value follows dt, but the frame times are uneven"
                                                : "EVEN: the game moves it smoothly every frame (look after the game: "
                                                  "rendering / presenting)");
    e.samples.clear();
  }

  // One frame of an episode. `active` = the game says the slide is running; `value` = what is followed.
  void Follow(Episode &e, bool active, float value, float previous, float dt, float real_ms, const char *detail_name,
              float detail)
  {
    const float change = value - previous;
    const bool moving = change != 0.0f;
    if (!e.open)
    {
      if (!active && !moving)
      {
        return;
      }
      e.open = true;
      e.quiet = 0;
      e.first = previous;
      e.samples.clear();
      REXLOG_INFO("pdz_camera_blend: {} begins at {:.4f} (game frame {}, paced {} Hz)", e.name, previous, g_frame,
                  pdz::pace::EffectiveHz());
    }
    Sample s;
    s.dt = dt;
    s.real_ms = real_ms;
    s.rate = dt > 0.0f ? change / dt : 0.0f;
    e.samples.push_back(s);
    if (moving)
    {
      e.last = value;
    }
    if (g_frame_lines < kMaxFrameLines)
    {
      ++g_frame_lines;
      REXLOG_INFO("pdz_camera_blend: {} frame {} dt {:.2f} ms real {:.2f} ms | value {:.4f} ({:+.4f}, {:+.3f} {}) | "
                  "{} {:.3f}{}",
                  e.name, g_frame, dt * 1000.0f, real_ms, value, change, s.rate, e.unit, detail_name, detail,
                  moving ? "" : " HELD");
    }
    e.quiet = (active || moving) ? 0 : e.quiet + 1;
    if (e.quiet >= kQuietFramesToEnd || e.samples.size() >= 4096)
    {
      Close(e);
    }
  }
} // namespace

namespace pdz::camblend
{
  // From player_move.cpp's hook of the mover update sub_82697088 (player characters only), after Rare's update:
  // once per game frame per player, with that frame's dt.
  void OnPlayerMove(const uint8_t *base, uint32_t character, uint32_t mover, float dt)
  {
    if (!REXCVAR_GET(pdz_camera_blend_log))
    {
      if (g_have_prev)
      {
        g_have_prev = false; // switched off: forget the open episodes
        g_zoom.open = g_ladder.open = false;
        g_zoom.samples.clear();
        g_ladder.samples.clear();
      }
      return;
    }
    if (!IsGuestPointer(character) || !IsGuestPointer(mover) || Load32(base, character) != kPlayerVtable)
    {
      return;
    }
    ++g_frame;
    // One player only: the first one seen, until it has not been updated for two seconds' worth of calls.
    if (character != g_player)
    {
      if (g_player && g_frame - g_player_frame < 240)
      {
        return;
      }
      g_player = character;
      g_have_prev = false;
      Close(g_zoom);
      Close(g_ladder);
    }
    g_player_frame = g_frame;
    const auto now = std::chrono::steady_clock::now();
    const float zoom = LoadF(base, character + kZoomValue);
    const float timer = LoadF(base, character + kZoomTimer);
    const float yaw = LoadF(base, character + kViewYawDegrees);
    const float feet_y = LoadF(base, mover + kMoverPosition + 4);
    const bool on_ladder = Load32(base, mover + kMoverLadderA) != 0 || Load32(base, mover + kMoverLadderB) != 0 ||
                           base[mover + kMoverMode] != 0;
    if (g_have_prev && std::isfinite(zoom) && std::isfinite(yaw) && std::isfinite(feet_y))
    {
      const float real_ms = std::chrono::duration<float, std::milli>(now - g_prev_time).count();
      Follow(g_zoom, timer > 0.0f, zoom, g_prev_zoom, dt, real_ms, "timer s", timer);
      // The ladder: the feet's height is the climb; the view yaw is logged beside it (its turn at the mount and the
      // climb-off is the pan). A warp shows as one huge step and ends the episode on the next quiet frames.
      float turn = yaw - g_prev_yaw;
      turn -= 360.0f * std::round(turn / 360.0f);
      Follow(g_ladder, on_ladder, feet_y, g_prev_feet_y, dt, real_ms, "view yaw turn deg/s", dt > 0.0f ? turn / dt : 0.0f);
    }
    g_prev_time = now;
    g_prev_zoom = zoom;
    g_prev_yaw = yaw;
    g_prev_feet_y = feet_y;
    g_have_prev = true;
  }
} // namespace pdz::camblend
