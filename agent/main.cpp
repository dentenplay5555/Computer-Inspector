#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <cstring>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include "metric.hpp"
#include <string>
#include <stdexcept>
#ifdef _WIN32
  constexpr const char* osname="windows";
#else 
  constexpr const char* const osname="linux";
#endif
using json=nlohmann::json;
std::atomic<bool> r{1};
void hde(int) {r=0;}
int main(int argc, char *argv[]) {
  char hn[200+200];
  ix::initNetSystem();
  ix::WebSocket ws;
  int stau=ghn(hn,sizeof(hn));
  if (stau!=0) std::strcpy(hn, "unknown");
  std::signal(SIGINT, hde);
  std::string id=hn,port="8080",host="127.0.0.1",surl;
  for (int i=1;i<argc;i++) {
    if (std::string(argv[i])=="--server"&&i+1<argc) surl=argv[++i];
    else if (std::string(argv[i])=="--id"&&i+1<argc) {
      if (std::strlen(argv[i+1])<=100)id=argv[++i];
      else std::cerr<<"id too long using "<<id<<" as id\n";
    }
    else if (std::string(argv[i])=="--port"&&i+1<argc) {
      try {
        int p=std::stoi(argv[++i]);
        if (p>=1&&p<=65535) port=std::to_string(p);
      } catch (const std::invalid_argument&e) {
        std::cerr<<"invalid_argument error"<<'\n'<<"using default port 8080...\n";
      } catch (std::out_of_range&e) {
        std::cerr<<"out_of_range error"<<'\n'<<"using default port 8080...\n";
      }
    }
    else if (std::string(argv[i])=="--host"&&i+1<argc) host=argv[++i];
  }
  std::string url=surl.empty() ? "ws://"+host+":"+port+"/agent" : surl;
  ws.setUrl(url);
  ws.setPingInterval(10);
  ws.setOnMessageCallback([](const ix::WebSocketMessagePtr& msg){
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: 
        std::cout<<"initialized Web socket\n";
        break;
      case ix::WebSocketMessageType::Close: 
        std::cout<<"Disconnecting...\n"; 
        break;
      case ix::WebSocketMessageType::Error: 
        std::cerr<<"An error has occur: "<<msg->errorInfo.reason<<'\n'; 
        break;
      default: break;
    }
  });
  ws.start();
  col cor;
  while (r) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!r) break;
    stats rev=cor.st();
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count(); 
    json j; 
    j=rev;
    j["type"]="metrics"; j["id"]=id; j["os"]=osname; j["ts"]=ms;
    try {
      std::string jd=j.dump(-1,' ',false,json::error_handler_t::replace);
      std::cout<<jd<<'\n';
      auto send_stau=ws.send(jd);
      if (!send_stau.success) std::cerr<<"WS: failed to send message "<<(send_stau.compressionError ? "compression error\n" : "Either agent is disconnected or socket error\n");
    } catch (const json::exception&e) {
      std::cerr<<"JSON error: "<<e.what()<<'\n';
    }
  }
  std::cerr<<"Stopping agent...\n";
  ws.stop();
  ix::uninitNetSystem();
  return 0;
}
