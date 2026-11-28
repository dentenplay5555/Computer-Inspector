#include "auth.hpp"
#include "common/keys.hpp"
#include "agent/json.hpp"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <mbedtls/pkcs5.h>
#include <mbedtls/sha256.h>
using json=nlohmann::json;

static int64_t now_ms_() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}

static std::string hex(const unsigned char* p,size_t n) {
  static const char* hx="0123456789abcdef";
  std::string s;
  for (size_t i=0;i<n;i++) {s+=hx[p[i]>>4]; s+=hx[p[i]&15];}
  return s;
}
static std::string pbkdf2(const std::string& pw,const std::string& salt,int iter) {
  unsigned char out[32];
  if (mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256,(const unsigned char*)pw.data(),pw.size(),
                                    (const unsigned char*)salt.data(),salt.size(),(unsigned)iter,sizeof out,out)!=0) return std::string();
  return hex(out,sizeof out);
}
static std::string sha(const std::string& s) {
  unsigned char h[32];
  mbedtls_sha256((const unsigned char*)s.data(),s.size(),h,0);
  return hex(h,sizeof h);
}

auth::auth(std::string dir):dir_(std::move(dir)) {}

bool auth::load(std::string& err) {
  std::lock_guard<std::mutex> lock(mu_);
  std::string p=dir_+"/auth.json";
  std::error_code ec;
  if (!std::filesystem::exists(p,ec)) return true;       // no password yet
  std::ifstream f(p,std::ios::binary);
  std::stringstream ss;
  ss<<f.rdbuf();
  json j=json::parse(ss.str(),nullptr,false);
  if (j.is_discarded()||!j.is_object()||!j.contains("pw")||!j["pw"].is_object()) {
    err=p+" is damaged; remove it to set a new teacher password";
    return false;
  }
  salt_=j["pw"].value("salt","");
  hash_=j["pw"].value("hash","");
  iter_=j["pw"].value("iter",0);
  if (salt_.empty()||hash_.empty()||iter_<=0) {err=p+" is damaged; remove it to set a new teacher password"; return false;}
  return true;
}

bool auth::save() {
  json j={{"pw",{{"salt",salt_},{"iter",iter_},{"hash",hash_}}}};
  return write_secret_file(dir_+"/auth.json",j.dump(2)+"\n");
}

bool auth::has_password() const {std::lock_guard<std::mutex> lock(mu_); return !hash_.empty();}

std::string auth::set_password(const std::string& pw) {
  if (pw.size()<8) return "too short";
  if (pw.size()>256) return "too long";
  std::string salt=rand_hex(16);
  const int iter=600000;                                  // owasp 2023 for pbkdf2-hmac-sha256
  std::string h=pbkdf2(pw,salt,iter);
  if (salt.empty()||h.empty()) return "cannot hash";
  std::lock_guard<std::mutex> lock(mu_);
  std::string os=salt_,oh=hash_; int oi=iter_;
  salt_=salt; hash_=h; iter_=iter;
  if (!save()) {salt_=os; hash_=oh; iter_=oi; return "cannot save";}
  return "";
}

bool auth::check_password(const std::string& pw) const {
  std::string salt,h; int iter;
  {std::lock_guard<std::mutex> lock(mu_); salt=salt_; h=hash_; iter=iter_;}
  if (h.empty()) return false;
  return same_secret(pbkdf2(pw,salt,iter),h);
}

std::string auth::new_session() {
  std::string t=rand_hex(32);
  if (t.empty()) return t;
  int64_t n=now_ms_();
  std::lock_guard<std::mutex> lock(mu_);
  sess_[sha(t)]=sess{n,n};
  return t;
}

std::string auth::login(const std::string& ip,const std::string& pw,std::string& why,int& retry_s) {
  int64_t n=now_ms_();
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it=fail_.find(ip);
    if (it!=fail_.end()&&it->second.until>n) {
      why="locked";
      retry_s=(int)((it->second.until-n+999)/1000);
      return std::string();
    }
  }
  if (!check_password(pw)) {
    std::lock_guard<std::mutex> lock(mu_);
    fails& f=fail_[ip];
    if (++f.n>=5) {
      f.lock_ms=f.lock_ms?std::min<int64_t>(f.lock_ms*2,15LL*60*1000):60000;
      f.until=n+f.lock_ms;
      f.n=0;
      retry_s=(int)(f.lock_ms/1000);
      why="locked";
    } else why="wrong";
    return std::string();
  }
  {std::lock_guard<std::mutex> lock(mu_); fail_.erase(ip);}
  return new_session();
}

bool auth::session(const std::string& token,int64_t now) {
  if (token.size()!=64) return false;
  std::lock_guard<std::mutex> lock(mu_);
  auto it=sess_.find(sha(token));
  if (it==sess_.end()) return false;
  if (now-it->second.seen>IDLE_MS||now-it->second.created>MAX_MS) {sess_.erase(it); return false;}
  it->second.seen=now;
  return true;
}

void auth::logout(const std::string& token) {std::lock_guard<std::mutex> lock(mu_); sess_.erase(sha(token));}

void auth::logout_others(const std::string& keep) {
  std::lock_guard<std::mutex> lock(mu_);
  std::string k=sha(keep);
  for (auto it=sess_.begin();it!=sess_.end();) it=it->first==k?std::next(it):sess_.erase(it);
}

int auth::active(int64_t now,int64_t within_ms) const {
  std::lock_guard<std::mutex> lock(mu_);
  int c=0;
  for (auto&[k,s]:sess_) if (now-s.seen<=within_ms) c++;
  return c;
}

void auth::audit(const std::string& ip,const std::string& what) {
  std::time_t t=std::time(nullptr);
  std::tm u{};
#ifdef _WIN32
  localtime_s(&u,&t);
#else
  localtime_r(&t,&u);
#endif
  char ts[32];
  std::strftime(ts,sizeof ts,"%Y-%m-%d %H:%M:%S",&u);
  std::string line=std::string(ts)+"  "+(ip.empty()?"-":ip)+"  "+what;
  for (char& c:line) if (c=='\n'||c=='\r') c=' ';      // one event, one line
  std::lock_guard<std::mutex> lock(log_);
  std::ofstream f(dir_+"/audit.log",std::ios::app|std::ios::binary);
  f<<line<<'\n';
}
