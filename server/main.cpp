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
#include <unordered_set>
#include <sstream>
#include <fstream>
#include <ixwebsocket/IXHttpServer.h>
#include <filesystem>
#include <stdexcept>
#include <cmath>
#include "netinfo.hpp"
#include "history.hpp"
#include <memory>
using json=nlohmann::json;
struct mah {
  std::string id,ip,os;
  double cpu=0,ram=0,disk=0,rx=0,tx=0;
  int64_t l_s=0;
};
std::mutex g_mu;
std::map<std::string, mah> g_mah;
std::unordered_set<const void*> g_dash;
std::atomic<bool> r{1};
std::string pi="<localhost ip>";
std::unique_ptr<history> g_hist;  // has its own lock; never call it while holding g_mu
std::string read_f(const std::string&pth) {
  std::ifstream f(pth,std::ios::binary);
  std::stringstream ss;
  ss<<f.rdbuf();
  return ss.str();
}
int64_t now_ms() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
void hde(int) {
  std::cout<<"\nStopping...\n";
  r=0;
}
const char* stat(const mah&m, int64_t n) {
  if (n-m.l_s>30000) return "offline";
  else if (m.cpu>=90||m.ram>=90||m.disk>=95) return "crit";
  else if (m.cpu>=80||m.ram>=80||m.disk>=85) return "warn";
  return "ok";
}
json b_snp(const std::vector<mah>&snp,int64_t n) {
  json arr=json::array();
  for (auto&m:snp) {
    arr.push_back({
      {"id",m.id},{"ip",m.ip},{"os",m.os},
      {"status",stat(m,n)},
      {"cpu",m.cpu},{"ram",m.ram},{"disk",m.disk},
      {"rx",m.rx},{"tx",m.tx},
      {"age",n-m.l_s},
      {"abn",g_hist->abnormal(m.id,n)},
      {"learn",g_hist->learning(m.id,n)}
    });
  }
  return {{"type","snapshot"},{"ts",n},{"machines",arr}};
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
    if (!std::isfinite(cpu) || !std::isfinite(ram) || !std::isfinite(disk) || !std::isfinite(rx) || !std::isfinite(tx))return;
    if (cpu<0 || cpu>100 || ram<0 || ram>100 || disk<0 || disk>100 || rx<0 || tx<0) return;
    int64_t now=now_ms();
    {
    std::lock_guard<std::mutex> lock(g_mu);
    mah&m=g_mah[id];
    m.id=id;
    m.ip=ip;
    m.cpu=cpu;
    m.ram=ram;
    m.tx=tx;
    m.rx=rx;
    m.disk=disk;
    m.l_s=now;
    m.os=os;
    }
    g_hist->add(id,now,cpu,rx,tx);
  } catch (json::exception&) {return;}
}
// "%41-01" -> "A-01". Returns false on a malformed escape.
bool url_dec(const std::string&in,std::string&out) {
  out.clear();
  for (size_t i=0;i<in.size();i++) {
    if (in[i]=='%') {
      if (i+2>=in.size()||!std::isxdigit((unsigned char)in[i+1])||!std::isxdigit((unsigned char)in[i+2])) return false;
      out+=(char)std::stoi(in.substr(i+1,2),nullptr,16); i+=2;
    } else if (in[i]=='+') out+=' ';
    else out+=in[i];
  }
  return true;
}
int main(int argc,char*argv[]) {
  hist_cfg hc;
  for (int i=1;i<argc;i++) {
    std::string a=argv[i];
    if (a=="--learn-min"&&i+1<argc) {
      try {int v=std::stoi(argv[++i]); if (v>=0&&v<=24*60) hc.learn_ms=(int64_t)v*60000;}
      catch (const std::exception&) {std::cerr<<"--learn-min needs minutes, using 15\n";}
    }
    else if (a=="--history-dir"&&i+1<argc) hc.dir=argv[++i];
    else if (a=="--no-history-files") hc.dir.clear();
  }
  g_hist=std::make_unique<history>(hc);
  ix::initNetSystem();
  std::cout<<"History: "<<g_hist->load()<<" saved points loaded from "<<(hc.dir.empty()?std::string("(memory only)"):hc.dir+"/")<<", learning "<<hc.learn_ms/60000<<" min\n";
  ix::WebSocketServer wsr(8080,"0.0.0.0");
  netif rst=ipvfour();
  std::cout<<std::string(40,'=')<<"\nSERVER LOG\nUsing Network config: \n";
  if (rst.net_ip.empty()){std::cerr<<"Error: Network IPv4 not found\n";}
  pi=rst.net_ip;
  std::cout<<"Network interface: "<<rst.net_inter<<"\nLocal ip: "<<pi<<'\n';
  wsr.setOnClientMessageCallback([](std::shared_ptr<ix::ConnectionState> state, ix::WebSocket&ws,const ix::WebSocketMessagePtr& msg){
    switch (msg->type) {
      case ix::WebSocketMessageType::Open:
        if (msg->openInfo.uri=="/dashboard") {
          {std::lock_guard<std::mutex> lock(g_mu); g_dash.insert(&ws);}
        }
        std::cout<<"connected: "<<state->getRemoteIp()<<'\n';
        break;
      case ix::WebSocketMessageType::Close: 
        {std::lock_guard<std::mutex> lock(g_mu); g_dash.erase(&ws);}
        std::cout<<"Disconnecting...\n";
        break;
      case ix::WebSocketMessageType::Error: 
        std::cerr<<"An error has occur: "<<msg->errorInfo.reason<<'\n'; 
        break;
      case ix::WebSocketMessageType::Message:
        bool is_d;
        {std::lock_guard<std::mutex> lock(g_mu); is_d=g_dash.count(&ws)>0;}
        if (!is_d) hmsg(state->getRemoteIp(),msg->str); 
        break;
      default: break;
    }
  });
  std::signal(SIGINT,hde);
  auto rc=wsr.listen();
  if (!rc.first) {
    std::cerr<<"Error: "<<rc.second<<'\n';
    return 1;
  }
  std::cout<<"Starting server...\n";
  wsr.start();
  std::cout<<"Server can recv client now...\n";
  ix::HttpServer http(8081,"0.0.0.0");
  http.setOnConnectionCallback(
    [](ix::HttpRequestPtr rq,std::shared_ptr<ix::ConnectionState>)->ix::HttpResponsePtr {
      ix::WebSocketHttpHeaders head;
      const std::string hp="/history?id=";
      if (rq->uri.compare(0,hp.size(),hp)==0) {
        std::string id;
        // only ids the server has already seen; anything else is 404
        if (url_dec(rq->uri.substr(hp.size()),id)&&g_hist->has(id)) {
          head["Content-Type"]="application/json";
          head["Cache-Control"]="no-store";
          return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,g_hist->series(id).dump(-1,' ',false,json::error_handler_t::replace));
        }
      }
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
  auto hr=http.listen();
  if (!hr.first){ std::cerr<<"HTTP error: "<<hr.second<<'\n';return 1;}
  http.start();
  std::cout<<"Server is now serving web page vist: http://"<<pi<<":8081\n";
  while (r) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!r) break;
    std::vector<mah> snp;
    { 
      std::lock_guard<std::mutex> lock(g_mu);
      for (auto &[id,m] : g_mah)   snp.push_back(m);
    }
    std::cout<<std::left<<std::setw(16)<<"ID"<<std::setw(9)<<"STATUS"<<std::right<<std::setw(7)<<"CPU%"<<std::setw(7)<<"RAM%"<<std::setw(7)<<"DISK%"<<std::setw(8)<<"RX(kb)"<<std::setw(7)<<"TX(kb)"<<'\n';
    int64_t n=now_ms();
    try {
      std::string pl=b_snp(snp,n).dump(-1,' ',false,json::error_handler_t::replace);
      {
      std::lock_guard<std::mutex> lock(g_mu);
        for (const auto&c:wsr.getClients()) if (g_dash.count(c.get()))c->send(pl);
      }
      for (const auto&m:snp) {
        std::cout<<std::left<<std::setw(16)<<m.id<<std::setw(9)<<stat(m,n)<<std::right<<std::fixed<<std::setprecision(1)<<std::setw(7)<<m.cpu<<std::setw(7)<<m.ram<<std::setw(7)<<m.disk<<std::setw(7)<<m.rx<<std::setw(7)<<m.tx<<'\n';
      }
    } catch (const json::exception&e) {
      std::cerr<<"JSON EXCEPTION ERROR: "<<e.what()<<'\n';
    }
    std::cout<<"----\n";
  }
  http.stop();
  wsr.stop();
  ix::uninitNetSystem();
  return 0;
}
