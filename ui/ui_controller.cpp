// ui/ui_controller.cpp -- see ui_controller.h.

#include "ui_controller.h"

#include "../chain.h"
#include "../effect.h"
#include "../footswitch_control.h"
#include "../fx_factory.h"
#include "../fx_id.h"
#include "../manifest.h"
#include "../meta.h"
#include "../pedal_controls.h"
#include "../presets.h"
#include "../rigs.h"
#include "../setlists.h"
#include "../effects/tuner.h"

#include "leds.h"
#include "input_level.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// palette -- mirrors the web UI's design tokens (webui/src/app.css :root):
// gold-on-near-black with jewel-tone accents, within RGB565.
const lv_color_t kAccent = LV_COLOR_MAKE(0xC9, 0xA4, 0x5C);    // --gold (focus/titles)
const lv_color_t kAccentHi = LV_COLOR_MAKE(0xE6, 0xC0, 0x74);  // --gold-bright (active label)
const lv_color_t kText = LV_COLOR_MAKE(0xEC, 0xE7, 0xDB);      // --text (warm off-white)
const lv_color_t kMuted = LV_COLOR_MAKE(0x8D, 0x88, 0x7B);     // --muted
const lv_color_t kFaint = LV_COLOR_MAKE(0x5C, 0x58, 0x4F);     // --faint (hints/empty)
const lv_color_t kPanel = LV_COLOR_MAKE(0x1C, 0x1C, 0x23);     // --panel (containers)
const lv_color_t kInset = LV_COLOR_MAKE(0x15, 0x15, 0x1B);     // --inset (wells/knob bg)
const lv_color_t kLine = LV_COLOR_MAKE(0x45, 0x45, 0x4F);      // --line (hairline border)
const lv_color_t kGood = LV_COLOR_MAKE(0x3F, 0xAE, 0x74);      // --emerald
const lv_color_t kWarn = LV_COLOR_MAKE(0xE0, 0xA3, 0x41);      // --topaz
const lv_color_t kBad = LV_COLOR_MAKE(0xD3, 0x4A, 0x5E);       // --ruby

constexpr float kMasterMax = 2.0f;
constexpr float kMasterStep = 0.05f;

// Footswitch palette (1-indexed for display): FS1 red, FS2 green, FS3 blue,
// FS4 yellow -- matching the NeoPixels 0..3 under each stomp.
struct FsColor { uint8_t r, g, b; const char* name; };
const FsColor kFsColors[4] = {
  {0xFF, 0x45, 0x3A, "FS1"},   // red    (web fsColors.ts)
  {0x30, 0xD1, 0x58, "FS2"},   // green
  {0x4D, 0x96, 0xFF, "FS3"},   // blue
  {0xFF, 0xCC, 0x00, "FS4"},   // yellow
};

// LED status brightness, as a % of full range. The ws2812-pio overlay now runs
// at full range (brightness=255), so the byte we write IS the brightness -- we
// pick a comfortable status level here instead of leaning on a global dimmer.
// ~40% reads as a clear "on", ~20% as a dim "off"; above ~50% is harsh to look at.
constexpr int kLedOnPct  = 40;
constexpr int kLedDimPct = 20;

constexpr int kRowY0 = 38, kRowH = 30, kRowStep = 34, kRowW = 300, kRowX = 10;

const char* kNoteNames[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                              "F#", "G",  "G#", "A",  "A#", "B"};

// The character ring the char-picker dials through (caps -> lower -> digits ->
// a few separators). Wraps at both ends.
const std::string kNameRing =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 -_";
constexpr int kNameMax = 18;   // names fit the 320px row at ~16px/cell

// A styled, non-scrolling container used for every box/row.
lv_obj_t* mkContainer(lv_obj_t* parent, int w, int h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(o, kPanel, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, kLine, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_radius(o, 6, 0);   // echo the web's rounded cards
  lv_obj_set_style_pad_all(o, 4, 0);
  return o;
}

// A display-only "knob": an LVGL arc styled like the web's swept-fill knobs
// (controls/Knob.svelte) -- an inset track ring with a gold indicator swept by
// value. The encoders drive the value, so the arc never takes pointer input and
// shows no draggable knob dot. Caller positions it and sets the value/center text.
lv_obj_t* mkKnob(lv_obj_t* parent, int d) {
  lv_obj_t* a = lv_arc_create(parent);
  lv_obj_set_size(a, d, d);
  lv_arc_set_rotation(a, 135);
  lv_arc_set_bg_angles(a, 0, 270);   // 270deg sweep, 90deg gap at the bottom
  lv_arc_set_range(a, 0, 100);
  lv_arc_set_value(a, 0);
  lv_obj_clear_flag(a, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_style(a, NULL, LV_PART_KNOB);   // no draggable dot
  lv_obj_set_style_arc_width(a, 5, LV_PART_MAIN);
  lv_obj_set_style_arc_width(a, 5, LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(a, kInset, LV_PART_MAIN);
  lv_obj_set_style_arc_color(a, kAccent, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(a, LV_OPA_TRANSP, LV_PART_MAIN);   // hollow center
  lv_obj_set_style_border_width(a, 0, LV_PART_MAIN);
  return a;
}

// Format a param value the way the web UI does (unit-aware).
void fmtParam(char* buf, size_t n, const Param* p) {
  float v = p->get();
  const std::string& u = p->unit;
  float range = p->max - p->min;
  if (u == "%")            snprintf(buf, n, "%d%%", (int)lroundf(v));
  else if (u.empty())      (range <= 3.0f) ? snprintf(buf, n, "%d", (int)lroundf(v))
                                           : snprintf(buf, n, "%.2f", v);
  else if (u == "Hz" || u == "ms") snprintf(buf, n, "%.0f %s", v, u.c_str());
  else                     snprintf(buf, n, "%.1f %s", v, u.c_str());
}

int paramPct(const Param* p) {
  float range = p->max - p->min;
  if (range <= 0) return 0;
  return (int)std::clamp(lroundf((p->get() - p->min) / range * 100.0f), 0L, 100L);
}

}  // namespace

UiController::UiController(Chain& chain, PedalControls& ctl, FxFactory& factory,
                          fx::Tuner* tuner, std::string ampName,
                          std::string baseDir)
    : chain_(chain), ctl_(ctl), factory_(factory), tuner_(tuner),
      ampName_(std::move(ampName)), baseDir_(std::move(baseDir)),
      rigDir_(baseDir_ + "/rigs"), presetDir_(baseDir_ + "/presets"),
      setlistDir_(baseDir_ + "/setlists") {}

void UiController::begin() { goTo(Home); }

// ---------------- navigation ----------------

void UiController::goTo(Page p, Effect* fx) {
  page_ = p;
  if (p == PedalControl) current_ = fx;
  rebuild();
}

void UiController::rebuild() {
  items_.clear();
  focus_ = 0;
  homeMaster_ = homeRig_ = masterBar_ = masterVal_ = powerLbl_ = nullptr;
  for (int k = 0; k < 3; k++) ctlName_[k] = ctlVal_[k] = ctlArc_[k] = nullptr;

  switch (page_) {
    case Home:          buildHome();         break;
    case InputList:
    case OutputList:    buildList(page_);    break;
    case HintPage:      buildHint();         break;
    case FxPicker:      buildFxPicker();     break;
    case AssignPage:    buildAssign();       break;
    case MenuPage:      buildMenu();         break;
    case PedalControl:  buildPedalControl(); break;
    case MasterControl: buildMasterControl();break;
    case NameEntryPage: buildNameEntry();    break;
    case RigsPage:      buildRigs();         break;
    case PresetPage:    buildPresetPage();   break;
    case SetlistList:   buildSetlistList();  break;
    case SetlistView:   buildSetlistView();  break;
    case PerfPage:      buildPerf();         break;
  }
  applyFocus();
}

void UiController::moveFocus(int dir) {
  int n = (int)items_.size();
  if (n == 0) return;
  focus_ = ((focus_ + dir) % n + n) % n;
  applyFocus();
}

void UiController::applyFocus() {
  for (int i = 0; i < (int)items_.size(); i++) {
    bool f = (i == focus_);
    lv_obj_set_style_border_color(items_[(size_t)i].obj, f ? kAccent : kLine, 0);
    lv_obj_set_style_border_width(items_[(size_t)i].obj, f ? 2 : 1, 0);
  }
}

void UiController::select() {
  if (focus_ < 0 || focus_ >= (int)items_.size()) return;
  FocusItem it = items_[(size_t)focus_];
  switch (it.action) {
    case ActBack:        back(); break;
    case ActGotoInput:   goTo(InputList); break;
    case ActGotoOutput:  goTo(OutputList); break;
    case ActGotoMenu:    goTo(MenuPage); break;
    case ActGotoHint:    goTo(HintPage); break;
    case ActOpenEffect:  paramBase_ = 0; goTo(PedalControl, it.fx); break;
    case ActOpenMaster:  goTo(MasterControl); break;
    case ActGotoAssign:  goTo(AssignPage); break;
    case ActOpenPicker:  pickerSlot_ = it.idx; goTo(FxPicker); break;
    case ActAddFx:
      if (pickerSlot_ >= 0)
        chain_.fxPlace(pickerSlot_, factory_.create((size_t)it.idx));
      goTo(Home);
      break;
    case ActRemoveFx: {
      int slot = current_ ? chain_.fxSlotOf(current_) : -1;
      if (slot >= 0) chain_.fxRemove(slot);
      goTo(Home);
      break;
    }
    case ActTogglePower:
      if (current_) current_->enabled.store(!current_->enabled.load());
      break;
    case ActToggleBypass: ctl_.bypassed.store(!ctl_.bypassed.load()); break;
    case ActToggleTuner:
      if (tuner_) tuner_->enabled.store(!tuner_->engaged());
      break;
    case ActLoadRig: {
      // Context: on the Setlist view the idx indexes the setlist's rigs; on the
      // Rigs page it indexes the rig catalogue.
      std::string nm;
      if (page_ == SetlistView) {
        if (it.idx >= 0 && it.idx < (int)setlistRigs_.size())
          nm = setlistRigs_[(size_t)it.idx].name;
      } else if (it.idx >= 0 && it.idx < (int)rigNames_.size()) {
        nm = rigNames_[(size_t)it.idx];
      }
      if (!nm.empty()) loadRig(nm);
      rebuild();
      break;
    }
    case ActParamBank:
      if (current_) {
        int total = (int)current_->params.size();
        paramBase_ = (paramBase_ + 3 < total) ? paramBase_ + 3 : 0;
        rebuild();
      }
      break;

    // ---- rigs ----
    case ActGotoRigs: goTo(RigsPage); break;
    case ActRigSave:
      if (!currentRig_.empty() && rigs::save(rigDir_, currentRig_, chain_, ctl_)) {
        manifest::upsertFile(baseDir_, "rig", rigDir_ + "/" + currentRig_ + ".json");
        currentRigClean_ = meta::contentHash(rigs::capture(chain_, ctl_));
      }
      rebuild();
      break;
    case ActRigSaveAs: beginName(OpRigSaveAs, currentRig_, RigsPage); break;
    case ActRigNew:
      for (int s = chain_.fxSlotCount() - 1; s >= 0; s--)
        if (chain_.fxAt(s)) chain_.fxRemove(s);
      ctl_.masterLevel.store(1.0f);
      ctl_.bypassed.store(false);
      currentRig_.clear();
      currentRigClean_.clear();
      rebuild();
      break;

    // ---- per-pedal presets ----
    case ActGotoPresets: goTo(PresetPage); break;
    case ActPresetLoad:
      if (current_ && it.idx >= 0 && it.idx < (int)presetNames_.size())
        presets::load(presetDir_, fxBaseKind(current_->type_id()),
                      presetNames_[(size_t)it.idx], *current_);
      goTo(PedalControl, current_);
      break;
    case ActPresetSaveAs: beginName(OpPresetSaveAs, "", PresetPage, current_); break;

    // ---- setlists ----
    case ActGotoSetlists: goTo(SetlistList); break;
    case ActSetlistNew: beginName(OpSetlistNew, "", SetlistList); break;
    case ActSetlistOpen:
      if (it.idx >= 0 && it.idx < (int)setlistNames_.size()) {
        openSetlist(setlistNames_[(size_t)it.idx]);
        goTo(SetlistView);
      }
      break;
    case ActSetlistAddCur:
      if (!currentRig_.empty() && !openSetlist_.empty()) {
        manifest::Index idx = manifest::load(baseDir_);
        setlists::RigRef ref;
        ref.id = idx.idForName("rig", currentRig_);
        ref.name = currentRig_;
        setlistRigs_.push_back(ref);
        if (setlists::save(setlistDir_, openSetlist_, setlistRigs_))
          manifest::upsertFile(baseDir_, "setlist",
                               setlistDir_ + "/" + openSetlist_ + ".json");
        rebuild();
      }
      break;
    case ActPerfStart: perfIdx_ = 0; goTo(PerfPage); break;

    case ActRigRename: case ActRigDelete: case ActPresetRename:
    case ActPresetDelete: case ActSetlistRename: case ActSetlistDelete:
    case ActSetlistRemove:
      // These are reached via footswitches (onFootswitch), not select.
      break;
    case ActNone: break;
  }
}

void UiController::back() {
  switch (page_) {
    case Home: break;
    case InputList:
    case OutputList:
    case HintPage:
    case MenuPage:      goTo(Home); break;
    case FxPicker:
    case AssignPage:    goTo(Home); break;
    case MasterControl: goTo(Home); break;
    case PedalControl: {
      Page parent = Home;
      if (current_) {
        if (current_->section == Section::Input)  parent = InputList;
        else if (current_->section == Section::Output) parent = OutputList;
        else parent = Home;
      }
      goTo(parent);
      break;
    }
    case RigsPage:      goTo(MenuPage); break;
    case PresetPage:    goTo(PedalControl, current_); break;
    case SetlistList:   goTo(MenuPage); break;
    case SetlistView:   goTo(SetlistList); break;
    case PerfPage:      goTo(SetlistView); break;
    case NameEntryPage: cancelName(); break;   // (normally intercepted in handle)
  }
}

void UiController::onKnob(int which, int dir) {
  // Home FX grid: Enc1 turn moves the focused pedal within the order (Enc3 still
  // adjusts master via the fallback below).
  if (page_ == Home && which == 0 &&
      focus_ >= 0 && focus_ < (int)items_.size()) {
    FocusItem& it = items_[(size_t)focus_];
    if (it.action == ActOpenEffect && it.fx) {
      int from = it.idx;
      int to = from + (dir < 0 ? -1 : 1);
      chain_.fxMove(from, dir);   // no-op at the grid edge
      if (to < 0 || to >= chain_.fxSlotCount()) to = from;
      rebuild();
      // follow the pedal to its new slot (a tile's idx is its slot index)
      for (int i = 0; i < (int)items_.size(); i++)
        if (items_[(size_t)i].obj && items_[(size_t)i].idx == to &&
            (items_[(size_t)i].action == ActOpenEffect || items_[(size_t)i].action == ActOpenPicker)) {
          focus_ = i;
          break;
        }
      applyFocus();
    }
    return;
  }
  if (page_ == PedalControl && current_) {
    int pi = paramBase_ + which;
    if (pi < (int)current_->params.size()) { stepParam(current_, pi, dir); return; }
  }
  if (page_ == MasterControl) { adjustMaster(dir); return; }
  if (which == 2) adjustMaster(dir);   // Enc3 = master everywhere else (and as fallback)
}

void UiController::adjustMaster(int dir) {
  float m = ctl_.masterLevel.load() + (float)dir * kMasterStep;
  ctl_.masterLevel.store(std::clamp(m, 0.0f, kMasterMax));
}

void UiController::stepParam(Effect* fx, int paramIdx, int dir) {
  Param* p = fx->params[(size_t)paramIdx].get();
  float range = p->max - p->min;
  float v = p->get();
  auto integral = [](float x) { return std::fabs(x - std::round(x)) < 1e-6f; };
  bool enumLike = p->unit.empty() && range >= 1.0f && range <= 3.0f &&
                  integral(p->min) && integral(p->max);
  if (enumLike)         v = std::round(v) + (float)dir;
  else if (range > 0)   v += (float)dir * (range / 50.0f);
  p->set(v);   // clamps internally
}

// ---------------- builders ----------------

lv_obj_t* UiController::newScreen() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  // Cells on the home page overlap 1px past the panel edge to merge borders; that
  // makes the screen technically scrollable and draws a stray scrollbar. We never
  // scroll, so turn it off everywhere.
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(scr, LV_SCROLLBAR_MODE_OFF);
  return scr;
}

void UiController::topBar(lv_obj_t* scr, const char* title) {
  lv_obj_t* back = mkContainer(scr, 64, 24);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 6, 6);
  lv_obj_t* bl = lv_label_create(back);
  lv_label_set_text(bl, "< Back");
  lv_obj_set_style_text_color(bl, kText, 0);
  lv_obj_align(bl, LV_ALIGN_LEFT_MID, 0, 0);
  items_.push_back({back, bl, ActBack, nullptr, 0});

  lv_obj_t* t = lv_label_create(scr);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(t, kAccent, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);
}

lv_obj_t* UiController::addRow(lv_obj_t* scr, int rowIndex, Action a, Effect* fx, int idx) {
  lv_obj_t* o = mkContainer(scr, kRowW, kRowH);
  lv_obj_align(o, LV_ALIGN_TOP_LEFT, kRowX, kRowY0 + rowIndex * kRowStep);
  lv_obj_t* l = lv_label_create(o);
  lv_obj_set_style_text_color(l, kText, 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  items_.push_back({o, l, a, fx, idx});
  return o;
}

// The combined home dashboard. One borderless table fills the 320x240 panel --
// no rounded cards, no gaps; cells overlap by 1px so their hairlines merge.
//   row 0: Input | Rig | Output      (section buttons + the loaded rig)
//   mid  : the live 4x2 FX grid       (tap a pedal to edit, "+" to add;
//                                       Enc1 on a focused pedal moves it)
//   row 2: Menu | Assign | ? | Master (global actions + the output level)
// Merges what used to be the separate Home and FX-grid pages.
void UiController::buildHome() {
  lv_obj_t* scr = newScreen();

  // Inclusive-bounds flat cell spanning [x0,x1]x[y0,y1]; neighbours that share a
  // boundary value overlap by 1px so their borders draw as a single line.
  auto cell = [&](int x0, int y0, int x1, int y1) -> lv_obj_t* {
    lv_obj_t* o = lv_obj_create(scr);
    lv_obj_set_size(o, x1 - x0 + 1, y1 - y0 + 1);
    lv_obj_align(o, LV_ALIGN_TOP_LEFT, x0, y0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(o, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, kLine, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_set_style_pad_all(o, 2, 0);
    return o;
  };
  auto centerLabel = [&](lv_obj_t* o, const char* txt, lv_color_t col) -> lv_obj_t* {
    lv_obj_t* l = lv_label_create(o);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_color(l, col, 0);
    lv_obj_center(l);
    return l;
  };

  const int topY1 = 33, midY1 = 118, lowY1 = 203, botY1 = 239;

  // --- row 0: Input | Rig | Output ---
  lv_obj_t* in = cell(0, 0, 70, topY1);
  centerLabel(in, "Input", kText);
  items_.push_back({in, nullptr, ActGotoInput, nullptr, 0});

  lv_obj_t* rig = cell(70, 0, 250, topY1);
  homeRig_ = centerLabel(rig, currentRig_.empty() ? "- No Rig -"
                                                  : currentRig_.c_str(), kAccent);
  items_.push_back({rig, homeRig_, ActGotoRigs, nullptr, 0});

  lv_obj_t* out = cell(250, 0, 320, topY1);
  centerLabel(out, "Output", kText);
  items_.push_back({out, nullptr, ActGotoOutput, nullptr, 0});

  // --- middle: the FX grid (4 cols x 2 rows of positional slots) ---
  const int fxFocus = (int)items_.size();   // default the cursor to the first pedal
  const int xb[5] = {0, 80, 160, 240, 320};
  const int yb[3] = {topY1, midY1, lowY1};
  for (int slot = 0; slot < chain_.fxSlotCount(); slot++) {
    int r = slot / 4, c = slot % 4;
    lv_obj_t* o = cell(xb[c], yb[r], xb[c + 1], yb[r + 1]);
    lv_obj_t* l = lv_label_create(o);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, xb[c + 1] - xb[c] - 8);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -4);
    if (Effect* fx = chain_.fxAt(slot)) {
      lv_label_set_text(l, fx->name().c_str());
      lv_obj_set_style_text_color(l, fx->enabled.load() ? kText : kMuted, 0);
      tileBinding(o, fx);
      items_.push_back({o, l, ActOpenEffect, fx, slot});
    } else {
      lv_label_set_text(l, "+");
      lv_obj_set_style_text_color(l, kFaint, 0);
      items_.push_back({o, l, ActOpenPicker, nullptr, slot});
    }
  }

  // --- row 2: Menu | Assign | ? | Master ---
  lv_obj_t* mn = cell(0, lowY1, 70, botY1);
  centerLabel(mn, "Menu", kText);
  items_.push_back({mn, nullptr, ActGotoMenu, nullptr, 0});

  lv_obj_t* as = cell(70, lowY1, 150, botY1);
  centerLabel(as, "Assign", kText);
  items_.push_back({as, nullptr, ActGotoAssign, nullptr, 0});

  lv_obj_t* hp = cell(150, lowY1, 196, botY1);
  centerLabel(hp, "?", kAccent);
  items_.push_back({hp, nullptr, ActGotoHint, nullptr, 0});

  lv_obj_t* ms = cell(196, lowY1, 320, botY1);
  homeMaster_ = centerLabel(ms, "Master: --", kText);
  items_.push_back({ms, homeMaster_, ActOpenMaster, nullptr, 0});

  if (chain_.fxSlotCount() > 0) focus_ = fxFocus;
}

// A read-only cheat-sheet of the hardware controls, reached from the home "?".
void UiController::buildHint() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "CONTROLS");

  static const char* lines[] = {
    "Nav: turn=move  push=select  hold=quit",
    "Enc1: push=back   turn=param 1 / move FX",
    "Enc2: turn=param 2",
    "Enc3: turn=param 3 / master",
    "FS1-4: tap=toggle assigned FX",
    "Hold FS4: tuner",
    "FS1+FS2: prev rig    FS3+FS4: next rig",
  };
  int y = 44;
  for (const char* s : lines) {
    lv_obj_t* l = lv_label_create(scr);
    lv_label_set_text(l, s);
    lv_obj_set_style_text_color(l, kText, 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 10, y);
    y += 27;
  }
}

void UiController::buildList(Page p) {
  lv_obj_t* scr = newScreen();
  Section sec = Section::Input;
  const char* title = "INPUT";
  if (p == OutputList) { sec = Section::Output; title = "OUTPUT"; }
  topBar(scr, title);

  int row = 0;
  if (p == OutputList) {
    lv_obj_t* o = addRow(scr, row++, ActOpenMaster, nullptr, 0);
    (void)o;
    lv_label_set_text(items_.back().lbl, "Output Level  >");
  }
  for (auto* fx : chain_.effects()) {
    if (fx->section != sec) continue;
    addRow(scr, row++, ActOpenEffect, fx, 0);
    setEffectRowText(items_.back());
  }
  focus_ = 0;
}

// Tint a pedal tile and add a small color chip showing which footswitch drives
// it (and whether the binding is inverted). No-op for unbound effects.
void UiController::tileBinding(lv_obj_t* tile, Effect* fx) {
  if (!fx) return;
  int fs = fx->fsAssign.load();
  if (fs < 0 || fs > 3) return;
  const FsColor& c = kFsColors[fs];
  lv_obj_set_style_bg_color(
      tile,
      LV_COLOR_MAKE((uint8_t)(c.r / 6), (uint8_t)(c.g / 6), (uint8_t)(c.b / 6)), 0);
  lv_obj_t* chip = lv_label_create(tile);
  char b[16];
  snprintf(b, sizeof b, "%s%s", c.name, fx->fsMode.load() == 1 ? " inv" : "");
  lv_label_set_text(chip, b);
  lv_obj_set_style_text_color(chip, LV_COLOR_MAKE(c.r, c.g, c.b), 0);
  lv_obj_align(chip, LV_ALIGN_BOTTOM_MID, 0, 0);
}

// Footswitch-assignment mode: the same FX pedals, but pressing a physical
// footswitch (handled in onFootswitch) binds it to the focused pedal instead of
// toggling it. Tiles are not selectable here.
void UiController::buildAssign() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "ASSIGN");

  const int cols = 4, tileW = 70, tileH = 62, gapX = 6, gapY = 8;
  const int x0 = 8, y0 = 40;

  for (int slot = 0; slot < chain_.fxSlotCount(); slot++) {
    int r = slot / cols, col = slot % cols;
    int x = x0 + col * (tileW + gapX);
    int y = y0 + r * (tileH + gapY);
    lv_obj_t* o = mkContainer(scr, tileW, tileH);
    lv_obj_align(o, LV_ALIGN_TOP_LEFT, x, y);
    lv_obj_t* l = lv_label_create(o);
    lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(l, tileW - 8);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, -6);
    Effect* fx = chain_.fxAt(slot);
    if (fx) {
      lv_label_set_text(l, fx->name().c_str());
      lv_obj_set_style_text_color(l, kText, 0);
      tileBinding(o, fx);
    } else {
      lv_label_set_text(l, "-");
      lv_obj_set_style_text_color(l, kFaint, 0);
    }
    // fx may be null for empty slots; onFootswitch ignores those.
    items_.push_back({o, l, ActNone, fx, slot});
  }

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "press a footswitch: bind / invert / clear");
  lv_obj_set_style_text_color(hint, kFaint, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// Choose a kind to drop into pickerSlot_. Duplicates are allowed, so every
// registered kind is listed every time. FocusItem::idx = the kind index.
void UiController::buildFxPicker() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "ADD FX");
  const auto& kinds = factory_.kinds();
  for (int i = 0; i < (int)kinds.size(); i++) {
    addRow(scr, i, ActAddFx, nullptr, i);
    lv_label_set_text(items_.back().lbl, kinds[(size_t)i].name.c_str());
  }
}

void UiController::buildMenu() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "MENU");
  int row = 0;
  addRow(scr, row++, ActToggleBypass, nullptr, 0);  // text set in refresh()
  addRow(scr, row++, ActToggleTuner, nullptr, 0);
  addRow(scr, row++, ActGotoRigs, nullptr, 0);
  lv_label_set_text(items_.back().lbl, "Rigs...");
  addRow(scr, row++, ActGotoSetlists, nullptr, 0);
  lv_label_set_text(items_.back().lbl, "Setlists...");
}

void UiController::buildPedalControl() {
  lv_obj_t* scr = newScreen();
  topBar(scr, current_ ? current_->name().c_str() : "");
  if (!current_) return;

  addRow(scr, 0, ActTogglePower, current_, 0);
  powerLbl_ = items_.back().lbl;

  // Per-pedal presets for this effect's kind (top-right, like the web panel).
  lv_obj_t* pr = mkContainer(scr, 84, 24);
  lv_obj_align(pr, LV_ALIGN_TOP_RIGHT, -6, 6);
  lv_obj_t* prl = lv_label_create(pr);
  lv_label_set_text(prl, "Presets");
  lv_obj_set_style_text_color(prl, kText, 0);
  lv_obj_center(prl);
  items_.push_back({pr, prl, ActGotoPresets, current_, 0});

  // FX-grid pedals can be taken out of the chain from here. Kept at the bottom-
  // left; the param-bank button (added below) sits bottom-right so they don't
  // touch. (An effect lives in an FX slot iff it's currently placed in one.)
  if (chain_.fxSlotOf(current_) >= 0) {
    lv_obj_t* o = mkContainer(scr, 96, 24);
    lv_obj_align(o, LV_ALIGN_BOTTOM_LEFT, 6, -8);
    lv_obj_t* l = lv_label_create(o);
    lv_label_set_text(l, "Remove FX");
    lv_obj_set_style_text_color(l, kBad, 0);
    lv_obj_center(l);
    items_.push_back({o, l, ActRemoveFx, current_, 0});
  }

  // Up to three params at once, each shown as a swept-fill knob (driven by the
  // matching encoder K1/K2/K3) with the value in its center and the name beside.
  int total = (int)current_->params.size();
  const int knobD = 44;
  for (int k = 0; k < 3; k++) {
    int pi = paramBase_ + k;
    if (pi >= total) break;
    Param* p = current_->params[(size_t)pi].get();
    int y = 72 + k * 52;

    lv_obj_t* arc = mkKnob(scr, knobD);
    lv_obj_align(arc, LV_ALIGN_TOP_LEFT, kRowX + 2, y);
    ctlArc_[k] = arc;

    lv_obj_t* vl = lv_label_create(arc);   // value sits in the knob's hollow center
    lv_obj_set_style_text_color(vl, kText, 0);
    lv_label_set_text(vl, "");
    lv_obj_center(vl);
    ctlVal_[k] = vl;

    lv_obj_t* nm = lv_label_create(scr);
    char nb[48];
    snprintf(nb, sizeof nb, "K%d  %s", k + 1, p->name.c_str());
    lv_label_set_text(nm, nb);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(nm, kAccentHi, 0);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, kRowX + 2 + knobD + 14, y + 12);
    ctlName_[k] = nm;
  }

  if (total > 3) {
    lv_obj_t* o = mkContainer(scr, 140, 24);
    lv_obj_align(o, LV_ALIGN_BOTTOM_RIGHT, -6, -8);
    lv_obj_t* l = lv_label_create(o);
    char b[40];
    int last = std::min(paramBase_ + 3, total);
    snprintf(b, sizeof b, "params %d-%d  >", paramBase_ + 1, last);
    lv_label_set_text(l, b);
    lv_obj_set_style_text_color(l, kText, 0);
    lv_obj_center(l);
    items_.push_back({o, l, ActParamBank, current_, 0});
  }
}

void UiController::buildMasterControl() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "OUTPUT LEVEL");

  masterVal_ = lv_label_create(scr);
  lv_obj_set_style_text_font(masterVal_, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(masterVal_, kText, 0);
  lv_obj_align(masterVal_, LV_ALIGN_CENTER, 0, -24);

  // A fader-style bar, mirroring the web's custom Fader: inset track, gold fill.
  masterBar_ = lv_bar_create(scr);
  lv_obj_set_size(masterBar_, 280, 12);
  lv_obj_align(masterBar_, LV_ALIGN_CENTER, 0, 16);
  lv_bar_set_range(masterBar_, 0, 200);
  lv_obj_set_style_radius(masterBar_, 6, LV_PART_MAIN);
  lv_obj_set_style_bg_color(masterBar_, kInset, LV_PART_MAIN);
  lv_obj_set_style_radius(masterBar_, 6, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(masterBar_, kAccent, LV_PART_INDICATOR);
}

void UiController::buildTuner() {
  // The takeover replaces the current page's widgets; drop their (now-deleted)
  // focus items so nothing stale can be referenced while tuning.
  items_.clear();
  focus_ = 0;
  lv_obj_t* scr = newScreen();
  lv_obj_t* t = lv_label_create(scr);
  lv_label_set_text(t, "TUNER");
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(t, kAccent, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);

  tNote_ = lv_label_create(scr);
  lv_obj_set_style_text_font(tNote_, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(tNote_, kText, 0);
  lv_obj_align(tNote_, LV_ALIGN_CENTER, 0, -24);
  lv_label_set_text(tNote_, "--");

  tCents_ = lv_bar_create(scr);
  lv_obj_set_size(tCents_, 288, 18);
  lv_obj_align(tCents_, LV_ALIGN_CENTER, 0, 16);
  lv_bar_set_mode(tCents_, LV_BAR_MODE_SYMMETRICAL);
  lv_bar_set_range(tCents_, -50, 50);
  lv_bar_set_value(tCents_, 0, LV_ANIM_OFF);

  tInfo_ = lv_label_create(scr);
  lv_obj_set_style_text_color(tInfo_, kMuted, 0);
  lv_obj_align(tInfo_, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_label_set_text(tInfo_, "play a note...");
}

// ---------------- char-picker (on-device text entry) ----------------

// Open the char-picker for `op`, seeded with `initial`, returning to `returnTo`
// on commit/cancel. `fx`/`orig` carry context for preset and rename ops.
void UiController::beginName(NameOp op, const std::string& initial, Page returnTo,
                            Effect* fx, std::string orig) {
  nameOp_ = op;
  nameBuf_ = initial;
  if ((int)nameBuf_.size() > kNameMax) nameBuf_.resize(kNameMax);
  nameCursor_ = (int)nameBuf_.size();   // caret at the append slot
  nameReturn_ = returnTo;
  nameFx_ = fx;
  nameOrig_ = std::move(orig);
  goTo(NameEntryPage);
}

// A row of character cells; the caret cell is highlighted gold. The trailing
// (empty) cell is the append slot. Rebuilt on every keystroke -- cheap at human
// pace -- so the page reflects nameBuf_/nameCursor_ directly.
void UiController::buildNameEntry() {
  lv_obj_t* scr = newScreen();

  const char* prompt = "NAME";
  switch (nameOp_) {
    case OpRigSaveAs:    prompt = "SAVE RIG AS"; break;
    case OpRigRename:    prompt = "RENAME RIG"; break;
    case OpPresetSaveAs: prompt = "SAVE PRESET AS"; break;
    case OpPresetRename: prompt = "RENAME PRESET"; break;
    case OpSetlistNew:   prompt = "NEW SETLIST"; break;
    case OpSetlistRename:prompt = "RENAME SETLIST"; break;
    case NameNone:       break;
  }
  lv_obj_t* t = lv_label_create(scr);
  lv_label_set_text(t, prompt);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(t, kAccent, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);

  const int cellW = 16, cellH = 26, gap = 1;
  int n = (int)nameBuf_.size() + 1;              // +1 for the append cell
  int totalW = n * cellW + (n - 1) * gap;
  int x0 = (320 - totalW) / 2;
  if (x0 < 4) x0 = 4;
  for (int i = 0; i < n; i++) {
    lv_obj_t* cell = lv_obj_create(scr);
    lv_obj_set_size(cell, cellW, cellH);
    lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(cell, kInset, 0);
    lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
    bool caret = (i == nameCursor_);
    lv_obj_set_style_border_color(cell, caret ? kAccent : kLine, 0);
    lv_obj_set_style_border_width(cell, caret ? 2 : 1, 0);
    lv_obj_set_style_radius(cell, 3, 0);
    lv_obj_set_style_pad_all(cell, 0, 0);
    lv_obj_align(cell, LV_ALIGN_TOP_LEFT, x0 + i * (cellW + gap), 70);
    lv_obj_t* l = lv_label_create(cell);
    char cb[2] = {i < (int)nameBuf_.size() ? nameBuf_[(size_t)i] : ' ', 0};
    lv_label_set_text(l, cb);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(l, kText, 0);
    lv_obj_center(l);
  }

  lv_obj_t* hint1 = lv_label_create(scr);
  lv_label_set_text(hint1, "Nav: dial   push: next   Back: delete   Enc2: move");
  lv_obj_set_style_text_color(hint1, kFaint, 0);
  lv_obj_align(hint1, LV_ALIGN_BOTTOM_MID, 0, -24);

  lv_obj_t* hint2 = lv_label_create(scr);
  lv_label_set_text(hint2, "FS1 = save     FS2 = cancel");
  lv_obj_set_style_text_color(hint2, kMuted, 0);
  lv_obj_align(hint2, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void UiController::nameTurn(int dir) {
  int len = (int)nameBuf_.size();
  int ringN = (int)kNameRing.size();
  if (nameCursor_ >= len) {
    // append slot: dialing starts a new character
    if (len >= kNameMax) return;
    char c = dir > 0 ? kNameRing[0] : kNameRing[(size_t)(ringN - 1)];
    nameBuf_.push_back(c);
  } else {
    size_t at = kNameRing.find(nameBuf_[(size_t)nameCursor_]);
    int idx = at == std::string::npos ? 0 : (int)at;
    idx = ((idx + dir) % ringN + ringN) % ringN;
    nameBuf_[(size_t)nameCursor_] = kNameRing[(size_t)idx];
  }
  rebuild();
}

void UiController::nameAdvance() {
  int len = (int)nameBuf_.size();
  if (nameCursor_ < len && len < kNameMax) { nameCursor_++; rebuild(); }
}

void UiController::nameBackspace() {
  if (nameBuf_.empty()) { cancelName(); return; }
  if (nameCursor_ > 0) {
    nameBuf_.erase((size_t)(nameCursor_ - 1), 1);
    nameCursor_--;
    rebuild();
  }
}

void UiController::nameMoveCaret(int dir) {
  int len = (int)nameBuf_.size();
  nameCursor_ = std::clamp(nameCursor_ + (dir > 0 ? 1 : -1), 0, len);
  rebuild();
}

// Trim surrounding spaces, then run the pending op against the persistence layer
// and refresh the manifest the same way the web endpoints do.
void UiController::commitName() {
  std::string name = nameBuf_;
  size_t a = name.find_first_not_of(' ');
  size_t b = name.find_last_not_of(' ');
  name = (a == std::string::npos) ? "" : name.substr(a, b - a + 1);
  if (name.empty()) { cancelName(); return; }

  switch (nameOp_) {
    case OpRigSaveAs:
      if (rigs::save(rigDir_, name, chain_, ctl_)) {
        manifest::upsertFile(baseDir_, "rig", rigDir_ + "/" + name + ".json");
        currentRig_ = name;   // the saved rig is now the loaded one
        currentRigClean_ = meta::contentHash(rigs::capture(chain_, ctl_));
      }
      break;
    case OpRigRename:
      if (!nameOrig_.empty() && nameOrig_ != name &&
          rigs::rename(rigDir_, nameOrig_, name)) {
        manifest::removeFile(baseDir_, rigDir_ + "/" + nameOrig_ + ".json");
        manifest::upsertFile(baseDir_, "rig", rigDir_ + "/" + name + ".json");
        if (currentRig_ == nameOrig_) currentRig_ = name;   // keep tracking it
      }
      break;
    case OpPresetSaveAs:
      if (nameFx_ && presets::save(presetDir_, fxBaseKind(nameFx_->type_id()), name, *nameFx_))
        manifest::upsertFile(baseDir_, "preset",
                             presetDir_ + "/" + fxBaseKind(nameFx_->type_id()) + "/" + name + ".json");
      break;
    case OpPresetRename:
      if (nameFx_ && !nameOrig_.empty() && nameOrig_ != name &&
          presets::rename(presetDir_, fxBaseKind(nameFx_->type_id()), nameOrig_, name)) {
        manifest::removeFile(baseDir_,
                             presetDir_ + "/" + fxBaseKind(nameFx_->type_id()) + "/" + nameOrig_ + ".json");
        manifest::upsertFile(baseDir_, "preset",
                             presetDir_ + "/" + fxBaseKind(nameFx_->type_id()) + "/" + name + ".json");
      }
      break;
    case OpSetlistNew:
      if (setlists::save(setlistDir_, name, {}))
        manifest::upsertFile(baseDir_, "setlist", setlistDir_ + "/" + name + ".json");
      break;
    case OpSetlistRename: {
      // setlists has no rename helper: copy the ordered refs to the new name and
      // drop the old file. (Mints a fresh setlist id -- acceptable; nothing
      // references a setlist by id.)
      std::vector<setlists::RigRef> rr;
      if (!nameOrig_.empty() && nameOrig_ != name &&
          setlists::load(setlistDir_, nameOrig_, rr) &&
          setlists::save(setlistDir_, name, rr)) {
        setlists::remove(setlistDir_, nameOrig_);
        manifest::removeFile(baseDir_, setlistDir_ + "/" + nameOrig_ + ".json");
        manifest::upsertFile(baseDir_, "setlist", setlistDir_ + "/" + name + ".json");
        if (openSetlist_ == nameOrig_) openSetlist_ = name;
      }
      break;
    }
    case NameNone: break;
  }
  Page ret = nameReturn_;
  nameOp_ = NameNone;
  nameFx_ = nullptr;
  goTo(ret);
}

void UiController::cancelName() {
  Page ret = nameReturn_;
  nameOp_ = NameNone;
  nameFx_ = nullptr;
  goTo(ret);
}

// ---------------- rigs + per-pedal presets ----------------

// Load a rig and remember it as the clean baseline, so rigDirty() can tell when
// the live chain has drifted from what's on disk (the web's unsaved-`*` logic).
void UiController::loadRig(const std::string& name) {
  if (rigs::load(rigDir_, name, chain_, ctl_, factory_)) {
    currentRig_ = name;
    currentRigClean_ = meta::contentHash(rigs::capture(chain_, ctl_));
  }
}

bool UiController::rigDirty() const {
  if (currentRig_.empty()) return false;
  return meta::contentHash(rigs::capture(chain_, ctl_)) != currentRigClean_;
}

void UiController::deleteRig(const std::string& name) {
  if (rigs::remove(rigDir_, name)) {
    manifest::removeFile(baseDir_, rigDir_ + "/" + name + ".json");
    if (currentRig_ == name) { currentRig_.clear(); currentRigClean_.clear(); }
  }
}

// Rig manager: Save/Save As/New at the top, then every saved rig (select =
// load). A focused rig row takes FS1 = rename, FS2 = delete (the device's
// footswitch-as-context-action idiom, same as the Assign page).
void UiController::buildRigs() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "RIGS");

  int row = 0;
  if (!currentRig_.empty()) {
    addRow(scr, row++, ActRigSave, nullptr, 0);
    std::string s = "Save  (" + currentRig_ + (rigDirty() ? " *)" : ")");
    lv_label_set_text(items_.back().lbl, s.c_str());
  }
  addRow(scr, row++, ActRigSaveAs, nullptr, 0);
  lv_label_set_text(items_.back().lbl, "Save As...");
  addRow(scr, row++, ActRigNew, nullptr, 0);
  lv_label_set_text(items_.back().lbl, "New (empty board)");

  rigNames_ = rigs::list(rigDir_);
  for (int i = 0; i < (int)rigNames_.size(); i++) {
    addRow(scr, row++, ActLoadRig, nullptr, i);
    std::string s = (rigNames_[(size_t)i] == currentRig_ ? "* " : "  ") +
                    rigNames_[(size_t)i];
    lv_label_set_text(items_.back().lbl, s.c_str());
  }

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "select: load   FS1: rename   FS2: delete");
  lv_obj_set_style_text_color(hint, kFaint, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// Per-pedal presets for the current effect's kind (a knob snapshot, like the
// web's PedalPresets). Save As at the top, then each preset (select = load);
// FS1 = rename, FS2 = delete the focused preset.
void UiController::buildPresetPage() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "PRESETS");
  if (!current_) return;
  std::string kind = fxBaseKind(current_->type_id());

  int row = 0;
  addRow(scr, row++, ActPresetSaveAs, current_, 0);
  lv_label_set_text(items_.back().lbl, "Save As...");

  presetNames_ = presets::list(presetDir_, kind);
  for (int i = 0; i < (int)presetNames_.size(); i++) {
    addRow(scr, row++, ActPresetLoad, current_, i);
    lv_label_set_text(items_.back().lbl, presetNames_[(size_t)i].c_str());
  }

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "select: load   FS1: rename   FS2: delete");
  lv_obj_set_style_text_color(hint, kFaint, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// ---------------- setlists + performance ----------------

// Load a setlist's ordered rig refs and reconcile each cached name against the
// manifest (rename-safe), mirroring the web's /api/setlist resolution.
void UiController::openSetlist(const std::string& name) {
  openSetlist_ = name;
  setlistRigs_.clear();
  setlists::load(setlistDir_, name, setlistRigs_);
  perfIdx_ = 0;
  manifest::Index idx = manifest::load(baseDir_);
  for (auto& r : setlistRigs_) {
    if (!r.id.empty()) {
      std::string cur = idx.nameForId(r.id);
      if (!cur.empty()) r.name = cur;
    }
  }
}

void UiController::buildSetlistList() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "SETLISTS");

  int row = 0;
  addRow(scr, row++, ActSetlistNew, nullptr, 0);
  lv_label_set_text(items_.back().lbl, "New...");

  setlistNames_ = setlists::list(setlistDir_);
  for (int i = 0; i < (int)setlistNames_.size(); i++) {
    addRow(scr, row++, ActSetlistOpen, nullptr, i);
    lv_label_set_text(items_.back().lbl, setlistNames_[(size_t)i].c_str());
  }

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "select: open   FS1: rename   FS2: delete");
  lv_obj_set_style_text_color(hint, kFaint, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

void UiController::buildSetlistView() {
  lv_obj_t* scr = newScreen();
  topBar(scr, openSetlist_.empty() ? "SETLIST" : openSetlist_.c_str());

  int row = 0;
  if (!setlistRigs_.empty()) {
    addRow(scr, row++, ActPerfStart, nullptr, 0);
    lv_label_set_text(items_.back().lbl, "Perform  >");
  }
  addRow(scr, row++, ActSetlistAddCur, nullptr, 0);
  lv_label_set_text(items_.back().lbl,
                    currentRig_.empty() ? "Add current rig (save first)"
                                        : ("Add: " + currentRig_).c_str());

  for (int i = 0; i < (int)setlistRigs_.size(); i++) {
    addRow(scr, row++, ActLoadRig, nullptr, i);
    char b[48];
    snprintf(b, sizeof b, "%d. %s", i + 1, setlistRigs_[(size_t)i].name.c_str());
    lv_label_set_text(items_.back().lbl, b);
  }

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "select: load   FS2: remove from setlist");
  lv_obj_set_style_text_color(hint, kFaint, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// Performance mode: a big current-rig readout + position; Nav/Enc1 step prev/
// next (loading each rig), footswitches still toggle effects live, Back exits.
void UiController::buildPerf() {
  lv_obj_t* scr = newScreen();

  lv_obj_t* t = lv_label_create(scr);
  lv_label_set_text(t, openSetlist_.c_str());
  lv_obj_set_style_text_font(t, &lv_font_montserrat_20, 0);
  lv_obj_set_style_text_color(t, kAccent, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 8);

  int n = (int)setlistRigs_.size();
  const char* rigName =
      (perfIdx_ >= 0 && perfIdx_ < n) ? setlistRigs_[(size_t)perfIdx_].name.c_str() : "--";
  lv_obj_t* nm = lv_label_create(scr);
  lv_obj_set_style_text_font(nm, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(nm, kText, 0);
  lv_label_set_text(nm, rigName);
  lv_obj_align(nm, LV_ALIGN_CENTER, 0, -8);

  char pos[16];
  snprintf(pos, sizeof pos, "%d / %d", n ? perfIdx_ + 1 : 0, n);
  lv_obj_t* pl = lv_label_create(scr);
  lv_obj_set_style_text_font(pl, &lv_font_montserrat_16, 0);
  lv_obj_set_style_text_color(pl, kMuted, 0);
  lv_label_set_text(pl, pos);
  lv_obj_align(pl, LV_ALIGN_CENTER, 0, 28);

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "Nav / Enc1: prev / next      Back: exit");
  lv_obj_set_style_text_color(hint, kFaint, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -6);
}

void UiController::perfStep(int dir) {
  int n = (int)setlistRigs_.size();
  if (n == 0) return;
  perfIdx_ = std::clamp(perfIdx_ + (dir > 0 ? 1 : -1), 0, n - 1);
  loadRig(setlistRigs_[(size_t)perfIdx_].name);
  rebuild();
}

// Step prev/next through the saved-rig catalogue (the FS1+FS2 / FS3+FS4 combos).
// Wraps around; loads the first/last rig when nothing is currently loaded.
void UiController::stepRig(int dir) {
  std::vector<std::string> names = rigs::list(rigDir_);
  int n = (int)names.size();
  if (n == 0) return;
  int cur = -1;
  for (int i = 0; i < n; i++)
    if (names[(size_t)i] == currentRig_) { cur = i; break; }
  int next = (cur < 0) ? (dir > 0 ? 0 : n - 1)
                       : ((cur + (dir > 0 ? 1 : -1)) % n + n) % n;
  loadRig(names[(size_t)next]);
  rebuild();
}

// ---------------- event entry + per-frame refresh ----------------

void UiController::handle(const UiEvent& e) {
  if (tunerActive_) {
    // While the tuner owns the screen, the only interaction is to leave it.
    if (e.kind == UiEvent::NavSelect || e.kind == UiEvent::Back ||
        (e.kind == UiEvent::FsHold && e.value == 3))
      if (tuner_) tuner_->enabled.store(false);
    return;
  }
  if (page_ == NameEntryPage) {
    // Char-picker: Nav dials/advances, Enc1-push (Back) deletes, Enc2 moves the
    // caret, FS1 saves, FS2 cancels. (Nav-hold still quits the app in main.cpp.)
    switch (e.kind) {
      case UiEvent::NavRotate: nameTurn(e.value); break;
      case UiEvent::NavSelect: nameAdvance(); break;
      case UiEvent::Back:      nameBackspace(); break;
      case UiEvent::Enc2Turn:  nameMoveCaret(e.value); break;
      case UiEvent::Footswitch:
        if (e.value == 0)      commitName();
        else if (e.value == 1) cancelName();
        break;
      default: break;
    }
    return;
  }
  if (page_ == PerfPage) {
    // Performance mode: Nav/Enc1 step rigs; footswitches still toggle effects
    // live; Enc3 still rides master; Back exits to the setlist.
    switch (e.kind) {
      case UiEvent::NavRotate:  perfStep(e.value); break;
      case UiEvent::Enc1Turn:   perfStep(e.value); break;
      case UiEvent::Enc3Turn:   adjustMaster(e.value); break;
      case UiEvent::Back:       goTo(SetlistView); break;
      case UiEvent::Footswitch: toggleFootswitch(chain_, ctl_, e.value); break;
      case UiEvent::RigStep:    perfStep(e.value); break;
      default: break;
    }
    return;
  }
  switch (e.kind) {
    case UiEvent::NavRotate: moveFocus(e.value); break;
    case UiEvent::NavSelect: select(); break;
    case UiEvent::Back:      back(); break;
    case UiEvent::Enc1Turn:  onKnob(0, e.value); break;
    case UiEvent::Enc2Turn:  onKnob(1, e.value); break;
    case UiEvent::Enc3Turn:  onKnob(2, e.value); break;
    case UiEvent::FsHold:
      if (e.value == 3 && tuner_) tuner_->enabled.store(true);
      break;
    case UiEvent::Footswitch: onFootswitch(e.value); break;
    case UiEvent::RigStep:    stepRig(e.value); break;
    default: break;
  }
}

// A footswitch tap. In assignment mode it (re)binds the focused pedal; normally
// it latches the footswitch and drives every effect bound to it.
void UiController::onFootswitch(int fs) {
  if (fs < 0 || fs > 3) return;
  if (page_ == AssignPage) {
    if (focus_ >= 0 && focus_ < (int)items_.size() && items_[(size_t)focus_].fx) {
      int slot = items_[(size_t)focus_].idx;
      cycleAssign(items_[(size_t)focus_].fx, fs);
      rebuild();                       // refresh the tile's color/chip
      for (int i = 0; i < (int)items_.size(); i++)
        if (items_[(size_t)i].idx == slot && items_[(size_t)i].fx) { focus_ = i; break; }
      applyFocus();
    }
    return;
  }

  // List pages use FS1 = rename, FS2 = delete on the focused row (the same
  // footswitch-as-context-action idiom as the Assign page). FS3/FS4 are ignored.
  if (page_ == RigsPage || page_ == PresetPage || page_ == SetlistList ||
      page_ == SetlistView) {
    if (focus_ < 0 || focus_ >= (int)items_.size()) return;
    const FocusItem& it = items_[(size_t)focus_];
    if (page_ == RigsPage && it.action == ActLoadRig &&
        it.idx >= 0 && it.idx < (int)rigNames_.size()) {
      std::string nm = rigNames_[(size_t)it.idx];
      if (fs == 0) beginName(OpRigRename, nm, RigsPage, nullptr, nm);
      else if (fs == 1) { deleteRig(nm); rebuild(); }
    } else if (page_ == PresetPage && it.action == ActPresetLoad && current_ &&
               it.idx >= 0 && it.idx < (int)presetNames_.size()) {
      std::string nm = presetNames_[(size_t)it.idx];
      std::string kind = fxBaseKind(current_->type_id());
      if (fs == 0) beginName(OpPresetRename, nm, PresetPage, current_, nm);
      else if (fs == 1) {
        presets::remove(presetDir_, kind, nm);
        manifest::removeFile(baseDir_, presetDir_ + "/" + kind + "/" + nm + ".json");
        rebuild();
      }
    } else if (page_ == SetlistList && it.action == ActSetlistOpen &&
               it.idx >= 0 && it.idx < (int)setlistNames_.size()) {
      std::string nm = setlistNames_[(size_t)it.idx];
      if (fs == 0) beginName(OpSetlistRename, nm, SetlistList, nullptr, nm);
      else if (fs == 1) {
        setlists::remove(setlistDir_, nm);
        manifest::removeFile(baseDir_, setlistDir_ + "/" + nm + ".json");
        rebuild();
      }
    } else if (page_ == SetlistView && it.action == ActLoadRig && fs == 1 &&
               it.idx >= 0 && it.idx < (int)setlistRigs_.size()) {
      setlistRigs_.erase(setlistRigs_.begin() + it.idx);
      if (setlists::save(setlistDir_, openSetlist_, setlistRigs_))
        manifest::upsertFile(baseDir_, "setlist",
                             setlistDir_ + "/" + openSetlist_ + ".json");
      rebuild();
    }
    return;
  }

  toggleFootswitch(chain_, ctl_, fs);   // flip the shared latch + sync bound FX
}

// Cycle this effect's binding for one footswitch: unassigned (or bound to a
// different FS) -> normal -> inverted -> unassigned. Then sync its enabled state.
void UiController::cycleAssign(Effect* fx, int fs) {
  int cur = fx->fsAssign.load();
  if (cur != fs)            { fx->fsAssign.store(fs); fx->fsMode.store(0); }
  else if (fx->fsMode.load() == 0) { fx->fsMode.store(1); }
  else                      { fx->fsAssign.store(-1); fx->fsMode.store(0); }

  int a = fx->fsAssign.load();
  if (a >= 0) fx->enabled.store(ctl_.fsEngaged[a].load() ^ (fx->fsMode.load() == 1));
}

// Light NeoPixels 0..3 in each footswitch's color -- always lit so the colors
// stay readable: bright when that switch is engaged, dim when off. The two top
// LEDs are input-level meters (green/orange/red): LED 5 = input 1 (L), LED 4 =
// input 2 (R), reading the analog detector on hardware or the digital audio peak
// in the sim. Only pushes a frame when something changed, so the LCD and ADC
// don't fight over the SPI bus every tick.
void UiController::updateLeds(Leds& leds) {
  // Advance both input meters every tick (the averaging windows need it), whether
  // or not we end up pushing a frame.
  const bool useAdc = inputLevel_ && inputLevel_->available();
  VuState vs[2];
  for (int i = 0; i < 2; i++) {
    float amp;
    VuThresholds th;
    if (useAdc) {
      int raw = inputLevel_->read(i);
      if (raw < 0) raw = 512;                          // read error -> treat as silent
      amp = std::fabs(512.0f - (float)raw) + 512.0f;   // rectify about baseline (analogVU)
      th = adcThresholds();                            // TODO: bias by ALSA capture volume
    } else {
      amp = ctl_.inPeak[i].exchange(0.0f, std::memory_order_relaxed);  // read-and-clear peak-hold
      th = digitalThresholds();
    }
    vs[i] = vu_[i].update(amp, th);
  }

  // Change-detector: footswitch states (bits 0..3) + the two meter states (2 bits
  // each at bits 4..7).
  uint32_t sig = 0;
  for (int fs = 0; fs < 4; fs++)
    if (ctl_.fsEngaged[fs].load()) sig |= 1u << fs;
  sig |= (uint32_t)vs[0] << 4;
  sig |= (uint32_t)vs[1] << 6;
  if (sig == ledSig_) return;
  ledSig_ = sig;

  // Scale each color to the on/off status level (the byte we write is the
  // actual brightness now). Proportional scaling keeps the hue, so a dimmed
  // two-channel color like yellow stays yellow instead of dropping a channel.
  auto lvl = [](uint8_t v, int pct) -> uint8_t { return (uint8_t)(v * pct / 100); };

  leds.clear();
  for (int fs = 0; fs < 4; fs++) {
    const FsColor& c = kFsColors[fs];
    int pct = ctl_.fsEngaged[fs].load() ? kLedOnPct : kLedDimPct;
    leds.set(fs, lvl(c.r, pct), lvl(c.g, pct), lvl(c.b, pct));
  }
  // Input-level meters (already brightness-scaled inside VuMeter::color).
  uint8_t r, g, b;
  VuMeter::color(vs[0], r, g, b); leds.set(5, r, g, b);   // input 1 / L
  VuMeter::color(vs[1], r, g, b); leds.set(4, r, g, b);   // input 2 / R
  leds.show();
}

void UiController::setEffectRowText(const FocusItem& it) {
  if (!it.fx || !it.lbl) return;
  bool on = it.fx->enabled.load();
  std::string s = it.fx->name();
  s += on ? "   [ON]" : "   [off]";
  lv_label_set_text(it.lbl, s.c_str());
  lv_obj_set_style_text_color(it.lbl, on ? kText : kMuted, 0);
}

void UiController::refreshTuner() {
  if (!tuner_) return;
  int note = tuner_->noteIndex();
  if (note < 0) {
    lv_label_set_text(tNote_, "--");
    lv_obj_set_style_text_color(tNote_, kFaint, 0);
    lv_bar_set_value(tCents_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(tCents_, kFaint, LV_PART_INDICATOR);
    lv_label_set_text(tInfo_, "play a note...");
    return;
  }
  float cents = tuner_->cents();
  bool inTune = std::fabs(cents) <= 5.0f;
  bool close = std::fabs(cents) <= 15.0f;
  lv_color_t col = inTune ? kGood : (close ? kWarn : kBad);

  char nm[8];
  snprintf(nm, sizeof nm, "%s%d", kNoteNames[note], tuner_->octave());
  lv_label_set_text(tNote_, nm);
  lv_obj_set_style_text_color(tNote_, col, 0);
  lv_bar_set_value(tCents_, (int)lroundf(cents), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(tCents_, col, LV_PART_INDICATOR);

  char buf[48];
  snprintf(buf, sizeof buf, "%+d cents   %.1f Hz", (int)lroundf(cents), tuner_->freqHz());
  lv_label_set_text(tInfo_, buf);
}

void UiController::refresh() {
  bool eng = tuner_ && tuner_->engaged();
  if (eng && !tunerActive_)      { tunerActive_ = true;  buildTuner(); }
  else if (!eng && tunerActive_) { tunerActive_ = false; rebuild(); }
  if (tunerActive_) { refreshTuner(); return; }

  char buf[48];
  switch (page_) {
    case Home:
      if (homeMaster_) {
        snprintf(buf, sizeof buf, "Master: %d%%",
                 (int)lroundf(ctl_.masterLevel.load() * 100.0f));
        lv_label_set_text(homeMaster_, buf);
      }
      if (homeRig_) {
        std::string s = currentRig_.empty()
                            ? "- No Rig -"
                            : currentRig_ + (rigDirty() ? " *" : "");
        lv_label_set_text(homeRig_, s.c_str());
      }
      // FX tiles show only the name; reflect live on/off as a color change.
      for (auto& it : items_)
        if (it.action == ActOpenEffect && it.fx && it.lbl)
          lv_obj_set_style_text_color(it.lbl,
              it.fx->enabled.load() ? kText : kMuted, 0);
      break;

    case InputList:
    case OutputList:
      for (auto& it : items_)
        if (it.action == ActOpenEffect) setEffectRowText(it);
      break;

    case HintPage:
    case FxPicker:
    case AssignPage:
      break;

    case MenuPage:
      for (auto& it : items_) {
        if (it.action == ActToggleBypass) {
          snprintf(buf, sizeof buf, "Bypass: %s", ctl_.bypassed.load() ? "ON" : "off");
          lv_label_set_text(it.lbl, buf);
        } else if (it.action == ActToggleTuner) {
          snprintf(buf, sizeof buf, "Tuner: %s",
                   (tuner_ && tuner_->engaged()) ? "ON" : "off");
          lv_label_set_text(it.lbl, buf);
        }
      }
      break;

    case PedalControl:
      if (current_ && powerLbl_) {
        snprintf(buf, sizeof buf, "Power: %s", current_->enabled.load() ? "ON" : "off");
        lv_label_set_text(powerLbl_, buf);
      }
      if (current_) {
        int total = (int)current_->params.size();
        for (int k = 0; k < 3; k++) {
          int pi = paramBase_ + k;
          if (pi >= total || !ctlVal_[k]) continue;
          Param* p = current_->params[(size_t)pi].get();
          char vb[32];
          fmtParam(vb, sizeof vb, p);
          lv_label_set_text(ctlVal_[k], vb);
          if (ctlArc_[k]) lv_arc_set_value(ctlArc_[k], paramPct(p));
        }
      }
      break;

    case MasterControl:
      if (masterVal_) {
        int pct = (int)lroundf(ctl_.masterLevel.load() * 100.0f);
        snprintf(buf, sizeof buf, "%d%%", pct);
        lv_label_set_text(masterVal_, buf);
        if (masterBar_) lv_bar_set_value(masterBar_, pct, LV_ANIM_OFF);
      }
      break;

    // These pages have no continuously-changing values; they refresh on rebuild.
    case NameEntryPage:
    case RigsPage:
    case PresetPage:
    case SetlistList:
    case SetlistView:
    case PerfPage:
      break;
  }
}
