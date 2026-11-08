#include <iostream>
#include <thread>
#include <chrono>
#include <cmath>
#include <unistd.h>
#include "metric.hpp"
using json=nlohmann::json;
int main() {
  char hn[200+200];
  int stau=gethostname(hn,sizeof(hn));
  col cor;
  while (1) {
    auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    json j;
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stats rev=cor.st();
    j=rev;
    j["type"]="metrics"; j["id"]=hn; j["os"]="linux"; j["ts"]=ms;
    std::cout<<j.dump()<<'\n';
  }
}
