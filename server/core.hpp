#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_set>
#include <map>
#include <vector>
#include "netinfo.hpp"
#include "history.hpp"

namespace ix { class WebSocketServer; class HttpServer; }

// Latest numbers reported by one agent.
struct mah {
  std::string id,ip,os;
  double cpu=0,ram=0,disk=0,rx=0,tx=0;
  int64_t l_s=0;
};

enum class lvl { info, warn, err };

int64_t now_ms();
const char* status_of(const mah&m,int64_t n); // "ok", "warn", "crit" or "offline"

// Owns the agent WebSocket server (ws_port) and the dashboard HTTP server (http_port).
// Shared by the console server and the GUI so both behave the same.
class core {
public:
  int ws_port=8080, http_port=8081;
  std::function<void(lvl,const std::string&)> on_log;
  // Called once a second from the tick thread, after the snapshot went to the dashboards.
  std::function<void(const std::vector<mah>&,int64_t)> on_tick;
  // History / baseline settings; read once, on the first start().
  hist_cfg hist;

  core();
  ~core();
  // Detects the LAN address, starts both servers and the tick thread.
  // Returns false and fills err when a port cannot be opened.
  bool start(std::string&err);
  void stop();
  bool running() const {return run_;}

  netif net() const;
  std::vector<mah> machines() const;
  int dash_count() const;
  int agent_count() const;
  // Abnormal values ("cpu", "rx", "tx") and learning state; empty/false before the first start().
  std::vector<std::string> abnormal(const std::string&id,int64_t now) const;
  bool learning(const std::string&id,int64_t now) const;

private:
  void log(lvl l,const std::string&s);
  void hmsg(const std::string&ip,const std::string&txt);
  void loop();

  mutable std::mutex mu_;
  std::map<std::string,mah> mah_;
  std::unordered_set<const void*> dash_;
  netif net_;
  std::unique_ptr<ix::WebSocketServer> wsr_;
  std::unique_ptr<ix::HttpServer> http_;
  std::unique_ptr<history> hist_;  // has its own lock; never call it while holding mu_
  std::thread th_;
  std::atomic<bool> run_{false};
};
