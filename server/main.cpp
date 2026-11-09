#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <ixwebsocket/IXWebSocketServer.h>
#include "agent/metric.hpp"
#include <utility>
#include <string>
#include <ixwebsocket/IXNetSystem.h>
#include <json.hpp>
using json=nlohmann::json;
std::atomic<bool> r{1};
void hde(int) {
  std::cout<<"Stopping...\n";
  r=0;
}
int main() {
  ix::initNetSystem();
  ix::WebSocketServer wsr(8080,"0.0.0.0");
  wss.setOnClientMessageCallback([](std::shared_ptr<ix::ConnectionState> state, ix::WebSocket& ws,const ix::WebSocketMessagePtr& msg){
    switch (msg->type) {
    case ix::WebSocketMessageType::Open: std::cout<<"connected: "<<state->getRemoteIp()<<'\n'; break;
      case ix::WebSocketMessageType::Close: std::cout<<"Disconnecting...\n"; break;
      case ix::WebSocketMessageType::Error: std::cerr<<"An error has occur: "<<msg->errorInfo.reason<<'\n'; break;
      default: break;
    }
  });
  std::signal(SIGINT,hde);
  auto rc=wsr.listen();
  if (!r.first) {
    std::cerr<<"Error: "<<r.second;
  }
  wsr.start();
  std::cout<<"Starting server...\n";
  while (r) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!r) break;
    std::string txt;
    stats rv;
    //continue;
  }
  wss.stop();
  ix::uninitNetSystem();
  return 0;
}
