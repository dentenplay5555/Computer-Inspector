#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include "netinfo.hpp"
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <vector>
netif ipvfour() {
  netif ans;
  ULONG len=0,fl=GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
  if (GetAdaptersAddresses(AF_INET,fl,nullptr,nullptr,&len)!=ERROR_BUFFER_OVERFLOW)return ans;
  std::vector<char> buf(len);
  auto *aa=reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
  if (GetAdaptersAddresses(AF_INET,fl,nullptr,aa,&len)!=NO_ERROR) return ans;
  for(;aa;aa=aa->Next) {
    if(aa->OperStatus!=IfOperStatusUp||aa->IfType==IF_TYPE_SOFTWARE_LOOPBACK) continue;
    if(!aa->FirstUnicastAddress)continue;
    auto *sin=reinterpret_cast<sockaddr_in*>(aa->FirstUnicastAddress->Address.lpSockaddr);
    char ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET,&sin->sin_addr,ip,sizeof(ip));
    ans.net_inter=aa->AdapterName;
    ans.net_ip=ip;
    break;
  }
  return ans;
}
