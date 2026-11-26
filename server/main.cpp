#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <iomanip>
#include <string>
#include <ixwebsocket/IXNetSystem.h>
#include "core.hpp"
std::atomic<bool> r{1};
void hde(int) {
  std::cout<<"\nStopping...\n";
  r=0;
}
int main() {
  ix::initNetSystem();
  core srv;
  std::cout<<std::string(40,'=')<<"\nSERVER LOG\n";
  srv.on_log=[](lvl l,const std::string&s){(l==lvl::info?std::cout:std::cerr)<<s<<'\n';};
  srv.on_tick=[](const std::vector<mah>&snp,int64_t n){
    std::cout<<std::left<<std::setw(16)<<"ID"<<std::setw(9)<<"STATUS"<<std::right<<std::setw(7)<<"CPU%"<<std::setw(7)<<"RAM%"<<std::setw(7)<<"DISK%"<<std::setw(8)<<"RX(kb)"<<std::setw(7)<<"TX(kb)"<<'\n';
    for (const auto&m:snp) {
      std::cout<<std::left<<std::setw(16)<<m.id<<std::setw(9)<<status_of(m,n)<<std::right<<std::fixed<<std::setprecision(1)<<std::setw(7)<<m.cpu<<std::setw(7)<<m.ram<<std::setw(7)<<m.disk<<std::setw(7)<<m.rx<<std::setw(7)<<m.tx<<'\n';
    }
    std::cout<<"----\n";
  };
  std::signal(SIGINT,hde);
  std::string err;
  if (!srv.start(err)) {ix::uninitNetSystem(); return 1;}
  while (r) std::this_thread::sleep_for(std::chrono::milliseconds(200));
  srv.stop();
  ix::uninitNetSystem();
  return 0;
}
