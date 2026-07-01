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
//   FS1+FS2      : previous rig    FS3+FS4: next rig   (two-switch combos)
//
// Pages: Home is the combined dashboard (Input/Rig/Output bar + the live FX grid
// + Menu/Assign/?/Master) -> section lists -> per-pedal control page; plus a Menu
// (bypass/tuner/rigs), a controls cheat-sheet (?), and the full-screen Tuner.

#pragma once

#include "../ui_events.h"
#include "../input_vu.h"
#include "../setlists.h"

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
               fx::Tuner* tuner, std::string ampName, std::string baseDir);

  void begin();                   // build the initial (Home) page
  void handle(const UiEvent& e);  // act on one input event
  void refresh();                 // per-frame: live values + tuner takeover
  void updateLeds(Leds& leds);    // per-frame: footswitch + input-meter LEDs (UI thread)

  // Optional: the analog input-level detectors. When present and available()
  // (hardware) the meter LEDs read it; otherwise they fall back to the digital
  // audio peak in PedalControls::inPeak (simulator).
  void setInputLevel(pistomp::InputLevel* lvl) { inputLevel_ = lvl; }

private:
  enum Page { Home, InputPage, FxPicker, AssignPage, OutputPage,
              MenuPage, PedalControl, MasterControl,
              NameEntryPage, RigsPage, PresetPage, SetlistList, SetlistView,
              PerfPage, HintPage };

  enum Action {
    ActNone, ActBack, ActGotoInput, ActGotoOutput, ActGotoMenu, ActGotoHint,
    ActOpenEffect, ActOpenMaster, ActTogglePower, ActEditParam, ActToggleBypass,
    ActToggleTuner, ActLoadRig, ActParamBank,
    ActOpenPicker, ActAddFx, ActRemoveFx, ActGotoAssign,
    // rigs
    ActGotoRigs, ActRigSave, ActRigSaveAs, ActRigRename, ActRigDelete, ActRigNew,
    // per-pedal presets
    ActGotoPresets, ActPresetLoad, ActPresetSaveAs, ActPresetRename, ActPresetDelete,
    // setlists + performance
    ActGotoSetlists, ActSetlistOpen, ActSetlistNew, ActSetlistRename,
    ActSetlistDelete, ActSetlistAddCur, ActSetlistRemove, ActPerfStart
  };

  // A pending text-entry operation: what to do with the string the char-picker
  // returns. Set by beginName(), consumed by commitName().
  enum NameOp { NameNone, OpRigSaveAs, OpRigRename, OpPresetSaveAs,
                OpPresetRename, OpSetlistNew, OpSetlistRename };

  struct FocusItem {
    lv_obj_t* obj = nullptr;   // focusable container (highlight target)
    lv_obj_t* lbl = nullptr;   // inner label, for dynamic text updates
    Action action = ActNone;
    Effect* fx = nullptr;
    int idx = 0;               // rig index / param-bank payload / param index
    // Extra widgets for the Input/Output param cells, kept for per-frame refresh
    // (trailing so the common 5-field aggregate inits elsewhere still compile):
    //   ActEditParam  -> arc = the knob, val = the value caption (shown while
    //                    editing this param; lbl is the name caption otherwise);
    //   ActTogglePower-> arc = the slide-switch handle (moved to reflect on/off).
    lv_obj_t* arc = nullptr;
    lv_obj_t* val = nullptr;
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
  void buildHome();           // combined: section buttons + live FX grid + actions

  // --- Input / Output combined pages (single nav-encoder editing) ------------
  // These mirror the web UI's whole Input / Output sections on one screen: every
  // pedal's knobs + enable switch, plus the level meters along the bottom. The
  // nav encoder walks the params/switches; click a knob to edit (turn to change,
  // click again to stop), click a switch to flip it; Enc1-push backs out.
  void buildInputPage();
  void buildOutputPage();
  lv_color_t knobColorFor(const Effect* fx) const;   // web KNOB_COLORS by kind
  lv_obj_t* groupBox(lv_obj_t* scr, int x0, int y0, int x1, int y1, const char* title);
  void paramCell(lv_obj_t* scr, int x, int y, int w, int h,
                 Effect* fx, int paramIdx, lv_color_t col);
  void switchCell(lv_obj_t* scr, int x, int y, Effect* fx);
  lv_obj_t* meterBar(lv_obj_t* scr, int x, int y, int w, const char* label,
                     lv_obj_t** outVal, bool fromEnd = false);
  void ioSelect();             // nav click on Input/Output: edit param / flip switch
  void editFocusedParam(int dir);
  void refreshIoPage();        // per-frame: knob values, switches, meters

  void buildHint();           // hardware-controls cheat sheet
  void buildFxPicker();       // choose an unplaced FX to add
  void buildAssign();         // bind footswitches to FX pedals
  void tileBinding(lv_obj_t* tile, Effect* fx);  // color/chip a tile by its FS
  void buildMenu();
  void buildPedalControl();
  void buildMasterControl();
  void buildTuner();
  void refreshTuner();

  // --- char-picker (on-device text entry; no keyboard) ---
  void buildNameEntry();
  void beginName(NameOp op, const std::string& initial, Page returnTo,
                 Effect* fx = nullptr, std::string orig = "");
  void nameTurn(int dir);     // dial the character under the caret
  void nameAdvance();         // accept + move caret right (append slot at end)
  void nameBackspace();       // delete char before caret (cancel if empty)
  void nameMoveCaret(int dir);
  void commitName();          // run the pending NameOp with nameBuf_
  void cancelName();

  // --- rigs + per-pedal presets (Phase D) ---
  void buildRigs();
  void buildPresetPage();
  void loadRig(const std::string& name);   // load + track clean baseline
  bool rigDirty() const;                    // current chain != loaded rig
  void deleteRig(const std::string& name);

  // --- setlists + performance stepping (Phase E) ---
  void buildSetlistList();
  void buildSetlistView();
  void buildPerf();
  void openSetlist(const std::string& name);
  void perfStep(int dir);                   // load prev/next rig in the setlist
  void stepRig(int dir);                    // load prev/next rig from the catalogue (FS combo)

  // --- live-value refresh helpers ---
  void setEffectRowText(const FocusItem& it);

  Chain& chain_;
  PedalControls& ctl_;
  FxFactory& factory_;
  fx::Tuner* tuner_;
  pistomp::InputLevel* inputLevel_ = nullptr;   // analog level source (null/absent in sim)
  VuMeter vu_[2];                                // input-level meters: [0]=in1/L, [1]=in2/R
  std::string ampName_;
  std::string baseDir_;      // library root (holds rigs/ presets/ setlists/ manifest.json)
  std::string rigDir_;       // baseDir_/rigs
  std::string presetDir_;    // baseDir_/presets
  std::string setlistDir_;   // baseDir_/setlists

  Page page_ = Home;
  std::vector<FocusItem> items_;
  int focus_ = 0;

  Effect* current_ = nullptr;   // effect on the PedalControl page
  int paramBase_ = 0;           // knob bank offset when an effect has > 3 params
  int pickerSlot_ = -1;         // grid slot the FxPicker is filling
  std::vector<std::string> rigNames_;   // backs the Menu's "Rig: <name>" rows

  // char-picker state
  NameOp nameOp_ = NameNone;
  std::string nameBuf_;         // the string being typed
  int nameCursor_ = 0;          // caret position (0..len; len == append slot)
  std::string nameOrig_;        // original name (rename ops)
  Effect* nameFx_ = nullptr;    // target effect (preset ops)
  Page nameReturn_ = Home;      // page to return to on commit/cancel

  // rig state
  std::string currentRig_;      // name of the loaded rig ("" = unsaved/new)
  std::string currentRigClean_; // contentHash of the rig as loaded (for dirty)
  std::vector<std::string> presetNames_;   // backs the PresetPage rows

  // setlist + performance state
  std::vector<std::string> setlistNames_;       // backs the SetlistList rows
  std::string openSetlist_;                     // setlist being viewed/performed
  std::vector<setlists::RigRef> setlistRigs_;   // its ordered rig refs
  int perfIdx_ = 0;                             // current position in performance

  bool tunerActive_ = false;    // tuner full-screen takeover currently shown

  // Input/Output pages: false = nav-turn moves the cursor; true = nav-turn edits
  // the focused param's value (toggled by clicking a knob). Reset on every page.
  bool editing_ = false;

  uint32_t ledSig_ = 0xFFFFFFFF;                       // last LED frame; -1 = force draw

  // dynamic widgets kept for per-frame refresh()
  lv_obj_t* homeMaster_ = nullptr;
  lv_obj_t* homeRig_    = nullptr;   // home top-bar rig-name label
  lv_obj_t* ctlName_[3] = {nullptr, nullptr, nullptr};
  lv_obj_t* ctlVal_[3]  = {nullptr, nullptr, nullptr};
  lv_obj_t* ctlArc_[3]  = {nullptr, nullptr, nullptr};   // swept-fill knob per param
  lv_obj_t* powerLbl_   = nullptr;
  lv_obj_t* masterBar_  = nullptr;
  lv_obj_t* masterVal_  = nullptr;

  // Input/Output meter widgets (rebuilt per page; null when off that page).
  float inMeterDb_ = -60.0f;        // latest input level (dBFS), published by updateLeds
  lv_obj_t* inLvlBar_   = nullptr;  // input level meter
  lv_obj_t* inLvlVal_   = nullptr;
  lv_obj_t* inGateMark_ = nullptr;  // gate-threshold marker line on the level bar
  lv_obj_t* inCompMark_ = nullptr;  // comp-threshold marker line
  lv_obj_t* inGrBar_    = nullptr;  // gain-reduction meter
  lv_obj_t* inGrVal_    = nullptr;
  int       inBarW_     = 0;        // level-bar inner width (for marker placement)
  lv_obj_t* outLBar_    = nullptr;  // output L/R level meters
  lv_obj_t* outLVal_    = nullptr;
  lv_obj_t* outRBar_    = nullptr;
  lv_obj_t* outRVal_    = nullptr;
  float     outDb_[2]   = {-60.0f, -60.0f};   // smoothed output level per channel

  // tuner takeover widgets
  lv_obj_t* tNote_ = nullptr;
  lv_obj_t* tCents_ = nullptr;
  lv_obj_t* tInfo_ = nullptr;
};
