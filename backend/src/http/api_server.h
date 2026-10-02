#pragma once
// Localhost-only HTTP API server (httplib). Stable versioned JSON DTOs.
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "app/embedded_assets.h"

namespace httplib { class Server; }

namespace mra {

// Loads the full analysis pipeline for a replay+beatmap pair and registers a
// session. Returns (ok, sessionIdOrError).
struct LoadOutcome {
    bool ok = false;
    std::string sessionId;
    std::string errorCode;    // e.g. REPLAY_DECODE_FAILED
    std::string message;
    std::string details;
};

LoadOutcome loadReplaySession(const std::string& replayPath,
                              const std::string& beatmapPath,
                              const std::string& audioPath,
                              const std::string& skinFolder);

class ApiServer {
  public:
    ApiServer();   // out-of-line: httplib::Server is incomplete in this header
    ~ApiServer();
    // Binds 127.0.0.1 on the requested port; on success port() is the actual
    // bound port (when requestedPort == 0 an ephemeral port is allocated).
    bool start(int requestedPort);
    void stop();
    int port() const { return port_; }

    // Serves non-/api GET paths (embedded frontend / dev dist dir).
    void setAssetProvider(AssetProvider provider) {
        assetProvider_ = std::move(provider);
    }

  private:
    void registerRoutes(httplib::Server& svr);
    int port_ = 0;
    std::unique_ptr<httplib::Server> svr_;
    std::unique_ptr<std::thread> listenThread_;
    AssetProvider assetProvider_;
};

}  // namespace mra
