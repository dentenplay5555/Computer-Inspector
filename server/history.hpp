#pragma once
// per-machine history and "normal behaviour" baseline
//
// for every machine and every tracked value (cpu, rx, tx)
//  - a 1-hour ring buffer of 10-second averages, for graphs (/history)
//  - the baseline: during the learning period an exact running mean and variance
//    (Welford); after it, an EWMA that starts from those values and follows slow changes
//  - an abnormal flag: z-score > z_limit and value > an absolute floor,
//    held for `sustain` samples in a row, and only after the learning period
//
// thread safety: every public method takes this class's own mutex and nothing else,
// so it can be called from any thread. never call it while holding g_mu
#include <array>
#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>
#include "agent/json.hpp"

struct hist_cfg {
  std::string dir="history";      // where <id>.jsonl files go; empty = memory only
  int64_t learn_ms=15*60*1000;    // no alerts during each machine's first 15 min
  double half_life_s=600;         // EWMA half-life: old samples weigh half after 10 min
  double z_limit=3;
  int sustain=5;                  // samples in a row (about 1 per second)
  double floor_cpu=20, floor_net=100;   // % and KB/s: below this, never abnormal
  double sd_min_cpu=2, sd_min_net=5;    // stop z exploding when a value is almost constant
  size_t max_machines=200;
};

class history {
public:
  static constexpr int NV=3;                 // cpu, rx, tx
  static constexpr const char* NAMES[NV]={"cpu","rx","tx"};
  static constexpr int STEP_MS=10000, SLOTS=360;   // 10 s x 360 = 1 hour

  explicit history(hist_cfg c);
  // reads <dir>/*.jsonl so baselines and graphs survive a restart. returns lines loaded
  size_t load();
  // one sample from an agent (server time). writes a line to disk when a 10 s bucket closes
  void add(const std::string&id,int64_t now,double cpu,double rx,double tx);
  bool has(const std::string&id) const;
  // names of the values that are abnormal right now, e.g. {"rx"}. empty when stale
  std::vector<std::string> abnormal(const std::string&id,int64_t now) const;
  bool learning(const std::string&id,int64_t now) const;
  // graph data for /history: {"id","step_s","t":[...],"cpu":[...],"rx":[...],"tx":[...],"base":{...}}
  nlohmann::json series(const std::string&id) const;
  // true when the id can be used as a file name (letters, digits, - _ .; no leading dot)
  static bool safe_id(const std::string&id);

private:
  struct val {
    double mean=0,var=0; bool init=false;
    int64_t n=0;          // samples seen while learning (Welford)
    int run=0; bool abn=false;
  };
  struct mach {
    int64_t first=0,last=0;
    std::array<val,NV> v;
    // current 10 s bucket being averaged
    int64_t bucket=-1; std::array<double,NV> sum{}; int cnt=0;
    // ring buffer of closed buckets
    std::vector<int64_t> t; std::vector<std::array<double,NV>> x; size_t head=0;
  };
  void update(mach&m,const std::array<double,NV>&x,int64_t now,double dt_s,bool detect);
  void push_bucket(mach&m,int64_t t,const std::array<double,NV>&x);
  double floor_of(int i) const {return i==0?cfg_.floor_cpu:cfg_.floor_net;}
  double sdmin_of(int i) const {return i==0?cfg_.sd_min_cpu:cfg_.sd_min_net;}

  hist_cfg cfg_;
  mutable std::mutex mu_;
  std::mutex io_;
  std::map<std::string,mach> m_;
};
