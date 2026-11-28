#include "keystore.hpp"
#include "common/keys.hpp"
#include "agent/json.hpp"
#include <cctype>
#include <fstream>
#include <sstream>
using json=nlohmann::json;

bool load_key(const std::string& path,stored_key& k) {
  std::ifstream f(path,std::ios::binary);
  if (!f) return false;
  std::stringstream ss;
  ss<<f.rdbuf();
  json j=json::parse(ss.str(),nullptr,false);
  if (j.is_discarded()||!j.is_object()) return false;
  for (const char* f2:{"pin","id","key"}) if (!j.contains(f2)||!j[f2].is_string()) return false;
  k.pin=j["pin"].get<std::string>();
  k.id=j["id"].get<std::string>();
  k.key=j["key"].get<std::string>();
  return !k.key.empty()&&!k.id.empty();
}

bool save_key(const std::string& path,const stored_key& k) {
  return write_secret_file(path,json{{"pin",k.pin},{"id",k.id},{"key",k.key}}.dump(2)+"\n");
}

bool same_pin(const std::string& a,const std::string& b) {
  auto n=[](const std::string& s){std::string o; for (char c:s) if (std::isxdigit((unsigned char)c)) o+=(char)std::tolower((unsigned char)c); return o;};
  return !n(a).empty()&&n(a)==n(b);
}
