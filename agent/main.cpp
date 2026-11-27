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
#include "keystore.hpp"
#include "common/tlspin.hpp"
#include "common/keys.hpp"
#include "common/exedir.hpp"
#include <ixwebsocket/IXSocketTLSOptions.h>
#include <fstream>
#include <mutex>
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
std::atomic<bool> refused{false};   // the server said no for good: exit with an error
void hde(int) {r=0;}
int main(int argc, char *argv[]) {
  char hn[200+200];
  ix::initNetSystem();
  if (!tls_global_init()) {std::cerr<<"Error: TLS could not be initialised\n"; return 1;}
  ix::WebSocket ws;
  int stau=ghn(hn,sizeof(hn));
  if (stau!=0) std::strcpy(hn, "unknown");
  std::signal(SIGINT, hde);
  std::string id=hn,port="8080",host="127.0.0.1",surl,pin,joinkey,keyfile=exe_dir()+"/agent.key";
  bool id_given=false;
  for (int i=1;i<argc;i++) {
    if (std::string(argv[i])=="--server"&&i+1<argc) surl=argv[++i];
    else if (std::string(argv[i])=="--id"&&i+1<argc) {
      if (std::strlen(argv[i+1])<=100) {id=argv[++i]; id_given=true;}
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
    else if (std::string(argv[i])=="--join-key"&&i+1<argc) joinkey=argv[++i];
    else if (std::string(argv[i])=="--join-key-file"&&i+1<argc) {
      // on the server pc itself: the join key from the server's agents.json
      std::ifstream f(argv[++i]);
      json jf=json::parse(f,nullptr,false);
      if (jf.is_discarded()||!jf.is_object()||!jf.contains("join_key")||!jf["join_key"].is_string()) {
        std::cerr<<"Error: no join key in "<<argv[i]<<'\n';
        return 2;
      }
      joinkey=jf["join_key"].get<std::string>();
    }
    else if (std::string(argv[i])=="--key-file"&&i+1<argc) keyfile=argv[++i];
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
  // this pc's own key, if it joined this server before (same pin)
  stored_key sk;
  bool have_key=load_key(keyfile,sk)&&same_pin(sk.pin,pin);
  if (have_key&&sk.id!=id) {
    if (id_given) {
      std::cerr<<"Error: this pc joined as "<<sk.id<<", not "<<id<<". Drop --id, or ask the teacher to\n"
                 "       revoke "<<sk.id<<"'s key and join again with --join-key.\n";
      return 2;
    }
    id=sk.id;                            // the key belongs to that id
  }
  if (!have_key&&joinkey.empty()) {
    std::cerr<<"Error: this pc has not joined the server yet. Run it once with --join-key <key>\n"
               "       (the join key is in the server window and the server log).\n";
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
  // signing in (see README "Joining and keys"): the server says hello with a random nonce,
  // the agent answers with a signature, and metrics only count after "ok" / "welcome"
  std::atomic<bool> authed{false};
  std::mutex km;                         // guards sk, have_key, nonce
  std::string nonce;
  if (!have_key) sk={pin,id,""};
  ws.setOnMessageCallback([&](const ix::WebSocketMessagePtr& msg){
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: 
        std::cout<<"Connected, signing in...\n";
        break;
      case ix::WebSocketMessageType::Close: 
        authed=false;
        std::cout<<"Disconnected\n"; 
        break;
      case ix::WebSocketMessageType::Message: {
        json m=json::parse(msg->str,nullptr,false);
        if (m.is_discarded()||!m.is_object()) break;
        std::string type=m.value("type","");
        std::lock_guard<std::mutex> lock(km);
        if (type=="hello") {
          nonce=m.value("nonce","");
          if (have_key) ws.send(json{{"type","auth"},{"id",id},{"sig",hmac_hex(sk.key,msg_auth(nonce,id))}}.dump());
          else ws.send(json{{"type","join"},{"id",id},{"proof",hmac_hex(norm_join_key(joinkey),msg_join(nonce,id))}}.dump());
        } else if (type=="welcome") {
          sk={pin,id,m.value("key","")};
          have_key=true;
          if (!save_key(keyfile,sk)) std::cerr<<"Error: joined, but cannot save the key to "<<keyfile<<"; the next start will fail\n";
          else std::cout<<"Joined. This pc's own key is saved in "<<keyfile<<" (--join-key is no longer needed)\n";
          authed=true;
        } else if (type=="ok") {
          authed=true;
          std::cout<<"Signed in as "<<id<<'\n';
        } else if (type=="rekey") {
          // save first, then confirm: until the server sees the confirmation it keeps the old key
          stored_key nk{pin,id,m.value("key","")};
          if (nk.key.empty()||!save_key(keyfile,nk)) {std::cerr<<"Error: cannot save the new key to "<<keyfile<<"; keeping the old one\n"; break;}
          sk=nk;
          ws.send(json{{"type","rekeyed"},{"sig",hmac_hex(sk.key,msg_rekey(nonce,id))}}.dump());
          std::cout<<"Key rotated\n";
        } else if (type=="denied") {
          authed=false;
          std::string why=m.value("why","");
          if (why=="not_joined"&&have_key&&!joinkey.empty()) {
            std::cerr<<"The server does not know this pc's key (revoked?); joining again with --join-key\n";
            have_key=false;
            ws.send(json{{"type","join"},{"id",id},{"proof",hmac_hex(norm_join_key(joinkey),msg_join(nonce,id))}}.dump());
          } else {
            if (why=="not_joined") std::cerr<<"Error: the server does not know this pc (its key was revoked, or the server was reset). Run with --join-key <key> to join again.\n";
            else if (why=="bad_join_key") std::cerr<<"Error: the join key is wrong. Copy it again from the server window.\n";
            else if (why=="already_joined") std::cerr<<"Error: "<<id<<" already joined. If this pc was reinstalled, ask the teacher to revoke "<<id<<"'s key, then join again.\n";
            else if (why=="bad_key") std::cerr<<"Error: the server rejected this pc's key. Ask the teacher to revoke "<<id<<"'s key, then join again with --join-key.\n";
            else std::cerr<<"Error: the server refused this agent ("<<why<<")\n";
            refused=true;
            r=0;
          }
        }
        break;
      }
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
    if (!authed) continue;                // nothing counts before signing in
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
  return refused?3:0;
}
