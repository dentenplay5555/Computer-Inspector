#include <iostream>
#include <fstream>
#include <sstream>
#include <cstdint>
#include <string>
#include <sys/statvfs.h>
#include "metric.hpp"
bool recpt(ct &ou) {
  std::ifstream f("/proc/stat");
  std::string l,aa;
  if (!f) {std::cerr<<"open /proc/stat failed\n"; return 1;}
  std::getline(f,l);
  int o=0;
  std::istringstream in(l);
  uint64_t t=0,i=0,a=0;
  in>>aa;
  while (in>>a) {
    o++;
    if (o==4||o==5)i+=a;
    if(o<=8)t+=a;
  }
  ou.i=i;ou.t=t;
  return 0;
}
bool remem(uint64_t &t,uint64_t &av) {
  std::ifstream f("/proc/meminfo");
  if (!f){std::cerr<<"open /proc/meminfo failed\n"; return 1;}
  std::string l;
  while (std::getline(f,l)) {
    std::istringstream i(l);
    std::string k;
    uint64_t v=0;
    if (!(i>>k>>v))continue;
    if (k=="MemTotal:")t=v;
    if (k=="MemAvailable:")av=v;
  }
  return 0;
}
bool renet(uint64_t &rx,uint64_t &tx) {
  rx=0,tx=0;
  std::ifstream f("/proc/net/dev");
  if (!f) {std::cerr<<"Error: Cannot read proc/net/dev file is nowhere to be found or unable to read\n";return 1;}
  std::string l;
  for (int i=0;i<2;i++)std::getline(f,l);
  while (std::getline(f,l)) {
    auto fi = l.find(':');
    if (fi==std::string::npos)continue;
    std::string n=l.substr(0,fi),ss;
    uint64_t rt[16];
    int i=0;
    if (n=="    lo") continue;
    std::istringstream val(l.substr(fi+1));
    while (i<16&&val>>rt[i]) i++;
    if (i<9)continue;
    rx+=rt[0];
    tx+=rt[8];
  }
  return 0;
}
bool reds(uint64_t &t,uint64_t &av) {
  struct statvfs s;
  if (statvfs("/",&s)!=0) {
    std::cerr<<"Error: Wrong path or access denied\n";
    return 1;
  }
  t =(s.f_blocks-s.f_bfree)*s.f_frsize;
  av=(uint64_t)s.f_bavail*s.f_frsize;
  return 0;
}
