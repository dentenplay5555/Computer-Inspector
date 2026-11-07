#include "metric.hpp"
#include <iostream>
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
    if(tds+avds>0)stt.ds_pe=tds/(tds+avds)*100;
    else std::cerr<<"Error: Disk total is 0";
  }
  if (!rtcpv_t){p_tt=ou.t; p_ie=ou.i;}
  if (!rtnev_t){p_rx=rx; p_tx=tx;}
  return stt;
}
/*
struct stats {
  long double cp_per=0,ra_pe=0,ds_pe=0;
  uint64_t ra_t=0,ra_us=0;
  long double ne_rx=0,ne_tx=0;
  uint64_t ds_us=0,ds_t=0;
};
*/
void to_json(json&j,const stats&s) {
  j={
    {"cpu", roundl((double)s.cp_per)},
    {"ram", {
        {"used",s.ra_us/1024},
        {"total", s.ra_t/1024},
        {"per", roundl((double)s.ra_pe)}
      }
    },
    {"net", {
        {"rx",roundl((double)s.ne_rx)},
        {"tx",roundl((double)s.ne_tx)}
      }
    },
    {"dis",{
        {"use",roundl(s.ds_us/1073741824.0)},
        {"tot",roundl(s.ds_t/1073741824.0)},
        {"per",roundl((double)s.ds_pe)}
      }
    }
  };
}
