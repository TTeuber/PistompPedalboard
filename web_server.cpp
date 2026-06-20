// web_server.cpp -- see web_server.h.

#include "web_server.h"

#include "chain.h"
#include "effect.h"
#include "pedal_controls.h"
#include "presets.h"

#include <httplib.h>
#include <json.hpp>

#include <cstdio>

using nlohmann::json;

namespace {

// Full state WITH metadata, so the browser can build its own sliders.
json fullState(const Chain& chain, const PedalControls& ctl) {
  json doc;
  doc["master"] = ctl.masterLevel.load();
  doc["bypassed"] = ctl.bypassed.load();

  json order = json::array();
  json effects = json::array();
  for (const auto& fx : chain.effects()) {
    order.push_back(fx->type_id());
    json e;
    e["type"] = fx->type_id();
    e["name"] = fx->name();
    e["enabled"] = fx->enabled.load();
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

WebServer::WebServer(Chain& chain, PedalControls& ctl, std::string webDir,
                     std::string presetDir)
    : chain_(chain), ctl_(ctl), webDir_(std::move(webDir)),
      presetDir_(std::move(presetDir)), svr_(std::make_unique<httplib::Server>()) {
}

WebServer::~WebServer() { stop(); }

void WebServer::setupRoutes() {
  // Static frontend (index.html, app.js, style.css) from the deployed web dir.
  if (!webDir_.empty()) svr_->set_mount_point("/", webDir_);

  svr_->Get("/api/state", [this](const httplib::Request&, httplib::Response& res) {
    res.set_content(fullState(chain_, ctl_).dump(), "application/json");
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

  svr_->Get("/api/presets", [this](const httplib::Request&, httplib::Response& res) {
    json arr = presets::list(presetDir_);
    res.set_content(arr.dump(), "application/json");
  });

  // {name}
  svr_->Post("/api/preset/load", [this](const httplib::Request& req, httplib::Response& res) {
    json b;
    if (!parseBody(req, res, b)) return;
    std::string name = b.value("name", std::string{});
    if (name.empty() || !presets::load(presetDir_, name, chain_, ctl_)) {
      res.status = 404;
      res.set_content("{\"error\":\"no such preset\"}", "application/json");
      return;
    }
    res.set_content(fullState(chain_, ctl_).dump(), "application/json");
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
