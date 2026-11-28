#include "agentkeys.hpp"
#include "common/keys.hpp"
#include "agent/json.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
using json=nlohmann::json;

static int64_t now_ms_() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}

agent_keys::agent_keys(std::string path):path_(std::move(path)) {}

bool agent_keys::load(std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  std::error_code ec;
  if (!std::filesystem::exists(path_,ec)) {
    join_=make_join_key();
    if (join_.empty()) {err="no random numbers for the join key"; return false;}
    if (!save()) {err="cannot write "+path_; return false;}
    return true;
  }
  std::ifstream f(path_,std::ios::binary);
  std::stringstream ss;
  ss<<f.rdbuf();
  json j=json::parse(ss.str(),nullptr,false);
  if (j.is_discarded()||!j.is_object()||!j.contains("join_key")||!j["join_key"].is_string()) {
    err=path_+" is damaged; fix it, or remove it to start over (every pc must then join again)";
    return false;
  }
  join_=j["join_key"].get<std::string>();
  if (j.contains("agents")&&j["agents"].is_object())
    for (auto&[id,v]:j["agents"].items()) {
      if (!v.is_object()||!v.contains("key")||!v["key"].is_string()) continue;
      pc p;
      p.key=v["key"].get<std::string>();
      p.since=v.value("since",(int64_t)0);
      p.pending=v.value("pending","");
      pcs_[id]=p;
    }
  return true;
}

bool agent_keys::save() {
  json a=json::object();
  for (auto&[id,p]:pcs_) {
    json v={{"key",p.key},{"since",p.since}};
    if (!p.pending.empty()) v["pending"]=p.pending;
    a[id]=v;
  }
  json j={{"join_key",join_},{"agents",a}};
  return write_secret_file(path_,j.dump(2)+"\n");
}

std::string agent_keys::join_key() const {std::lock_guard<std::mutex> lock(mu_); return join_;}

std::string agent_keys::new_join_key() {
  std::lock_guard<std::mutex> lock(mu_);
  std::string k=make_join_key();
  if (k.empty()) return join_;
  join_=k;
  save();
  return join_;
}

agent_keys::verdict agent_keys::join(const std::string& id,const std::string& nonce,const std::string& proof,std::string& key) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!same_secret(proof,hmac_hex(norm_join_key(join_),msg_join(nonce,id)))) return verdict::bad_signature;
  // the join key alone must not take over a pc that already joined
  if (pcs_.count(id)) return verdict::already_joined;
  pc p;
  p.key=rand_hex(32);
  p.since=now_ms_();
  if (p.key.empty()) return verdict::bad_signature;
  pcs_[id]=p;
  save();
  key=p.key;
  return verdict::ok;
}

agent_keys::verdict agent_keys::auth(const std::string& id,const std::string& nonce,const std::string& sig) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it=pcs_.find(id);
  if (it==pcs_.end()) return verdict::not_joined;
  pc& p=it->second;
  if (same_secret(sig,hmac_hex(p.key,msg_auth(nonce,id)))) return verdict::ok;
  if (!p.pending.empty()&&same_secret(sig,hmac_hex(p.pending,msg_auth(nonce,id)))) {
    // the agent saved the rotated key but its confirmation never arrived: finish it now
    p.key=p.pending;
    p.pending.clear();
    p.since=now_ms_();
    save();
    return verdict::ok;
  }
  return verdict::bad_signature;
}

bool agent_keys::rotation_due(const std::string& id,int64_t now) const {
  std::lock_guard<std::mutex> lock(mu_);
  auto it=pcs_.find(id);
  return it!=pcs_.end()&&rotate_ms>0&&now-it->second.since>=rotate_ms;
}

std::string agent_keys::start_rotation(const std::string& id) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it=pcs_.find(id);
  if (it==pcs_.end()) return std::string();
  if (it->second.pending.empty()) {
    it->second.pending=rand_hex(32);
    if (it->second.pending.empty()) return std::string();
    save();                          // saved first: a crash now still accepts either key
  }
  return it->second.pending;
}

bool agent_keys::finish_rotation(const std::string& id,const std::string& nonce,const std::string& sig,int64_t now) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it=pcs_.find(id);
  if (it==pcs_.end()||it->second.pending.empty()) return false;
  if (!same_secret(sig,hmac_hex(it->second.pending,msg_rekey(nonce,id)))) return false;
  it->second.key=it->second.pending;
  it->second.pending.clear();
  it->second.since=now;
  save();
  return true;
}

bool agent_keys::revoke(const std::string& id) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!pcs_.erase(id)) return false;
  save();
  return true;
}

bool agent_keys::joined(const std::string& id) const {std::lock_guard<std::mutex> lock(mu_); return pcs_.count(id)>0;}
