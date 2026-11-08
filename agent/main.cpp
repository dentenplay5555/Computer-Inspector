#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include "metric.hpp"
using json=nlohmann::json;
std::atomic<bool> r{1};
void hde(int) {
  std::cout<<"Stopping agent...\n";
  r=0;
}
int main(int argc, char *argv[]) {
  char hn[200+200];
  int stau=gethostname(hn,sizeof(hn));
  if (stau!=0) std::strcpy(hn, "unknown");
  std::signal(SIGINT, hde);
  std::string url="ws://127.0.0.1:8080/agent",id=hn;
  for (int i=1;i<argc;i++) {
    if (std::string(argv[i])=="--server"&&i+1<argc) url=argv[++i];
    if (std::string(argv[i])=="--id"&&i+1<argc)id=argv[i+1];
  }
  ix::initNetSystem();
  ix::WebSocket ws;
  ws.setUrl(url);
  ws.setPingInterval(10);
  ws.setOnMessageCallback([](const ix::WebSocketMessagePtr& msg){
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: std::cout<<"initialized Web socket\n"; break;
      case ix::WebSocketMessageType::Close: std::cout<<"Disconnecting...\n"; break;
      case ix::WebSocketMessageType::Error: std::cerr<<"An error has occur: "<<msg->errorInfo.reason<<'\n'; break;
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
    j["type"]="metrics"; j["id"]=id; j["os"]="linux"; j["ts"]=ms;
    std::string jd=j.dump();
    std::cout<<jd<<'\n';
    ws.send(jd);
  }
  ws.stop();
  ix::uninitNetSystem();
  return 0;
}
