#include "metric.hpp"
#include <iostream>
#include <cmath>
#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#endif
static double round1(double x) {return std::round(x*10.0)/10.0;}
using json=nlohmann::json;
col::col() {
  ct ou={0,0};
  bool rtcpv=recpt(ou);
  p_ie=ou.i;p_tt=ou.t;
  if (rtcpv) std::cerr<<"Error: Cannot read cpu stat\n";
  bool rtnev=renet(p_rx,p_tx);
  if (rtnev) std::cerr<<"Error: Cannot read network stat\n";
}
stats col::st() {
  ct ou={0,0};
  stats stt;
  uint64_t rx=0,tx=0,t=0,av=0,tds=0,avds=0;
  bool rtcpv_t=recpt(ou);
  bool rtnev_t=renet(rx,tx);
  bool rememv=remem(t,av);
  bool redsv=reds(tds,avds);
  if (ou.t>p_tt&&!rtcpv_t)stt.cp_per=100.0*((double)(ou.t-p_tt)-(double)(ou.i-p_ie))/(double)(ou.t-p_tt);
  else if (!rtcpv_t) std::cerr<<"Error: Counter Reset \n Note: This can triggered by trying to divide by 0\n";
  if (rtcpv_t)std::cerr<<"Error: Cannot read cpu stat\n";
  if (rtnev_t)std::cerr<<"Error: Cannot read network stat\n";
  if (rememv)std::cerr<<"Error: Cannot read memory stat\n";
  if (redsv)std::cerr<<"Error: Cannot read disk stat\n";
  if (!rtnev_t) {
    if(rx >= p_rx) stt.ne_rx=(rx-p_rx)/1024.0;
    if(tx >= p_tx) stt.ne_tx=(tx-p_tx)/1024.0;
  }
  if (!rememv&&t>0&&av<=t) {
    stt.ra_t=t;
    stt.ra_us=t-av;
    stt.ra_pe=100.0*(t-av)/t;
  }
  if (!redsv&&tds+avds>0) {
    stt.ds_us=tds;
    stt.ds_t=tds+avds;
    if(tds+avds>0)stt.ds_pe=tds*100.0/(tds+avds);
    else std::cerr<<"Error: Disk total is 0";
  }
  if (!rtcpv_t){p_tt=ou.t; p_ie=ou.i;}
  if (!rtnev_t){p_rx=rx; p_tx=tx;}
  return stt;
}
void to_json(json&j,const stats&s) {
  j={
    {"cpu", round1((double)s.cp_per)},
    {"ram", {
        {"used_mb",s.ra_us/1024},
        {"total_mb", s.ra_t/1024},
        {"per", round1((double)s.ra_pe)}
      }
    },
    {"net", {
        {"rx_kbps",round1((double)s.ne_rx)},
        {"tx_kbps",round1((double)s.ne_tx)}
      }
    },
    {"disk",{
        {"use_gb",round1(s.ds_us/1073741824.0)},
        {"tot_gb",round1(s.ds_t/1073741824.0)},
        {"per",round1((double)s.ds_pe)}
      }
    }
  };
}
#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
  std::string cp_ne() {
    unsigned r[12]={};
#if defined(_MSC_VER)
    int regs[4];
    __cpuid(regs,0x80000000);
    unsigned mx=static_cast<unsigned>(regs[0]);
#else
    unsigned mx=__get_cpuid_max(0x80000000,nullptr);
#endif
    if (mx<0x80000004) return "unknown";
    for (int i=0;i<3;i++) {
#if defined(_MSC_VER)
      __cpuid(regs,0x80000002+i);
      for (int j=0;j<4;j++) r[i*4+j]=static_cast<unsigned>(regs[j]);
#else
      __get_cpuid(0x80000002+i,&r[i*4],&r[i*4+1],&r[i*4+2],&r[i*4+3]);
#endif
    }
    std::string s(reinterpret_cast<char*>(r),48);
    size_t z=s.find('\0');
    if (z!=std::string::npos)s.resize(z);
    size_t a=s.find_first_not_of(' '),b=s.find_last_not_of(' ');
    if (b==std::string::npos) return "unknown";
    s=s.substr(a,b-a+1);
    return s;
  }
#else
  std::string cp_ne() {return "unknown";}
#endif
