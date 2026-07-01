// web_server.h -- the network control surface (the primary UI for this phase).
//
// A cpp-httplib server on its own thread. It edits the SAME atomics the audio
// thread reads (per-effect Param + global PedalControls), so no extra locking is
// needed -- a momentary cross-block inconsistency while several params change is
// inaudible. Routes are generic (driven by the Effect/Param model), so adding an
// effect needs zero changes here.

#pragma once

#include "tempo.h"

#include <memory>
#include <string>
#include <thread>

class Chain;
class FxFactory;
struct PedalControls;
namespace httplib { class Server; }

class WebServer {
public:
  WebServer(Chain& chain, PedalControls& ctl, FxFactory& factory,
            std::string webDir, std::string rigDir, std::string presetDir,
            std::string setlistDir, std::string baseDir);
  ~WebServer();

  // Spawn the server thread. Returns false if the port can't be bound.
  bool start(const std::string& host, int port);
  void stop();

private:
  void setupRoutes();

  Chain& chain_;
  PedalControls& ctl_;
  FxFactory& factory_;
  std::string webDir_;
  std::string rigDir_;      // whole-chain rigs
  std::string presetDir_;   // per-pedal presets (by kind)
  std::string setlistDir_;  // ordered rig lists
  std::string baseDir_;     // library root (parent of the three dirs above)
  std::unique_ptr<httplib::Server> svr_;
  std::thread thread_;
  tempo::TapTempo tap_;     // tap-tempo accumulator for POST /api/tap
};
