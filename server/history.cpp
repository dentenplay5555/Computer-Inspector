#include "history.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <filesystem>
#include <cctype>
#include <fstream>
namespace fs=std::filesystem;
using json=nlohmann::json;

static constexpr int64_t STALE_MS=30000;     // same as "offline" in the server
static constexpr size_t KEEP_LINES=8640;     // 1 day of 10 s buckets per file
static double r1(double x) {return std::round(x*10.0)/10.0;}

history::history(hist_cfg c):cfg_(std::move(c)) {}

bool history::safe_id(const std::string&id) {
  if (id.empty()||id.size()>64||id[0]=='.') return false;
  for (unsigned char ch:id) if (!(std::isalnum(ch)||ch=='-'||ch=='_'||ch=='.')) return false;
  return true;
}

// learning period (detect=false): exact running mean/variance (Welford), so the baseline
// is right after a few dozen samples. an EWMA started from zero spread would take many
// half-lives to reach the real variance and raise false alerts in the meantime
// after learning: EWMA mean/variance (incremental form, Finch 2009). the baseline is not
// updated with samples that look abnormal, so a spike cannot slowly turn itself into "normal"
void history::update(mach&m,const std::array<double,NV>&x,int64_t now,double dt_s,bool detect) {
  (void)now;
  double alpha=1.0-std::exp(-dt_s*std::log(2.0)/cfg_.half_life_s);
  for (int i=0;i<NV;i++) {
    val&v=m.v[i];
    if (!detect) {
      v.n++;
      double d=x[i]-v.mean;
      v.mean+=d/v.n;
      v.var+=(d*(x[i]-v.mean)-v.var)/v.n;
      v.init=true; v.run=0; v.abn=false;
      continue;
    }
    if (!v.init) {v.mean=x[i]; v.var=0; v.init=true; continue;}
    double sd=std::max(std::sqrt(v.var),sdmin_of(i));
    double z=(x[i]-v.mean)/sd;
    bool cond=detect&&z>cfg_.z_limit&&x[i]>floor_of(i);
    v.run=cond?v.run+1:0;
    v.abn=v.run>=cfg_.sustain;
    if (!cond) {
      double d=x[i]-v.mean,inc=alpha*d;
      v.mean+=inc;
      v.var=(1.0-alpha)*(v.var+d*inc);
    }
  }
}

void history::push_bucket(mach&m,int64_t t,const std::array<double,NV>&x) {
  if (m.t.size()<(size_t)SLOTS) {m.t.push_back(t); m.x.push_back(x); return;}
  m.t[m.head]=t; m.x[m.head]=x;
  m.head=(m.head+1)%SLOTS;
}

void history::add(const std::string&id,int64_t now,double cpu,double rx,double tx) {
  std::string line;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it=m_.find(id);
    if (it==m_.end()) {
      if (m_.size()>=cfg_.max_machines) return;
      it=m_.emplace(id,mach{}).first;
    }
    mach&m=it->second;
    if (!m.first) m.first=now;   // new machine, or reloaded without a saved baseline
    double dt=m.last?std::clamp((now-m.last)/1000.0,0.2,10.0):1.0;
    m.last=now;
    std::array<double,NV> x{cpu,rx,tx};
    update(m,x,now,dt,now-m.first>=cfg_.learn_ms);
    int64_t b=now/STEP_MS;
    if (m.bucket!=b) {
      if (m.cnt>0) {
        std::array<double,NV> avg;
        for (int i=0;i<NV;i++) avg[i]=r1(m.sum[i]/m.cnt);
        int64_t t=m.bucket*STEP_MS;
        push_bucket(m,t,avg);
        if (!cfg_.dir.empty()&&safe_id(id)) {
          // save the baseline itself (mean, variance per value) and when learning began,
          // so a restart restores it exactly. rebuilding it from 10 s averages would
          // underestimate the per-second spread and cause false alerts
          json b=json::array();
          for (auto&v:m.v) b.push_back({v.mean,v.var,v.n});
          line=json{{"t",t},{"cpu",avg[0]},{"rx",avg[1]},{"tx",avg[2]},{"b",b},{"f",m.first}}.dump();
        }
      }
      m.bucket=b; m.sum={}; m.cnt=0;
    }
    for (int i=0;i<NV;i++) m.sum[i]+=x[i];
    m.cnt++;
  }
  if (!line.empty()) {   // file write happens outside mu_
    std::lock_guard<std::mutex> lock(io_);
    std::error_code ec;
    fs::create_directories(cfg_.dir,ec);
    std::ofstream f(fs::path(cfg_.dir)/(id+".jsonl"),std::ios::app);
    f<<line<<'\n';
  }
}

size_t history::load() {
  if (cfg_.dir.empty()) return 0;
  std::error_code ec;
  if (!fs::is_directory(cfg_.dir,ec)) return 0;
  size_t total=0;
  std::lock_guard<std::mutex> io(io_);
  for (const auto&e:fs::directory_iterator(cfg_.dir,ec)) {
    if (!e.is_regular_file()||e.path().extension()!=".jsonl") continue;
    std::string id=e.path().stem().string();
    if (!safe_id(id)) continue;
    std::deque<std::string> lines;
    size_t seen=0;
    {
      std::ifstream f(e.path());
      for (std::string s;std::getline(f,s);) {
        if (s.empty()) continue;
        lines.push_back(s); seen++;
        if (lines.size()>KEEP_LINES) lines.pop_front();
      }
    }
    if (seen>lines.size()) {   // keep files from growing forever: rewrite with the last day only
      std::ofstream f(e.path(),std::ios::trunc);
      for (auto&s:lines) f<<s<<'\n';
    }
    std::lock_guard<std::mutex> lock(mu_);
    if (m_.size()>=cfg_.max_machines) break;
    mach m;
    json lastb; int64_t lastf=0;
    for (auto&s:lines) {
      json j=json::parse(s,nullptr,false);
      if (j.is_discarded()||!j.is_object()) continue;
      int64_t t=j.value("t",(int64_t)0);
      std::array<double,NV> x{j.value("cpu",0.0),j.value("rx",0.0),j.value("tx",0.0)};
      bool ok=t>0;
      for (double d:x) ok=ok&&std::isfinite(d)&&d>=0;
      if (!ok) continue;
      push_bucket(m,t,x);
      m.last=t;
      if (j.contains("b")&&j["b"].is_array()&&j["b"].size()==NV) {lastb=j["b"]; lastf=j.value("f",(int64_t)0);}
      total++;
    }
    if (!m.last) continue;
    // restore the newest saved baseline; without one, learning starts again on the next sample
    bool restored=lastf>0;
    for (int i=0;i<NV&&restored;i++) {
      const json&p=lastb[i];
      restored=p.is_array()&&p.size()==3&&p[0].is_number()&&p[1].is_number()&&p[2].is_number_integer()
        &&std::isfinite(p[0].get<double>())&&std::isfinite(p[1].get<double>())&&p[1].get<double>()>=0;
    }
    if (restored) {
      m.first=lastf;
      for (int i=0;i<NV;i++) {m.v[i].mean=lastb[i][0]; m.v[i].var=lastb[i][1]; m.v[i].n=lastb[i][2]; m.v[i].init=true;}
    }
    m_[id]=std::move(m);
  }
  return total;
}

bool history::has(const std::string&id) const {
  std::lock_guard<std::mutex> lock(mu_);
  return m_.count(id)>0;
}

std::vector<std::string> history::abnormal(const std::string&id,int64_t now) const {
  std::vector<std::string> out;
  std::lock_guard<std::mutex> lock(mu_);
  auto it=m_.find(id);
  if (it==m_.end()||now-it->second.last>STALE_MS) return out;
  for (int i=0;i<NV;i++) if (it->second.v[i].abn) out.push_back(NAMES[i]);
  return out;
}

bool history::learning(const std::string&id,int64_t now) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it=m_.find(id);
  return it!=m_.end()&&now-it->second.first<cfg_.learn_ms;
}

json history::series(const std::string&id) const {
  std::lock_guard<std::mutex> lock(mu_);
  json out={{"id",id},{"step_s",STEP_MS/1000}};
  auto it=m_.find(id);
  if (it==m_.end()) return out;
  const mach&m=it->second;
  json t=json::array(),v[NV]={json::array(),json::array(),json::array()};
  size_t n=m.t.size();
  for (size_t k=0;k<n;k++) {
    size_t i=n<(size_t)SLOTS?k:(m.head+k)%SLOTS;
    t.push_back(m.t[i]);
    for (int j=0;j<NV;j++) v[j].push_back(m.x[i][j]);
  }
  out["t"]=t;
  json base=json::object();
  for (int j=0;j<NV;j++) {
    out[NAMES[j]]=v[j];
    base[NAMES[j]]={{"mean",r1(m.v[j].mean)},{"sd",r1(std::sqrt(m.v[j].var))}};
  }
  out["base"]=base;
  return out;
}
