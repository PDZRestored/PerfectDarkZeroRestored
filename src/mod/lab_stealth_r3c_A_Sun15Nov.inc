  // Round 3c, Sun 15 Nov night, helper A (fix 1): the layer's per-level memory is cleared at every level start.
  // The level start test (R3cLevelStart) and the clear itself (R3cClearLayerMemory, which calls R3cBReset, R3cCReset
  // and R3cDReset) are in lab_stealth_r3c_A2_Sun15Nov.inc, included at file scope near the end of lab_stealth.cpp,
  // after every record of the layer is defined (the radio calls, the sound followers, the lamp incident and the cue
  // swap are defined after this point). Nothing here.
