#include <iostream>
#include <atomic>
#include <csignal>
#include <chrono>
#include <thread>
#include <iomanip>
#include <string>
#include <stdexcept>
#include <ixwebsocket/IXNetSystem.h>
#include "core.hpp"
#include "auth.hpp"
#include "common/exedir.hpp"
#include "common/tlspin.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

// reads a line from the console without showing what is typed
static std::string read_hidden(const char* prompt) {
  std::cout<<prompt<<std::flush;
  std::string s;
#ifdef _WIN32
  HANDLE h=GetStdHandle(STD_INPUT_HANDLE);
  DWORD mode=0;
  bool tty=GetConsoleMode(h,&mode);
  if (tty) SetConsoleMode(h,mode&~ENABLE_ECHO_INPUT);
  std::getline(std::cin,s);
  if (tty) SetConsoleMode(h,mode);
#else
  termios old{};
  bool tty=tcgetattr(STDIN_FILENO,&old)==0;
  if (tty) {termios t=old; t.c_lflag&=~ECHO; tcsetattr(STDIN_FILENO,TCSANOW,&t);}
  std::getline(std::cin,s);
  if (tty) tcsetattr(STDIN_FILENO,TCSANOW,&old);
#endif
  std::cout<<'\n';
  if (!s.empty()&&s.back()=='\r') s.pop_back();
  return s;
}

// server --set-password: sets (or replaces) the teacher password and exits
static int set_password_cmd() {
  if (!tls_global_init()) {std::cerr<<"Error: no random numbers available\n"; return 1;}
  auth a(exe_dir());
  std::string err;
  if (!a.load(err)) {std::cerr<<"Error: "<<err<<'\n'; return 1;}
  std::string p1=read_hidden("New teacher password (at least 8 characters): ");
  std::string p2=read_hidden("Type it again: ");
  if (p1!=p2) {std::cerr<<"The two passwords are not the same. Nothing changed.\n"; return 1;}
  std::string why=a.set_password(p1);
  if (!why.empty()) {std::cerr<<"Not set: "<<why<<"\n"; return 1;}
  a.audit("console","teacher password set (server --set-password)");
  std::cout<<"Teacher password saved in "<<exe_dir()<<"/auth.json. A running server needs a restart to use it.\n";
  return 0;
}
std::atomic<bool> r{1};
void hde(int) {
  std::cout<<"\nStopping...\n";
  r=0;
}
int main(int argc,char*argv[]) {
  core srv;
  for (int i=1;i<argc;i++) if (std::string(argv[i])=="--set-password") return set_password_cmd();
  for (int i=1;i<argc;i++) {
    std::string a=argv[i];
    if (a=="--learn-min"&&i+1<argc) {
      try {int v=std::stoi(argv[++i]); if (v>=0&&v<=24*60) srv.hist.learn_ms=(int64_t)v*60000;}
      catch (const std::exception&) {std::cerr<<"--learn-min needs minutes, using 15\n";}
    }
    else if (a=="--history-dir"&&i+1<argc) srv.hist.dir=argv[++i];
    else if (a=="--no-history-files") srv.hist.dir.clear();
    else if ((a=="--rotate-hours"||a=="--rotate-minutes")&&i+1<argc) {
      // how old a pc's key may get before it is replaced (default 24 hours; 0 = never)
      try {int v=std::stoi(argv[++i]); if (v>=0) srv.rotate_ms=(int64_t)v*(a=="--rotate-hours"?3600000:60000);}
      catch (const std::exception&) {std::cerr<<a<<" needs a number, using 24 hours\n";}
    }
  }
  ix::initNetSystem();
  std::cout<<std::string(40,'=')<<"\nSERVER LOG\n";
  srv.on_log=[](lvl l,const std::string&s){(l==lvl::info?std::cout:std::cerr)<<s<<std::endl;};   // flush: logs may go to a file
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
