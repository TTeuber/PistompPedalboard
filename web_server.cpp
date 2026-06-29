// web_server.cpp -- see web_server.h.

#include "web_server.h"

#include "chain.h"
#include "effect.h"
#include "footswitch_control.h"
#include "fx_factory.h"
#include "fx_id.h"
#include "pedal_controls.h"
#include "manifest.h"
#include "rigs.h"
#include "presets.h"
#include "setlists.h"
#include "effects/comp.h"
#include "effects/gate.h"
#include "effects/tuner.h"

#include <httplib.h>
#include <json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using nlohmann::json;

namespace {

// Linear peak (0..1) -> dBFS, floored so silence reads a finite meter bottom
// rather than -inf. The web meters use a -60 dB floor for their scale.
double dbfs(float peak) {
  return 20.0 * std::log10(std::max(peak, 1e-3f));  // 1e-3 = -60 dBFS
}

const char* sectionName(Section s) {
  switch (s) {
    case Section::Input:  return "input";
    case Section::Fx:     return "fx";
    case Section::Output: return "output";
    case Section::Hidden: return "hidden";
  }
  return "fx";
}

// Full state WITH metadata, so the browser can build its own UI: per-effect
// section + FX-grid slot + footswitch assignment, plus the grid size and the
// pickable FX kinds. The browser mirrors the device's Input / FX / Output layout.
json fullState(const Chain& chain, const PedalControls& ctl,
               const FxFactory& factory) {
  json doc;
  doc["master"] = ctl.masterLevel.load();
  doc["bypassed"] = ctl.bypassed.load();
  doc["fxSlotCount"] = Chain::kFxSlots;

  // Latched footswitch state, FS1..FS4 -- so the browser can show + toggle the
  // same switches the device has (the LCD/LEDs and the web share one truth).
  json fsw = json::array();
  for (int i = 0; i < 4; i++) fsw.push_back(ctl.fsEngaged[i].load());
  doc["footswitches"] = fsw;

  json order = json::array();
  json effects = json::array();
  for (const auto& fx : chain.effects()) {
    order.push_back(fx->type_id());
    json e;
    e["type"] = fx->type_id();
    e["name"] = fx->name();
    e["enabled"] = fx->enabled.load();
    e["section"] = sectionName(fx->section);
    e["slot"] = chain.fxSlotOf(fx);        // -1 unless it lives in the FX grid
    e["fsAssign"] = fx->fsAssign.load();   // -1 = unassigned, else 0..3
    e["fsMode"] = fx->fsMode.load();       // 0 normal, 1 inverted
    json params = json::array();
    for (const auto& p : fx->params) {
      params.push_back({{"id", p->id},
                        {"name", p->name},
                        {"unit", p->unit},
                        {"min", p->min},
                        {"max", p->max},
                        {"def", p->def},
                        {"value", p->get()}});
    }
    e["params"] = params;
    effects.push_back(e);
  }
  doc["order"] = order;     // chain order (data, not hardcoded -> reorderable)
  doc["effects"] = effects;

  json kinds = json::array();
  for (const auto& k : factory.kinds())
    kinds.push_back({{"type", k.type}, {"name", k.name}});
  doc["fxKinds"] = kinds;   // the picker's menu of placeable effects
  return doc;
}

// Parse a JSON request body; returns false (and 400s) on malformed input.
bool parseBody(const httplib::Request& req, httplib::Response& res, json& out) {
  try {
    out = json::parse(req.body);
    return true;
  } catch (const std::exception&) {
    res.status = 400;
    res.set_content("{\"error\":\"bad json\"}", "application/json");
    return false;
  }
}

void ok(httplib::Response& res) {
  res.set_content("{\"ok\":true}", "application/json");
}

}  // namespace

WebServer::WebServer(Chain& chain, PedalControls& ctl, FxFactory& factory,
                     std::string webDir, std::string rigDir,
                     std::string presetDir, std::string setlistDir,
                     std::string baseDir)
    : chain_(chain), ctl_(ctl), factory_(factory), webDir_(std::move(webDir)),
      rigDir_(std::move(rigDir)), presetDir_(std::move(presetDir)),
      setlistDir_(std::move(setlistDir)), baseDir_(std::move(baseDir)),
      svr_(std::make_unique<httplib::Server>()) {
}

WebServer::~WebServer() { stop(); }

void WebServer::setupRoutes() {
  // Static frontend (index.html, app.js, style.css) from the deployed web dir.
  if (!webDir_.empty()) svr_->set_mount_point("/", webDir_);

  svr_->Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  svr_->Get("/api/telemetry", [this](const httplib::Request&, httplib::Response& res) {
    json t = {{"dspPermille", ctl_.dspPermille.load()},
              {"xruns", ctl_.xruns.load()}};
    res.set_content(t.dump(), "application/json");
  });

  // Live audio levels for the input/output meters. Like telemetry, these change
  // every audio block, so they ride a fast poll -- NOT the SSE state stream
  // (which would re-render the whole board). Every read CLEARS the peak holds so
  // the meters fall back naturally between polls (and a disabled gate/comp decays
  // to zero reduction). inputDb/outputDbL/outputDbR are dBFS (-60 floor); grDb is
  // the shared input-section gain reduction: gate + comp summed. Input is mono at
  // the meter tap (louder channel); the output is stereo, so it gets L/R bars.
  svr_->Get("/api/meters", [this](const httplib::Request&, httplib::Response& res) {
    float inPk  = std::max(ctl_.inPeakWeb[0].exchange(0.0f), ctl_.inPeakWeb[1].exchange(0.0f));
    float outL  = ctl_.outPeak[0].exchange(0.0f);
    float outR  = ctl_.outPeak[1].exchange(0.0f);
    float gr = 0.0f;
    if (auto* g = dynamic_cast<fx::Gate*>(chain_.find("gate"))) gr += g->takeGrDb();
    if (auto* c = dynamic_cast<fx::Comp*>(chain_.find("comp"))) gr += c->takeGrDb();
    json m = {{"inputDb", dbfs(inPk)},
              {"outputDbL", dbfs(outL)}, {"outputDbR", dbfs(outR)}, {"grDb", gr}};
    res.set_content(m.dump(), "application/json");
  });

  // Live tuner readout. The tuner is a hidden chain effect that publishes pitch
  // atomics (the UI loop pumps analyze()); the web tuner modal polls this a few
  // times a second while open. note = 0..11 (C..B), -1 = no confident pitch.
  svr_->Get("/api/tuner", [this](const httplib::Request&, httplib::Response& res) {
    auto* t = static_cast<fx::Tuner*>(chain_.find("tuner"));
    json j = t ? json{{"engaged", t->engaged()}, {"note", t->noteIndex()},
                      {"octave", t->octave()}, {"cents", t->cents()}, {"freq", t->freqHz()}}
               : json{{"engaged", false}, {"note", -1}, {"octave", 0}, {"cents", 0.0}, {"freq", 0.0}};
    res.set_content(j.dump(), "application/json");
  });

  // {effect, param, value}
  svr_->Post("/api/param", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    Effect* fx = chain_.find(b.value("effect", std::string{}));
    Param* p = fx ? fx->param(b.value("param", std::string{})) : nullptr;
    if (!p || !b.contains("value")) { res.status = 404; return; }
    p->set((float)b["value"].get<double>());
    ok(res);
  });

  // {effect, enabled}
  svr_->Post("/api/enabled", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    Effect* fx = chain_.find(b.value("effect", std::string{}));
    if (!fx) { res.status = 404; return; }
    fx->enabled.store(b.value("enabled", true));
    ok(res);
  });

  // {value}
  svr_->Post("/api/master", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    ctl_.masterLevel.store((float)b.value("value", 1.0));
    ok(res);
  });

  // {bypassed}
  svr_->Post("/api/bypass", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    ctl_.bypassed.store(b.value("bypassed", false));
    ok(res);
  });

  // ---- FX grid editing (mirrors the device's FxGrid page) ------------------
  // All three return the full state so the browser re-renders the grid exactly
  // as the chain now is. Chain edit methods are internally synchronized.

  // {slot, kind, preset?}  -- kind = index into fxKinds; mints a fresh instance.
  // An optional `preset` name voices the new pedal from a saved knob snapshot for
  // its kind, so the picker can place + voice in one atomic step.
  svr_->Post("/api/fx/add", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    int slot = b.value("slot", -1);
    size_t kind = (size_t)b.value("kind", -1);
    auto fx = factory_.create(kind);
    if (slot < 0 || slot >= Chain::kFxSlots || !fx) { res.status = 400; return; }
    chain_.fxPlace(slot, std::move(fx));
    std::string preset = b.value("preset", std::string{});
    if (!preset.empty())
      if (Effect* placed = chain_.fxAt(slot))
        presets::load(presetDir_, fxBaseKind(placed->type_id()), preset, *placed);
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // {slot}
  svr_->Post("/api/fx/remove", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    chain_.fxRemove(b.value("slot", -1));
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // {slot, dir}  -- dir < 0 toward front, dir > 0 toward back.
  svr_->Post("/api/fx/move", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    chain_.fxMove(b.value("slot", -1), b.value("dir", 0));
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // {slot, to}  -- drag-and-drop reorder: move the effect in `slot` to sit
  // before `to`, repacking the chain (an empty/-1 `to` appends).
  svr_->Post("/api/fx/reorder", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    chain_.fxReorder(b.value("slot", -1), b.value("to", -1));
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // {slot, to}  -- free-form drag-and-drop: drop `slot` onto `to`, preserving
  // exact cell positions (move into an empty cell, swap with an occupied one).
  // Unlike /reorder this never repacks, so the grid keeps the user's gaps.
  svr_->Post("/api/fx/moveto", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    chain_.fxMoveTo(b.value("slot", -1), b.value("to", -1));
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // {effect, fs, mode}  -- fs 0..3 (or -1 to clear), mode 0 normal / 1 inverted.
  // Mirrors the device assign page; keeps `enabled` consistent with the binding
  // (footswitches default to engaged, so normal = on, inverted = off).
  svr_->Post("/api/assign", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    Effect* fx = chain_.find(b.value("effect", std::string{}));
    if (!fx) { res.status = 404; return; }
    int fs = b.value("fs", -1);
    int mode = b.value("mode", 0);
    if (fs < 0 || fs > 3) {
      fx->fsAssign.store(-1);
      fx->fsMode.store(0);
    } else {
      fx->fsAssign.store(fs);
      fx->fsMode.store(mode);
      // Sync enabled from the bound switch's actual latched state (normal = on
      // when engaged, inverted = on when not) -- same rule as the device.
      fx->enabled.store(ctl_.fsEngaged[fs].load() ^ (mode == 1));
    }
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // {fs, engaged?}  -- toggle (or, if `engaged` is given, set) a footswitch
  // latch, then sync every effect bound to it. Mirrors a physical FS tap; the
  // device LEDs + LCD pick the change up on their next frame.
  svr_->Post("/api/footswitch", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    int fs = b.value("fs", -1);
    if (fs < 0 || fs > 3) { res.status = 400; return; }
    if (b.contains("engaged")) {
      ctl_.fsEngaged[fs].store(b["engaged"].get<bool>());
      applyFootswitch(chain_, ctl_, fs);
    } else {
      toggleFootswitch(chain_, ctl_, fs);
    }
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // Server-Sent Events: a long-lived stream that pushes the FULL state whenever
  // it changes -- so a turn of a hardware encoder (or an edit from another
  // browser) shows up live without polling. We DIFF the serialized state every
  // ~100ms rather than hand-incrementing a revision at each mutation site: the
  // state blob is tiny, and a diff can't forget to fire. Telemetry is NOT in
  // here (it changes every audio block) -- the browser polls that separately so
  // it doesn't trigger a re-render storm.
  svr_->Get("/api/events", [this](const httplib::Request&, httplib::Response& res) {
    res.set_header("Cache-Control", "no-cache");
    auto last  = std::make_shared<std::string>();  // last blob sent on THIS stream
    auto quiet = std::make_shared<int>(0);          // ticks since the last write
    res.set_chunked_content_provider(
        "text/event-stream",
        [this, last, quiet](size_t /*offset*/, httplib::DataSink& sink) {
          // Throttle the poll; the sleep also bounds CPU and caps change latency.
          std::this_thread::sleep_for(std::chrono::milliseconds(100));
          if (!ctl_.running.load()) { sink.done(); return false; }

          std::string cur = fullState(chain_, ctl_, factory_).dump();
          if (cur != *last) {
            *last = cur;
            *quiet = 0;
            std::string msg = "data: " + cur + "\n\n";
            return sink.write(msg.data(), msg.size());   // false => client gone
          }
          // Heartbeat (~every 3s) so a dead connection is detected and proxies
          // don't time the stream out.
          if (++*quiet >= 30) {
            *quiet = 0;
            static const std::string ping = ": ping\n\n";
            return sink.write(ping.data(), ping.size());
          }
          return true;   // nothing changed this tick; we'll be called again
        });
  });

  // ---- Rigs: whole-chain snapshots (was "presets") -------------------------

  svr_->Get("/api/rigs", [this](const httplib::Request&, httplib::Response& res) {
    json arr = rigs::list(rigDir_);
    res.set_content(arr.dump(), "application/json");
  });

  // {name}
  svr_->Post("/api/rig/load", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    std::string name = b.value("name", std::string{});
    if (name.empty() || !rigs::load(rigDir_, name, chain_, ctl_, factory_)) {
      res.status = 404;
      res.set_content("{\"error\":\"no such rig\"}", "application/json");
      return;
    }
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // {name}
  svr_->Post("/api/rig/save", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    std::string name = b.value("name", std::string{});
    if (name.empty() || !rigs::save(rigDir_, name, chain_, ctl_)) {
      res.status = 400;
      res.set_content("{\"error\":\"save failed\"}", "application/json");
      return;
    }
    manifest::upsertFile(baseDir_, "rig",
                         (std::filesystem::path(rigDir_) / (name + ".json")).string());
    ok(res);
  });

  // ---- Per-pedal presets: knob snapshots scoped by effect kind -------------
  // The browser passes the effect's instance id (type_id); we derive the kind
  // so a preset saved on "drive" applies to "drive-2" too. Load/save/delete
  // act on the one named effect; load returns full state so the knobs update.

  // ?effect=<type_id>  -> { kind, names:[...] }
  // Or ?kind=<base_kind> to list a kind directly (used by the add-effect picker,
  // which needs presets for a kind that isn't placed on the board yet).
  svr_->Get("/api/pedal-presets", [this](const httplib::Request& req, httplib::Response& res) {
    std::string kind = req.get_param_value("kind");
    if (kind.empty()) {
      Effect* fx = chain_.find(req.get_param_value("effect"));
      if (!fx) { res.status = 404; return; }
      kind = fxBaseKind(fx->type_id());
    }
    json out = {{"kind", kind}, {"names", presets::list(presetDir_, kind)}};
    res.set_content(out.dump(), "application/json");
  });

  // {effect, name}
  svr_->Post("/api/pedal-preset/load", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    Effect* fx = chain_.find(b.value("effect", std::string{}));
    std::string name = b.value("name", std::string{});
    if (!fx || name.empty() ||
        !presets::load(presetDir_, fxBaseKind(fx->type_id()), name, *fx)) {
      res.status = 404;
      res.set_content("{\"error\":\"no such preset\"}", "application/json");
      return;
    }
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // {effect, name}
  svr_->Post("/api/pedal-preset/save", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    Effect* fx = chain_.find(b.value("effect", std::string{}));
    std::string name = b.value("name", std::string{});
    std::string kind = fx ? fxBaseKind(fx->type_id()) : std::string{};
    if (!fx || name.empty() || !presets::save(presetDir_, kind, name, *fx)) {
      res.status = 400;
      res.set_content("{\"error\":\"save failed\"}", "application/json");
      return;
    }
    manifest::upsertFile(
        baseDir_, "preset",
        (std::filesystem::path(presetDir_) / kind / (name + ".json")).string());
    ok(res);
  });

  // {effect, name}
  svr_->Post("/api/pedal-preset/delete", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    Effect* fx = chain_.find(b.value("effect", std::string{}));
    std::string name = b.value("name", std::string{});
    if (!fx || name.empty()) { res.status = 400; return; }
    std::string kind = fxBaseKind(fx->type_id());
    presets::remove(presetDir_, kind, name);
    manifest::removeFile(
        baseDir_,
        (std::filesystem::path(presetDir_) / kind / (name + ".json")).string());
    ok(res);
  });

  // ---- Setlists: an ordered list of rig names ------------------------------
  // Stepping (next/prev rig) is client-side; the server just persists the order.

  svr_->Get("/api/setlists", [this](const httplib::Request&, httplib::Response& res) {
    json arr = setlists::list(setlistDir_);
    res.set_content(arr.dump(), "application/json");
  });

  // ?name=<n>  -> { name, id, rigs:[ {id, name, missing} ] }
  // Each rig ref is resolved through the manifest: a renamed rig still resolves
  // by id (we return its CURRENT name); a deleted one comes back missing:true so
  // the UI can flag it instead of silently breaking.
  svr_->Get("/api/setlist", [this](const httplib::Request& req, httplib::Response& res) {
    std::string name = req.get_param_value("name");
    std::vector<setlists::RigRef> refs;
    std::string id;
    if (name.empty() || !setlists::load(setlistDir_, name, refs, &id)) {
      res.status = 404;
      res.set_content("{\"error\":\"no such setlist\"}", "application/json");
      return;
    }
    manifest::Index idx = manifest::load(baseDir_);
    json rigsArr = json::array();
    for (const auto& r : refs) {
      std::string outName = r.name;
      bool missing = true;
      if (!r.id.empty()) {
        std::string cur = idx.nameForId(r.id);
        if (!cur.empty()) { outName = cur; missing = false; }
      } else if (!idx.idForName("rig", r.name).empty()) {
        missing = false;  // legacy ref still resolvable by name
      }
      rigsArr.push_back({{"id", r.id}, {"name", outName}, {"missing", missing}});
    }
    json out = {{"name", name}, {"id", id}, {"rigs", rigsArr}};
    res.set_content(out.dump(), "application/json");
  });

  // {name, rigs:[ name | {id, name} ]}  -- the server resolves each rig's id from
  // its name via the manifest (names are unique), so the UI can stay name-based.
  svr_->Post("/api/setlist/save", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    std::string name = b.value("name", std::string{});
    manifest::Index idx = manifest::load(baseDir_);
    std::vector<setlists::RigRef> refs;
    if (b.contains("rigs") && b["rigs"].is_array()) {
      for (const auto& r : b["rigs"]) {
        std::string rid, rname;
        if (r.is_string()) {
          rname = r.get<std::string>();
        } else if (r.is_object()) {
          rname = r.value("name", std::string{});
          rid = r.value("id", std::string{});
        }
        if (rid.empty()) rid = idx.idForName("rig", rname);
        refs.push_back({rid, rname});
      }
    }
    if (name.empty() || !setlists::save(setlistDir_, name, refs)) {
      res.status = 400;
      res.set_content("{\"error\":\"save failed\"}", "application/json");
      return;
    }
    manifest::upsertFile(baseDir_, "setlist",
                         (std::filesystem::path(setlistDir_) / (name + ".json")).string());
    ok(res);
  });

  // {name}
  svr_->Post("/api/setlist/delete", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    std::string name = b.value("name", std::string{});
    if (name.empty()) { res.status = 400; return; }
    setlists::remove(setlistDir_, name);
    manifest::removeFile(baseDir_,
                         (std::filesystem::path(setlistDir_) / (name + ".json")).string());
    ok(res);
  });
}

bool WebServer::start(const std::string& host, int port) {
  setupRoutes();
  if (!svr_->bind_to_port(host.c_str(), port)) {
    fprintf(stderr, "web: failed to bind %s:%d\n", host.c_str(), port);
    return false;
  }
  thread_ = std::thread([this] { svr_->listen_after_bind(); });
  return true;
}

void WebServer::stop() {
  if (svr_) svr_->stop();
  if (thread_.joinable()) thread_.join();
}
