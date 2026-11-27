// macos version of the readers in metric.hpp (macOS has no /proc)
// same units as linux.cpp: cpu in ticks, memory in kB, network in total bytes
#include <iostream>
#include <cstdint>
#include <vector>
#include <sys/statvfs.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/route.h>
#include <mach/mach.h>
#include <unistd.h>
#include "metric.hpp"

// cpu ticks for all cores together, idle counted the same way as linux.cpp
bool recpt(ct &ou) {
  host_cpu_load_info_data_t info;
  mach_msg_type_number_t n=HOST_CPU_LOAD_INFO_COUNT;
  if (host_statistics(mach_host_self(),HOST_CPU_LOAD_INFO,(host_info_t)&info,&n)!=KERN_SUCCESS) {
    std::cerr<<"host_statistics (cpu) failed\n";
    return 1;
  }
  uint64_t t=0;
  for (int i=0;i<CPU_STATE_MAX;i++) t+=info.cpu_ticks[i];
  ou.i=info.cpu_ticks[CPU_STATE_IDLE];
  ou.t=t;
  return 0;
}

// used memory as activity monitor counts it: app memory + wired + compressed
// (app memory = internal pages that are not purgeable); available = total - used
bool remem(uint64_t &t,uint64_t &av) {
  uint64_t total=0;
  size_t len=sizeof total;
  if (sysctlbyname("hw.memsize",&total,&len,nullptr,0)!=0||total==0) {
    std::cerr<<"sysctl hw.memsize failed\n";
    return 1;
  }
  vm_statistics64_data_t vm;
  mach_msg_type_number_t n=HOST_VM_INFO64_COUNT;
  if (host_statistics64(mach_host_self(),HOST_VM_INFO64,(host_info64_t)&vm,&n)!=KERN_SUCCESS) {
    std::cerr<<"host_statistics64 (memory) failed\n";
    return 1;
  }
  vm_size_t page=0;
  host_page_size(mach_host_self(),&page);
  uint64_t app=vm.internal_page_count>vm.purgeable_count?vm.internal_page_count-vm.purgeable_count:0;
  uint64_t used=(app+vm.wire_count+vm.compressor_page_count)*(uint64_t)page;
  if (used>total) used=total;
  t=total/1024;
  av=(total-used)/1024;
  return 0;
}

// bytes in and out of every interface except loopback; NET_RT_IFLIST2 gives 64-bit
// counters (getifaddrs only has 32-bit ones, which wrap after 4 GB)
bool renet(uint64_t &rx,uint64_t &tx) {
  rx=0,tx=0;
  int mib[6]={CTL_NET,PF_ROUTE,0,0,NET_RT_IFLIST2,0};
  size_t len=0;
  if (sysctl(mib,6,nullptr,&len,nullptr,0)!=0) {std::cerr<<"sysctl NET_RT_IFLIST2 failed\n";return 1;}
  std::vector<char> buf(len);
  if (sysctl(mib,6,buf.data(),&len,nullptr,0)!=0) {std::cerr<<"sysctl NET_RT_IFLIST2 failed\n";return 1;}
  for (char*p=buf.data();p<buf.data()+len;) {
    auto*h=reinterpret_cast<if_msghdr*>(p);
    if (h->ifm_msglen==0) break;
    if (h->ifm_type==RTM_IFINFO2) {
      auto*h2=reinterpret_cast<if_msghdr2*>(p);
      if (!(h2->ifm_flags&IFF_LOOPBACK)) {
        rx+=h2->ifm_data.ifi_ibytes;
        tx+=h2->ifm_data.ifi_obytes;
      }
    }
    p+=h->ifm_msglen;
  }
  return 0;
}

// same as linux: the volume that holds "/" (on apfs this reports the whole container)
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

int ghn(char* hon,size_t sohn) {return gethostname(hon,sohn);}
