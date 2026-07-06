// fx_registry.h -- the one list of every FX kind the pedalboard offers.
//
// Registration lives in its own translation unit (not pedalboard.cpp, which
// owns main() and the hardware wiring) so the test binary can link the exact
// same registry the app runs: every kind registered here is automatically
// swept by the DSP invariant tests in tests/.

#pragma once

class FxFactory;

// Register every FX kind with the factory, in "Add FX" picker order.
void registerAllFx(FxFactory& fx);
