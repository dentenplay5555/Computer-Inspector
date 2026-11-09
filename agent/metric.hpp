#pragma once
#include <cstdint>
#include "agent/json.hpp"
using json=nlohmann::json;
struct stats {
  long double cp_per=0,ra_pe=0,ds_pe=0;
  uint64_t ra_t=0,ra_us=0;
  long double ne_rx=0,ne_tx=0;
  uint64_t ds_us=0,ds_t=0;
};
struct ct {
  uint64_t i,t;
};
bool recpt(ct&); bool remem(uint64_t&, uint64_t&);
bool renet(uint64_t&,uint64_t&); bool reds(uint64_t&, uint64_t&);
void to_json(json&, const stats&); 
class col {
  public: 
    col();
    stats st();
  private:
    uint64_t p_ie=0,p_tt=0,p_rx=0,p_tx=0;
};
