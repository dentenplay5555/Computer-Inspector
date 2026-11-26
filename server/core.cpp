#include "core.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXHttpServer.h>
#include "agent/json.hpp"
using json=nlohmann::json;

static std::string read_f(const std::string&pth) {
  std::ifstream f(pth,std::ios::binary);
  std::stringstream ss;
  ss<<f.rdbuf();
  return ss.str();
}
int64_t now_ms() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
const char* status_of(const mah&m,int64_t n) {
  if (n-m.l_s>30000) return "offline";
  else if (m.cpu>=90||m.ram>=90||m.disk>=95) return "crit";
  else if (m.cpu>=80||m.ram>=80||m.disk>=85) return "warn";
  return "ok";
}
static json b_snp(const std::vector<mah>&snp,int64_t n) {
  json arr=json::array();
  for (auto&m:snp) {
    arr.push_back({
      {"id",m.id},{"ip",m.ip},{"os",m.os},
      {"status",status_of(m,n)},
      {"cpu",m.cpu},{"ram",m.ram},{"disk",m.disk},
      {"rx",m.rx},{"tx",m.tx},
      {"age",n-m.l_s}
    });
  }
  return {{"type","snapshot"},{"ts",n},{"machines",arr}};
}

core::core()=default;
core::~core() {stop();}

void core::log(lvl l,const std::string&s) {if (on_log) on_log(l,s);}

void core::hmsg(const std::string&ip,const std::string&txt) {
  try {
    json j=json::parse(txt,nullptr,false);
    if (j==nlohmann::json::value_t::discarded)return;
    if (!j.is_object())return;
    if (j.value("type","")!="metrics")return;
    std::string id=j.value("id","");
    if (id.empty()) return;
    double cpu=j.value("cpu",0.0),ram=0;
    if (j.contains("ram")&&j["ram"].is_object())ram=j["ram"].value("per",0.0);
    double tx=0,rx=0;
    if (j.contains("net")&&j["net"].is_object()){tx=j["net"].value("tx_kbps",0.0);rx=j["net"].value("rx_kbps",0.00);}
    double disk=0;
    if (j.contains("disk")&&j["disk"].is_object())disk=j["disk"].value("per",0.0);
    std::string os=j.value("os","");
    if (!std::isfinite(cpu) || !std::isfinite(ram) || !std::isfinite(disk) || !std::isfinite(rx) || !std::isfinite(tx))return;
    if (cpu<0 || cpu>100 || ram<0 || ram>100 || disk<0 || disk>100 || rx<0 || tx<0) return;
    std::lock_guard<std::mutex> lock(mu_);
    mah&m=mah_[id];
    m.id=id;
    m.ip=ip;
    m.cpu=cpu;
    m.ram=ram;
    m.tx=tx;
    m.rx=rx;
    m.disk=disk;
    m.l_s=now_ms();
    m.os=os;
  } catch (json::exception&) {return;}
}

bool core::start(std::string&err) {
  if (run_) return true;
  {
    netif rst=ipvfour();
    std::lock_guard<std::mutex> lock(mu_);
    net_=rst;
  }
  if (net_.net_ip.empty()) log(lvl::warn,"Network IPv4 not found");
  else log(lvl::info,"Network interface: "+net_.net_inter+", local ip: "+net_.net_ip);

  wsr_=std::make_unique<ix::WebSocketServer>(ws_port,"0.0.0.0");
  wsr_->setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState> state,ix::WebSocket&ws,const ix::WebSocketMessagePtr&msg){
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: {
        bool d=msg->openInfo.uri=="/dashboard";
        if (d) {std::lock_guard<std::mutex> lock(mu_); dash_.insert(&ws);}
        log(lvl::info,std::string(d?"dashboard":"agent")+" connected: "+state->getRemoteIp());
        break;
      }
      case ix::WebSocketMessageType::Close: {
        bool d;
        {std::lock_guard<std::mutex> lock(mu_); d=dash_.erase(&ws)>0;}
        log(lvl::info,std::string(d?"dashboard":"agent")+" disconnected: "+state->getRemoteIp());
        break;
      }
      case ix::WebSocketMessageType::Error:
        log(lvl::err,"An error has occur: "+msg->errorInfo.reason);
        break;
      case ix::WebSocketMessageType::Message: {
        bool is_d;
        {std::lock_guard<std::mutex> lock(mu_); is_d=dash_.count(&ws)>0;}
        if (!is_d) hmsg(state->getRemoteIp(),msg->str);
        break;
      }
      default: break;
    }
  });
  auto rc=wsr_->listen();
  if (!rc.first) {
    err="WebSocket port "+std::to_string(ws_port)+": "+rc.second;
    log(lvl::err,err);
    wsr_.reset();
    return false;
  }
  wsr_->start();
  log(lvl::info,"Agents can connect on port "+std::to_string(ws_port));

  http_=std::make_unique<ix::HttpServer>(http_port,"0.0.0.0");
  http_->setOnConnectionCallback(
    [](ix::HttpRequestPtr rq,std::shared_ptr<ix::ConnectionState>)->ix::HttpResponsePtr {
      ix::WebSocketHttpHeaders head;
      if (rq->uri=="/"||rq->uri=="/index.html") {
        std::string by=read_f("dashboard/index.html");
        if (!by.empty()) {
          head["Content-Type"]="text/html; charset=utf-8";
          return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,by);
        }
      }
      std::string nf="Not Found";
      head["Content-Type"]="text/html; charset=utf-8";
      return std::make_shared<ix::HttpResponse>(404,"Not Found",ix::HttpErrorCode::Ok,head,nf);
    }
  );
  auto hr=http_->listen();
  if (!hr.first) {
    err="HTTP port "+std::to_string(http_port)+": "+hr.second;
    log(lvl::err,err);
    http_.reset();
    wsr_->stop();
    wsr_.reset();
    return false;
  }
  http_->start();
  log(lvl::info,"Dashboard is served at http://"+(net_.net_ip.empty()?std::string("127.0.0.1"):net_.net_ip)+":"+std::to_string(http_port));

  run_=true;
  th_=std::thread([this]{loop();});
  return true;
}

void core::loop() {
  while (run_) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!run_) break;
    std::vector<mah> snp=machines();
    int64_t n=now_ms();
    try {
      std::string pl=b_snp(snp,n).dump(-1,' ',false,json::error_handler_t::replace);
      std::lock_guard<std::mutex> lock(mu_);
      for (const auto&c:wsr_->getClients()) if (dash_.count(c.get())) c->send(pl);
    } catch (const json::exception&e) {
      log(lvl::err,std::string("JSON EXCEPTION ERROR: ")+e.what());
    }
    if (on_tick) on_tick(snp,n);
  }
}

void core::stop() {
  if (!run_) return;
  run_=false;
  if (th_.joinable()) th_.join();
  if (http_) {http_->stop(); http_.reset();}
  if (wsr_) {wsr_->stop(); wsr_.reset();}
  {std::lock_guard<std::mutex> lock(mu_); dash_.clear();}
  log(lvl::info,"Server stopped");
}

netif core::net() const {std::lock_guard<std::mutex> lock(mu_); return net_;}

std::vector<mah> core::machines() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<mah> v;
  for (auto&[id,m]:mah_) v.push_back(m);
  return v;
}

int core::dash_count() const {std::lock_guard<std::mutex> lock(mu_); return (int)dash_.size();}

int core::agent_count() const {
  if (!run_||!wsr_) return 0;
  std::lock_guard<std::mutex> lock(mu_);
  int n=0;
  for (const auto&c:wsr_->getClients()) if (!dash_.count(c.get())) n++;
  return n;
}
