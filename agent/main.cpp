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
    json j={
        {"cpu", 23.4},
        {"dis", {{"use",0000},{"tot",0000},{"per",0000}}},
        {"ram", {{"used",0000},{"total", 0000},{"oer", 0000}}},
        {"net", {{"rx",0000},{"tx",0000}}}
    };
    std::this_thread::sleep_for(std::chrono::seconds(1));
    stats rev=cor.st();
    j=rev;
    j["typ"]="metrics"; j["id"]=hn; j["os"]="linux";
    std::cout<<j.dump()<<'\n';
  }
}
