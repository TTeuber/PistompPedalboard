// web_server.cpp -- see web_server.h.

#include "web_server.h"

#include "chain.h"
#include "effect.h"
#include "footswitch_control.h"
#include "fx_factory.h"
#include "pedal_controls.h"
#include "presets.h"

#include <httplib.h>
#include <json.hpp>

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>

using nlohmann::json;

namespace {

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
                     std::string webDir, std::string presetDir)
    : chain_(chain), ctl_(ctl), factory_(factory), webDir_(std::move(webDir)),
      presetDir_(std::move(presetDir)), svr_(std::make_unique<httplib::Server>()) {
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

  // {slot, kind}  -- kind = index into fxKinds; mints a fresh instance.
  svr_->Post("/api/fx/add", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    int slot = b.value("slot", -1);
    size_t kind = (size_t)b.value("kind", -1);
    auto fx = factory_.create(kind);
    if (slot < 0 || slot >= Chain::kFxSlots || !fx) { res.status = 400; return; }
    chain_.fxPlace(slot, std::move(fx));
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

  svr_->Get("/api/presets", [this](const httplib::Request&, httplib::Response& res) {
    json arr = presets::list(presetDir_);
    res.set_content(arr.dump(), "application/json");
  });

  // {name}
  svr_->Post("/api/preset/load", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    std::string name = b.value("name", std::string{});
    if (name.empty() || !presets::load(presetDir_, name, chain_, ctl_, factory_)) {
      res.status = 404;
      res.set_content("{\"error\":\"no such preset\"}", "application/json");
      return;
    }
    res.set_content(fullState(chain_, ctl_, factory_).dump(), "application/json");
  });

  // {name}
  svr_->Post("/api/preset/save", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    std::string name = b.value("name", std::string{});
    if (name.empty() || !presets::save(presetDir_, name, chain_, ctl_)) {
      res.status = 400;
      res.set_content("{\"error\":\"save failed\"}", "application/json");
      return;
    }
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
