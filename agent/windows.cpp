#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "metric.hpp"
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <iostream>
#include <string>
int ghn(char *hon,size_t shn) {
  DWORD s=static_cast<DWORD>(shn);
  if(GetComputerNameA(hon,&s)) return 0;
  return 1;
}
bool remem(uint64_t &t,uint64_t &av) {
  MEMORYSTATUSEX sas; 
  sas.dwLength=sizeof(sas);
  if (GlobalMemoryStatusEx(&sas)) {
    // kB, like linux.cpp and macos.cpp (metric.cpp turns it into MB)
    t=sas.ullTotalPhys/1024;
    av=sas.ullAvailPhys/1024;
    return 0;
  }
  std::cerr<<"Error: unexpected error at reading memory occured\n";
  return 1;
}
bool reds(uint64_t &t,uint64_t &av) {
  std::string rt="C:\\";
  char buf[16]; 
  DWORD n=GetEnvironmentVariableA("SystemDrive",buf,sizeof(buf));
  if (n>0&&n<16) rt=std::string(buf)+"\\";
  ULARGE_INTEGER freby,totby,totfreby;
  if (!GetDiskFreeSpaceExA(rt.c_str(),&freby,&totby,&totfreby)) {std::cerr<<"Error: Either this error trigger from premission denied or wrong path in src\n";return 1;}
  t=totby.QuadPart-totfreby.QuadPart;
  av=freby.QuadPart;
  return 0;
}
static inline uint64_t ft2ull(const FILETIME &f) {
  return (static_cast<uint64_t>(f.dwHighDateTime)<<32) | f.dwLowDateTime;
}
bool recpt(ct &ou) {
  FILETIME idt, kei,usrt;
  if (!GetSystemTimes(&idt, &kei, &usrt)) {
    std::cerr<<"Error: cannot GetSystemTimes, It failed\n";
    return 1;
  }
  uint64_t id=ft2ull(idt),ke=ft2ull(kei),usr=ft2ull(usrt);
  ou.i=id;
  ou.t=ke+usr;
  return 0;
}
bool renet(uint64_t &rx,uint64_t &tx) {
  rx=0,tx=0;
  PMIB_IF_TABLE2 tb=nullptr;
  if (GetIfTable2(&tb)!=NO_ERROR) {std::cerr<<"Error: Cannot read network status\n";return 1;}
  for (ULONG i=0;i<tb->NumEntries;i++) {
    const MIB_IF_ROW2 &r=tb->Table[i];
    if (!r.InterfaceAndOperStatusFlags.HardwareInterface) continue;
    // filter layers (e.g. "QoS Packet Scheduler") sit on top of the same adapter and
    // repeat its byte counts; count the adapter once
    if (r.InterfaceAndOperStatusFlags.FilterInterface) continue;
    if (r.OperStatus!=IfOperStatusUp) continue;
    rx+=r.InOctets;
    tx+=r.OutOctets;
  }
  FreeMibTable(tb);
  return 0;
}
