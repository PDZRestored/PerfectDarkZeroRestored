// Counter-Operative round 4, item 2 (respawn near the agent): the level's wake groups. Built only with PDZ_COUNTEROP.
//
// Rare's levels put most numbered guards to sleep in the script's Setup (SetDormant(1) = guard vtable +0x340 =
// 825818E0) and wake them in groups from the script's message handler (vtable +0x24) when a trigger volume of the
// level data sends a "type 7" message (setup +0x1E / +0x22: kind 08 ContactVolume, type 07, arg = the group).
// The tables below (generated from the TU3 image and the level packages) give, per level, each group: the message,
// the script's once bit, the guards' ids, and the volumes (centre + size, cm) that send the message.
//   NextGroup()      the group the agent is heading into next: the not-yet-fired group whose nearest wake volume is
//                    nearest to her, within pdz_counterop_next_area_m.
//   InNextArea(g)    a sleeping guard may be taken only when his id is in that group.
//   WakeGroupOf(g)   queue Rare's own wake (825818E0(guard, 0), the very call the script makes) for every other
//                    sleeping guard of the taken guard's group, a few a frame (Step()). Nothing else of the script's
//                    case runs: no once bit, no orders, no messages, so the level's own trigger still fires in full
//                    when the agent gets there (its SetDormant(0) is then a no-op on the awake guards).
// Call-ins: counter_op_guards.cpp Pick() (InNextArea), counter_op.cpp SpawnAt() (WakeGroupOf), Tick() (Step), Reset(),
// counter_op_triggers.cpp's level message hook 8214E520 (OnLevelMessage). No hook of its own.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <unordered_set>
#include <vector>

#include <rex/cvar.h>
#include <rex/hook.h>
#include <rex/logging.h>
#include <rex/ppc/func.h>

#include <fmt/format.h>

#include "counter_op_guards.h"
#include "guest_readable_Sat21Nov.h" // Sat 21 Nov evening: names read only mapped memory

bool PdzCounterOpRooftopsDropAreaOk(uint32_t guard, std::string &note); // round 13 e, counter_op_rooftops.cpp

REXCVAR_DEFINE_DOUBLE(pdz_counterop_next_area_m, 35.0, "Gameplay",
                      "Counter-Operative: a sleeping original guard may be taken only when the agent is within this "
                      "many metres of the trigger that wakes his group (and it is the nearest such trigger not yet "
                      "fired); his group is then woken with him")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_next_area_guard_m, 50.0, "Gameplay",
                      "Counter-Operative: a sleeping guard of the agent's next wake group may also be taken when the "
                      "guard himself is within this many metres of her (Rooftops' first group stands 35-45 m from her "
                      "start with its trigger further off; log 053's 'lop2' was 91 m away)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_wake_group, true, "Gameplay",
                    "Counter-Operative (default on): taking a sleeping guard wakes the rest of his group (Rare's own "
                    "wake), so player 2 never stands alone among sleeping guards")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_area_rule, true, "Gameplay",
                    "Counter-Operative (round 5, default on): player 2 takes only guards of the agent's current area "
                    "or of the next area on her route (and that only in reach: pdz_counterop_next_area_m / "
                    "_next_area_guard_m), awake or asleep; a guard of an area further ahead is never taken. The areas "
                    "are Rare's wake groups (Subway, Rooftops, Nightclub, both Trinity levels). Off = round 4")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_area_behind, true, "Gameplay",
                    "Counter-Operative: guards of areas the agent has already been through may be taken too (off: only "
                    "her current and next area)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_area_came_m, 40.0, "Gameplay",
                      "Counter-Operative: an awake guard of an area further ahead may be taken when he has walked at "
                      "least 15 m from his post and is now within this many metres of the agent, on her floor (0 = "
                      "never)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REXCVAR_DEFINE_BOOL(pdz_counterop_area_next_in_order, true, "Gameplay",
                    "Counter-Operative (default on): the agent's next area is the one after her current area in the "
                    "level's order. Off = the area of the nearest wake trigger that has not fired (round 5), which "
                    "could name an area two ahead")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

// Round 16 (Fri 20 Nov, item 6): areas by where a guard stands, and Rare's scripted scenes left to Rare.
REXCVAR_DEFINE_BOOL(pdz_counterop_area_reach_by_position, true, "Gameplay",
                    "Counter-Operative (round 16, default on): a guard of the agent's NEXT area is in reach only by "
                    "where he stands: within pdz_counterop_next_area_guard_m of her, or within "
                    "pdz_counterop_area_entry_m of that area's own wake trigger while she is within "
                    "pdz_counterop_next_area_m of it. Off = round 5: her distance to the trigger alone was enough "
                    "(Subway log 027: 'war1' taken 60 m ahead of her, deep in the hangar)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_area_entry_m, 30.0, "Gameplay",
                      "Counter-Operative: with pdz_counterop_area_reach_by_position, a guard of the agent's next area "
                      "counts as in reach when he stands within this many metres of that area's wake trigger")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_area_by_position, true, "Gameplay",
                    "Counter-Operative (round 16, default on): an AWAKE guard's area is the area of the nearest guard "
                    "post of the level's area table (the posts of the guards of its wake groups and id lists, his own "
                    "included) within pdz_counterop_area_post_m of where he stands now; further from every post, his "
                    "wake group / table / squad area as before. Sleeping guards stand at their posts: unchanged. Off = "
                    "by wake group / table / squad only")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_area_post_m, 12.0, "Gameplay",
                      "Counter-Operative: with pdz_counterop_area_by_position, how near (metres) a guard must stand to "
                      "a post of the area table for that post's area to count as his")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_scene_hold, true, "Gameplay",
                    "Counter-Operative (round 16, default on): a guard Rare's level script still keeps asleep is never "
                    "player 2's body, on any level. The script puts those guards to sleep in its Setup and wakes each "
                    "group together when the agent reaches its trigger; the group's own scripted scene (Subway's police "
                    "on the platform, door kicks, patrol starts) begins then. Awake guards are taken instead; with none "
                    "he waits. Off = round 4-15: a sleeping guard of her next area could be taken (Subway log 027: "
                    "'zsb2')")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_DOUBLE(pdz_counterop_scene_hold_max_s, 30.0, "Gameplay",
                      "Counter-Operative (round 16, default 30 s): with pdz_counterop_scene_hold, after this many "
                      "seconds without a body a sleeping guard of a wake group may be taken again (nobody else is "
                      "woken with him); a sleeping guard the level script wakes on a cue of its own after twice as "
                      "long. Awake guards always come first. 0 = never (player 2 may wait minutes)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);
REXCVAR_DEFINE_BOOL(pdz_counterop_scene_no_early_wake, true, "Gameplay",
                    "Counter-Operative (round 16, default on): taking a guard never wakes sleeping guards of a wake "
                    "group whose own trigger has not fired; they wake when the level wakes them, so their scripted "
                    "scene plays when the agent gets there. Off = rounds 4-15: his area's sleepers were woken with him "
                    "(Subway log 027: the platform police woken 107 s before the level's own trigger)")
    .lifecycle(rex::cvar::Lifecycle::kHotReload);

REX_EXTERN(sub_825818E0); // SetDormant (character, 1 sleep / 0 wake): guard vtable +0x340

bool PdzCounterOpSceneHoldNoTimeout(); // counter_op_quick_Sat21Nov.cpp
namespace counter_op_wake
{
  namespace
  {
    struct Volume
    {
      float x, y, z, sx, sy, sz; // centre and size (cm); rotation ignored (UNSURE whether size is full or half)
    };
    struct Group
    {
      uint8_t type, arg;  // the level message (7 = wake an area; Rooftops also 8 / 33)
      uint32_t once_word; // [script +0x3EC] + this ...
      uint32_t once_bit;  // ... has this bit once the script ran the case (0 = not known)
      const char *ids;    // comma list
      Volume volumes[3];  // unused entries are all 0
    };
    // Subway (level script vtable 0x820A6030), from wakemap.py ware
    constexpr Group kSubway[] = {
        {7, 1, 0x0, 0x00000002u, "lor1,lor2,lorp,lop2,fac1,fac2", {{-3251.f, 201.f, -3939.f, 1000.f, 301.f, 850.f}}},
        {7, 2, 0x0, 0x00000004u, "arm1,arm2,arm3,arm4,armp,arp2,arp3,arp4", {{-66.f, 146.f, -6992.f, 100.f, 400.f, 1000.f}}},
        {7, 3, 0x0, 0x00000008u, "saf1,saf2,saf3,saf4,saf5,18-1,s18b,phon,s19a,s19b", {{2247.f, 246.f, -9165.f, 100.f, 200.f, 250.f}, {5900.f, 322.f, -8873.f, 400.f, 300.f, 100.f}}},
        {7, 4, 0x0, 0x00000010u, "sub1,sub2,sub3,sub4,sub5,sub6,zsb1,zsb2,zsb3,zsb4,zsb5,zsb6,zsb7,zsb8,zsb9,zsba", {{6294.f, -216.f, -12954.f, 1000.f, 800.f, 100.f}}},
        {7, 5, 0x0, 0x00000020u, "out1,out2,out3,out4,inn1,inn2,inn3,inn4,inn5,inn6,sin1,sin2,sin3,sin4,s16a,s16b", {{8434.f, -1829.f, -19826.f, 800.f, 400.f, 100.f}}},
    };
    // Rooftops (level script vtable 0x820A4B08), from wakemap.py roof
    constexpr Group kRooftops[] = {
        {7, 1, 0x0, 0x00000002u, "mff1,mff2,tff1,tff2", {{3005.f, 2009.f, 4321.f, 600.f, 200.f, 400.f}, {3693.f, 1093.f, 4267.f, 200.f, 200.f, 400.f}}},
        {7, 2, 0x0, 0x00000004u, "sni2", {{4080.f, 1919.f, 5473.f, 100.f, 200.f, 230.f}}},
        {7, 3, 0x0, 0x00000008u, "top1,top2,top3,s17a", {{5408.f, 2236.f, 4855.f, 800.f, 500.f, 200.f}, {7398.f, 2641.f, 1777.f, 1200.f, 500.f, 200.f}}},
        {7, 5, 0x0, 0x00000020u, "bfm1,bfm2,bfm3", {{3516.f, 200.f, 2679.f, 100.f, 250.f, 700.f}, {4585.f, 321.f, 1568.f, 100.f, 200.f, 1500.f}}},
        {7, 8, 0x0, 0x00000100u, "gft1,gft2", {{5432.f, 679.f, 6367.f, 100.f, 200.f, 100.f}, {5277.f, 679.f, 7850.f, 380.f, 200.f, 380.f}}},
        {7, 9, 0x0, 0x00000200u, "gft3,gf31,gf32,gf33", {{5612.f, 969.f, 8611.f, 400.f, 200.f, 400.f}, {6410.f, 1120.f, 9277.f, 400.f, 200.f, 20.f}}},
        {7, 13, 0x0, 0x00002000u, "s131,s132,s133,s141,s142,s143,s20a", {{10101.f, 448.f, 15151.f, 200.f, 200.f, 300.f}}},
        {7, 14, 0x8, 0x00000020u, "top3", {{5369.f, 2267.f, 6444.f, 300.f, 500.f, 100.f}}},
        {7, 15, 0x8, 0x00000040u, "s15a,s15b", {{4548.f, 2525.f, 4995.f, 100.f, 300.f, 100.f}}},
        {8, 33, 0x4, 0x00020000u, "s20a", {{8000.f, 661.f, 9709.f, 500.f, 200.f, 1000.f}, {8995.f, 754.f, 9420.f, 500.f, 200.f, 1000.f}}},
    };
    // Nightclub (level script vtable 0x8209D57C), from wakemap.py club
    constexpr Group kNightclub[] = {
        {7, 1, 0x4, 0x00000010u, "rin2,r2bu,s04a,s05a,s05b,s05d,s05c", {{440.f, 250.f, 1751.f, 250.f, 200.f, 300.f}, {617.f, 242.f, 226.f, 150.f, 200.f, 700.f}, {4759.f, 245.f, 1771.f, 200.f, 200.f, 130.f}}},
        {7, 2, 0x4, 0x00000020u, "s06a,s03a", {{2590.f, -200.f, -438.f, 150.f, 400.f, 250.f}, {4564.f, 245.f, 1784.f, 150.f, 200.f, 100.f}, {617.f, 242.f, 226.f, 150.f, 200.f, 700.f}}},
        {7, 3, 0x4, 0x00000040u, "rin3", {{4434.f, 245.f, -877.f, 500.f, 200.f, 500.f}, {1523.f, 1658.f, -1085.f, 235.f, 100.f, 250.f}}},
        {7, 6, 0x4, 0x00000200u, "s09a,s09b,s09c,s09d,s09e", {{2229.f, 400.f, -2868.f, 50.f, 400.f, 700.f}}},
        {7, 8, 0x4, 0x00000800u, "s11a,s11b,s11c,s11d,s11e,s22a,s22b,s22c,s22d", {{3121.f, 1257.f, -3686.f, 350.f, 200.f, 200.f}, {3124.f, 1203.f, -1983.f, 350.f, 200.f, 200.f}}},
        {7, 9, 0x4, 0x00001000u, "s12a,s12b,s12c,s12d,s21a,s21b,s21c,s21d", {{-161.f, 1658.f, -2783.f, 150.f, 300.f, 100.f}, {-50.f, 2073.f, -4050.f, 820.f, 300.f, 200.f}, {-469.f, 1608.f, -3929.f, 120.f, 300.f, 150.f}}},
        {7, 10, 0x4, 0x00002000u, "s16a,s16b,s16c", {{-2762.f, 2073.f, -2856.f, 120.f, 100.f, 230.f}}},
        {7, 20, 0x20, 0x00100000u, "s08c", {{1702.f, -194.f, -313.f, 0.f, 0.f, 0.f}}},
    };
    // Trinity Infiltration (level script vtable 0x8209EB00), from wakemap.py cs1
    constexpr Group kTrinity1[] = {
        {7, 5, 0x0, 0x00001000u, "s05a,s05b,s05c,s05d,s06a,s06b,s06c,s06d,s06e,s07z,s07a,s07b,s07c,s07d,s07e,s07f,s07g,s07h,s07i,s07j,s07k,s07l,s07m,s07n,s08a,s08b,s14a,s14b,s14g,s14h,s14i,s14j,s14k,s14l,s15a,s15b,drca", {{-8293.f, -4401.f, 7345.f, 2000.f, 300.f, 2000.f}}},
        {7, 9, 0x0, 0x00008000u, "s09a,s09b,s09c,s09d,s09e,s09f,s09g,s09h", {{9131.f, -4726.f, 6453.f, 100.f, 300.f, 600.f}}},
        {7, 10, 0x0, 0x00002000u, "s10a,s10b,s10c,s10d,s10e,s10f,s10g,s10h", {{-1113.f, -6685.f, -10911.f, 100.f, 400.f, 300.f}}},
        {7, 11, 0x0, 0x00004000u, "s11a,s11b,s11c,s11d,s11e,s11f,s11g", {{-9813.f, -4672.f, 5226.f, 400.f, 300.f, 100.f}}},
        {7, 12, 0x4, 0x00010000u, "s14d,s14e,s14f,s14c", {{-8403.f, -4696.f, -945.f, 500.f, 300.f, 300.f}, {-10652.f, -5900.f, 3723.f, 500.f, 300.f, 300.f}}},
    };
    // Trinity Escape (level script vtable 0x8209F8D8), from wakemap.py cs2
    constexpr Group kTrinity2[] = {
        {7, 1, 0x0, 0x00800000u, "s01a,s01b,s01c", {{-3502.f, -6554.f, -2161.f, 600.f, 300.f, 200.f}}},
        {7, 3, 0x0, 0x01000000u, "s03a,s03b,s03c,s03d,s03e,s03f,s04a,s04b,s04c,s04d,s04e,s04f", {{68.f, -6445.f, -3499.f, 500.f, 100.f, 100.f}}},
        {7, 7, 0x4, 0x00000800u, "s07a,s07b,s07c,s07d,s07e,s07f,s07g,s07h,s05a,s05b,s05c,s05d", {{-2748.f, -5948.f, -10192.f, 100.f, 300.f, 300.f}, {-2777.f, -4727.f, -10182.f, 100.f, 300.f, 300.f}}},
        {7, 9, 0x0, 0x04000000u, "s09a,s09b,s09c,s09d,s09e,s09f,s09g,s09h,s09i", {{-8249.f, -4726.f, 4869.f, 50.f, 300.f, 400.f}}},
        {7, 10, 0x0, 0x08000000u, "s10a,s10b,s10c,s10d,s10e,s10f", {{-2633.f, -4696.f, 1631.f, 200.f, 200.f, 400.f}}},
        {7, 11, 0x0, 0x40000000u, "s08a,s08b,s08c", {{-5029.f, -5460.f, 3008.f, 200.f, 300.f, 400.f}}},
        {7, 12, 0x0, 0x10000000u, "s12a,s12b,s12c,s12d,s12e,s12f,s12g,s12h,s12i,s12j,s11a,s11b,s11c,s11d", {{7577.f, -4772.f, 7677.f, 100.f, 300.f, 300.f}}},
        {7, 14, 0x4, 0x00000004u, "s14a,s14b,s14c,s14d", {{2165.f, -4726.f, -10765.f, 500.f, 200.f, 500.f}}},
        {7, 17, 0x4, 0x80000000u, "s17a,s17b,s17c,s17d,s17e,s17f,s17g,s17h,s17i,s17j,s31a,s31b,s31c,s21a,s21b,s21c", {{-8246.f, 952.f, 7287.f, 100.f, 300.f, 300.f}}},
    };
    // Round 5 (item 1): AREAS. Area 0 = the opening (before any wake trigger); area i = one wake message of the
    // table above (entered when the level sends it). A guard's area: his wake group; else the reviewed id lists
    // below (the originals Rare leaves awake at the start, or wakes on a cue); else his squad number (setup +60,
    // Rare's getter 8252DE10 = guard vtable +0x34C: the designers' section number; it also covers template clones).
    // Made by the round 5 helper's areas.py from the level data (the job folder, d/areas_<level>.txt).
    struct Area
    {
      const char *name;   // from Rare's setup folder names
      uint8_t type, arg;  // the entry message (0, 0 = area 0, no trigger)
      const char *squads; // comma list of setup +60 values
      const char *awake;  // eligible ids awake at the start
      const char *cue;    // eligible ids asleep at the start and in no wake group (the script wakes them on a cue)
    };
    // Subway
    constexpr Area kSubwayAreas[] = {
        {"garage (bikers)", 0, 0, "1", "bik3,bik4,bik2,bik5,bik1", ""},
        {"warehouse front (lorry / factory front)", 7, 1, "2,3,17", "war1,fac3", ""},
        {"armoury", 7, 2, "4,15", "", ""},
        {"safe/subwayentrancepatrol/phonebooth", 7, 3, "5,6,7,18", "", ""},
        {"subway/zhangssubway", 7, 4, "8,9", "", ""},
        {"outertrain/innertrain/secondinnertrain", 7, 5, "10,11,12,16", "", ""},
    };
    // Rooftops
    constexpr Area kRooftopsAreas[] = {
        {"opening", 0, 0, "30", "", ""},
        {"middlefloorfront/topfloorfront", 7, 1, "1,5,6", "bff2,bff1,bff4", ""},
        {"sniper", 7, 2, "2", "", ""},
        {"topfloor/bridgeroom", 7, 3, "10,17,18", "", "17-3,17-2"},
        {"bottomfloormiddle", 7, 5, "3", "", ""},
        {"greengauzefloortwo", 7, 8, "8", "", ""},
        {"greengauzefloortwo/greengauzefloorthree", 7, 9, "4,7,9,11", "", "s112,s111,s113,bfb4,ggo1,ggo2,ggo3,bfb3"},
        {"roofend/doubleglazingsalesman", 7, 13, "12,13,14,19", "", "s123,s122,s121,s194,s193,s192,s191"},
        {"topfloor", 7, 14, "", "", ""},
        {"chinesebalcony", 7, 15, "15", "", ""},
        {"doubleglazingsalesman", 8, 33, "", "", ""},
    };
    // Nightclub
    constexpr Area kNightclubAreas[] = {
        {"gardenpatrol/frontpierpatrol", 0, 0, "1", "s19b,s19a,s01a", ""},
        {"ringleader/warehousepatrol/warehouse", 7, 1, "3,4,5", "s03b", ""},
        {"alleypatrol/sidepier", 7, 2, "6,7", "", ""},
        {"ringleader", 7, 3, "", "", ""},
        {"restaurant", 7, 6, "2,18", "", "s10c"},
        {"middlefloorfront/middlefloorfrontpatrol/firstdancesecondsquad", 7, 8, "10,11,20,22", "", "s20a,s20b,s20c,s20d"},
        {"middlefloorrear/middlefloorrearpatrol/seconddancesecondwave", 7, 9, "12,21", "", ""},
        {"topfloorfront", 7, 10, "16", "", ""},
        {"kitchen", 7, 20, "8", "", "s08b,s08a"},
    };
    // Trinity Infiltration
    constexpr Area kTrinity1Areas[] = {
        {"observatoryguard", 0, 0, "3", "", "s03e,s03d,s03b,s03c,s03a"},
        {"hangar/rollcallcaptain/rollcallguard", 7, 5, "1,2,6,7,8,14,15", "", "s01d,s01c,s18a,s18b,s18c,s17c,s16c,s17a,s17b,s16a,s16b"},
        {"donutscientist", 7, 9, "9,35,36", "", ""},
        {"archaeologyscientist", 7, 10, "10,33,34", "", ""},
        {"rnrscientist", 7, 11, "11,31,32", "", ""},
        {"patroller", 7, 12, "5", "", ""},
    };
    // Trinity Escape
    constexpr Area kTrinity2Areas[] = {
        {"opening", 0, 0, "", "", ""},
        {"startguard", 7, 1, "1,2", "", "s01d,s01e,s01f"},
        {"archaeologyguard", 7, 3, "3,4", "", ""},
        {"hangaraguard/door", 7, 7, "5,6,7", "", "s06h,s06b,s06c,s05h,s06a,s06f,s06g,s06d,s06e,s05f,s05g,s05e"},
        {"rnrguard", 7, 9, "9", "", ""},
        {"donutlab", 7, 10, "10", "", ""},
        {"door", 7, 11, "8,15,16", "", "s08e,s08f,s08d"},
        {"hangarbguard/door", 7, 12, "11,12,13", "", "s11f,s11e,s13a"},
        {"floodconsoleguard", 7, 14, "14", "", ""},
        {"aboveseaguard/aboveseacisquad/jetpackguard", 7, 17, "17,18,30", "", "s18a,s18b,s18d,s18c,s18e"},
    };
    struct Level
    {
      uint32_t script_vtable;
      const char *name;
      const Group *groups;
      size_t count;
      const Area *areas;
      size_t area_count;
    };
    constexpr Level kLevels[] = {
        {0x820A6030, "Subway", kSubway, std::size(kSubway), kSubwayAreas, std::size(kSubwayAreas)},
        {0x820A4B08, "Rooftops", kRooftops, std::size(kRooftops), kRooftopsAreas, std::size(kRooftopsAreas)},
        {0x8209D57C, "Nightclub", kNightclub, std::size(kNightclub), kNightclubAreas, std::size(kNightclubAreas)},
        {0x8209EB00, "Trinity Infiltration", kTrinity1, std::size(kTrinity1), kTrinity1Areas, std::size(kTrinity1Areas)},
        {0x8209F8D8, "Trinity Escape", kTrinity2, std::size(kTrinity2), kTrinity2Areas, std::size(kTrinity2Areas)},
    };

    constexpr uint32_t kGame = 0x82D6BFD8, kLevelOwner = 0x1C, kLevelTarget = 0x2D8, kLevelTargetScript = 0x74;
    constexpr uint32_t kScriptFlags = 0x3EC; // -> the script's flag words (Subway 82366D70: lwz r11,0x3ec(r31))
    constexpr uint32_t kCharList = 0x82E454A0, kGuardVtable = 0x820C45A0;
    constexpr uint32_t kPosition = 48, kSetup = 84, kHealth = 120, kHidden = 6272, kSetupId = 52;
    constexpr uint32_t kWakePerFrame = 4;

    struct Post
    {
      float x, y, z; // setup +4
      int area;      // by his wake group or the area table's id lists (never by squad number)
    };

    struct State
    {
      const Level *level = nullptr;
      uint32_t script = 0;
      uint64_t fired = 0;          // bit i: group i's message was sent (or its once bit is set)
      int current_area = 0;        // round 5: the area of the last wake message the level sent (0 = the opening)
      std::vector<uint32_t> queue; // guards still to wake
      std::vector<Post> posts;     // round 16: the table guards' posts (built once a level, Posts())
      std::unordered_set<uint32_t> woke;      // round 16: guards Step woke (WokeIt: the guards tracker's log)
      std::unordered_set<uint64_t> logged;    // round 16: (guard, rule) already logged this level
    } g;

    // Round 16: one line the first time a round-16 rule acts on a guard (per rule), at most 60 a level.
    void Round16Log(uint32_t guard, uint32_t rule, const std::string &text)
    {
      if (g.logged.size() >= 60 || !g.logged.insert((uint64_t(rule) << 32) | guard).second)
      {
        return;
      }
      REXLOG_INFO("pdz_counterop: {}", text);
    }

    uint32_t Load32(const uint8_t *base, uint32_t a)
    {
      const uint8_t *p = base + a;
      return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
    }
    float LoadFloat(const uint8_t *base, uint32_t a)
    {
      const uint32_t bits = Load32(base, a);
      float f;
      std::memcpy(&f, &bits, 4);
      return f;
    }
    bool Ptr(uint32_t a) { return a >= 0x10000u && a < 0xF0000000u; }

    // The level script: [[[0x82D6BFD8 + 0x1C] + 0x2D8] + 0x74] (as counter_op_guards.cpp's DatacoreScript).
    uint32_t Script(const uint8_t *base)
    {
      const uint32_t owner = Load32(base, kGame + kLevelOwner);
      const uint32_t target = pdz::guestmem::Ok(base, owner, kLevelTarget + 4) ? Load32(base, owner + kLevelTarget) : 0;
      const uint32_t script = pdz::guestmem::Ok(base, target, kLevelTargetScript + 4) ? Load32(base, target + kLevelTargetScript) : 0;
      return Ptr(script) ? script : 0;
    }

    const Level *FindLevel(const uint8_t *base)
    {
      const uint32_t script = Script(base);
      if (script != g.script)
      {
        g = State{};
        g.script = script;
        const uint32_t vt = script ? Load32(base, script) : 0;
        for (const Level &l : kLevels)
        {
          if (l.script_vtable == vt)
          {
            g.level = &l;
          }
        }
      }
      return g.level;
    }

    // The guard's 4-character setup id, or "".
    std::string Id(const uint8_t *base, uint32_t c)
    {
      // (Sat 21 Nov evening: every step tested as mapped memory)
      const uint32_t setup = pdz::guestmem::Ok(base, c, kSetup + 4) ? Load32(base, c + kSetup) : 0;
      const uint32_t p = pdz::guestmem::Ok(base, setup, kSetupId + 4) ? Load32(base, setup + kSetupId) : 0;
      std::string s;
      for (int i = 0; pdz::guestmem::Ok(base, p, 4) && i < 4 && base[p + i] >= 32 && base[p + i] < 127; ++i)
      {
        s += char(base[p + i]);
      }
      return s;
    }
    bool InList(const char *list, const std::string &id)
    {
      if (id.empty())
      {
        return false;
      }
      const std::string s = std::string(",") + list + ",";
      return s.find("," + id + ",") != std::string::npos;
    }
    int GroupOf(const Level &l, const std::string &id)
    {
      for (size_t i = 0; i < l.count; ++i)
      {
        if (InList(l.groups[i].ids, id))
        {
          return int(i); // a guard in two groups (Rooftops top3, s20a) counts for the first
        }
      }
      return -1;
    }

    bool Fired(const uint8_t *base, const Level &l, size_t i)
    {
      if (g.fired & (1ull << i))
      {
        return true;
      }
      const Group &gr = l.groups[i];
      const uint32_t flags = gr.once_bit ? Load32(base, g.script + kScriptFlags) : 0;
      if (Ptr(flags) && (Load32(base, flags + gr.once_word) & gr.once_bit))
      {
        g.fired |= 1ull << i;
        return true;
      }
      return false;
    }

    // Distance (cm) from a point to the nearest of the group's volumes (axis-aligned box, size taken as full size).
    float VolumeDistance(const Group &gr, const float p[3])
    {
      float best = 1e30f;
      for (const Volume &v : gr.volumes)
      {
        if (v.x == 0.0f && v.y == 0.0f && v.z == 0.0f)
        {
          continue;
        }
        const float dx = std::max(0.0f, std::fabs(p[0] - v.x) - v.sx * 0.5f);
        const float dy = std::max(0.0f, std::fabs(p[1] - v.y) - v.sy * 0.5f);
        const float dz = std::max(0.0f, std::fabs(p[2] - v.z) - v.sz * 0.5f);
        best = std::min(best, std::sqrt(dx * dx + dy * dy + dz * dz));
      }
      return best;
    }

    // Rare's character list (every guard the level built, dormant ones included).
    template <typename F> void EachGuard(const uint8_t *base, F &&f)
    {
      uint32_t node = Load32(base, kCharList + 0x710);
      const uint32_t end = Load32(base, kCharList + 0x714);
      for (int i = 0; Ptr(node) && node != end && i < 2048; ++i)
      {
        const uint32_t c = Load32(base, node);
        node = Load32(base, node + 8);
        if (Ptr(c) && Load32(base, c) == kGuardVtable)
        {
          f(c);
        }
      }
    }
    constexpr uint32_t kSetupSquad = 60;   // setup +0x3C: the squad number
    constexpr uint32_t kSetupPosition = 4; // setup +4: the post

    uint32_t SquadOf(const uint8_t *base, uint32_t guard)
    {
      const uint32_t setup = Load32(base, guard + kSetup);
      return Ptr(setup) ? Load32(base, setup + kSetupSquad) : 0;
    }

    // The area a wake group belongs to (the Area row with the same message); -1 = none.
    int AreaOfGroup(const Level &l, size_t group)
    {
      for (size_t a = 1; a < l.area_count; ++a)
      {
        if (l.areas[a].type == l.groups[group].type && l.areas[a].arg == l.groups[group].arg)
        {
          return int(a);
        }
      }
      return -1;
    }

    // The area of a guard and which rule gave it; -1 = not in the table.
    int AreaOfGuardIn(const uint8_t *base, const Level &l, uint32_t guard, std::string *how)
    {
      const std::string id = Id(base, guard);
      const int gi = GroupOf(l, id);
      if (gi >= 0)
      {
        if (how)
        {
          *how = "its wake group";
        }
        return AreaOfGroup(l, size_t(gi));
      }
      for (size_t a = 0; a < l.area_count; ++a)
      {
        if (InList(l.areas[a].awake, id) || InList(l.areas[a].cue, id))
        {
          if (how)
          {
            *how = "the level table";
          }
          return int(a);
        }
      }
      const uint32_t squad = SquadOf(base, guard);
      for (size_t a = 0; squad && a < l.area_count; ++a)
      {
        if (InList(l.areas[a].squads, std::to_string(squad)))
        {
          if (how)
          {
            *how = fmt::format("its squad number {}", squad);
          }
          return int(a);
        }
      }
      return -1;
    }

    // Round 16: the posts (setup +4) of every guard the area table places by wake group or id list, built the first
    // time they are asked for in a level (every guard is built at the level load; dormant ones are in Rare's list).
    const std::vector<Post> &Posts(const uint8_t *base, const Level &l)
    {
      if (!g.posts.empty())
      {
        return g.posts;
      }
      EachGuard(base, [&](uint32_t c) {
        const std::string id = Id(base, c);
        const int gi = GroupOf(l, id);
        int area = gi >= 0 ? AreaOfGroup(l, size_t(gi)) : -1;
        for (size_t a = 0; area < 0 && a < l.area_count; ++a)
        {
          if (InList(l.areas[a].awake, id) || InList(l.areas[a].cue, id))
          {
            area = int(a);
          }
        }
        const uint32_t setup = Load32(base, c + kSetup);
        if (area >= 0 && Ptr(setup))
        {
          g.posts.push_back({LoadFloat(base, setup + kSetupPosition), LoadFloat(base, setup + kSetupPosition + 4),
                             LoadFloat(base, setup + kSetupPosition + 8), area});
        }
      });
      return g.posts;
    }

    // Round 16: the area of the post nearest to p (cm), -1 when none is within pdz_counterop_area_post_m; post_m =
    // the distance to that nearest post (m).
    int PositionArea(const uint8_t *base, const Level &l, const float p[3], float *post_m)
    {
      float best = 1e30f;
      int area = -1;
      for (const Post &q : Posts(base, l))
      {
        const float dx = p[0] - q.x, dy = p[1] - q.y, dz = p[2] - q.z;
        const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (d < best)
        {
          best = d;
          area = q.area;
        }
      }
      *post_m = best / 100.0f;
      return best <= float(REXCVAR_GET(pdz_counterop_area_post_m)) * 100.0f ? area : -1;
    }

    // Round 16: the distance (m) from p to the nearest wake trigger of this area that has not fired (1e28 = none).
    float EntryDistance(const uint8_t *base, const Level &l, int area, const float p[3])
    {
      float best = 1e30f;
      for (size_t i = 0; i < l.count; ++i)
      {
        if (AreaOfGroup(l, i) == area && !Fired(base, l, i))
        {
          best = std::min(best, VolumeDistance(l.groups[i], p));
        }
      }
      return best / 100.0f;
    }

    // Round 16: pdz_counterop_scene_hold_max_s has run out for a sleeping guard of a wake group (never for one the
    // script wakes on a cue).
    bool SceneHoldLifted(const Level *l, const std::string &id)
    {
      const double max_s = REXCVAR_GET(pdz_counterop_scene_hold_max_s);
      if (!(max_s > 0.0) || PdzCounterOpSceneHoldNoTimeout()) // Sat 21 Nov: never lifted by time
      {
        return false;
      }
      const bool grouped = l && GroupOf(*l, id) >= 0;
      return counter_op_guards::PickWait() >= (grouped ? max_s : 2.0 * max_s);
    }
    bool SceneHolds(const Level *l, const std::string &id, bool asleep)
    {
      return asleep && REXCVAR_GET(pdz_counterop_scene_hold) && !SceneHoldLifted(l, id);
    }
  } // namespace

  void Reset() { g = State{}; }
  bool WokeIt(uint32_t guard) { return g.woke.count(guard) != 0; }
  bool HasAreaTable(const uint8_t *base)
  {
    const Level *l = FindLevel(base);
    return l && l->areas && REXCVAR_GET(pdz_counterop_area_rule);
  }

  // Sun 22 Nov (item 9): the area at a point (by the nearest table guard's post) and an area's name.
  int AreaAtPosition(const uint8_t *base, const float p[3], float *post_m)
  {
    const Level *l = FindLevel(base);
    *post_m = -1.0f;
    if (!l || !l->areas || !REXCVAR_GET(pdz_counterop_area_rule))
    {
      return -1;
    }
    return PositionArea(base, *l, p, post_m);
  }
  const char *AreaName(const uint8_t *base, int area)
  {
    const Level *l = FindLevel(base);
    return l && l->areas && area >= 0 && size_t(area) < l->area_count ? l->areas[area].name : "";
  }

  // W4: every message the level manager passes on to the script (after the player-2 drop, so only real ones).
  void OnLevelMessage(const uint8_t *base, uint8_t type, uint8_t arg)
  {
    const Level *l = FindLevel(base);
    for (size_t i = 0; l && i < l->count; ++i)
    {
      if (l->groups[i].type == type && l->groups[i].arg == arg && !(g.fired & (1ull << i)))
      {
        g.fired |= 1ull << i;
        REXLOG_INFO("pdz_counterop: wake: the level woke group {} of {} (message {}/{}: {})", i, l->name, type, arg,
                    l->groups[i].ids);
        const int a = l->areas ? AreaOfGroup(*l, i) : -1;
        if (a >= 0)
        {
          REXLOG_INFO("pdz_counterop: area: the agent entered area {} '{}' of {} (message {}/{}); she was in area {} "
                      "'{}'",
                      a, l->areas[a].name, l->name, type, arg, g.current_area, l->areas[g.current_area].name);
          g.current_area = a;
        }
      }
    }
  }

  // The group the agent is heading into next: the nearest not-yet-fired group within the cvar's range; -1 = none
  // (also on a level with no table: there a dormant guard is never taken).
  int NextGroup(const uint8_t *base, uint32_t agent, float *distance_m)
  {
    const Level *l = FindLevel(base);
    if (!l || !Ptr(agent))
    {
      return -1;
    }
    const float p[3] = {LoadFloat(base, agent + kPosition), LoadFloat(base, agent + kPosition + 4),
                        LoadFloat(base, agent + kPosition + 8)};
    int best = -1;
    float best_d = 1e30f; // the nearest unfired group, however far (InNextArea applies the ranges)
    for (size_t i = 0; i < l->count; ++i)
    {
      if (Fired(base, *l, i))
      {
        continue;
      }
      const float d = VolumeDistance(l->groups[i], p);
      if (d <= best_d)
      {
        best_d = d;
        best = int(i);
      }
    }
    if (distance_m)
    {
      *distance_m = best_d / 100.0f;
    }
    return best;
  }

  // W1: may this dormant guard be taken now?
  bool InNextArea(const uint8_t *base, uint32_t guard, uint32_t agent)
  {
    const Level *l = FindLevel(base);
    if (!l)
    {
      return false;
    }
    float trigger_m = 0.0f;
    const int next = NextGroup(base, agent, &trigger_m);
    if (next < 0 || !InList(l->groups[next].ids, Id(base, guard)))
    {
      return false;
    }
    // Her next group: near its trigger, or the guard himself near her.
    const float dx = LoadFloat(base, guard + kPosition) - LoadFloat(base, agent + kPosition);
    const float dy = LoadFloat(base, guard + kPosition + 4) - LoadFloat(base, agent + kPosition + 4);
    const float dz = LoadFloat(base, guard + kPosition + 8) - LoadFloat(base, agent + kPosition + 8);
    const float guard_m = std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f;
    return trigger_m <= float(REXCVAR_GET(pdz_counterop_next_area_m)) ||
           guard_m <= float(REXCVAR_GET(pdz_counterop_next_area_guard_m));
  }

  // W2: player 2 took this dormant guard: queue the wake of the others of his group.
  void WakeGroupOf(const uint8_t *base, uint32_t guard)
  {
    const Level *l = FindLevel(base);
    if (!l || !REXCVAR_GET(pdz_counterop_wake_group))
    {
      return;
    }
    const int gi = GroupOf(*l, Id(base, guard));
    if (gi < 0)
    {
      REXLOG_INFO("pdz_counterop: wake: taken guard {:08X} ('{}') is in no wake group of {}: nobody woken", guard,
                  Id(base, guard), l->name);
      return;
    }
    // Round 16 (item 6b): the group's own trigger has not fired: its guards wake when the level wakes them.
    if (REXCVAR_GET(pdz_counterop_scene_no_early_wake) && !Fired(base, *l, size_t(gi)))
    {
      REXLOG_INFO("pdz_counterop: wake: taken guard '{}' is of group {} of {}: its sleeping guards are left to the "
                  "level's own trigger (it has not fired; their scripted scene starts then: "
                  "pdz_counterop_scene_no_early_wake)",
                  Id(base, guard), gi, l->name);
      return;
    }
    g.queue.clear();
    std::string ids;
    EachGuard(base, [&](uint32_t c) {
      if (c == guard || !(Load32(base, c + kHidden) & 0x10) || LoadFloat(base, c + kHealth) <= 0.0f)
      {
        return; // the taken one stays hidden; awake or dead ones are left alone
      }
      const std::string id = Id(base, c);
      if (InList(l->groups[gi].ids, id))
      {
        g.queue.push_back(c);
        ids += (ids.empty() ? "" : ",") + id;
      }
    });
    REXLOG_INFO("pdz_counterop: wake: taken guard '{}' is of group {} of {} (message {}/{}): waking {} sleeping "
                "guard(s) of it ({}), {} a frame; the level's own trigger stays armed",
                Id(base, guard), gi, l->name, l->groups[gi].type, l->groups[gi].arg, g.queue.size(),
                ids.empty() ? "-" : ids, kWakePerFrame);
  }

  // Round 5 (item 1). The agent's current area (the last wake message sent; 0 = the opening) and her next one (round
  // 6: the next area in the level's order with an unfired trigger); next = -1 when there is none.
  void AreasOfJo(const uint8_t *base, uint32_t agent, int *current, int *next, float *trigger_m)
  {
    const Level *l = FindLevel(base);
    *current = l && l->areas ? g.current_area : -1;
    *next = -1;
    *trigger_m = 0.0f;
    if (l && l->areas && REXCVAR_GET(pdz_counterop_area_next_in_order) && Ptr(agent))
    {
      // Round 6 (item 2): her next area is the one AFTER her current one in the level's order (the first later area
      // with a wake trigger that has not fired), not the area of whichever unfired trigger is nearest: crossing the
      // Subway garage the armoury's trigger (area 2) comes nearer than the warehouse front's (area 1), and round 5
      // then took and woke the armoury while she was still in the garage.
      const float p[3] = {LoadFloat(base, agent + kPosition), LoadFloat(base, agent + kPosition + 4),
                          LoadFloat(base, agent + kPosition + 8)};
      for (size_t a = size_t(g.current_area) + 1; a < l->area_count && *next < 0; ++a)
      {
        float best = 1e30f;
        for (size_t i = 0; i < l->count; ++i)
        {
          if (AreaOfGroup(*l, i) == int(a) && !Fired(base, *l, i))
          {
            best = std::min(best, VolumeDistance(l->groups[i], p));
          }
        }
        if (best < 1e29f)
        {
          *next = int(a);
          *trigger_m = best / 100.0f;
        }
      }
      return;
    }
    float d = 0.0f;
    const int group = NextGroup(base, agent, &d);
    *next = l && l->areas && group >= 0 ? AreaOfGroup(*l, size_t(group)) : -1;
    *trigger_m = d;
  }

  std::string AreaLine(const uint8_t *base, uint32_t agent)
  {
    const Level *l = FindLevel(base);
    if (!l || !l->areas)
    {
      return "no area table for this level";
    }
    int cur = 0, next = -1;
    float trigger_m = 0.0f;
    AreasOfJo(base, agent, &cur, &next, &trigger_m);
    const bool reach = next >= 0 && trigger_m <= float(REXCVAR_GET(pdz_counterop_next_area_m));
    return next >= 0 ? fmt::format("Jo in area {} '{}' (next: {} '{}', its trigger {:.0f} m away: {})", cur,
                                   l->areas[cur].name, next, l->areas[next].name, trigger_m,
                                   reach ? "in reach" : "not in reach by the trigger")
                     : fmt::format("Jo in area {} '{}' (no area ahead)", cur, l->areas[cur].name);
  }

  // The round 5 candidate filter. asleep = the guard is dormant. Returns true when he may be taken; note = the log
  // text (always set). On a level with no area table, or with the rule off: round 4's rule.
  bool AreaAllows(const uint8_t *base, uint32_t guard, uint32_t agent, bool asleep, bool next_only, std::string &note)
  {
    if (PdzCounterOpRooftopsDropAreaOk(guard, note)) { return true; } // round 13 e: Rooftops, a landed guard of Killian's ship
    const Level *l = FindLevel(base);
    const std::string id = Id(base, guard);
    // Round 16 (item 6b), every level: a guard the level script still keeps asleep belongs to his group's scripted
    // wake (Subway log 027: 'zsb2' of the platform police, taken 107 s before the level woke them).
    if (SceneHolds(l, id, asleep))
    {
      note = fmt::format("guard '{}': refused (asleep: Rare's level script still holds him; it wakes his group together "
                         "when the agent reaches its trigger and their scripted scene starts then, so he is taken only "
                         "once the level itself has woken him: pdz_counterop_scene_hold)",
                         id);
      Round16Log(guard, 1, "scene hold: " + note);
      return false;
    }
    if (!l || !l->areas || !REXCVAR_GET(pdz_counterop_area_rule))
    {
      const bool ok = !asleep || !next_only || InNextArea(base, guard, agent);
      note = fmt::format("guard '{}': no area table for this level (or the area rule is off): {}", id,
                         ok ? "allowed (round 4's rule)" : "refused (asleep, not of her next wake group)");
      return ok;
    }
    int cur = 0, next = -1;
    float trigger_m = 0.0f;
    AreasOfJo(base, agent, &cur, &next, &trigger_m);
    std::string how;
    const int table_area = AreaOfGuardIn(base, *l, guard, &how);
    int ga = table_area;
    const auto name = [&](int a) { return a >= 0 && size_t(a) < l->area_count ? l->areas[a].name : "none"; };
    const float gx = LoadFloat(base, guard + kPosition), gy = LoadFloat(base, guard + kPosition + 4),
                gz = LoadFloat(base, guard + kPosition + 8);
    const float gp[3] = {gx, gy, gz};
    // Round 16 (item 6a): an awake guard is of the area he stands in (the nearest post of the table near him).
    if (!asleep && REXCVAR_GET(pdz_counterop_area_by_position))
    {
      float post_m = 0.0f;
      const int pa = PositionArea(base, *l, gp, &post_m);
      if (pa >= 0 && pa != table_area)
      {
        Round16Log(guard, 2,
                   fmt::format("area by position: guard '{}' at ({:.0f}, {:.0f}, {:.0f}) stands {:.1f} m from a post of "
                               "area {} '{}': counted as of that area ({} gives area {} '{}'; "
                               "pdz_counterop_area_by_position)",
                               id, gx, gy, gz, post_m, pa, name(pa), table_area >= 0 ? how : "nothing", table_area,
                               name(table_area)));
        how = fmt::format("where he stands ({:.0f} m from a post of it; {} gives area {})", post_m,
                          table_area >= 0 ? how : "nothing", table_area);
        ga = pa;
      }
    }
    const float dx = gx - LoadFloat(base, agent + kPosition), dy = gy - LoadFloat(base, agent + kPosition + 4),
                dz = gz - LoadFloat(base, agent + kPosition + 8);
    const float guard_m = std::sqrt(dx * dx + dy * dy + dz * dz) / 100.0f;
    const bool reach_round5 = next >= 0 && (trigger_m <= float(REXCVAR_GET(pdz_counterop_next_area_m)) ||
                                            guard_m <= float(REXCVAR_GET(pdz_counterop_next_area_guard_m)));
    // Round 16 (item 6a): her next area is in reach by where HE stands: near her, or near that area's own entry
    // trigger while she is near it (an area can be 100 m deep: Subway's area 1 runs from the lorry yard to the hangar).
    bool reach = reach_round5;
    float entry_m = 0.0f;
    if (next >= 0 && REXCVAR_GET(pdz_counterop_area_reach_by_position))
    {
      entry_m = EntryDistance(base, *l, next, gp);
      reach = guard_m <= float(REXCVAR_GET(pdz_counterop_next_area_guard_m)) ||
              (trigger_m <= float(REXCVAR_GET(pdz_counterop_next_area_m)) &&
               entry_m <= float(REXCVAR_GET(pdz_counterop_area_entry_m)));
    }
    const std::string head =
        next >= 0 ? fmt::format("area rule: Jo in area {} '{}' (next: {} '{}', its trigger {:.0f} m away); guard '{}' "
                                "is of area {} '{}' by {}, {}, {:.0f} m from her: ",
                                cur, name(cur), next, name(next), trigger_m, id, ga, name(ga),
                                ga >= 0 ? how : "nothing", asleep ? "asleep" : "awake", guard_m)
                  : fmt::format("area rule: Jo in area {} '{}' (no area ahead); guard '{}' is of area {} '{}' by {}, "
                                "{}, {:.0f} m from her: ",
                                cur, name(cur), id, ga, name(ga), ga >= 0 ? how : "nothing",
                                asleep ? "asleep" : "awake", guard_m);
    // A sleeping guard the level wakes on a cue (in no wake group) is never taken asleep: waking him is the script's.
    if (asleep && GroupOf(*l, id) < 0)
    {
      note = head + "refused (asleep and woken by the script on a cue, in no wake group)";
      return false;
    }
    if (ga < 0)
    {
      note = head + (asleep ? "refused (asleep, in no area)" : "allowed (in no area of the table: an awake guard)");
      return !asleep;
    }
    if (ga == cur)
    {
      note = head + "allowed (her current area)";
      return true;
    }
    if (ga == next)
    {
      if (reach_round5 && !reach)
      {
        note = head + fmt::format("refused (her next area, but he stands {:.0f} m from her and {:.0f} m from that "
                                  "area's wake trigger: deeper in it than she can reach yet; "
                                  "pdz_counterop_area_reach_by_position)",
                                  guard_m, entry_m);
        Round16Log(guard, 3, "reach by position: " + note);
        return false;
      }
      note = head + (reach ? "allowed (her next area, and she is in reach of it)"
                           : "refused (her next area, but she is not in reach of it yet)");
      return reach;
    }
    // The area's own trigger has fired (she has been through it), or area 0 once she left it: behind her.
    bool behind = ga == 0;
    for (size_t i = 0; i < l->count && !behind; ++i)
    {
      behind = AreaOfGroup(*l, i) == ga && Fired(base, *l, i);
    }
    if (behind && REXCVAR_GET(pdz_counterop_area_behind))
    {
      note = head + "allowed (an area she has already been through)";
      return true;
    }
    // An awake guard who has walked to her (a patrol, one who heard the shots): by where he is now.
    const uint32_t setup = Load32(base, guard + kSetup);
    const float came_m = float(REXCVAR_GET(pdz_counterop_area_came_m));
    if (!asleep && came_m > 0.0f && Ptr(setup))
    {
      const float px = gx - LoadFloat(base, setup + kSetupPosition),
                  py = gy - LoadFloat(base, setup + kSetupPosition + 4),
                  pz = gz - LoadFloat(base, setup + kSetupPosition + 8);
      const float moved_m = std::sqrt(px * px + py * py + pz * pz) / 100.0f;
      if (moved_m >= 15.0f && guard_m <= came_m && std::fabs(dy) <= 600.0f)
      {
        note = head + fmt::format("allowed (he has walked {:.0f} m from his post and is with her now)", moved_m);
        return true;
      }
    }
    note = head + (behind ? "refused (an area behind her; pdz_counterop_area_behind is off)"
                          : "refused (an area further ahead: never taken)");
    return false;
  }

  // Round 12 (item 6), for counter_op_guards::Pick's relaxed steps.
  int AreaOfGuard(const uint8_t *base, uint32_t guard, bool asleep, bool *liftable)
  {
    const Level *l = FindLevel(base);
    // Round 16 (item 6b): the relaxed steps never lift the scene hold either.
    const bool held = SceneHolds(l, Id(base, guard), asleep);
    if (!l || !l->areas)
    {
      *liftable = !held; // no table: round 4's rule refused him only for sleeping outside her next wake group
      return -1;
    }
    const int area = AreaOfGuardIn(base, *l, guard, nullptr);
    *liftable = area >= 0 && !held && !(asleep && GroupOf(*l, Id(base, guard)) < 0);
    return area;
  }

  // Round 5: called for EVERY taken guard, awake or asleep: every sleeping guard of every wake group of the taken
  // guard's area is woken (four a frame through Step(), Rare's own 825818E0(guard, 0)), plus the sleeping grouped
  // guards of his own squad (Rooftops squad 8 and Trinity Infiltration squad 14 are split over two groups).
  void WakeAreaOf(const uint8_t *base, uint32_t guard)
  {
    const Level *l = FindLevel(base);
    if (!l || !REXCVAR_GET(pdz_counterop_wake_group))
    {
      return;
    }
    // Round 12 (item 6): a guard taken by a relaxed step (no guard fitted the area rule) is of an area the agent has
    // not reached: nobody is woken with him, so that area still wakes on the level's own trigger when she gets there.
    if (counter_op_guards::LastPickRelaxed() > 0)
    {
      REXLOG_INFO("pdz_counterop: wake: taken guard '{}' was a relaxed pick (step {}): nobody else is woken; his area "
                  "wakes on the level's own trigger",
                  Id(base, guard), counter_op_guards::LastPickRelaxed());
      return;
    }
    if (!l->areas || !REXCVAR_GET(pdz_counterop_area_rule))
    {
      if (Load32(base, guard + kHidden) & 0x10)
      {
        WakeGroupOf(base, guard); // round 4: only a sleeping guard's own group
      }
      return;
    }
    std::string how;
    const int area = AreaOfGuardIn(base, *l, guard, &how);
    const uint32_t squad = SquadOf(base, guard);
    if (area <= 0)
    {
      REXLOG_INFO("pdz_counterop: wake: taken guard '{}' is of area {} ({}): nobody to wake", Id(base, guard), area,
                  area == 0 ? "the opening: nobody sleeps there" : "not in the table");
      return;
    }
    uint64_t wanted = 0; // the groups to wake
    for (size_t i = 0; i < l->count; ++i)
    {
      if (AreaOfGroup(*l, i) == area)
      {
        wanted |= 1ull << i;
      }
    }
    // Round 6 (item 2): a squad mate who sleeps in ANOTHER area (Subway: fac3 / war1 stand at the warehouse front but
    // are of the armoury's squad 4) is woken only when that area is no further than her next area: nobody two
    // areas ahead of her is ever woken.
    int limit = g.current_area;
    for (size_t a = size_t(g.current_area) + 1; a < l->area_count; ++a)
    {
      bool unfired = false;
      for (size_t i = 0; i < l->count; ++i)
      {
        unfired = unfired || (AreaOfGroup(*l, i) == int(a) && !Fired(base, *l, i));
      }
      if (unfired)
      {
        limit = int(a);
        break;
      }
    }
    limit = std::max(limit, area);
    g.queue.clear();
    std::string ids, left, held;
    EachGuard(base, [&](uint32_t c) {
      if (c == guard || !(Load32(base, c + kHidden) & 0x10) || LoadFloat(base, c + kHealth) <= 0.0f)
      {
        return; // the taken one stays hidden; awake or dead ones are left alone
      }
      const std::string id = Id(base, c);
      const int gi = GroupOf(*l, id);
      if (gi < 0)
      {
        return;
      }
      const bool mate = squad && SquadOf(base, c) == squad;
      if (!(wanted & (1ull << gi)) && mate && REXCVAR_GET(pdz_counterop_area_next_in_order) &&
          AreaOfGroup(*l, size_t(gi)) > limit)
      {
        left += (left.empty() ? "" : ",") + id;
        return;
      }
      // Round 16 (item 6b): never ahead of the level's own trigger (their scripted scene starts with it).
      if (((wanted & (1ull << gi)) || mate) && REXCVAR_GET(pdz_counterop_scene_no_early_wake) &&
          !Fired(base, *l, size_t(gi)))
      {
        held += (held.empty() ? "" : ",") + id;
        return;
      }
      if ((wanted & (1ull << gi)) || mate)
      {
        g.queue.push_back(c);
        ids += (ids.empty() ? "" : ",") + id;
      }
    });
    if (!left.empty())
    {
      REXLOG_INFO("pdz_counterop: wake: squad mates of '{}' left asleep ({}): their area is further ahead than the "
                  "agent's next area {}",
                  Id(base, guard), left, limit);
    }
    if (!held.empty())
    {
      REXLOG_INFO("pdz_counterop: wake: sleeping guards of the area of '{}' left to the level ({}): their wake group's own "
                  "trigger has not fired, and their scripted scene starts with it (pdz_counterop_scene_no_early_wake)",
                  Id(base, guard), held);
    }
    REXLOG_INFO("pdz_counterop: wake: taken guard '{}' (squad {}) is of area {} '{}' of {} by {}: waking the area's {} "
                "sleeping guard(s) ({}), {} a frame; the level's own trigger stays armed",
                Id(base, guard), squad, area, l->areas[area].name, l->name, how, g.queue.size(),
                ids.empty() ? (held.empty() ? "none: all awake already" : "none: the others wait for the level")
                            : ids,
                kWakePerFrame);
  }

  // W3: every game-thread tick. taken = the guard player 2 has replaced (never woken here).
  void Step(PPCContext &ctx, uint8_t *base, uint32_t taken)
  {
    for (uint32_t n = 0; n < kWakePerFrame && !g.queue.empty(); ++n)
    {
      const uint32_t c = g.queue.back();
      g.queue.pop_back();
      if (c == taken || !Ptr(c) || Load32(base, c) != kGuardVtable || !(Load32(base, c + kHidden) & 0x10))
      {
        continue;
      }
      rex::CallFrame frame(ctx);
      frame.ctx.r3.u64 = c;
      frame.ctx.r4.u64 = 0;
      sub_825818E0(frame.ctx, base); // reads r3 and r4 only (as counter_op_guards.cpp's ForceStep calls it)
      g.woke.insert(c);              // round 16: the guards tracker logs this wake as ours, not the level script's
    }
  }
} // namespace counter_op_wake
