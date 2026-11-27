#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include "metric.hpp"
#include "pin.hpp"
#include "common/tlspin.hpp"
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <string>
#include <stdexcept>
#ifdef _WIN32
  constexpr const char* osname="windows";
#elif defined(__APPLE__)
  constexpr const char* const osname="macos";
#else 
  constexpr const char* const osname="linux";
#endif
using json=nlohmann::json;
std::atomic<bool> r{1};
void hde(int) {r=0;}
int main(int argc, char *argv[]) {
  char hn[200+200];
  ix::initNetSystem();
  if (!tls_global_init()) {std::cerr<<"Error: TLS could not be initialised\n"; return 1;}
  ix::WebSocket ws;
  int stau=ghn(hn,sizeof(hn));
  if (stau!=0) std::strcpy(hn, "unknown");
  std::signal(SIGINT, hde);
  std::string id=hn,port="8080",host="127.0.0.1",surl,pin;
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
    else if (std::string(argv[i])=="--pin"&&i+1<argc) pin=argv[++i];
    else if (std::string(argv[i])=="--pin-file"&&i+1<argc) {
      // on the server pc itself: take the pin from the server's certificate file
      std::string err;
      if (!pin_from_cert_file(argv[++i],pin,err)) {std::cerr<<"Error: "<<err<<'\n'; return 2;}
    }
  }
  // the server is only trusted with its certificate pin (shown in the server window and log)
  if (pin.empty()) {
    std::cerr<<"Error: --pin is missing. Copy it from the server window or the server log\n"
               "       (\"Agent pin: ...\"), e.g. agent --host 192.168.1.10 --pin 3fa2-91bc-...\n"
               "       On the server pc itself, --pin-file <folder of the server>/server.crt also works.\n";
    return 2;
  }
  std::string url=surl.empty() ? "wss://"+host+":"+port+"/agent" : surl;
  if (url.rfind("wss://",0)!=0) {std::cerr<<"Error: --server must start with wss://\n"; return 2;}
  {
    // host and port for the pin check, from the url: wss://host:port/...
    std::string rest=url.substr(6);
    rest=rest.substr(0,rest.find('/'));
    auto c=rest.rfind(':');
    host=c==std::string::npos?rest:rest.substr(0,c);
    port=c==std::string::npos?"443":rest.substr(c+1);
  }
  std::string pem,info;
  while (r) {
    pin_result pr=fetch_pinned_cert(host,std::atoi(port.c_str()),pin,pem,info);
    if (pr==pin_result::ok) break;
    std::cerr<<(pr==pin_result::mismatch?"Error: NOT connecting: ":"Waiting for the server: ")<<info<<'\n';
    for (int k=0;k<30&&r;k++) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  if (!r) {ix::uninitNetSystem(); return 0;}
  std::cout<<"Server certificate matches the pin\n";
  ix::SocketTLSOptions tlso;
  tlso.caFile=pem;                       // trust this certificate and nothing else
  tlso.disable_hostname_validation=true; // it is pinned, the address may change (DHCP)
  ws.setTLSOptions(tlso);
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
