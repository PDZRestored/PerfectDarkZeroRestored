// Counter-Operative round 13: the two general rules every level depends on. Built only with PDZ_COUNTEROP.
//   G1 (counter_op_allies.cpp):   the agent's AI friends ignore player 2 completely, and he cannot hurt them.
//   G2 (counter_op_vehicles.cpp): vehicles, and guns mounted on vehicles, are closed to player 2.
// All three functions are cheap and safe to call at any time (they answer false / do nothing when no Counter-Op
// game runs).
#pragma once

#include <cstdint>

// G1. `character` is an AI character on the agent's side (Jack, Jonathan, the CI soldiers, the pilot, Carrington,
// Datacore's friendly troopers ...): never the agent herself, never player 2, never a prop. Decided by Rare's own team
// masks as the friend test 8251EBD0 reads them (character +128 team bits / +130 allies / +132 enemies), asked from
// the character about the agent: the agent's team bits are among his team or allies and not among his enemies.
// The masks are read live, so a trooper the agent turned against herself stops being an ally at once.
// False when no Counter-Op game runs. Does not depend on the G1 cvar (it is a plain question).
bool PdzCounterOpIsAgentAlly(const uint8_t *base, uint32_t character);

// G2: an exception. Player 2 MAY use this vehicle.
// THE KEY: what the gate sees is the first argument of Rare's mount 8266A8E8(controller r3, character r4, seat r5,
// flag r6): the vehicle's CONTROLLER object (the base class 82669280: bike, hovercraft, jetpack, and the turret of a
// vehicle; its +0x0C is the vehicle prop). To make registering easy the gate accepts EITHER of two things, pass
// whichever you have:
//   - the vehicle PROP (vtable 820CA9E0; what Rare's script lookup by id returns, what the Use action 825D2E90 gets
//     as r3), or
//   - its CONTROLLER ([prop + 0x364]; controller + 0x0C = the prop).
// An allowed vehicle is allowed whole: every seat of it and every gun mounted on it. The same key lifts the "he
// cannot damage vehicles" rule for nothing: an allowed vehicle still takes no damage from him.
// The list (32 entries) is CLEARED AT EVERY LEVEL BOUNDARY (counter_op.cpp Reset: level start, level end, restart),
// so register from your level's tick or setup once the Counter-Op game runs (PdzCounterOpRunning()), not earlier;
// registering the same object again is harmless. allow = false takes it off the list again.
void PdzCounterOpVehicleAllow(uint32_t guest_object, bool allow);
bool PdzCounterOpVehicleAllowed(uint32_t guest_object); // exact match on what was registered (prop or controller)

// counter_op.cpp's Reset calls this (round 13 a): the exception list is emptied.
void PdzCounterOpRules13Reset();
