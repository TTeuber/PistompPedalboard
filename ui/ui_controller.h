// ui/ui_controller.h -- the on-device navigation UI for the pedalboard.
//
// Replaces the old read-only pedal_ui status screen with a small page-based
// navigator driven entirely by the hardware encoders/switches (no touch). It runs
// ONLY on the UI/main thread (LVGL isn't thread-safe): the input thread posts
// UiEvents (ui_events.h), the main loop drains them into handle(), and calls
// refresh() each frame to update live values + manage the tuner takeover.
//
// Interaction model (Nav-led scheme):
//   Nav encoder  : turn = move cursor, push = select/enter, hold = quit (main.cpp)
//   Enc1 push    : Back (up one level)
//   Enc1/2/3 turn: edit the 3 shown params on a control page; otherwise Enc3 = master
//
// Pages: Home (Input/FX/Output boxes) -> section lists -> per-pedal control page;
// plus a Menu (bypass/tuner/rigs) and the full-screen Tuner takeover.

#pragma once

#include "../ui_events.h"
#include "../input_vu.h"

#include "lvgl.h"

#include <cstdint>
#include <string>
#include <vector>

class Chain;
class FxFactory;
class Leds;
struct PedalControls;
class Effect;
namespace fx { class Tuner; }
namespace pistomp { class InputLevel; }

class UiController {
public:
  UiController(Chain& chain, PedalControls& ctl, FxFactory& factory,
               fx::Tuner* tuner, std::string ampName, std::string rigDir);

  void begin();                   // build the initial (Home) page
  void handle(const UiEvent& e);  // act on one input event
  void refresh();                 // per-frame: live values + tuner takeover
  void updateLeds(Leds& leds);    // per-frame: footswitch + input-meter LEDs (UI thread)

  // Optional: the analog input-level detectors. When present and available()
  // (hardware) the meter LEDs read it; otherwise they fall back to the digital
  // audio peak in PedalControls::inPeak (simulator).
  void setInputLevel(pistomp::InputLevel* lvl) { inputLevel_ = lvl; }

private:
  enum Page { Home, InputList, FxGrid, FxPicker, AssignPage, OutputList,
              MenuPage, PedalControl, MasterControl };

  enum Action {
    ActNone, ActBack, ActGotoInput, ActGotoFx, ActGotoOutput, ActGotoMenu,
    ActOpenEffect, ActOpenMaster, ActTogglePower, ActToggleBypass,
    ActToggleTuner, ActLoadRig, ActParamBank,
    ActOpenPicker, ActAddFx, ActRemoveFx, ActGotoAssign
  };

  struct FocusItem {
    lv_obj_t* obj = nullptr;   // focusable container (highlight target)
    lv_obj_t* lbl = nullptr;   // inner label, for dynamic text updates
    Action action = ActNone;
    Effect* fx = nullptr;
    int idx = 0;               // rig index / param-bank payload
  };

  // --- navigation ---
  void goTo(Page p, Effect* fx = nullptr);
  void rebuild();              // build widgets for page_
  void moveFocus(int dir);
  void applyFocus();
  void select();
  void back();
  void onKnob(int which, int dir);   // which = 0/1/2 (Enc1/2/3)
  void onFootswitch(int fs);         // FS tap: toggle bound effects, or assign
  void cycleAssign(Effect* fx, int fs);   // unassigned -> normal -> inverted -> off
  void adjustMaster(int dir);
  void stepParam(Effect* fx, int paramIdx, int dir);

  // --- builders ---
  lv_obj_t* newScreen();
  void topBar(lv_obj_t* scr, const char* title);   // adds Back as item 0
  lv_obj_t* addRow(lv_obj_t* scr, int rowIndex, Action a, Effect* fx, int idx);
  void buildHome();
  void buildList(Page p);     // InputList / OutputList
  void buildFxGrid();         // FX middle region: tiles + "+ Add"
  void buildFxPicker();       // choose an unplaced FX to add
  void buildAssign();         // bind footswitches to FX pedals
  void tileBinding(lv_obj_t* tile, Effect* fx);  // color/chip a tile by its FS
  void buildMenu();
  void buildPedalControl();
  void buildMasterControl();
  void buildTuner();
  void refreshTuner();

  // --- live-value refresh helpers ---
  void setEffectRowText(const FocusItem& it);

  Chain& chain_;
  PedalControls& ctl_;
  FxFactory& factory_;
  fx::Tuner* tuner_;
  pistomp::InputLevel* inputLevel_ = nullptr;   // analog level source (null/absent in sim)
  VuMeter vu_[2];                                // input-level meters: [0]=in1/L, [1]=in2/R
  std::string ampName_;
  std::string rigDir_;

  Page page_ = Home;
  std::vector<FocusItem> items_;
  int focus_ = 0;

  Effect* current_ = nullptr;   // effect on the PedalControl page
  int paramBase_ = 0;           // knob bank offset when an effect has > 3 params
  int pickerSlot_ = -1;         // grid slot the FxPicker is filling
  std::vector<std::string> rigNames_;   // backs the Menu's "Rig: <name>" rows

  bool tunerActive_ = false;    // tuner full-screen takeover currently shown

  uint32_t ledSig_ = 0xFFFFFFFF;                       // last LED frame; -1 = force draw

  // dynamic widgets kept for per-frame refresh()
  lv_obj_t* homeMaster_ = nullptr;
  lv_obj_t* ctlName_[3] = {nullptr, nullptr, nullptr};
  lv_obj_t* ctlVal_[3]  = {nullptr, nullptr, nullptr};
  lv_obj_t* ctlBar_[3]  = {nullptr, nullptr, nullptr};
  lv_obj_t* powerLbl_   = nullptr;
  lv_obj_t* masterBar_  = nullptr;
  lv_obj_t* masterVal_  = nullptr;

  // tuner takeover widgets
  lv_obj_t* tNote_ = nullptr;
  lv_obj_t* tCents_ = nullptr;
  lv_obj_t* tInfo_ = nullptr;
};
