// Restored sounds, round 2 (Tue 17 Nov 2026 night, job fix2). TU3 addresses. Needs pdz_restored_sounds; each family
// has its own switch (on).
//
// PROPS THAT FALL OR GET KNOCKED (item 12).
//   Rare's physics-contact sound routine is sub_82511A90 (the census's caller 82512078 is its play call). For each
//   physics contact it asks the object for its template record (object vfunc +72) and skips the object unless one of
//   the record's two contact flags is set (+80 / +81, 82511D94-82511DDC); with +81 set, a contact faster than the
//   record's lowest speed (+348; +352 the loudest) plays the record's bounce sound (+172, the template's third sound
//   slot) at the contact, at most 4 at a time; a slide plays the push sound (+176). Read from the templates
//   (prop_template_slots_Tue17Nov.tsv in the job folder): the bottles, boxes, drums, monitors, phones and the metal
//   table have +81 = 1, which is why those are heard; the metal crates (container\metal_crate_small / _standard /
//   _very_large: the Nightclub's filing cabinets and crates) list their own STOCK_CRATE_HIT_METAL in the bounce slot
//   but have +81 = 0, so Rare's routine never reaches them. Rare's own siblings show it is a slip of the data:
//   wood_crate_standard and crate_wide have +81 = 1, crate_wide_chunk / wood_crate_small / wood / sabotcrate (the same
//   sounds) 0; drumoil 1, the Sabotage drum 0; furniture\table_metal and vehicle\car play STOCK_CRATE_HIT_METAL with
//   +81 = 1.
//   Here: a mid-function hook right after Rare's vfunc +72 call (82511D94, r3 = the record) sets +81 for the records
//   of the templates below, once, so that Rare's own routine plays the template's own bounce sound with Rare's own
//   speeds, loudness and limit. The template is named by its own path, which the level data keeps just before the
//   record ("...\proptemplate\<type>\<name>\propattributes.xml", the test unwired_sounds_Tue17Nov.cpp uses). A push
//   slot that names another prop's sound (STOCK_OILDRUM_PUSH on a metal crate, a ricochet on a pot) is pointed at an
//   empty name, so only the impact is restored. No noise for the AI (the routine makes none).
//   Families (switches): metal crates / cabinets (pdz_snd_contact_metal_crates: STOCK_CRATE_HIT_METAL), wooden crates
//   (pdz_snd_contact_wood_crates: STOCK_CRATE_WOOD_FALL), the Sabotage drum (pdz_snd_contact_drums:
//   STOCK_OILDRUM_BOUNCE), pots (pdz_snd_contact_pots: STOCK_POT_BOUNCE_HARD), melons (pdz_snd_contact_melons:
//   STOCK_MELON_BOUNCE), metal bits (pdz_snd_contact_metal_bits: a shot padlock, the vent cover's bits:
//   STOCK_METAL_BOUNCE). Item 16 (a prop's own unplayed recording, at a moment that asks for nothing today): tables
//   (pdz_snd_contact_tables: furniture\table's bounce slot borrows the door's STOCK_DOOR_WOOD_THUD and its contact
//   sound is off, so a table knocked over is silent; Rare recorded STOCK_TABLE_TIP), cans (pdz_snd_contact_cans:
//   container\can_small has an empty bounce slot and its contact sound off; Rare recorded STOCK_CAN_BOUNCE_HARD):
//   the bounce slot is pointed at that recording's name. Not here, and why: see the job's SUMMARY (borrowed sounds such as STOCK_DOOR_WOOD_THUD on
//   tables, ricochet fillers, fixed objects that never move, the plastic bins whose bounce slot is a wooden crate's).
// PIGEONS (item 15). A pigeon is a plain prop (template natural\pigeon) owned by an invisible bird spawner (DP_BirdSpawn,
//   prop type 63: the levels' "birdspawn" group; Nightclub 10, also Rooftops and Subway). The spawner keeps its birds
//   hidden in a pool; a scare (a shot or a hit near it, the player close, an alert) runs its trigger sub_82535FD8,
//   which takes birds from the pool and sends them flying (flying count at spawner +0x3B0); +0x404 is its state (0
//   armed, above 0 re-arming, -1 spent); its position is at +0x3F0 (cm); its update is sub_82535D70 (f1 = dt). No
//   function of the spawner asks for a sound. Rare's sound-range picker sub_82206888 still gives any cue named
//   "_PIGEON_COO" a 500 cm range (the string at 82095534), a trace of the planned sound.
//   pdz_snd_pigeon_flaps: when the trigger has sent birds off, STOCK_PIGEON_FLAPS at the spawner (Rare's own takes).
//   pdz_snd_pigeon_coo: an armed spawner within 15 m of Jo coos (STOCK_PIGEON_COO, Rare's 500 cm range) every 8 to 20
//   s; never once it has been scared (the birds are gone) until it re-arms. Sound only, no noise for the AI.
// LOCKED DOOR (item 13). A player's use press on a closed door runs its use handler sub_82562358; a locked door
//   (lock byte +1184 / +1185 not 0, 82562484) shows its HUD message (sub_823B1660 at 825624D0) and asks the door sound
//   player sub_82563788 for slot 0 (825624DC: the only call with r4 = 0), whose name is the template's fifth door sound
//   (+200): empty in every door template, so nothing is heard. Only a human player's press reaches it, once per press.
//   pdz_snd_door_locked: after Rare's own call for slot 0, when Rare started no sound (the door's playing-sound field
//   +1196 did not change, door sounds not off +1165), STOCK_DOOR_LOCKED plays at the door (+48, Rare's own position
//   for its door sounds); at most once a second per door.
// CAMSPY ROTATION STOP (item 16). The CamSpy's sound update sub_82541A10 (r3 = the CamSpy, called at 8253EDEC) keeps
//   its rotate loop (STOCK_CAMSPY_ROTATE, handle at +0x77C) while its turn rate (+0x56C) is above Rare's threshold,
//   and stops and releases it when the turn ends (82541C84-82541CA4): nothing plays there. Rare recorded
//   STOCK_CAMSPY_ROTATE_STOP. pdz_snd_camspy_rotate_stop: when the loop handle goes from set to 0 in that update, the
//   stop sound plays, unpositioned as Rare's CamSpy sounds are while the player steers it.
// BULLET IMPACT SURFACES (item 11, log only). Rare's impact routine sub_8223F6B8 (r3 shooter, r4 the object hit or
//   0, r6 the surface id, r7 the hit place) plays the surface's impact (+140, sub_821FFDE0, every hit) and its
//   ricochet (+144, sub_821FFE60, about 4 hits in 10): a prop's sounds come from the surface of the collision
//   triangle that was hit, never from its template's bullet slot. No surface has an empty impact with a ricochet,
//   so the Nightclub bins' "ricochet, no impact" is not a broken name. pdz_snd_hit_surface_log: one line per
//   (object class, surface) the first time it is hit in a session, with the surface's two names and where, so the
//   bins' surface can be named from one shot in play.
// AUDIOSCOPE ZOOM (item 14). The audioscope is DP_SoundAlyser (vtable 820C95E0, update sub_825C1BC0); its zoom is
//   sub_825C3D48(this, dt), run only while the scope is up (+1356 == 6): +1704 the zoom fraction 0..1 (the left
//   trigger picks one of six steps when not scanning, +1408 == 0; while scanning it moves by itself, +1724 the
//   direction 0 / 1 in / 2 out). Nothing asks for a sound there. pdz_snd_audioscope_zoom: STOCK_AUDIOSCOPE_ZOOM (one
//   0.37 s take) when the zoom steps, or when a scanning zoom starts to move; at most every 0.15 s; unpositioned, as
//   the audioscope's own lock-on and scan sounds are; only for the local player's audioscope (Rare's owner test).
//   Fri 20 Nov night: on by default; pdz_snd_audioscope_zoom_restart: each press restarts it (see the hook).
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include <fmt/format.h>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/ppc/stack.h>
#include <rex/ppc/func.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>

#include "bymodel_sounds_Wed18Nov.h"
#include "guest_function.h"
#include "restored_voice_Wed18Nov.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov: pdz_fix_readable_cache

REXCVAR_DECLARE(bool, pdz_restored_sounds);
REXCVAR_DECLARE(bool, pdz_snd_effect_sparks); // (restored_sounds_Tue17Nov.cpp)
namespace pdz::restored_sounds
{
  void SparkSound(PPCContext &ctx, uint8_t *base, const float at[3], bool light, uint32_t caller);
}
REXCVAR_DEFINE_BOOL(pdz_snd_contact_metal_crates, true, "Gameplay",
                    "Restored sound: metal crates and filing cabinets make their own metal crate sound when they fall, "
                    "land or knock into something (the game's own contact sound, which their data had switched off). "
                    "Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_contact_cameras, true, "Gameplay",
                    "Restored sound: a security camera that has been shot down makes the metal knock sound its data "
                    "names when it lands or is knocked about (the game's own contact sound, which its data had "
                    "switched off). Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_contact_wood_crates, true, "Gameplay",
                    "Restored sound: the wooden crates that were silent when knocked (chunks, small crates, Sabotage "
                    "crates) make the wooden crate fall sound the other wooden crates make. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_contact_drums, true, "Gameplay",
                    "Restored sound: the Sabotage drum bounces with the oil drum's own bounce sound, as the other oil "
                    "drums do. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_contact_pots, true, "Gameplay",
                    "Restored sound: big pots make their own pot bounce sound when knocked. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_contact_melons, true, "Gameplay",
                    "Restored sound: melons make their own melon bounce sound when they fall. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_contact_tables, true, "Gameplay",
                    "Restored sound: a wooden table knocked over plays the game's own table tip sound (it was silent). "
                    "Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_contact_cans, true, "Gameplay",
                    "Restored sound: a small can that falls or is knocked plays the game's own can bounce sound (it was "
                    "silent). Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_pigeon_flaps, true, "Gameplay",
                    "Restored sound: pigeons that take off make the game's own wing flaps where they rise. Needs "
                    "pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_coo_beyond_m, 5.0, "Gameplay",
                      "Restored sound: the pigeons' coo carries this many metres beyond the distance at which the "
                      "player scares that flock off (each flock has its own), so it can be heard at all")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_coo_volume, 1.0, "Gameplay",
                      "Restored sound: the pigeons' coo's loudness (0 to 1; 1 = the game's own). With its range (scare "
                      "distance + the flock's height above the player's floor + 12 m) it is about half loud just outside a "
                      "10 m scare distance (Thu 19 Nov night; 0.55 before)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_coo_fade_m, 12.0, "Gameplay",
                      "Restored sound: the flock coo's sound range is its scare distance + its height above the player's "
                      "floor + this many metres (the game's sound fades in a straight line to nothing at its range; a coo "
                      "is only asked for within the scare distance + pdz_snd_pigeon_coo_beyond_m, or its height + 8 m)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_pigeon_coo_birds, true, "Gameplay",
                    "Restored sound (Sat 21 Nov evening): a pigeon flock coos only while its own birds really exist: "
                    "pigeon objects in its spawner's pool or in flight (the birds a spawner creates itself count), not "
                    "just because Rare left a bird spawner in the level (Datacore's hold submersibles and rats). "
                    "Off = every spawner may coo (before)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_pigeon_flaps_birds, true, "Gameplay",
                    "Restored sound (Sat 21 Nov evening): pigeons' wing flaps play only when the birds that take off "
                    "really are pigeons (pigeon objects created or kept by that spawner; Datacore's spawners send off "
                    "submersibles and rats). "
                    "Off = every spawner's take-off flaps (before)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_coo_reach_max_m, 30.0, "Gameplay",
                      "Restored sound (Sat 21 Nov evening): a flock's coo is asked for within this many metres at most "
                      "(its scare distance + 5 m below that: the coo is only heard beyond the distance that scares the "
                      "birds off; Subway's flocks scare at 20-25 m), whatever its scare distance (one Datacore spawner's "
                      "is 100 m: 105 m before). 0 = no cap (before)")
    .range(0.0, 200.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_pigeon_coo_range_max_m, 25.0, "Gameplay",
                      "Restored sound (Sat 21 Nov evening): a flock coo's sound range is this many metres at most, or "
                      "its reach + 5 m when that is more (it could reach 60 m). 0 = no cap (before)")
    .range(0.0, 200.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_pigeon_coo, true, "Gameplay",
                    "Restored sound: where pigeons are waiting to be scared off, an occasional quiet coo (the game's "
                    "own recording, heard only close by). Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_door_locked, true, "Gameplay",
                    "Restored sound: trying a locked door plays the game's own 'door locked' sound at the door, as "
                    "the 'locked' message shows. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_camspy_rotate_stop, true, "Gameplay",
                    "Restored sound: the CamSpy's turn ends with the game's own rotate-stop sound. Needs "
                    "pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_fix_camspy_turn_sound, true, "Gameplay",
                    "Fix (Sun 22 Nov): the CamSpy's turning whirr (the game's own STOCK_CAMSPY_ROTATE) plays when it "
                    "turns left / right too, not only up / down (the game tested its up / down turn speed only), and "
                    "when the mouse turns it (pdz_mouse_direct_camspy turns its view directly). It stops as the "
                    "game's own does when the turn ends")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_fix_camspy_turn_mouse_hold_s, 0.1, "Gameplay",
                      "pdz_fix_camspy_turn_sound: the mouse turn counts as still turning this long after the last "
                      "mouse movement (mouse moves come in bursts: without it the whirr would stop and start between "
                      "them)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_fix_camspy_turn_log, false, "Debug",
                    "Log only: a 'pdz_camspy_turn:' line each time the CamSpy's turning whirr starts or stops, with "
                    "what turned it (up / down stick, left / right stick, mouse)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_hit_surface_log, true, "Debug",
                    "Log only: one 'pdz_snd_hit_surface:' line per object class and surface the first time a bullet "
                    "hits it in a session (which surface, its impact and ricochet sound names, where)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// (Wed 18 Nov, sound3: OFF until the tester has heard the new one-sound-per-zoom version. Fri 20 Nov night: ON, the
// tester's listening, log 032.)
REXCVAR_DEFINE_BOOL(pdz_snd_audioscope_zoom, true, "Gameplay",
                    "Restored sound: the audioscope makes the game's own zoom sound while its zoom moves (one sound "
                    "per zoom action, stopped when the zoom stops). Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_audioscope_zoom_restart, true, "Gameplay",
                    "Restored sound (Fri 20 Nov night): each new zoom press (the zoom moving again after a pause, or "
                    "turning from in to out) stops the zoom sound and starts it again from its beginning, so rapid "
                    "presses give one sound each instead of one long one. Off = one sound per run of presses. Needs "
                    "pdz_snd_audioscope_zoom")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Sat 21 Nov (spectate job, corrected by the tester): the zoom is ANALOG, set by how far the left trigger is pulled
// (Rare's zoom sub_825C3D48, when not scanning (+1408 == 0), sets the zoom +1704 from the trigger's depth: the owner's
// vtable +0x1F8 = sub_825ABFF8 -> the pad's first trigger). The sound follows the zoom's MOVEMENT
// (pdz_snd_audioscope_zoom_follow): it starts when the zoom starts to change; whenever the zoom turns (in -> out or
// out -> in) the playing sound is stopped and started again from its beginning; it is stopped when the zoom has stood
// still for 0.12 s (or the scope goes down). Rapid pulls and releases give a fresh start at each turn, never one long
// sound; one handle, stopped before every start: never two at once. Moving on in the same direction never restarts.
REXCVAR_DEFINE_BOOL(pdz_snd_audioscope_zoom_follow, true, "Gameplay",
                    "Restored sound (Sat 21 Nov): the audioscope's zoom sound follows the zoom's movement: it starts "
                    "when the zoom starts to change, starts again from its beginning whenever the zoom changes "
                    "direction (in / out), and stops when the zoom stands still. Off = Fri 20 Nov night's rule (a "
                    "restart also when the zoom moves again after a short pause). Needs pdz_snd_audioscope_zoom")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Rare's zoom moves in six stops (0, 0.2 ... 1.0, one stop per step of the trigger's depth), so during a slow pull it
// stands still between stops: with the default each stop is then its own short sound. A longer time keeps one sound
// through a slow pull.
REXCVAR_DEFINE_DOUBLE(pdz_snd_audioscope_zoom_still_s, 0.12, "Gameplay",
                      "pdz_snd_audioscope_zoom: the zoom sound stops when the zoom has not changed for this long "
                      "(seconds). Rare's zoom moves in six stops; a slow pull stands still between them: 0.12 = a short "
                      "sound per stop, about 0.4 = one sound through a slow pull")
    .range(0.05, 1.0)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_audioscope_trace, false, "Testing",
                    "TESTING TOOL (Sat 21 Nov): log the local player's audioscope state each time it changes (its mode "
                    "+1356, scanning +1408, zoom +1704) and every zoom sound start / stop with the number of zoom "
                    "sounds alive")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_snd_audioscope_zoom_press_gap_s, 0.06, "Gameplay",
                      "pdz_snd_audioscope_zoom_restart: a zoom that moves again after standing still this long (seconds) "
                      "is a new press")
    .range(0.01, 0.5)
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_contact_metal_bits, true, "Gameplay",
                    "Restored sound: a shot-off padlock and the bits of a vent cover make the metal bounce sound their "
                    "data names when they land. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

namespace
{
  enum Family : uint8_t
  {
    kNone = 0,
    kMetalCrates,
    kWoodCrates,
    kDrums,
    kPots,
    kMelons,
    kMetalBits,
    kTables,
    kCans,
    kCameras,
  };
  struct Row
  {
    const char *tpl;    // "type\\name" as in the path
    const char *bounce; // the bounce slot's name it must have (Rare's own for that prop)
    Family family;
    bool keep_push;     // the push slot is that prop's own (else it is pointed at an empty name)
    const char *use = nullptr; // item 16: the prop's own recording, put in the bounce slot (nullptr: keep the slot's)
  };
  constexpr Row kRows[] = {
      {"container\\metal_crate_small", "XACT_SOUNDBANK_STOCK_CRATE_HIT_METAL", kMetalCrates, false},
      {"container\\metal_crate_standard", "XACT_SOUNDBANK_STOCK_CRATE_HIT_METAL", kMetalCrates, false},
      {"container\\metal_crate_very_large", "XACT_SOUNDBANK_STOCK_CRATE_HIT_METAL", kMetalCrates, false},
      {"container\\crate_wide_chunk", "XACT_SOUNDBANK_STOCK_CRATE_WOOD_FALL", kWoodCrates, true},
      {"container\\wood_crate_small", "XACT_SOUNDBANK_STOCK_CRATE_WOOD_FALL", kWoodCrates, true},
      {"container\\wood", "XACT_SOUNDBANK_STOCK_CRATE_WOOD_FALL", kWoodCrates, true},
      {"misc\\sabotcrate", "XACT_SOUNDBANK_STOCK_CRATE_WOOD_FALL", kWoodCrates, true},
      {"misc\\sabotdrum", "XACT_SOUNDBANK_STOCK_OILDRUM_BOUNCE", kDrums, true},
      {"niknak\\african_pot", "XACT_SOUNDBANK_STOCK_POT_BOUNCE_HARD", kPots, false},
      {"niknak\\bigpot", "XACT_SOUNDBANK_STOCK_POT_BOUNCE_HARD", kPots, false},
      {"natural\\melon", "XACT_SOUNDBANK_STOCK_MELON_BOUNCE", kMelons, false},
      {"natural\\melon_bit", "XACT_SOUNDBANK_STOCK_MELON_BOUNCE", kMelons, false},
      {"hanger\\padlock", "XACT_SOUNDBANK_STOCK_METAL_BOUNCE", kMetalBits, false},
      {"structure\\vent_cover_bit", "XACT_SOUNDBANK_STOCK_METAL_BOUNCE", kMetalBits, false},
      {"furniture\\table", "XACT_SOUNDBANK_STOCK_DOOR_WOOD_THUD", kTables, false, "XACT_SOUNDBANK_STOCK_TABLE_TIP"},
      {"container\\can_small", "", kCans, false, "XACT_SOUNDBANK_STOCK_CAN_BOUNCE_HARD"},
      // (Wed 18 Nov: the security camera; its fallen head is knocked about in silence. Its push slot is empty.)
      {"electrical\\camera", "XACT_SOUNDBANK_STOCK_CRATE_HIT_METAL", kCameras, true},
  };
  constexpr const char *kFamilyName[] = {"", "metal crates / cabinets", "wooden crates", "the Sabotage drum", "pots", "melons",
                                         "metal bits", "tables", "cans", "security cameras"};
  constexpr uint32_t kFlag = 81, kBounce = 172, kPush = 176;
  constexpr uint32_t kBack = 200; // the template's path ends this close before its record (78 bytes in the packages)

  bool FamilyOn(Family f)
  {
    if (!REXCVAR_GET(pdz_restored_sounds))
    {
      return false;
    }
    switch (f)
    {
    case kMetalCrates: return REXCVAR_GET(pdz_snd_contact_metal_crates);
    case kWoodCrates: return REXCVAR_GET(pdz_snd_contact_wood_crates);
    case kDrums: return REXCVAR_GET(pdz_snd_contact_drums);
    case kPots: return REXCVAR_GET(pdz_snd_contact_pots);
    case kMelons: return REXCVAR_GET(pdz_snd_contact_melons);
    case kMetalBits: return REXCVAR_GET(pdz_snd_contact_metal_bits);
    case kTables: return REXCVAR_GET(pdz_snd_contact_tables);
    case kCans: return REXCVAR_GET(pdz_snd_contact_cans);
    case kCameras: return REXCVAR_GET(pdz_snd_contact_cameras);
    default: return false;
    }
  }

  uint32_t Load32(const uint8_t *base, uint32_t a)
  {
    const uint8_t *p = base + a;
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
  }
  void Store32(uint8_t *base, uint32_t a, uint32_t v)
  {
    uint8_t *p = base + a;
    p[0] = uint8_t(v >> 24), p[1] = uint8_t(v >> 16), p[2] = uint8_t(v >> 8), p[3] = uint8_t(v);
  }
  bool Guest(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }
  bool Readable(uint8_t *base, uint32_t address, uint32_t size)
  {
    // Sat 21 Nov (pdz_fix_readable_cache): the cached check (guest_readable_Sat21Nov.cpp); below = off.
    if (pdz::guestmem::CacheOn())
    {
      return pdz::guestmem::Readable(const_cast<uint8_t *>(base), address, size);
    }
    if (address < 0x10000u || uint64_t(address) + size > 0xFFFF0000ull)
    {
      return false;
    }
    for (uint32_t at = address; at < address + size; at = (at & ~0xFFFu) + 0x1000u)
    {
      size_t length = 1;
      rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
      if (!rex::memory::QueryProtect(base + at, length, access) ||
          (uint32_t(access) & uint32_t(rex::memory::PageAccess::kReadOnly)) == 0)
      {
        return false;
      }
    }
    return true;
  }

  struct Seen
  {
    Family family = kNone; // kNone: not one of ours (or not named)
    int row = -1;
    bool done = false;     // the flag (and the push slot) written
  };
  std::mutex g_mutex;
  std::unordered_map<uint32_t, Seen> g_seen; // record -> what it is (records live as long as the level's data)
  uint64_t g_enabled = 0;

  // Which of our rows the record at `rec` is, from its template's path just before it.
  int RowOf(uint8_t *base, uint32_t rec)
  {
    if (rec <= 0x10000u + kBack || !Readable(base, rec - kBack, kBack + kPush + 4))
    {
      return -1;
    }
    const std::string_view window(reinterpret_cast<const char *>(base + rec - kBack), kBack);
    constexpr std::string_view kTail = "\\propattributes.xml";
    const size_t tail = window.rfind(kTail);
    if (tail == std::string_view::npos)
    {
      return -1;
    }
    constexpr std::string_view kHead = "proptemplate\\";
    const size_t head = window.rfind(kHead, tail);
    if (head == std::string_view::npos)
    {
      return -1;
    }
    const std::string_view tpl = window.substr(head + kHead.size(), tail - head - kHead.size());
    for (int i = 0; i < int(sizeof(kRows) / sizeof(kRows[0])); ++i)
    {
      if (tpl == kRows[i].tpl)
      {
        return i;
      }
    }
    return -1;
  }
} // namespace

// Wed 18 Nov (sound3). Log 019: not one line of this file's contact part, and the metal crates still silent. Rare's
// routine only reaches its vfunc +72 call (and the hook below) for a prop whose contact record is on (prop +428,
// 82511C88), which the physics step sub_825E4AE8 switches on only for a prop with bit 0x80000000 in its flag word
// (prop +64), and that bit is set ONCE, at the prop's set-up, by sub_825165F8 (8251666C-825166F0), when its template
// has +80, +81 or +82 set then. A template with all three 0 (the metal crates, the camera) never got here, so the
// flag was never written. The flag is now written from a hook of sub_825165F8, before Rare reads it there (see the
// end of this part); Rare's own set-up then marks the prop and everything after is Rare's.
namespace pdz::prop_contacts
{
  // The template's "type\name" from the path before its record ("" if not found). Checks the memory (a syscall):
  // not for a hot path.
  std::string TemplateOf(uint8_t *base, uint32_t rec)
  {
    if (rec <= 0x10000u + kBack || !Readable(base, rec - kBack, kBack + kPush + 4))
    {
      return {};
    }
    const std::string_view window(reinterpret_cast<const char *>(base + rec - kBack), kBack);
    constexpr std::string_view kTail = "\\propattributes.xml";
    const size_t tail = window.rfind(kTail);
    constexpr std::string_view kHead = "proptemplate\\";
    const size_t head = tail == std::string_view::npos ? tail : window.rfind(kHead, tail);
    if (head == std::string_view::npos)
    {
      return {};
    }
    return std::string(window.substr(head + kHead.size(), tail - head - kHead.size()));
  }

  void Enable(uint8_t *base, uint32_t rec, bool fresh);
}

// Before 82511D94 in sub_82511A90 (r3 = the object's template record, from its vfunc +72).
void PdzPropContactRecord(PPCRegister &r3)
{
  const uint32_t rec = r3.u32;
  if (!Guest(rec) || !REXCVAR_GET(pdz_restored_sounds))
  {
    return;
  }
  auto *rt = rex::Runtime::instance();
  uint8_t *base = rt ? rt->virtual_membase() : nullptr;
  if (!base || base[rec + kFlag] != 0)
  {
    return; // (Rare's own contact sound is on for it: nothing of ours)
  }
  pdz::prop_contacts::Enable(base, rec, false);
}

// fresh: from the prop set-up hook: the record is named anew (a level's records are loaded anew, and an address
// can hold another template in the next level), not taken from what was remembered for that address.
void pdz::prop_contacts::Enable(uint8_t *base, uint32_t rec, bool fresh)
{
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_seen.size() > 8192)
  {
    g_seen.clear();
  }
  if (fresh)
  {
    g_seen.erase(rec);
  }
  auto it = g_seen.find(rec);
  if (it == g_seen.end())
  {
    Seen s;
    s.row = RowOf(base, rec);
    s.family = s.row >= 0 ? kRows[s.row].family : kNone;
    it = g_seen.emplace(rec, s).first;
    if (s.row < 0)
    {
      // (A record that has one of our bounce names but could not be named by its path: said once, so a play log shows
      // whether the path test works in memory as it does in the package files.)
      static int unnamed_logged = 0;
      const uint32_t bounce = Load32(base, rec + kBounce);
      if (unnamed_logged < 4 && Guest(bounce) && Readable(base, bounce, 64))
      {
        const std::string_view b(reinterpret_cast<const char *>(base + bounce), strnlen(reinterpret_cast<const char *>(base + bounce), 64));
        for (const Row &row : kRows)
        {
          if (*row.bounce && b == row.bounce)
          {
            ++unnamed_logged;
            std::string before; // (Wed 18 Nov: the readable text just before it, to see what it is)
            if (Readable(base, rec - 400, 400))
            {
              for (uint32_t a = rec - 400; a < rec; ++a)
              {
                const char ch = char(base[a]);
                before.push_back(ch >= 32 && ch < 127 ? ch : '.');
              }
            }
            REXLOG_INFO("pdz_snd_prop_contacts: a record {:08X} with the contact sound off and the bounce slot {} could not be "
                        "named from its template path (not found within {} bytes before it): left as the game has it | the 400 "
                        "bytes before it: {}",
                        rec, b.substr(15), kBack, before);
            break;
          }
        }
      }
    }
  }
  Seen &s = it->second;
  if (s.family == kNone || s.done || !FamilyOn(s.family))
  {
    return;
  }
  const Row &row = kRows[s.row];
  const uint32_t bounce = Load32(base, rec + kBounce);
  if (!Guest(bounce) || !Readable(base, bounce, 64))
  {
    s.family = kNone;
    return;
  }
  const char *bounce_name = reinterpret_cast<const char *>(base + bounce);
  const size_t bounce_len = strnlen(bounce_name, 64);
  if (std::string_view(bounce_name, bounce_len) != row.bounce)
  {
    // (not the slot we read in the data: left as Rare has it)
    REXLOG_INFO("pdz_snd_prop_contacts: {}: its bounce slot names '{}' here, not '{}': left as the game has it", row.tpl,
                std::string_view(bounce_name, bounce_len), row.bounce);
    s.family = kNone;
    return;
  }
  uint32_t empty_name = uint32_t(bounce + bounce_len); // (the bounce name's own end: an empty name)
  std::string_view played(row.bounce);
  if (row.use)
  {
    // Item 16: the prop's own recording in its bounce slot (a guest copy of the name, kept for the session).
    static std::unordered_map<std::string, uint32_t> names;
    uint32_t &g = names[row.use];
    if (!g)
    {
      const size_t n = std::strlen(row.use) + 2;
      g = REX_KERNEL_MEMORY()->SystemHeapAlloc(uint32_t(n));
      if (g)
      {
        std::memset(base + g, 0, n);
        std::memcpy(base + g, row.use, n - 2);
      }
    }
    if (!g)
    {
      return;
    }
    Store32(base, rec + kBounce, g);
    empty_name = g + uint32_t(std::strlen(row.use)); // (its end: an empty name)
    played = row.use;
  }
  std::string push_note = "its push slot is its own";
  if (!row.keep_push)
  {
    const uint32_t push = Load32(base, rec + kPush);
    if (Guest(push) && Readable(base, push, 2) && base[push] != 0)
    {
      push_note = fmt::format("its push slot ({}) is another prop's sound: pointed at an empty name, so a slide stays silent",
                              std::string_view(reinterpret_cast<const char *>(base + push), strnlen(reinterpret_cast<const char *>(base + push), 64)));
      Store32(base, rec + kPush, empty_name);
    }
    else
    {
      push_note = "its push slot is empty";
    }
  }
  base[rec + kFlag] = 1;
  s.done = true;
  ++g_enabled;
  REXLOG_INFO("pdz_snd_prop_contacts: {} ({}): the game's contact sound is switched on for it: its own {} when it falls, "
              "lands or knocks, with the game's own speeds and limits; {} ({} template record(s) so far)",
              row.tpl, kFamilyName[s.family], played.substr(15), push_note, g_enabled);
}

// The prop set-up (r3 = the prop): Rare marks the prop for contact tracking when its template has a contact flag.
// Ours is written just before, for the templates of the list above.
REX_EXTERN(__imp__sub_825165F8);
REX_HOOK_RAW(sub_825165F8)
{
  const uint32_t prop = ctx.r3.u32;
  // (Sun 22 Nov night (audit): the prop and its setup read only where mapped; the vtable in the game image.)
  if (Guest(prop) && REXCVAR_GET(pdz_restored_sounds) && pdz::guestmem::Ok(base, prop, 88))
  {
    // (Rare's own condition, 82516654-82516668: no setup, or the setup's byte +28 set.)
    const uint32_t setup = Load32(base, prop + 84);
    const uint32_t vtable = Load32(base, prop);
    if ((setup == 0 || (Guest(setup) && pdz::guestmem::Ok(base, setup, 29) && base[setup + 28] != 0)) && vtable >= 0x82000000u &&
        vtable < 0x83000000u)
    {
      if (PPCFunc *fn = pdz::GuestFunction(Load32(base, vtable + 72)))
      {
        uint32_t rec = 0;
        {
          rex::CallFrame frame(ctx);
          frame.ctx.r3.u64 = prop;
          fn(frame.ctx, base);
          rec = frame.ctx.r3.u32;
        }
        if (Guest(rec) && pdz::guestmem::Ok(base, rec, 84) && base[rec + 80] == 0 && base[rec + kFlag] == 0 && base[rec + 82] == 0)
        {
          pdz::prop_contacts::Enable(base, rec, true);
        }
      }
    }
  }
  __imp__sub_825165F8(ctx, base);
}

// --- Pigeons (item 15) ---------------------------------------------------------------------------------------------
REX_EXTERN(sub_82202C78);         // the name resolver (r3 = name): the cue, or -1
REX_EXTERN(sub_82202D20);         // cue reference (r3 = ref {char name[256]; cue}, r4 = name)
REX_EXTERN(sub_821FF428);         // sound from a reference (r3 = ref, r4 = position or 0): the sound, or 0
REX_EXTERN(sub_822061C8);         // play (r3 = sound, f1 = delay)
REX_EXTERN(__imp__sub_82535FD8);  // the bird spawner's trigger (r3 = spawner)
REX_EXTERN(__imp__sub_82535D70);  // the bird spawner's update (r3 = spawner, f1 = dt)

namespace
{
  constexpr uint32_t kFlyingCount = 0x3B0, kSpawnerState = 0x404, kSpawnerPos = 0x3F0;
  constexpr uint32_t kPlayerOne = 0x82E3C3E4, kCharPos = 48;
  float LoadF(const uint8_t *base, uint32_t a)
  {
    const uint32_t v = Load32(base, a);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
  }
  void StoreF(uint8_t *base, uint32_t a, float f)
  {
    uint32_t v;
    std::memcpy(&v, &f, 4);
    Store32(base, a, v);
  }
  uint32_t g_names[5] = {0, 0, 0, 0, 0}, g_where = 0; // guest scratch (the game thread only)

  // Rare's way of starting a positioned cue (as the spark sound does): true = started. at == nullptr: unpositioned.
  bool PlayAt(PPCContext &ctx, uint8_t *base, int which, const char *cue, const float at[3])
  {
    if (!g_names[which])
    {
      g_names[which] = REX_KERNEL_MEMORY()->SystemHeapAlloc(96);
      if (g_names[which])
      {
        std::memset(base + g_names[which], 0, 96);
        std::memcpy(base + g_names[which], cue, std::strlen(cue));
      }
    }
    if (!g_where)
    {
      g_where = REX_KERNEL_MEMORY()->SystemHeapAlloc(64);
    }
    if (!g_names[which] || !g_where)
    {
      return false;
    }
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = g_names[which];
      sub_82202C78(frame.ctx, base);
      if (int32_t(frame.ctx.r3.u32) == -1)
      {
        return false;
      }
    }
    std::memset(base + g_where, 0, 64);
    for (int k = 0; at && k < 3; ++k)
    {
      StoreF(base, g_where + 4 * k, at[k]);
    }
    uint32_t sound = 0;
    {
      rex::CallFrame frame(ctx);
      const std::string zeros(260, 0);
      const uint32_t ref = rex::ppc::stack_push(frame.ctx, base, zeros.data(), 260);
      frame.ctx.r3.u64 = ref;
      frame.ctx.r4.u64 = g_names[which];
      sub_82202D20(frame.ctx, base);
      frame.ctx.r3.u64 = ref;
      frame.ctx.r4.u64 = at ? g_where : 0;
      frame.ctx.r5.u64 = 0;
      frame.ctx.r6.u64 = 0;
      sub_821FF428(frame.ctx, base);
      sound = frame.ctx.r3.u32;
    }
    if (!Guest(sound))
    {
      return false;
    }
    rex::CallFrame frame(ctx);
    frame.ctx.r3.u64 = sound;
    frame.ctx.f1.f64 = 0.0;
    sub_822061C8(frame.ctx, base);
    pdz::snd::FreeWhenEnded(base, sound); // (Wed 18 Nov: it kept its sound object for ever)
    return true;
  }

  struct Coo
  {
    float next = -1.0f; // s until his next coo (below 0: not set)
    bool seen = false;  // its first update has been logged
    int birds_said = 0;      // Sat 21 Nov evening: its last BIRDS line (0 none yet, 1 yes, 2 no)
    uint32_t setup = 0;      // (its setup: a new level's spawner at the same address starts again)
    double birds_check = 0.0; // s until its birds are looked at again
    int drawn = 0;           // its birds with a model, at the last look
  };
  std::unordered_map<uint32_t, Coo> g_coo; // spawner -> its coo timer (the game thread only)
  uint32_t g_rng = 0x9E3779B9u;
  float Rand01()
  {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return float(g_rng & 0xFFFFFF) / float(0x1000000);
  }
  uint64_t g_flaps = 0, g_coos = 0;
} // namespace

namespace
{
  // Sat 21 Nov evening (the owner's correction: Subway's flocks are birds the spawner makes itself). A spawner's birds now,
  // read as Rare's take routine sub_825362D0 walks them: its pool at +0x364 (+0x14 the first node, +0x18 the end; node
  // +4 the bird, +0xC the next) and its birds in flight at +0x384 (the same list kind; its count +0x2C is the spawner's
  // +0x3B0). An empty pool makes a NEW bird from the spawner's template (sub_82277158 with [[+0x360] +0x38 / +0x34])
  // when it is scared; birds that land go back to the pool (sub_82536220). A bird counts as drawn when its setup
  // (+0x54) names a PIGEON model (+140) the level has loaded: on Datacore Rare's bird spawners hold submersibles and
  // rats. Every read is tested as mapped memory.
  struct BirdCount
  {
    int objects = 0, drawn = 0, flying = 0;
    std::string models;
  };
  BirdCount SpawnerBirds(const uint8_t *base, uint32_t spawner)
  {
    BirdCount b;
    b.flying = int(Load32(base, spawner + kFlyingCount));
    for (const uint32_t list : {spawner + 0x364u, spawner + 0x384u})
    {
      if (!pdz::guestmem::Ok(base, list, 0x1C))
      {
        continue;
      }
      uint32_t node = Load32(base, list + 0x14);
      const uint32_t end = Load32(base, list + 0x18);
      for (int i = 0; i < 64 && node != end && pdz::guestmem::Ok(base, node, 0x10); ++i, node = Load32(base, node + 0xC))
      {
        const uint32_t bird = Load32(base, node + 4);
        if (!pdz::guestmem::Ok(base, bird, 0x58))
        {
          continue;
        }
        ++b.objects;
        const uint32_t setup = Load32(base, bird + 0x54);
        const uint32_t m = pdz::guestmem::Ok(base, setup, 144) ? Load32(base, setup + 140) : 0;
        std::string name;
        if (pdz::guestmem::Ok(base, m, 64))
        {
          for (uint32_t k = 0; k < 63 && base[m + k] >= 32 && base[m + k] < 127; ++k)
          {
            name.push_back(char(base[m + k]));
          }
        }
        // (Datacore, Sat 21 Nov evening: Rare's "bird" spawners there hold submersibles (model
        // submersible_driven) and rats (props_Animal_rat): only a PIGEON model counts as a bird.)
        std::string lower = name;
        for (char &ch : lower)
        {
          ch = (ch >= 'A' && ch <= 'Z') ? char(ch - 'A' + 'a') : ch;
        }
        if (!name.empty() && lower.find("pigeon") != std::string::npos)
        {
          ++b.drawn;
        }
        if (!name.empty() && b.models.find(name) == std::string::npos && b.models.size() < 300)
        {
          b.models += (b.models.empty() ? "" : ", ") + name;
        }
      }
    }
    return b;
  }
  std::string BirdText(const BirdCount &b)
  {
    return fmt::format("{} object(s) in its pool / in flight, {} of them pigeons (models: {}; flying count {})", b.objects, b.drawn,
                       b.models.empty() ? std::string("none") : b.models, b.flying);
  }
} // namespace
namespace lab_stealth // lab_stealth.cpp: the walls and floors between a sound and a listener (its wall muffling)
{
  int SoundWallsBetween(PPCContext &ctx, uint8_t *base, const float src[3], const float ears[3]);
}
REXCVAR_DECLARE(double, pdz_snd_pigeon_flaps_range_m);
REXCVAR_DECLARE(double, pdz_snd_pigeon_coo_range_m);
REXCVAR_DECLARE(bool, pdz_snd_pigeon_coo_fix);    // (restored_sounds_r3_Wed18Nov.cpp, Fri 20 Nov night)
REXCVAR_DECLARE(double, pdz_snd_pigeon_coo_gain);
REXCVAR_DECLARE(double, pdz_snd_pigeon_coo_ear);
REXCVAR_DECLARE(double, pdz_snd_pigeon_coo_take_s);

REX_HOOK_RAW(sub_82535FD8)
{
  const uint32_t spawner = ctx.r3.u32;
  const bool on = Guest(spawner) && REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_pigeon_flaps);
  const uint32_t before = on ? Load32(base, spawner + kFlyingCount) : 0;
  __imp__sub_82535FD8(ctx, base);
  if (!on)
  {
    return;
  }
  const uint32_t after = Load32(base, spawner + kFlyingCount);
  if (after <= before || after > before + 64)
  {
    return;
  }
  if (REXCVAR_GET(pdz_snd_pigeon_flaps_birds))
  {
    // Sat 21 Nov evening: the birds that just took off (now in its flight list) must be real, drawn bird objects.
    const BirdCount b = SpawnerBirds(base, spawner);
    REXLOG_INFO("pdz_snd_pigeon_flaps: BIRDS {} at the take-off from the spawner at ({:.0f} {:.0f} {:.0f}): {}{}", b.drawn > 0 ? "YES" : "NO",
                LoadF(base, spawner + kSpawnerPos), LoadF(base, spawner + kSpawnerPos + 4), LoadF(base, spawner + kSpawnerPos + 8), BirdText(b),
                b.drawn > 0 ? "" : ": no flaps (pdz_snd_pigeon_flaps_birds)");
    if (b.drawn == 0)
    {
      return;
    }
  }
  const float at[3] = {LoadF(base, spawner + kSpawnerPos), LoadF(base, spawner + kSpawnerPos + 4), LoadF(base, spawner + kSpawnerPos + 8)};
  // Wed 18 Nov (sound3; heard from inside a building): the flaps are heard to pdz_snd_pigeon_flaps_range_m (the range
  // picker hook in restored_voice_Wed18Nov.cpp), and walls muffle them as the stealth layer muffles a sound for a
  // guard: each wall or floor between the birds and the nearest player's head halves that distance, and the
  // loudness. Too far (in the open or behind walls): not asked for at all, so no sound object is taken.
  float ears[3] = {0.0f, 0.0f, 0.0f};
  const float near_cm = pdz::snd::NearestPlayerCm(base, at, ears);
  const float range_cm = float(REXCVAR_GET(pdz_snd_pigeon_flaps_range_m)) * 100.0f;
  const uint64_t n = ++g_flaps;
  if (near_cm < 0.0f || near_cm > range_cm)
  {
    if ((n & (n - 1)) == 0)
    {
      REXLOG_INFO("pdz_snd_pigeon_flaps: {} pigeon(s) took off from the spawner at ({:.0f} {:.0f} {:.0f}), {:.0f} m from the "
                  "nearest player: beyond the flaps' {:.0f} m, not asked for ({} so far)",
                  after - before, at[0], at[1], at[2], near_cm / 100.0f, range_cm / 100.0f, n);
    }
    return;
  }
  ears[1] += 150.0f; // (the head, from the feet)
  const int walls = lab_stealth::SoundWallsBetween(ctx, base, at, ears);
  const float muffle = 1.0f / float(1 << (walls > 4 ? 4 : walls));
  pdz::snd::Result r = pdz::snd::Result::kRefused;
  const bool in_reach = near_cm <= range_cm * muffle;
  if (in_reach)
  {
    r = pdz::snd::Once(ctx, base, "XACT_SOUNDBANK_STOCK_PIGEON_FLAPS", at, walls > 0 ? muffle : -1.0f);
  }
  if ((n & (n - 1)) == 0 || (in_reach && r != pdz::snd::Result::kStarted))
  {
    REXLOG_INFO("pdz_snd_pigeon_flaps: {} pigeon(s) took off from the spawner at ({:.0f} {:.0f} {:.0f}), {:.1f} m from the nearest "
                "player, {} wall(s) between: STOCK_PIGEON_FLAPS {} ({} so far)",
                after - before, at[0], at[1], at[2], near_cm / 100.0f, walls,
                in_reach ? pdz::snd::Text(r) : "not asked for (muffled by the walls: out of reach)", n);
  }
}

REX_HOOK_RAW(sub_82535D70)
{
  const uint32_t spawner = ctx.r3.u32;
  const float dt = float(ctx.f1.f64);
  __imp__sub_82535D70(ctx, base);
  if (!Guest(spawner) || !REXCVAR_GET(pdz_restored_sounds) || !REXCVAR_GET(pdz_snd_pigeon_coo) || !(dt > 0.0f && dt < 0.5f))
  {
    return;
  }
  if (g_coo.size() > 256)
  {
    g_coo.clear();
  }
  // Wed 18 Nov (sound3; logs 019 / 020: not one coo). The old timer needed the spawner to stay armed for 8 to 20 s
  // without a break and was thrown away at every scare; in play the spawners near anyone are scared again the moment
  // they re-arm (every 60 s), so it never ran out; and it asked from 15 m, where Rare's 5 m range refuses the sound.
  // Now: a timer per spawner that runs whenever its update runs and is never thrown away; when it runs out, a coo
  // if the spawner is armed (its birds are waiting, state 0) and the nearest player is within the coo's range
  // (pdz_snd_pigeon_coo_range_m, 10 m: Wed 18 Nov: Rare's 5 m is inside the scare distance); if not,
  // it is tried again in 2 s (so a player who walks up to waiting birds hears one soon).
  Coo &c = g_coo[spawner];
  const float state = LoadF(base, spawner + kSpawnerState);
  // Sat 21 Nov evening (pdz_snd_pigeon_coo_birds; the owner: no coo on Datacore, an underwater base where no bird is ever
  // seen; Rare left 4 spawners there): a flock coos only where the level has its birds as objects (the world walk of
  // restored_sounds_r3 counts the pigeon models; the flocks' birds are parked objects until they fly).
  if (REXCVAR_GET(pdz_snd_pigeon_coo_birds))
  {
    // Sat 21 Nov evening (corrected): its own birds, looked at twice a second (a list walk, no syscalls on most frames).
    const uint32_t setup_now = Load32(base, spawner + 84);
    if (setup_now != c.setup)
    {
      c.setup = setup_now, c.birds_said = 0, c.drawn = 0, c.birds_check = 0.0;
    }
    c.birds_check -= dt;
    if (c.birds_check <= 0.0)
    {
      c.birds_check = 0.5;
      const BirdCount b = SpawnerBirds(base, spawner);
      c.drawn = b.drawn;
      const int said = b.drawn > 0 ? 1 : 2;
      if (said != c.birds_said)
      {
        c.birds_said = said;
        REXLOG_INFO("pdz_snd_pigeon_coo: BIRDS {} for the pigeon spawner at ({:.0f} {:.0f} {:.0f}) (state {:.1f}): {}{}", said == 1 ? "YES" : "NO",
                    LoadF(base, spawner + kSpawnerPos), LoadF(base, spawner + kSpawnerPos + 4), LoadF(base, spawner + kSpawnerPos + 8), state,
                    BirdText(b), said == 1 ? ": it may coo" : ": no birds of its own exist now, so it does not coo (pdz_snd_pigeon_coo_birds)");
      }
    }
    if (c.drawn == 0)
    {
      return;
    }
  }
  const float at[3] = {LoadF(base, spawner + kSpawnerPos), LoadF(base, spawner + kSpawnerPos + 4), LoadF(base, spawner + kSpawnerPos + 8)};
  if (!c.seen)
  {
    c.seen = true;
    static int first_lines = 0;
    if (first_lines < 24)
    {
      ++first_lines;
      // (Its four scare radii, setup +60 / +64 / +68 / +72, cm: a character event within that distance sends the
      // birds off. A coo is only ever heard if a player can stand within Rare's 5 m without scaring them.)
      const uint32_t setup = Load32(base, spawner + 84);
      const bool have = Guest(setup) && Readable(base, setup, 100);
      REXLOG_INFO("pdz_snd_pigeon_coo: a pigeon spawner at ({:.0f} {:.0f} {:.0f}) is updated (state {:.1f}: 0 = its birds are "
                  "waiting, above 0 = seconds until they are back, -1 = spent), the nearest player {:.0f} m away; its scare "
                  "distances {:.0f} / {:.0f} / {:.0f} / {:.0f} cm, birds back after {:.0f} s",
                  at[0], at[1], at[2], state, pdz::snd::NearestPlayerCm(base, at) / 100.0f, have ? LoadF(base, setup + 60) : -1.0f,
                  have ? LoadF(base, setup + 64) : -1.0f, have ? LoadF(base, setup + 68) : -1.0f,
                  have ? LoadF(base, setup + 72) : -1.0f, have ? LoadF(base, setup + 76) : -1.0f);
    }
  }
  if (c.next < 0.0f)
  {
    c.next = 4.0f + 8.0f * Rand01();
    return;
  }
  c.next -= dt;
  if (c.next > 0.0f)
  {
    return;
  }
  // Wed 18 Nov night (log 021: never heard): the birds fly off when a player comes within the spawner's own scare
  // distance (20 to 50 m in the log), so any shorter range is never heard. The coo carries to that spawner's
  // largest scare distance plus pdz_snd_pigeon_coo_beyond_m, very quiet, only while its flock sits (state 0), and
  // fades with distance (the cue's range) and walls as the flaps do (each wall or floor halves reach and loudness).
  float ears[3] = {0.0f, 0.0f, 0.0f};
  const float near_cm = pdz::snd::NearestPlayerCm(base, at, ears);
  float scare_cm = 0.0f;
  if (const uint32_t setup = Load32(base, spawner + 84); Guest(setup) && Readable(base, setup, 100))
  {
    for (uint32_t k = 60; k <= 72; k += 4)
    {
      const float v = LoadF(base, setup + k);
      if (v > scare_cm && v <= 10000.0f)
      {
        scare_cm = v;
      }
    }
  }
  // (Thu 19 Nov night: flocks sit on roofs too: their height above the player's floor counts, as for the single
  // pigeons and the buzz. A flock higher above her than its scare distance is asked for within that height + 8 m.)
  const float floor_y = ears[1] - 80.0f; // (her body point is about 80 cm above her feet)
  const float height_cm = std::clamp(at[1] - floor_y, 0.0f, 3000.0f);
  float reach_cm = std::max(scare_cm > 0.0f ? scare_cm + float(REXCVAR_GET(pdz_snd_pigeon_coo_beyond_m)) * 100.0f
                                            : float(REXCVAR_GET(pdz_snd_pigeon_coo_range_m)) * 100.0f,
                            height_cm + 800.0f);
  // (Sat 21 Nov evening, pdz_snd_pigeon_coo_reach_max_m: log 034, a Datacore spawner with a 100 m scare distance was
  // asked within 105 m and played with a 60 m range.)
  if (const float cap = float(REXCVAR_GET(pdz_snd_pigeon_coo_reach_max_m)) * 100.0f; cap > 0.0f)
  {
    reach_cm = std::min(reach_cm, cap);
  }
  if (state != 0.0f || near_cm < 0.0f || near_cm > reach_cm)
  {
    c.next = 2.0f; // (no birds waiting, or nobody within the coo's reach: asked again soon)
    return;
  }
  ears[1] += 150.0f; // (the head, from the feet)
  const int walls = lab_stealth::SoundWallsBetween(ctx, base, at, ears);
  const float muffle = 1.0f / float(1 << (walls > 4 ? 4 : walls));
  if (near_cm > reach_cm * muffle)
  {
    c.next = 2.0f; // (muffled by the walls: out of reach)
    return;
  }
  c.next = 8.0f + 12.0f * Rand01();
  // (Thu 19 Nov, log 022 06:44:12: started at 14.7 m with a 25 m range and loudness 0.25: Rare's gain falls in a
  // straight line to nothing at the range, about 0.10 there: unheard. The sound's range is now the scare distance +
  // pdz_snd_pigeon_coo_fade_m, loudness 0.55: about 0.34 at 15 m and 0.21 at 25 m.)
  // (Thu 19 Nov night: the range is the scare distance + the flock's height above the player's floor +
  // pdz_snd_pigeon_coo_fade_m (12), loudness 1.0: just outside a 10 m scare distance about 0.5, at 20 m about 0.4.)
  const float fade_cm = (scare_cm > 0.0f ? scare_cm : reach_cm) + height_cm + float(REXCVAR_GET(pdz_snd_pigeon_coo_fade_m)) * 100.0f;
  float range_cm = std::max(reach_cm, fade_cm);
  float volume = float(REXCVAR_GET(pdz_snd_pigeon_coo_volume)) * muffle;
  // (Fri 20 Nov night, pdz_snd_pigeon_coo_fix, restored_sounds_r3_Wed18Nov.cpp: measured from the listener, a range
  // that gives the listener most of the loudness, and the gain over the bank's quiet coo; walls still halve it.)
  const bool fix = REXCVAR_GET(pdz_snd_pigeon_coo_fix);
  const float lis_cm = pdz::snd::ListenerCm(base, at);
  if (fix && lis_cm >= 0.0f)
  {
    const float ear = float(REXCVAR_GET(pdz_snd_pigeon_coo_ear));
    range_cm = std::min(6000.0f, std::max(range_cm, lis_cm / (1.0f - ear)));
    volume = std::min(4.0f, volume * float(REXCVAR_GET(pdz_snd_pigeon_coo_gain)));
  }
  if (const float cap = float(REXCVAR_GET(pdz_snd_pigeon_coo_range_max_m)) * 100.0f; cap > 0.0f)
  {
    range_cm = std::min(range_cm, std::max(cap, reach_cm + 500.0f)); // (Sat 21 Nov evening: never 60 m)
  }
  pdz::snd::CooRangeM(range_cm / 100.0f);
  // (The coo cue loops for ever in the bank: with the fix a kept sound stopped after one take.)
  const pdz::snd::Result r =
      fix ? pdz::snd::Timed(ctx, base, "XACT_SOUNDBANK_STOCK_PIGEON_COO", at, volume, float(REXCVAR_GET(pdz_snd_pigeon_coo_take_s)))
          : pdz::snd::Once(ctx, base, "XACT_SOUNDBANK_STOCK_PIGEON_COO", at, volume);
  pdz::snd::CooRangeM(0.0f);
  const int snd_state = pdz::snd::LastOnceState();
  const float from = fix && lis_cm >= 0.0f ? lis_cm : near_cm;
  const float at_jo = volume * volume * std::max(0.0f, 1.0f - from / range_cm); // (heard squared)
  const uint64_t n = ++g_coos;
  REXLOG_INFO("pdz_snd_pigeon_coo: a pigeon spawner at ({:.0f} {:.0f} {:.0f}), {:.1f} m above the nearest player's floor, is waiting "
              "with its birds, that player {:.1f} m away, the listener (camera) {:.1f} m (its scare distance {:.0f} m, asked within "
              "{:.0f} m, sound range {:.0f} m, {} wall(s) between, loudness {:.2f}{}: about {:.2f} loud at the listener): "
              "STOCK_PIGEON_COO {}{} [{}] ({} so far)",
              at[0], at[1], at[2], height_cm / 100.0f, near_cm / 100.0f, lis_cm / 100.0f, scare_cm / 100.0f, reach_cm / 100.0f,
              range_cm / 100.0f, walls, volume, fix ? " with the coo gain" : "", at_jo, pdz::snd::Text(r),
              snd_state == 6 || snd_state == 8 ? fmt::format(" BUT CUT AT ONCE by the game's sound manager (state {})", snd_state) : std::string(),
              pdz::snd::Load(base, 10), n);
}

// --- Audioscope zoom (item 14) -------------------------------------------------------------------------------------
REX_EXTERN(__imp__sub_825C3D48); // the audioscope's zoom (r3 = the audioscope, f1 = dt)
// Wed 18 Nov (sound3). Log 019: the zoom sound "plays over itself and does not stop": it was started on every
// frame's zoom step (at most every 0.15 s) and no start was ever stopped or freed. Now ONE sound per zoom action: it
// starts when the zoom begins to move (and none of ours is playing), is never started again while it plays, and is
// stopped and freed when the zoom has stood still for 0.12 s, when the audioscope's zoom routine stops running (the
// scope is put down: ScopeTick below, once a frame), and at a level end (a kept sound of restored_voice).
namespace
{
  uint32_t g_scope_sound = 0;       // our kept sound (the game thread)
  bool g_scope_action = false;      // a zoom action is going on (its sound was asked for)
  std::chrono::steady_clock::time_point g_scope_moved{}, g_scope_ran{};
  uint64_t g_scope_n = 0, g_scope_stops = 0;
  int g_scope_dir = 0; // (Fri 20 Nov night: the last zoom change's direction, 1 in / -1 out)
}
REX_HOOK_RAW(sub_825C3D48)
{
  const uint32_t scope = ctx.r3.u32;
  const bool on = Guest(scope) && REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_audioscope_zoom);
  const float zoom_before = on ? LoadF(base, scope + 1704) : 0.0f;
  __imp__sub_825C3D48(ctx, base);
  if (!on)
  {
    if (g_scope_sound)
    {
      pdz::snd::Stop(ctx, base, g_scope_sound);
    }
    g_scope_action = false;
    return;
  }
  const uint32_t owner = Load32(base, scope + 92);
  if (!Guest(owner) || !pdz::guestmem::Ok(base, owner, 68) || !(Load32(base, owner + 64) & 2)) // (Sun 22 Nov night (audit): Ok)
  {
    return; // (not the local player's audioscope: Rare's own test)
  }
  const auto now = std::chrono::steady_clock::now();
  g_scope_ran = now;
  const float zoom_after = LoadF(base, scope + 1704);
  const bool moving = std::fabs(zoom_after - zoom_before) > 0.0005f;
  if (moving)
  {
    // (Fri 20 Nov night, log 032 22:28: rapid presses gave one long sound: the zoom never stood still for 0.12 s, so
    // it stayed one zoom action. pdz_snd_audioscope_zoom_restart: a new press is the zoom moving again after
    // pdz_snd_audioscope_zoom_press_gap_s still, or turning from in to out: the sound is stopped and started again.)
    const int dir = zoom_after > zoom_before ? 1 : -1;
    const bool restart = REXCVAR_GET(pdz_snd_audioscope_zoom_restart);
    const double still_s = std::chrono::duration<double>(now - g_scope_moved).count();
    const char *what = "the zoom starts to move";
    bool press = !g_scope_action;
    // Sat 21 Nov (pdz_snd_audioscope_zoom_follow): the movement rule: a start when the zoom starts to move (no action
    // going on), a restart at every turn, nothing else.
    if (restart && REXCVAR_GET(pdz_snd_audioscope_zoom_follow))
    {
      const bool turned = g_scope_action && dir != g_scope_dir;
      press = !g_scope_action || turned;
      what = !g_scope_action ? (dir > 0 ? "the zoom starts to move (in)" : "the zoom starts to move (out)")
             : turned       ? (dir > 0 ? "the zoom turns: out -> in" : "the zoom turns: in -> out")
                            : "";
      if (!press && REXCVAR_GET(pdz_snd_audioscope_trace))
      {
        REXLOG_INFO("pdz_snd_audioscope_trace: zoom {:.3f} -> {:.3f} ({}, same direction): the sound {}", zoom_before,
                    zoom_after, dir > 0 ? "in" : "out", pdz::snd::Alive(base, g_scope_sound) ? "plays on" : "has ended");
      }
    }
    else if (!press && restart)
    {
      if (dir != g_scope_dir)
      {
        press = true;
        what = "the zoom turns (in / out): a new press";
      }
      else if (still_s >= REXCVAR_GET(pdz_snd_audioscope_zoom_press_gap_s))
      {
        press = true;
        what = "the zoom moves again after standing still: a new press";
      }
    }
    g_scope_moved = now;
    g_scope_dir = dir;
    if (press)
    {
      g_scope_action = true; // one sound for this zoom press, started now or not at all
      pdz::snd::Result why = pdz::snd::Result::kStarted;
      const bool was_playing = pdz::snd::Alive(base, g_scope_sound);
      if (restart || !was_playing)
      {
        pdz::snd::Stop(ctx, base, g_scope_sound); // (one still playing is cut: the new press starts from the beginning)
        g_scope_sound = pdz::snd::Start(ctx, base, "XACT_SOUNDBANK_STOCK_AUDIOSCOPE_ZOOM", nullptr, -1.0f, why, 12);
      }
      if (REXCVAR_GET(pdz_snd_audioscope_trace))
      {
        REXLOG_INFO("pdz_snd_audioscope_trace: START #{} ({}; zoom {:.2f} -> {:.2f}; {}); zoom sounds alive now: {}",
                    g_scope_n + 1, what, zoom_before, zoom_after,
                    was_playing ? "the one playing was stopped first" : "none was playing",
                    pdz::snd::Alive(base, g_scope_sound) ? 1 : 0);
      }
      const uint64_t n = ++g_scope_n;
      if (n <= 8 || (n & (n - 1)) == 0 || !g_scope_sound)
      {
        REXLOG_INFO("pdz_snd_audioscope_zoom: {} ({:.2f} -> {:.2f}, still {:.2f} s before): {}STOCK_AUDIOSCOPE_ZOOM {} (not placed: "
                    "the listener's own; {}; {} so far)",
                    what, zoom_before, zoom_after, still_s > 99.0 ? 99.0 : still_s,
                    restart && was_playing ? "the one playing stopped, " : "", pdz::snd::Text(why),
                    restart ? "one sound per press, restarted at each press, stopped when the zoom stops"
                            : "one sound per zoom action; stopped when the zoom stops",
                    n);
      }
    }
    return;
  }
  if (g_scope_action && now - g_scope_moved > std::chrono::milliseconds(int64_t(1000.0 * REXCVAR_GET(pdz_snd_audioscope_zoom_still_s))))
  {
    g_scope_action = false;
    const bool was_playing = pdz::snd::Alive(base, g_scope_sound);
    pdz::snd::Stop(ctx, base, g_scope_sound); // (the zoom has stopped: so does its sound, if it still plays)
    const uint64_t n = ++g_scope_stops;
    if (REXCVAR_GET(pdz_snd_audioscope_trace))
    {
      REXLOG_INFO("pdz_snd_audioscope_trace: STOP #{} (the zoom stood still {:.2f} s at {:.2f}; {}); zoom sounds "
                  "alive now: {}",
                  n, REXCVAR_GET(pdz_snd_audioscope_zoom_still_s), zoom_after, was_playing ? "it was still playing: stopped" : "it had already ended by itself",
                  pdz::snd::Alive(base, g_scope_sound) ? 1 : 0);
    }
    if (was_playing && (n <= 8 || (n & (n - 1)) == 0))
    {
      REXLOG_INFO("pdz_snd_audioscope_zoom: the zoom stopped: STOCK_AUDIOSCOPE_ZOOM stopped ({} so far)", n);
    }
  }
}
// Sat 21 Nov (pdz_snd_audioscope_trace): the audioscope's update (DP_SoundAlyser, vtable 820C95E0): its state, each time
// it changes, for the local player's scope only.
REX_EXTERN(__imp__sub_825C1BC0);
REX_HOOK_RAW(sub_825C1BC0)
{
  const uint32_t scope = ctx.r3.u32;
  __imp__sub_825C1BC0(ctx, base);
  if (!REXCVAR_GET(pdz_snd_audioscope_trace) || !Guest(scope))
  {
    return;
  }
  const uint32_t owner = Load32(base, scope + 92);
  if (!Guest(owner) || !pdz::guestmem::Ok(base, owner, 68) || !(Load32(base, owner + 64) & 2)) // (Sun 22 Nov night (audit): Ok)
  {
    return;
  }
  static uint32_t last_mode = ~0u, last_scan = ~0u, lines = 0;
  static float last_zoom = -1.0f;
  const uint32_t mode = Load32(base, scope + 1356), scan = Load32(base, scope + 1408);
  const float zoom = LoadF(base, scope + 1704);
  if ((mode != last_mode || scan != last_scan || std::fabs(zoom - last_zoom) > 0.001f) && lines < 400)
  {
    ++lines;
    REXLOG_INFO("pdz_snd_audioscope_trace: scope {:08X} mode {} (6 = up: its zoom runs), scanning {}, zoom {:.3f}; "
                "zoom sound {}",
                scope, int32_t(mode), scan, zoom, pdz::snd::Alive(base, g_scope_sound) ? "playing" : "not playing");
    last_mode = mode, last_scan = scan, last_zoom = zoom;
  }
}
namespace pdz::r2
{
  // Once a frame: the audioscope was put down while its zoom sound played (its zoom routine no longer runs).
  void ScopeTick(PPCContext &ctx, uint8_t *base)
  {
    if (g_scope_sound && std::chrono::steady_clock::now() - g_scope_ran > std::chrono::milliseconds(250))
    {
      g_scope_action = false;
      pdz::snd::Stop(ctx, base, g_scope_sound);
    }
  }
}

// --- Locked door (item 13) -----------------------------------------------------------------------------------------
REX_EXTERN(__imp__sub_82563788); // the door sound player (r3 = door, r4 = slot; 0 = "locked")
namespace
{
  struct LockedDoor
  {
    uint32_t door = 0;
    std::chrono::steady_clock::time_point at{};
  };
  LockedDoor g_locked[8];
  int g_locked_next = 0;
  uint64_t g_locked_n = 0;
}
REX_HOOK_RAW(sub_82563788)
{
  const uint32_t door = ctx.r3.u32, slot = ctx.r4.u32;
  const bool on = slot == 0 && Guest(door) && REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_door_locked);
  const uint32_t playing_before = on ? Load32(base, door + 1196) : 0;
  const bool sounds_off = on && base[door + 1165] != 0;
  __imp__sub_82563788(ctx, base);
  if (!on || sounds_off || playing_before != 0)
  {
    return; // (door sounds off, or another of its sounds still plays: Rare's own early outs)
  }
  if (Load32(base, door + 1196) != playing_before)
  {
    return; // (Rare started a sound for slot 0: its own, nothing of ours)
  }
  const auto now = std::chrono::steady_clock::now();
  for (const LockedDoor &l : g_locked)
  {
    if (l.door == door && now - l.at < std::chrono::seconds(1))
    {
      return;
    }
  }
  g_locked[g_locked_next] = {door, now};
  g_locked_next = (g_locked_next + 1) % 8;
  const float at[3] = {LoadF(base, door + 48), LoadF(base, door + 52), LoadF(base, door + 56)};
  const bool started = PlayAt(ctx, base, 3, "XACT_SOUNDBANK_STOCK_DOOR_LOCKED", at);
  const uint64_t n = ++g_locked_n;
  if ((n & (n - 1)) == 0 || !started)
  {
    REXLOG_INFO("pdz_snd_door_locked: a locked door at ({:.0f} {:.0f} {:.0f}) was tried: the game asked for its 'locked' sound, "
                "which its data leaves empty: STOCK_DOOR_LOCKED {} ({} so far)",
                at[0], at[1], at[2], started ? "started there" : "could not be started", n);
  }
}

// --- CamSpy rotation stop (item 16) --------------------------------------------------------------------------------
REX_EXTERN(__imp__sub_82541A10); // the CamSpy's sound update (r3 = the CamSpy)
// Sun 22 Nov (pdz_fix_camspy_turn_sound). The update keeps the loop while abs(+0x56C) > 0.01 (82541BF4; 0.01 at
// 8208BBF4) and stops / releases it otherwise (82541C84). +0x56C is the CamSpy look state's (+0x568) PITCH turn
// speed; the YAW turn speed is the next field, +0x570, which Rare's test never reads, so turning left / right was
// silent. And the direct mouse (pdz_mouse_direct_camspy, mouse_look.cpp) turns the view angles itself: both speeds
// stay 0. Fix: while the pitch speed is under the threshold and the yaw speed is over it, or the mouse turned the
// view within pdz_fix_camspy_turn_mouse_hold_s, the pitch field reads as over the threshold for Rare's update only
// (put back right after it: nothing else of Rare's reads it in that call), so Rare's own code starts, keeps and
// stops the loop, and our ROTATE_STOP follows it as before.
namespace pdz::mouse
{
  double CamSpyMouseTurnAgo(float *yaw, float *pitch); // mouse_look.cpp
}
namespace
{
  uint64_t g_camspy_n = 0;
  uint64_t g_camspy_turn_n = 0;
  const char *g_camspy_why = "";
}
REX_HOOK_RAW(sub_82541A10)
{
  const uint32_t spy = ctx.r3.u32;
  const bool turn_fix = Guest(spy) && REXCVAR_GET(pdz_fix_camspy_turn_sound);
  const bool turn_log = REXCVAR_GET(pdz_fix_camspy_turn_log);
  constexpr float kRareTurn = 0.01f; // Rare's threshold (8208BBF4)
  uint32_t pitch_raw = 0;
  bool forced = false;
  float pitch_speed = 0.0f, yaw_speed = 0.0f, mouse_yaw = 0.0f, mouse_pitch = 0.0f;
  double mouse_ago = 1e9;
  if (turn_fix || (turn_log && Guest(spy)))
  {
    pitch_raw = Load32(base, spy + 0x56C);
    pitch_speed = std::bit_cast<float>(pitch_raw);
    yaw_speed = LoadF(base, spy + 0x570);
    mouse_ago = pdz::mouse::CamSpyMouseTurnAgo(&mouse_yaw, &mouse_pitch);
  }
  const bool mouse_turning = mouse_ago < REXCVAR_GET(pdz_fix_camspy_turn_mouse_hold_s);
  if (turn_fix && !(std::fabs(pitch_speed) > kRareTurn) && (std::fabs(yaw_speed) > kRareTurn || mouse_turning))
  {
    forced = true;
    Store32(base, spy + 0x56C, std::bit_cast<uint32_t>(2.0f * kRareTurn));
  }
  const bool on = Guest(spy) && REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_camspy_rotate_stop);
  const uint32_t loop_before = Guest(spy) ? Load32(base, spy + 0x77C) : 0;
  __imp__sub_82541A10(ctx, base);
  if (forced)
  {
    Store32(base, spy + 0x56C, pitch_raw);
  }
  const uint32_t loop_after = Guest(spy) ? Load32(base, spy + 0x77C) : 0;
  if (turn_log && Guest(spy))
  {
    // Once a second: the CamSpy's state (steered = its control flag +1571; its state +1356; its view +1380 / +1376).
    static auto last = std::chrono::steady_clock::time_point{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last >= std::chrono::seconds(1))
    {
      last = now;
      REXLOG_INFO("pdz_camspy_turn: CamSpy {:08X}: steered {}, state {}, view yaw {:.1f} pitch {:.1f}, up / down speed "
                  "{:+.3f}, left / right speed {:+.3f}, last mouse turn {:.2f} s ago, whirr {}",
                  spy, base[spy + 1571], Load32(base, spy + 1356), LoadF(base, spy + 1380), LoadF(base, spy + 1376),
                  pitch_speed, yaw_speed, mouse_ago > 1e8 ? -1.0 : mouse_ago, loop_after ? "on" : "off");
    }
  }
  if (turn_log && loop_before == 0 && loop_after != 0)
  {
    g_camspy_why = std::fabs(pitch_speed) > kRareTurn ? "the up / down stick (the game's own test)"
                   : std::fabs(yaw_speed) > kRareTurn ? "the left / right stick (pdz_fix_camspy_turn_sound)"
                   : mouse_turning                   ? "the mouse (pdz_fix_camspy_turn_sound)"
                                                     : "?";
    REXLOG_INFO("pdz_camspy_turn: STOCK_CAMSPY_ROTATE STARTED by {} | up / down speed {:+.3f}, left / right speed {:+.3f}, "
                "last mouse turn {:.2f} s ago (yaw {:+.3f} pitch {:+.3f} deg) | start {}",
                g_camspy_why, pitch_speed, yaw_speed, mouse_ago > 1e8 ? -1.0 : mouse_ago, mouse_yaw, mouse_pitch,
                ++g_camspy_turn_n);
  }
  else if (turn_log && loop_before != 0 && loop_after == 0)
  {
    REXLOG_INFO("pdz_camspy_turn: STOCK_CAMSPY_ROTATE STOPPED (it was started by {}) | up / down speed {:+.3f}, left / right "
                "speed {:+.3f}, last mouse turn {:.2f} s ago | stop {}",
                g_camspy_why, pitch_speed, yaw_speed, mouse_ago > 1e8 ? -1.0 : mouse_ago, g_camspy_turn_n);
  }
  if (!on || loop_before == 0 || loop_after != 0)
  {
    return;
  }
  const bool started = PlayAt(ctx, base, 4, "XACT_SOUNDBANK_STOCK_CAMSPY_ROTATE_STOP", nullptr);
  const uint64_t n = ++g_camspy_n;
  if ((n & (n - 1)) == 0 || !started)
  {
    REXLOG_INFO("pdz_snd_camspy_rotate_stop: the CamSpy's turn ended (its rotate loop stopped): STOCK_CAMSPY_ROTATE_STOP {} ({} so far)",
                started ? "started" : "could not be started", n);
  }
}

// --- Bullet impact surfaces (item 11, log only) --------------------------------------------------------------------
REX_EXTERN(__imp__sub_8223F6B8); // Rare's impact routine (r3 shooter, r4 object or 0, r5 part, r6 surface, r7 &pos)
REX_EXTERN(sub_824F4E68);        // the surface record of a surface id (r3) -> r3
REX_EXTERN(__imp__sub_82611C10); // Rare's emitter maker (the impact routine's particles: returns 8223F860 / 8B0 / 900 / 950)
REXCVAR_DEFINE_BOOL(pdz_snd_effect_sparks_rare_only, true, "Gameplay",
                    "pdz_snd_effect_sparks on an electrical surface: only when the game itself makes its spark "
                    "particles for that hit. Off = any hit on such a surface")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
namespace
{
  thread_local int t_impact_depth = 0;     // (Thu 19 Nov: inside Rare's impact routine)
  thread_local int t_impact_emitters = 0;  // ... and how many emitters Rare made there
  thread_local uint32_t t_wood_surface = 0; // (Thu 19 Nov: a rusty door was hit: its impact / ricochet sounds use this surface)
  constexpr uint32_t kSolidWood = 2;        // Rare's SolidWood: GUNS_BULLETHIT_WOOD / GUNS_RICOHET_WOOD (log 022, F12)
}
REXCVAR_DEFINE_BOOL(pdz_snd_rusty_door_wood_hits, true, "Gameplay",
                    "Restored sound: bullets that hit the wooden-looking rusty doors (model door_rusty_door: Subway, "
                    "Rooftops) make the game's own wooden impact and ricochet sounds instead of the metal ones of "
                    "their collision surface (sound only; the particles stay the game's). Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
// Sun 22 Nov (pdz_snd_bullet_water). Rare's data names GUNS_BULLETHIT_WATER (guns bank, cue bullethit_water: five
// takes of 0.30-0.41 s in stock.xwb) as the bullet sound (+164, and the bounce +172) of the template
// misc/animating_water (Trinity Escape's rising water, the Temple's water), but no code reads a template's bullet
// slot (the name stays as text in the level package): the impact sound is always the hit surface's (record +140,
// sub_821FFDE0), and both the animated water and the level's water (harbour, rivers, canals) are surface 26, whose
// impact is STOCK_FSTEP_PUDDLE. Surface 26 is the game's only water surface: water splash particles, deep-water
// landing, the puddle step for walking and the water step for running; there is no separate puddle surface
// (surface 28, wet dirt with splash particles, keeps its dirt hit). A bullet hit on a surface listed in
// pdz_snd_bullet_water_surfaces (default 26), on an object or on the level: for that one call of the impact sound
// the hit surface's record names GUNS_BULLETHIT_WATER instead (put back right after), so Rare's own impact call plays
// it: same moment, place, mix and range rule. The ricochet is untouched (empty on surface 26). Footsteps, body falls
// and the particles are untouched (walking in water keeps the puddle step). Rare's bullet goes on through the water
// and also hits the bottom (its own second impact, unchanged).
REXCVAR_DEFINE_BOOL(pdz_snd_bullet_water, true, "Gameplay",
                    "Restored sound (Sun 22 Nov): a bullet into water plays the game's own bullet-into-water sound "
                    "(GUNS_BULLETHIT_WATER, which the game's data names for its water but never plays) instead of the "
                    "puddle footstep splash, on the water surfaces listed in pdz_snd_bullet_water_surfaces (the "
                    "harbour, rivers, canals, and the rising water of Trinity Escape and the Temple). Walking in water "
                    "keeps the puddle step. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_STRING(pdz_snd_bullet_water_surfaces, "26", "Gameplay",
                      "pdz_snd_bullet_water: the level's collision surfaces (ids, comma separated) that are real water; "
                      "a bullet that hits one plays the bullet-into-water sound. 26 = the game's only water surface (its "
                      "particles are water splashes, landing in it is deep water; the harbour, rivers, canals and "
                      "floods). Empty = only the animated water objects")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_water_mute_bottom, true, "Gameplay",
                    "Restored sound (Sun 22 Nov): the game's bullet goes on through water and also hits the bottom, "
                    "whose hit sound drowned the bullet-into-water sound. When a bullet's hit on water played the "
                    "water sound (pdz_snd_bullet_water), that same bullet's next hit below the water surface makes no "
                    "impact or ricochet sound (its particles and marks stay). Needs pdz_snd_bullet_water")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_hit_log_all, false, "Testing",
                    "Test aid (Sun 22 Nov): a 'pdz_snd_hit_all:' line for EVERY bullet hit (object, surface, place, "
                    "Jo's place), at most 400 a session")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_snd_surface_table_log, false, "Debug",
                    "Log only (Sun 22 Nov): at the first bullet hit of a level, the game's collision surface table: "
                    "each surface's id and every name in its record (footsteps, body fall, impact, ricochet...)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
namespace
{
  thread_local int t_water = 0; // (Sun 22 Nov: 1 = a water object was hit, 2 = a water surface of the level)
  // Sun 22 Nov (pdz_snd_water_mute_bottom): the last water hit that played the water sound, to know the same bullet's
  // bottom hit (Rare's next impact call of that shot: same shooter, within 0.1 s, below the water surface, on in the
  // shot's direction); t_mute_bottom = inside that bottom hit's impact call: no impact / ricochet sound.
  struct WaterShot
  {
    bool valid = false;
    uint32_t shooter = 0;
    float at[3] = {0, 0, 0}, from[3] = {0, 0, 0};
    bool from_ok = false;
    std::chrono::steady_clock::time_point when{};
  };
  WaterShot g_water_shot;
  thread_local bool t_mute_bottom = false;
  thread_local bool t_water_making = false; // (Sun 22 Nov: inside Rare's impact sound call for a water hit)
  uint64_t g_mute_bottom_n = 0;
  uint32_t g_water_name = 0;    // guest copy of the name (kept for the session)
  uint64_t g_water_n = 0;
  uint32_t WaterName(uint8_t *base)
  {
    if (!g_water_name)
    {
      static const char kName[] = "XACT_SOUNDBANK_GUNS_BULLETHIT_WATER";
      g_water_name = REX_KERNEL_MEMORY()->SystemHeapAlloc(64);
      if (g_water_name)
      {
        std::memset(base + g_water_name, 0, 64);
        std::memcpy(base + g_water_name, kName, sizeof(kName));
      }
    }
    return g_water_name;
  }
  // Text at a guest pointer, up to 64 characters, or "" if it is not readable / not text.
  std::string GuestText(const uint8_t *base, uint32_t p)
  {
    std::string s;
    if (!pdz::guestmem::Ok(base, p, 64))
    {
      return s;
    }
    for (uint32_t i = 0; i < 64; ++i)
    {
      const char c = char(base[p + i]);
      if (c == 0)
      {
        break;
      }
      if (c < 32 || c > 126)
      {
        return {};
      }
      s.push_back(c);
    }
    return s;
  }
  // A hit on a listed water surface: 1 = on an object (Rare's animated water: Trinity Escape's rising water, the
  // Temple's), 2 = on the level (harbour, rivers, canals). 0 = not water.
  int WaterHit(const uint8_t *base, uint32_t object, uint32_t surface)
  {
    (void)base;
    const std::string list = REXCVAR_GET(pdz_snd_bullet_water_surfaces);
    size_t at = 0;
    while (at < list.size())
    {
      size_t end = list.find(',', at);
      end = end == std::string::npos ? list.size() : end;
      const std::string item = list.substr(at, end - at);
      if (!item.empty() && std::strtoul(item.c_str(), nullptr, 10) == surface && item.find_first_not_of("0123456789 ") == std::string::npos)
      {
        return object >= 0x10000u && object < 0xF0000000u ? 1 : 2;
      }
      at = end + 1;
    }
    return 0;
  }
}
// Sun 22 Nov (waterfix): the water hit's voice plays (heard-level meter, 60 ms after the start: 1 voice, -8.7 dB at
// 5 m, +1.1 dB over a footstep's gain), but Rare's takes (stock waves 415-420, the puddle step's own) are about 9 dB
// quieter than its bullet-on-stone takes and last 0.3-0.4 s, at the very moment of the gunshot: not heard. Rare's
// impact call makes the sound (sub_821FF428, returning to 821FF3EC) and then plays it; in between, its loudness
// (+332) gets the measured trim pdz_snd_trim_bullet_water (pdz::snd::Trimmed), as our own restored sounds' starts do.
namespace pdz::snd
{
  float Trimmed(const char *cue, float volume); // restored_voice_Wed18Nov.cpp
}
namespace pdz::water
{
  void OnSoundMade(uint8_t *base, uint32_t caller, uint32_t sound)
  {
    if (!t_water_making || caller != 0x821FF3ECu || !pdz::guestmem::Ok(base, sound, 336))
    {
      return;
    }
    const float was = LoadF(base, sound + 332);
    const float now = pdz::snd::Trimmed("GUNS_BULLETHIT_WATER", std::isfinite(was) && was > 0.0f ? was : 1.0f);
    Store32(base, sound + 332, std::bit_cast<uint32_t>(now));
    static int said = 0;
    if (said < 5)
    {
      ++said;
      REXLOG_INFO("pdz_snd_bullet_water: the water hit's loudness {:.2f} -> {:.2f} (pdz_snd_trim_bullet_water; line {} of 5)", was, now,
                  said);
    }
  }
}
REX_EXTERN(__imp__sub_821FFDE0); // the impact routine's hit sound (r3 surface, r4 &place): the surface record +140 cue
REX_EXTERN(__imp__sub_821FFE60); // ... and its ricochet sound (record +144)
REX_HOOK_RAW(sub_821FFDE0)
{
  if (t_mute_bottom && uint32_t(ctx.lr) == 0x8223F760u)
  {
    return; // (pdz_snd_water_mute_bottom: the same bullet's hit on the bottom under the water: no impact sound)
  }
  if (t_wood_surface && uint32_t(ctx.lr) == 0x8223F760u)
  {
    ctx.r3.u64 = t_wood_surface;
  }
  if (t_water && uint32_t(ctx.lr) == 0x8223F760u && WaterName(base))
  {
    uint32_t record = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = ctx.r3.u32;
      sub_824F4E68(frame.ctx, base);
      record = frame.ctx.r3.u32;
    }
    if (pdz::guestmem::Ok(base, record, 0x90))
    {
      const uint32_t was = Load32(base, record + 0x8C);
      Store32(base, record + 0x8C, WaterName(base));
      t_water_making = true;
      __imp__sub_821FFDE0(ctx, base);
      t_water_making = false;
      Store32(base, record + 0x8C, was);
      const uint64_t n = ++g_water_n;
      if (n <= 40 || (n & (n - 1)) == 0)
      {
        REXLOG_INFO("pdz_snd_bullet_water: a bullet hit {} : GUNS_BULLETHIT_WATER asked for by the game's impact call instead "
                    "of {} ({} so far)",
                    t_water == 1 ? "an object's water surface (animated water)" : "the level's water surface",
                    GuestText(base, was).empty() ? std::string("(no name)") : GuestText(base, was), n);
      }
      return;
    }
  }
  __imp__sub_821FFDE0(ctx, base);
}
REX_HOOK_RAW(sub_821FFE60)
{
  if (t_mute_bottom && uint32_t(ctx.lr) == 0x8223F774u)
  {
    return; // (pdz_snd_water_mute_bottom: ... and no ricochet)
  }
  if (t_wood_surface && uint32_t(ctx.lr) == 0x8223F774u)
  {
    ctx.r3.u64 = t_wood_surface;
  }
  __imp__sub_821FFE60(ctx, base);
}
REX_HOOK_RAW(sub_82611C10)
{
  if (t_impact_depth > 0)
  {
    ++t_impact_emitters;
  }
  __imp__sub_82611C10(ctx, base);
}
namespace
{
  struct HitSeen
  {
    uint32_t cls = 0, surface = 0;
  };
  HitSeen g_hits_seen[160];
  int g_hits_n = 0;
  std::string_view NameAt(const uint8_t *base, uint32_t record, uint32_t off)
  {
    // (Sun 22 Nov night (audit): the record and the name read only where mapped.)
    const uint32_t p = Guest(record) && pdz::guestmem::Ok(base, record, off + 4) ? Load32(base, record + off) : 0;
    if (!Guest(p) || !pdz::guestmem::Ok(base, p, 64))
    {
      return "(none)";
    }
    const char *t = reinterpret_cast<const char *>(base + p);
    const size_t n = strnlen(t, 64);
    return n ? std::string_view(t, n) : std::string_view("(empty)");
  }
}
REX_HOOK_RAW(sub_8223F6B8)
{
  const uint32_t object = ctx.r4.u32, surface = ctx.r6.u32, where = ctx.r7.u32;
  const bool log = REXCVAR_GET(pdz_snd_hit_surface_log) && g_hits_n < 160;
  float at[3] = {0.0f, 0.0f, 0.0f};
  if (log && Guest(where))
  {
    for (int i = 0; i < 3; ++i)
    {
      at[i] = LoadF(base, where + 4 * i);
    }
  }
  if (log)
  {
    const uint32_t cls = Guest(object) ? Load32(base, object) : 0; // (its class: the vtable; 0 = the level itself)
    bool seen = false;
    for (int i = 0; i < g_hits_n && !seen; ++i)
    {
      seen = g_hits_seen[i].cls == cls && g_hits_seen[i].surface == surface;
    }
    if (!seen)
    {
      g_hits_seen[g_hits_n++] = {cls, surface};
      uint32_t record = 0;
      {
        rex::CallFrame frame(ctx);
        frame.ctx.r3.u64 = surface;
        sub_824F4E68(frame.ctx, base);
        record = frame.ctx.r3.u32;
      }
      const uint32_t jo = Load32(base, kPlayerOne);
      const float jx = Guest(jo) ? LoadF(base, jo + kCharPos) : 0.0f, jy = Guest(jo) ? LoadF(base, jo + kCharPos + 4) : 0.0f,
                  jz = Guest(jo) ? LoadF(base, jo + kCharPos + 8) : 0.0f;
      REXLOG_INFO("pdz_snd_hit_surface: a bullet hit {} on surface {}: impact {}, ricochet {} (about 4 hits in 10) | hit at ({:.0f} "
                  "{:.0f} {:.0f}){} | Jo at ({:.0f} {:.0f} {:.0f}) | the first such hit this session",
                  Guest(object) ? fmt::format("an object {:08X} (class {:08X})", object, cls) : std::string("the level"), surface,
                  NameAt(base, record, 140), NameAt(base, record, 144), at[0], at[1], at[2],
                  Guest(object) ? " (in the object's own space)" : "", jx, jy, jz);
    }
  }
  // Wed 18 Nov night (log 021: a shot TV / wall monitor / red screen shows sparks and makes no spark sound; the
  // effect hook of round 2 never fired for them). The sparks of those panels are the impact emitters of the surface
  // the bullet hits: Rare's surface 19 (Electrical; record +108 / +112 / +116 / +120 its emitters). A bullet on an
  // electrical surface that has an emitter: the small spark there (pdz_snd_effect_sparks; SparkSound keeps them
  // 0.25 s apart). An object's hit point is in its own space, so the object's place is used for it. Not for a hit
  // flagged 0x200 in the ninth argument (no effects wanted).
  bool spark = false;
  float spark_at[3] = {0.0f, 0.0f, 0.0f};
  if (REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_effect_sparks) && !(Load32(base, ctx.r1.u32 + 84) & 0x200u))
  {
    uint32_t record = 0;
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = surface;
      sub_824F4E68(frame.ctx, base);
      record = frame.ctx.r3.u32;
    }
    if (Guest(record) && Load32(base, record) == 19 &&
        (Load32(base, record + 108) | Load32(base, record + 112) | Load32(base, record + 116) | Load32(base, record + 120)) != 0)
    {
      const uint32_t from = Guest(object) ? object + 48 : where;
      if (Guest(from))
      {
        spark = true;
        for (int i = 0; i < 3; ++i)
        {
          spark_at[i] = LoadF(base, from + 4 * i);
        }
      }
    }
  }
  const uint32_t spark_caller = uint32_t(ctx.lr);
  // Thu 19 Nov: the spark only where Rare's own spark shows: Rare's emitter routine (sub_82611C10) must have run
  // inside this impact call (it makes the surface's particles); a hit that makes none plays nothing.
  const int emit_before = t_impact_emitters;
  // Thu 19 Nov: a bullet on a rusty door (it looks wooden; Rare's collision surface for it is metal): its hit and
  // ricochet sounds are Rare's wooden ones (SolidWood), the particles stay Rare's.
  const uint32_t wood_before = t_wood_surface;
  if (REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_rusty_door_wood_hits) && surface != kSolidWood &&
      bymodel_sounds::IsRustyDoor(base, object))
  {
    t_wood_surface = kSolidWood;
    static int said = 0;
    if (said < 20)
    {
      ++said;
      REXLOG_INFO("pdz_snd_rusty_door: a bullet hit the rusty door {:08X} at ({:.0f} {:.0f} {:.0f}) on its surface {}: its impact and "
                  "ricochet sounds are Rare's wooden ones (surface {}) (line {} of 20)",
                  object, LoadF(base, object + 48), LoadF(base, object + 52), LoadF(base, object + 56), surface, kSolidWood, said);
    }
  }
  // Sun 22 Nov: the surface table, once per table (pdz_snd_surface_table_log), and the bullet into water.
  if (REXCVAR_GET(pdz_snd_surface_table_log))
  {
    static uint32_t dumped_array = 0, dumped_n = 0;
    const uint32_t g = Load32(base, 0x82D6BFD8u + 0x30);
    const uint32_t t = pdz::guestmem::Ok(base, g, 0x3C) ? Load32(base, g + 0x38) : 0;
    const uint32_t array = pdz::guestmem::Ok(base, t, 12) ? Load32(base, t) : 0;
    const uint32_t count = array ? Load32(base, t + 8) : 0;
    if (array && count > 0 && count < 256 && (array != dumped_array || count != dumped_n) &&
        pdz::guestmem::Ok(base, array, count * 4))
    {
      dumped_array = array, dumped_n = count;
      REXLOG_INFO("pdz_snd_surface_table: {} surfaces in the game's collision surface table", count);
      for (uint32_t i = 0; i < count; ++i)
      {
        const uint32_t rec = Load32(base, array + 4 * i);
        if (!pdz::guestmem::Ok(base, rec, 0xC0))
        {
          continue;
        }
        std::string names;
        for (uint32_t off = 4; off < 0xC0; off += 4)
        {
          const std::string s = GuestText(base, Load32(base, rec + off));
          if (s.size() >= 2)
          {
            names += fmt::format(" | +{} {}", off, s);
          }
        }
        REXLOG_INFO("pdz_snd_surface_table: surface {} (record {:08X}, +4 {:08X}){}", Load32(base, rec), rec,
                    Load32(base, rec + 4), names);
      }
    }
  }
  if (REXCVAR_GET(pdz_snd_hit_log_all))
  {
    static int all_n = 0;
    if (all_n < 400)
    {
      ++all_n;
      const uint32_t jo = Load32(base, kPlayerOne);
      REXLOG_INFO("pdz_snd_hit_all: {} surface {} at ({:.0f} {:.0f} {:.0f}) | Jo at ({:.0f} {:.0f} {:.0f}) | water rule {} ({})",
                  Guest(object) ? fmt::format("object {:08X} (class {:08X})", object, Load32(base, object)) : std::string("level"),
                  surface, Guest(where) ? LoadF(base, where) : 0.0f, Guest(where) ? LoadF(base, where + 4) : 0.0f,
                  Guest(where) ? LoadF(base, where + 8) : 0.0f, Guest(jo) ? LoadF(base, jo + kCharPos) : 0.0f,
                  Guest(jo) ? LoadF(base, jo + kCharPos + 4) : 0.0f, Guest(jo) ? LoadF(base, jo + kCharPos + 8) : 0.0f,
                  WaterHit(base, object, surface), all_n);
    }
  }
  // Sun 22 Nov (pdz_snd_water_mute_bottom): is this the bottom hit of the bullet that just played the water sound?
  const bool mute_before = t_mute_bottom;
  t_mute_bottom = false;
  if (g_water_shot.valid && REXCVAR_GET(pdz_snd_water_mute_bottom) && REXCVAR_GET(pdz_snd_bullet_water) &&
      REXCVAR_GET(pdz_restored_sounds))
  {
    WaterShot &w = g_water_shot;
    const auto now = std::chrono::steady_clock::now();
    if (now - w.when > std::chrono::milliseconds(100))
    {
      w.valid = false; // (too late: another shot)
    }
    else if (ctx.r3.u32 == w.shooter && Guest(where) && !Guest(object) && WaterHit(base, object, surface) == 0)
    {
      const float p[3] = {LoadF(base, where), LoadF(base, where + 4), LoadF(base, where + 8)};
      const float dx = p[0] - w.at[0], dz = p[2] - w.at[2];
      const float hd = std::sqrt(dx * dx + dz * dz);
      bool along = hd < 50.0f; // (a near-vertical shot: straight down)
      if (!along && w.from_ok)
      {
        const float sx = w.at[0] - w.from[0], sz = w.at[2] - w.from[2];
        const float sl = std::sqrt(sx * sx + sz * sz);
        along = sl > 1.0f && (sx * dx + sz * dz) / (sl * hd) > 0.8f;
      }
      if (p[1] < w.at[1] - 1.0f && along && hd < 5000.0f)
      {
        t_mute_bottom = true;
        w.valid = false;
        const uint64_t n = ++g_mute_bottom_n;
        REXLOG_INFO("pdz_snd_water_mute_bottom: the bullet that hit the water at ({:.0f} {:.0f} {:.0f}) then hit the bottom at "
                    "({:.0f} {:.0f} {:.0f}) (surface {}, {:.1f} m below the water): no impact / ricochet sound there ({} so far)",
                    w.at[0], w.at[1], w.at[2], p[0], p[1], p[2], surface, (w.at[1] - p[1]) / 100.0f, n);
      }
    }
  }
  const int water_before = t_water;
  t_water = REXCVAR_GET(pdz_restored_sounds) && REXCVAR_GET(pdz_snd_bullet_water) && !(Load32(base, ctx.r1.u32 + 84) & 0x200u)
                ? WaterHit(base, object, surface)
                : 0;
  if (t_water)
  {
    static int said = 0;
    if (said < 40)
    {
      ++said;
      float hx = 0, hy = 0, hz = 0;
      if (Guest(where))
      {
        hx = LoadF(base, where), hy = LoadF(base, where + 4), hz = LoadF(base, where + 8);
      }
      const uint32_t jo = Load32(base, kPlayerOne);
      REXLOG_INFO("pdz_snd_bullet_water: hit on {} on surface {} at ({:.0f} {:.0f} {:.0f}){} | Jo at ({:.0f} {:.0f} {:.0f}) (line {} of 40)",
                  t_water == 1 ? fmt::format("the water object {:08X} (class {:08X})", object, Load32(base, object))
                               : std::string("the level"),
                  surface, hx, hy, hz, t_water == 1 ? " (in the object's own space; the animated water sits at 0 0 0)" : "",
                  Guest(jo) ? LoadF(base, jo + kCharPos) : 0.0f, Guest(jo) ? LoadF(base, jo + kCharPos + 4) : 0.0f,
                  Guest(jo) ? LoadF(base, jo + kCharPos + 8) : 0.0f, said);
    }
  }
  const uint32_t shooter = ctx.r3.u32;
  const uint64_t water_n_before = g_water_n;
  ++t_impact_depth;
  __imp__sub_8223F6B8(ctx, base);
  --t_impact_depth;
  if (t_water && g_water_n != water_n_before && Guest(where))
  {
    // (the water sound was played for this hit: remember it for the same bullet's bottom hit)
    WaterShot &w = g_water_shot;
    w.valid = true;
    w.shooter = shooter;
    w.when = std::chrono::steady_clock::now();
    for (int i = 0; i < 3; ++i)
    {
      w.at[i] = LoadF(base, where + 4 * i);
    }
    w.from_ok = pdz::guestmem::Ok(base, shooter, 60);
    for (int i = 0; w.from_ok && i < 3; ++i)
    {
      w.from[i] = LoadF(base, shooter + kCharPos + 4 * i);
    }
  }
  t_mute_bottom = mute_before;
  t_water = water_before;
  t_wood_surface = wood_before;
  const int emitted = t_impact_emitters - emit_before;
  if (spark && emitted <= 0 && REXCVAR_GET(pdz_snd_effect_sparks_rare_only))
  {
    static int said_none = 0;
    if (said_none < 20)
    {
      ++said_none;
      REXLOG_INFO("pdz_snd_effect_sparks: a hit on an ELECTRICAL surface {} made no particles of Rare's (no emitter ran): no spark "
                  "sound (pdz_snd_effect_sparks_rare_only; line {} of 20)",
                  Guest(object) ? fmt::format("on the object {:08X}", object) : std::string("of the level"), said_none);
    }
    spark = false;
  }
  if (spark)
  {
    static int said = 0;
    if (said < 40)
    {
      ++said;
      REXLOG_INFO("pdz_snd_effect_sparks: a bullet hit an ELECTRICAL surface (Rare's surface 19, with spark emitters) {} at ({:.0f} "
                  "{:.0f} {:.0f}): the small spark is asked for there (line {} of 40)",
                  Guest(object) ? fmt::format("on the object {:08X}", object) : std::string("of the level"), spark_at[0],
                  spark_at[1], spark_at[2], said);
    }
    pdz::restored_sounds::SparkSound(ctx, base, spark_at, false, spark_caller);
  }
}

// --- Wed 18 Nov night: the shot-down security camera's fallen head (log 021: 170 contact lines, none for a camera) ---
// The head is a NEW prop made in the camera's break routine sub_825BFCA0 by the piece constructor sub_825CC180 (the
// call at 825BFFF4). It never passes the prop set-up (sub_825165F8), so its flag word +64 never gets Rare's "track
// contacts" bit 0x80000000, and the constructor sets its physics component's byte +212 (head +444), which keeps the
// physics step sub_825E4AE8 from switching its contact record on. Rare's knock routine sub_82511A90 therefore
// skips it. For the camera's head only (the caller), right after Rare has made it: the bit is set, the byte is
// cleared, its template's contact flag is on, and the record's start mute is over (the head lands after about
// 0.8 s). From then on Rare's own knock routine plays the template's own listed knock (electrical\camera:
// STOCK_CRATE_HIT_METAL) with Rare's speeds, loudness and limits.
// Thu 19 Nov: OFF by default (with it on the fallen head was no longer knocked about the level and a melee hit made a
// spark sound): the head is stock. The camera's break and fall sounds stay.
REXCVAR_DEFINE_BOOL(pdz_snd_camera_head_knocks, false, "Gameplay",
                    "Restored sound: the head of a security camera that was shot down knocks as it lands and bounces "
                    "(the game's own knock routine with the camera's own listed metal knock). Off by default: it changed "
                    "how the head moves. Needs pdz_restored_sounds")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REX_EXTERN(__imp__sub_825CC180);
REX_HOOK_RAW(sub_825CC180)
{
  const uint32_t caller = uint32_t(ctx.lr);
  __imp__sub_825CC180(ctx, base);
  const uint32_t head = ctx.r3.u32;
  if (caller != 0x825BFFF8u || !Guest(head) || !REXCVAR_GET(pdz_restored_sounds) || !REXCVAR_GET(pdz_snd_camera_head_knocks) ||
      !pdz::guestmem::Ok(base, head, 448)) // (Sun 22 Nov night (audit): the head read / written only where mapped)
  {
    return;
  }
  const uint64_t r3_keep = ctx.r3.u64;
  const uint32_t vtable = Load32(base, head);
  uint32_t rec = 0;
  if (vtable >= 0x82000000u && vtable < 0x83000000u) // (Sun 22 Nov night (audit): the game image)
  {
    if (PPCFunc *fn = pdz::GuestFunction(Load32(base, vtable + 72)))
    {
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = head;
      fn(frame.ctx, base);
      rec = frame.ctx.r3.u32;
    }
  }
  ctx.r3.u64 = r3_keep;
  const std::string tpl = Guest(rec) ? pdz::prop_contacts::TemplateOf(base, rec) : std::string();
  if (tpl != "electrical\\camera" || !pdz::guestmem::Ok(base, rec, kFlag + 1)) // (Sun 22 Nov night (audit): Ok)
  {
    REXLOG_INFO("pdz_snd_contact: a camera's fallen head {:08X} (class {:08X}) has the template '{}' (record {:08X}), not "
                "electrical\\camera: left as Rare made it (no knocks)",
                head, vtable, tpl, rec);
    return;
  }
  if (base[rec + kFlag] == 0)
  {
    pdz::prop_contacts::Enable(base, rec, true);
  }
  Store32(base, head + 64, Load32(base, head + 64) | 0x80000000u);
  base[head + 444] = 0;
  StoreF(base, head + 432, 0.0f);
  REXLOG_INFO("pdz_snd_contact: a camera's fallen head {:08X} (class {:08X}, template {}) at ({:.0f} {:.0f} {:.0f}): contact "
              "tracking switched on for it (flag word +64, component byte +212 cleared, start mute over): Rare's own knock "
              "routine plays its listed knock from now on (pdz_snd_camera_head_knocks)",
              head, vtable, tpl, LoadF(base, head + 48), LoadF(base, head + 52), LoadF(base, head + 56));
}
