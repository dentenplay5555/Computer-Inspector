#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <ixwebsocket/IXWebSocketServer.h>
#include <utility>
#include <string>
#include <ixwebsocket/IXNetSystem.h>
#include "agent/json.hpp"
#include <map>
#include <vector>
#include <iomanip>
#include <mutex>
using json=nlohmann::json;
struct mah {
  std::string id,ip,os;
  double cpu=0,ram=0,disk=0,rx=0,tx=0;
  int64_t l_s=0;
};
std::mutex g_mu;
std::map<std::string, mah> g_mah;
std::atomic<bool> r{1};
int64_t now_ms() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
void hde(int) {
  std::cout<<"\nStopping...\n";
  r=0;
}
void hmsg(const std::string&ip, const std::string&txt) {
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
    std::lock_guard<std::mutex> lock(g_mu);
    mah&m=g_mah[id];
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
int main() {
  ix::initNetSystem();
  ix::WebSocketServer wsr(8080,"0.0.0.0");
  wsr.setOnClientMessageCallback([](std::shared_ptr<ix::ConnectionState> state, ix::WebSocket&,const ix::WebSocketMessagePtr& msg){
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: std::cout<<"connected: "<<state->getRemoteIp()<<'\n'; break;
      case ix::WebSocketMessageType::Close: std::cout<<"Disconnecting...\n"; break;
      case ix::WebSocketMessageType::Error: std::cerr<<"An error has occur: "<<msg->errorInfo.reason<<'\n'; break;
      case ix::WebSocketMessageType::Message: hmsg(state->getRemoteIp(),msg->str); break;
      default: break;
    }
  });
  std::signal(SIGINT,hde);
  auto rc=wsr.listen();
  if (!rc.first) {
    std::cerr<<"Error: "<<rc.second<<'\n';
    return 1;
  }
  wsr.start();
  std::cout<<"Starting server...\n";
  while (r) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!r) break;
    std::vector<mah> snp;
    {
      std::lock_guard<std::mutex> lock(g_mu);
      for (auto &[id,m] : g_mah) snp.push_back(m);
    }
    std::cout<<std::left<<std::setw(16)<<"ID"<<std::setw(9)<<"STATUS"<<std::right<<std::setw(7)<<"CPU%"<<std::setw(7)<<"RAM%"<<std::setw(7)<<"DISK%"<<std::setw(8)<<"RX(kb)"<<std::setw(7)<<"TX(kb)"<<'\n';
    int64_t n=now_ms();
    for (const auto&m:snp) {
      bool o=n-m.l_s>30000;
      std::cout<<std::left<<std::setw(16)<<m.id<<std::setw(9)<<(o ? "OFFLINE" : "ok")<<std::right<<std::fixed<<std::setprecision(1)<<std::setw(7)<<m.cpu<<std::setw(7)<<m.ram<<std::setw(7)<<m.disk<<std::setw(7)<<m.rx<<std::setw(7)<<m.tx<<'\n';
    }
    std::cout<<"----\n";
  }
  wsr.stop();
  ix::uninitNetSystem();
  return 0;
}
