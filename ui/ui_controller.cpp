// ui/ui_controller.cpp -- see ui_controller.h.

#include "ui_controller.h"

#include "../chain.h"
#include "../effect.h"
#include "../footswitch_control.h"
#include "../fx_factory.h"
#include "../pedal_controls.h"
#include "../presets.h"
#include "../effects/tuner.h"

#include "leds.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {

// palette (matches the old pedal_ui status screen)
const lv_color_t kCyan = LV_COLOR_MAKE(0x00, 0xC8, 0xFF);
const lv_color_t kWhite = LV_COLOR_MAKE(0xFF, 0xFF, 0xFF);
const lv_color_t kGray = LV_COLOR_MAKE(0x90, 0x90, 0x90);
const lv_color_t kDim = LV_COLOR_MAKE(0x60, 0x60, 0x60);
const lv_color_t kBoxBg = LV_COLOR_MAKE(0x12, 0x16, 0x19);
const lv_color_t kBorder = LV_COLOR_MAKE(0x2A, 0x31, 0x38);
const lv_color_t kGood = LV_COLOR_MAKE(0x30, 0xFF, 0x30);
const lv_color_t kWarn = LV_COLOR_MAKE(0xFF, 0xC0, 0x00);
const lv_color_t kBad = LV_COLOR_MAKE(0xFF, 0x30, 0x30);

constexpr float kMasterMax = 2.0f;
constexpr float kMasterStep = 0.05f;

// Footswitch palette (1-indexed for display): FS1 red, FS2 green, FS3 blue,
// FS4 yellow -- matching the NeoPixels 0..3 under each stomp.
struct FsColor { uint8_t r, g, b; const char* name; };
const FsColor kFsColors[4] = {
  {0xFF, 0x30, 0x30, "FS1"},   // red
  {0x30, 0xFF, 0x30, "FS2"},   // green
  {0x40, 0x80, 0xFF, "FS3"},   // blue
  {0xFF, 0xC0, 0x00, "FS4"},   // yellow
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

// A styled, non-scrolling container used for every box/row.
lv_obj_t* mkContainer(lv_obj_t* parent, int w, int h) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_set_size(o, w, h);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(o, kBoxBg, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, kBorder, 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_radius(o, 4, 0);
  lv_obj_set_style_pad_all(o, 4, 0);
  return o;
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
                          std::string presetDir)
    : chain_(chain), ctl_(ctl), factory_(factory), tuner_(tuner),
      ampName_(std::move(ampName)), presetDir_(std::move(presetDir)) {}

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
  homeMaster_ = masterBar_ = masterVal_ = powerLbl_ = nullptr;
  for (int k = 0; k < 3; k++) ctlName_[k] = ctlVal_[k] = ctlBar_[k] = nullptr;

  switch (page_) {
    case Home:          buildHome();         break;
    case InputList:
    case OutputList:    buildList(page_);    break;
    case FxGrid:        buildFxGrid();       break;
    case FxPicker:      buildFxPicker();     break;
    case AssignPage:    buildAssign();       break;
    case MenuPage:      buildMenu();         break;
    case PedalControl:  buildPedalControl(); break;
    case MasterControl: buildMasterControl();break;
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
    lv_obj_set_style_border_color(items_[i].obj, f ? kCyan : kBorder, 0);
    lv_obj_set_style_border_width(items_[i].obj, f ? 2 : 1, 0);
  }
}

void UiController::select() {
  if (focus_ < 0 || focus_ >= (int)items_.size()) return;
  FocusItem it = items_[focus_];
  switch (it.action) {
    case ActBack:        back(); break;
    case ActGotoInput:   goTo(InputList); break;
    case ActGotoFx:      goTo(FxGrid); break;
    case ActGotoOutput:  goTo(OutputList); break;
    case ActGotoMenu:    goTo(MenuPage); break;
    case ActOpenEffect:  paramBase_ = 0; goTo(PedalControl, it.fx); break;
    case ActOpenMaster:  goTo(MasterControl); break;
    case ActGotoAssign:  goTo(AssignPage); break;
    case ActOpenPicker:  pickerSlot_ = it.idx; goTo(FxPicker); break;
    case ActAddFx:
      if (pickerSlot_ >= 0)
        chain_.fxPlace(pickerSlot_, factory_.create((size_t)it.idx));
      goTo(FxGrid);
      break;
    case ActRemoveFx: {
      int slot = current_ ? chain_.fxSlotOf(current_) : -1;
      if (slot >= 0) chain_.fxRemove(slot);
      goTo(FxGrid);
      break;
    }
    case ActTogglePower:
      if (current_) current_->enabled.store(!current_->enabled.load());
      break;
    case ActToggleBypass: ctl_.bypassed.store(!ctl_.bypassed.load()); break;
    case ActToggleTuner:
      if (tuner_) tuner_->enabled.store(!tuner_->engaged());
      break;
    case ActLoadPreset:
      if (it.idx >= 0 && it.idx < (int)presetNames_.size())
        presets::load(presetDir_, presetNames_[it.idx], chain_, ctl_, factory_);
      break;
    case ActParamBank:
      if (current_) {
        int total = (int)current_->params.size();
        paramBase_ = (paramBase_ + 3 < total) ? paramBase_ + 3 : 0;
        rebuild();
      }
      break;
    case ActNone: break;
  }
}

void UiController::back() {
  switch (page_) {
    case Home: break;
    case InputList:
    case FxGrid:
    case OutputList:
    case MenuPage:      goTo(Home); break;
    case FxPicker:
    case AssignPage:    goTo(FxGrid); break;
    case MasterControl: goTo(OutputList); break;
    case PedalControl: {
      Page parent = Home;
      if (current_) {
        if (current_->section == Section::Input)  parent = InputList;
        else if (current_->section == Section::Output) parent = OutputList;
        else parent = FxGrid;
      }
      goTo(parent);
      break;
    }
  }
}

void UiController::onKnob(int which, int dir) {
  // FX grid: Enc1 turn moves the focused pedal within the order (Enc3 still
  // adjusts master via the fallback below).
  if (page_ == FxGrid && which == 0 &&
      focus_ >= 0 && focus_ < (int)items_.size()) {
    FocusItem& it = items_[focus_];
    if (it.action == ActOpenEffect && it.fx) {
      int from = it.idx;
      int to = from + (dir < 0 ? -1 : 1);
      chain_.fxMove(from, dir);   // no-op at the grid edge
      if (to < 0 || to >= chain_.fxSlotCount()) to = from;
      rebuild();
      // follow the pedal to its new slot (a tile's idx is its slot index)
      for (int i = 0; i < (int)items_.size(); i++)
        if (items_[i].obj && items_[i].idx == to &&
            (items_[i].action == ActOpenEffect || items_[i].action == ActOpenPicker)) {
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
  float m = ctl_.masterLevel.load() + dir * kMasterStep;
  ctl_.masterLevel.store(std::clamp(m, 0.0f, kMasterMax));
}

void UiController::stepParam(Effect* fx, int paramIdx, int dir) {
  Param* p = fx->params[paramIdx].get();
  float range = p->max - p->min;
  float v = p->get();
  bool enumLike = p->unit.empty() && range >= 1.0f && range <= 3.0f &&
                  p->min == std::floor(p->min) && p->max == std::floor(p->max);
  if (enumLike)         v = std::round(v) + dir;
  else if (range > 0)   v += dir * (range / 50.0f);
  p->set(v);   // clamps internally
}

// ---------------- builders ----------------

lv_obj_t* UiController::newScreen() {
  lv_obj_t* scr = lv_screen_active();
  lv_obj_clean(scr);
  lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  return scr;
}

void UiController::topBar(lv_obj_t* scr, const char* title) {
  lv_obj_t* back = mkContainer(scr, 64, 24);
  lv_obj_align(back, LV_ALIGN_TOP_LEFT, 6, 6);
  lv_obj_t* bl = lv_label_create(back);
  lv_label_set_text(bl, "< Back");
  lv_obj_set_style_text_color(bl, kWhite, 0);
  lv_obj_align(bl, LV_ALIGN_LEFT_MID, 0, 0);
  items_.push_back({back, bl, ActBack, nullptr, 0});

  lv_obj_t* t = lv_label_create(scr);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_color(t, kCyan, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);
}

lv_obj_t* UiController::addRow(lv_obj_t* scr, int rowIndex, Action a, Effect* fx, int idx) {
  lv_obj_t* o = mkContainer(scr, kRowW, kRowH);
  lv_obj_align(o, LV_ALIGN_TOP_LEFT, kRowX, kRowY0 + rowIndex * kRowStep);
  lv_obj_t* l = lv_label_create(o);
  lv_obj_set_style_text_color(l, kWhite, 0);
  lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
  items_.push_back({o, l, a, fx, idx});
  return o;
}

void UiController::buildHome() {
  lv_obj_t* scr = newScreen();

  lv_obj_t* title = lv_label_create(scr);
  lv_label_set_text(title, "PISTOMP");
  lv_obj_set_style_text_color(title, kCyan, 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 8);

  const char* names[3] = {"INPUT", "FX", "OUTPUT"};
  Action acts[3] = {ActGotoInput, ActGotoFx, ActGotoOutput};
  int xs[3] = {-104, 0, 104};
  for (int i = 0; i < 3; i++) {
    lv_obj_t* o = mkContainer(scr, 92, 96);
    lv_obj_align(o, LV_ALIGN_CENTER, xs[i], -4);
    lv_obj_t* l = lv_label_create(o);
    lv_label_set_text(l, names[i]);
    lv_obj_set_style_text_color(l, kWhite, 0);
    lv_obj_center(l);
    items_.push_back({o, l, acts[i], nullptr, 0});
  }

  lv_obj_t* menu = mkContainer(scr, 70, 24);
  lv_obj_align(menu, LV_ALIGN_TOP_RIGHT, -6, 6);
  lv_obj_t* ml = lv_label_create(menu);
  lv_label_set_text(ml, "MENU");
  lv_obj_set_style_text_color(ml, kWhite, 0);
  lv_obj_center(ml);
  items_.push_back({menu, ml, ActGotoMenu, nullptr, 0});

  homeMaster_ = lv_label_create(scr);
  lv_obj_set_style_text_color(homeMaster_, kGray, 0);
  lv_obj_align(homeMaster_, LV_ALIGN_BOTTOM_MID, 0, -6);
  lv_label_set_text(homeMaster_, "Master: --");
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

// The reorderable FX region: a fixed 4x2 grid of positional slots. Occupied
// slots are pedal tiles (signal flows through them in slot order); empty slots
// are "+" tiles that open the picker. Select a pedal tile to edit it; turn Enc1
// on a focused pedal tile to move it (handled in onKnob). The tile's slot index
// is carried in FocusItem::idx.
void UiController::buildFxGrid() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "FX");

  // top-bar right: enter footswitch-assignment mode for these pedals.
  lv_obj_t* asg = mkContainer(scr, 76, 24);
  lv_obj_align(asg, LV_ALIGN_TOP_RIGHT, -6, 6);
  lv_obj_t* al = lv_label_create(asg);
  lv_label_set_text(al, "Assign");
  lv_obj_set_style_text_color(al, kWhite, 0);
  lv_obj_center(al);
  items_.push_back({asg, al, ActGotoAssign, nullptr, 0});

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
    if (Effect* fx = chain_.fxAt(slot)) {
      bool on = fx->enabled.load();
      lv_label_set_text(l, fx->name().c_str());
      lv_obj_set_style_text_color(l, on ? kWhite : kGray, 0);
      tileBinding(o, fx);
      items_.push_back({o, l, ActOpenEffect, fx, slot});
    } else {
      lv_label_set_text(l, "+");
      lv_obj_set_style_text_color(l, kDim, 0);
      items_.push_back({o, l, ActOpenPicker, nullptr, slot});
    }
  }

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "K1 turn = move pedal");
  lv_obj_set_style_text_color(hint, kDim, 0);
  lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -4);
}

// Tint a pedal tile and add a small color chip showing which footswitch drives
// it (and whether the binding is inverted). No-op for unbound effects.
void UiController::tileBinding(lv_obj_t* tile, Effect* fx) {
  if (!fx) return;
  int fs = fx->fsAssign.load();
  if (fs < 0 || fs > 3) return;
  const FsColor& c = kFsColors[fs];
  lv_obj_set_style_bg_color(tile, LV_COLOR_MAKE(c.r / 6, c.g / 6, c.b / 6), 0);
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
      lv_obj_set_style_text_color(l, kWhite, 0);
      tileBinding(o, fx);
    } else {
      lv_label_set_text(l, "-");
      lv_obj_set_style_text_color(l, kDim, 0);
    }
    // fx may be null for empty slots; onFootswitch ignores those.
    items_.push_back({o, l, ActNone, fx, slot});
  }

  lv_obj_t* hint = lv_label_create(scr);
  lv_label_set_text(hint, "press a footswitch: bind / invert / clear");
  lv_obj_set_style_text_color(hint, kDim, 0);
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

  presetNames_ = presets::list(presetDir_);
  for (int i = 0; i < (int)presetNames_.size(); i++) {
    addRow(scr, row++, ActLoadPreset, nullptr, i);
    std::string s = "Load: " + presetNames_[i];
    lv_label_set_text(items_.back().lbl, s.c_str());
  }
}

void UiController::buildPedalControl() {
  lv_obj_t* scr = newScreen();
  topBar(scr, current_ ? current_->name().c_str() : "");
  if (!current_) return;

  addRow(scr, 0, ActTogglePower, current_, 0);
  powerLbl_ = items_.back().lbl;

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

  int total = (int)current_->params.size();
  for (int k = 0; k < 3; k++) {
    int pi = paramBase_ + k;
    if (pi >= total) break;
    Param* p = current_->params[pi].get();
    int y = 74 + k * 44;

    lv_obj_t* nm = lv_label_create(scr);
    char nb[48];
    snprintf(nb, sizeof nb, "K%d  %s", k + 1, p->name.c_str());
    lv_label_set_text(nm, nb);
    lv_obj_set_style_text_color(nm, kCyan, 0);
    lv_obj_align(nm, LV_ALIGN_TOP_LEFT, kRowX + 4, y);
    ctlName_[k] = nm;

    lv_obj_t* vl = lv_label_create(scr);
    lv_obj_set_style_text_color(vl, kWhite, 0);
    lv_obj_align(vl, LV_ALIGN_TOP_RIGHT, -(kRowX + 4), y);
    ctlVal_[k] = vl;

    lv_obj_t* bar = lv_bar_create(scr);
    lv_obj_set_size(bar, kRowW - 8, 8);
    lv_obj_align(bar, LV_ALIGN_TOP_LEFT, kRowX + 4, y + 22);
    lv_bar_set_range(bar, 0, 100);
    ctlBar_[k] = bar;
  }

  if (total > 3) {
    lv_obj_t* o = mkContainer(scr, 140, 24);
    lv_obj_align(o, LV_ALIGN_BOTTOM_RIGHT, -6, -8);
    lv_obj_t* l = lv_label_create(o);
    char b[40];
    int last = std::min(paramBase_ + 3, total);
    snprintf(b, sizeof b, "params %d-%d  >", paramBase_ + 1, last);
    lv_label_set_text(l, b);
    lv_obj_set_style_text_color(l, kWhite, 0);
    lv_obj_center(l);
    items_.push_back({o, l, ActParamBank, current_, 0});
  }
}

void UiController::buildMasterControl() {
  lv_obj_t* scr = newScreen();
  topBar(scr, "OUTPUT LEVEL");

  masterVal_ = lv_label_create(scr);
  lv_obj_set_style_text_font(masterVal_, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(masterVal_, kWhite, 0);
  lv_obj_align(masterVal_, LV_ALIGN_CENTER, 0, -24);

  masterBar_ = lv_bar_create(scr);
  lv_obj_set_size(masterBar_, 280, 16);
  lv_obj_align(masterBar_, LV_ALIGN_CENTER, 0, 16);
  lv_bar_set_range(masterBar_, 0, 200);
}

void UiController::buildTuner() {
  // The takeover replaces the current page's widgets; drop their (now-deleted)
  // focus items so nothing stale can be referenced while tuning.
  items_.clear();
  focus_ = 0;
  lv_obj_t* scr = newScreen();
  lv_obj_t* t = lv_label_create(scr);
  lv_label_set_text(t, "TUNER");
  lv_obj_set_style_text_color(t, kCyan, 0);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 10);

  tNote_ = lv_label_create(scr);
  lv_obj_set_style_text_font(tNote_, &lv_font_montserrat_28, 0);
  lv_obj_set_style_text_color(tNote_, kWhite, 0);
  lv_obj_align(tNote_, LV_ALIGN_CENTER, 0, -24);
  lv_label_set_text(tNote_, "--");

  tCents_ = lv_bar_create(scr);
  lv_obj_set_size(tCents_, 288, 18);
  lv_obj_align(tCents_, LV_ALIGN_CENTER, 0, 16);
  lv_bar_set_mode(tCents_, LV_BAR_MODE_SYMMETRICAL);
  lv_bar_set_range(tCents_, -50, 50);
  lv_bar_set_value(tCents_, 0, LV_ANIM_OFF);

  tInfo_ = lv_label_create(scr);
  lv_obj_set_style_text_color(tInfo_, kGray, 0);
  lv_obj_align(tInfo_, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_label_set_text(tInfo_, "play a note...");
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
    default: break;
  }
}

// A footswitch tap. In assignment mode it (re)binds the focused pedal; normally
// it latches the footswitch and drives every effect bound to it.
void UiController::onFootswitch(int fs) {
  if (fs < 0 || fs > 3) return;
  if (page_ == AssignPage) {
    if (focus_ >= 0 && focus_ < (int)items_.size() && items_[focus_].fx) {
      int slot = items_[focus_].idx;
      cycleAssign(items_[focus_].fx, fs);
      rebuild();                       // refresh the tile's color/chip
      for (int i = 0; i < (int)items_.size(); i++)
        if (items_[i].idx == slot && items_[i].fx) { focus_ = i; break; }
      applyFocus();
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
// stay readable: bright when that switch is engaged, dim when off. LED 5 mirrors
// the global bypass (red). Only pushes a frame when something changed, so the
// LCD and footswitch ADC don't fight over the SPI bus every tick.
void UiController::updateLeds(Leds& leds) {
  bool bypassed = ctl_.bypassed.load();

  uint32_t sig = bypassed ? (1u << 4) : 0u;
  for (int fs = 0; fs < 4; fs++)
    if (ctl_.fsEngaged[fs].load()) sig |= 1u << fs;
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
  if (bypassed) leds.set(5, lvl(0xFF, kLedOnPct), 0, 0);   // spare LED = global bypass indicator
  leds.show();
}

void UiController::setEffectRowText(const FocusItem& it) {
  if (!it.fx || !it.lbl) return;
  bool on = it.fx->enabled.load();
  std::string s = it.fx->name();
  s += on ? "   [ON]" : "   [off]";
  lv_label_set_text(it.lbl, s.c_str());
  lv_obj_set_style_text_color(it.lbl, on ? kWhite : kGray, 0);
}

void UiController::refreshTuner() {
  if (!tuner_) return;
  int note = tuner_->noteIndex();
  if (note < 0) {
    lv_label_set_text(tNote_, "--");
    lv_obj_set_style_text_color(tNote_, kDim, 0);
    lv_bar_set_value(tCents_, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(tCents_, kDim, LV_PART_INDICATOR);
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
      break;

    case InputList:
    case OutputList:
      for (auto& it : items_)
        if (it.action == ActOpenEffect) setEffectRowText(it);
      break;

    case FxGrid:
      // Tiles show only the name; reflect live on/off as a color change.
      for (auto& it : items_)
        if (it.action == ActOpenEffect && it.fx && it.lbl)
          lv_obj_set_style_text_color(it.lbl,
              it.fx->enabled.load() ? kWhite : kGray, 0);
      break;

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
          Param* p = current_->params[pi].get();
          char vb[32];
          fmtParam(vb, sizeof vb, p);
          lv_label_set_text(ctlVal_[k], vb);
          if (ctlBar_[k]) lv_bar_set_value(ctlBar_[k], paramPct(p), LV_ANIM_OFF);
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
  }
}
