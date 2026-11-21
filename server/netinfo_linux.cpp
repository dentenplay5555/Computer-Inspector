#include "netinfo.hpp"
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
netif ipvfour() {
  struct ifaddrs *inter=nullptr;
  if (getifaddrs(&inter)==-1) return netif{};
  netif rst;
  for (auto *ifa=inter; ifa!=nullptr; ifa=ifa->ifa_next) {
    if (!ifa->ifa_addr) continue;
    if (ifa->ifa_addr->sa_family!=AF_INET) continue;
    if (!(ifa->ifa_flags&IFF_UP)) continue;
    if ((ifa->ifa_flags&IFF_LOOPBACK)) continue;
    char ip[INET_ADDRSTRLEN];
    auto *addr=reinterpret_cast<sockaddr_in*>(ifa->ifa_addr);
    if(!inet_ntop(AF_INET,&addr->sin_addr,ip,sizeof(ip)))continue;
    rst.net_inter=ifa->ifa_name;
    rst.net_ip=ip;
    break;
  }
  freeifaddrs(inter);
  return rst;
}
