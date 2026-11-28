#include "core.hpp"
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>
#include <cctype>
#include <filesystem>
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXHttpServer.h>
#include "agent/json.hpp"
#include "common/exedir.hpp"
#include "common/tlspin.hpp"
#include "common/keys.hpp"
#include <ixwebsocket/IXHttpServer.h>
#include <ixwebsocket/IXSocketTLSOptions.h>
using json=nlohmann::json;

static std::string read_f(const std::string&pth) {
  std::ifstream f(pth,std::ios::binary);
  std::stringstream ss;
  ss<<f.rdbuf();
  return ss.str();
}
// Puts tmp in place of dst. Some Windows C++ libraries refuse to rename onto an
// existing file, so if the first try fails, remove dst and try again.
static bool replace_file(const std::string&tmp,const std::string&dst) {
  std::error_code ec;
  std::filesystem::rename(tmp,dst,ec);
  if (!ec) return true;
  std::filesystem::remove(dst,ec);
  std::filesystem::rename(tmp,dst,ec);
  if (!ec) return true;
  std::filesystem::remove(tmp,ec);
  return false;
}
int64_t now_ms() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
const char* status_of(const mah&m,int64_t n) {
  if (n-m.l_s>30000) return "offline";
  else if (m.cpu>=90||m.ram>=90||m.disk>=95) return "crit";
  else if (m.cpu>=80||m.ram>=80||m.disk>=85) return "warn";
  return "ok";
}
// "%41-01" -> "A-01". returns false on a malformed escape
static bool url_dec(const std::string&in,std::string&out) {
  out.clear();
  for (size_t i=0;i<in.size();i++) {
    if (in[i]=='%') {
      if (i+2>=in.size()||!std::isxdigit((unsigned char)in[i+1])||!std::isxdigit((unsigned char)in[i+2])) return false;
      out+=(char)std::stoi(in.substr(i+1,2),nullptr,16); i+=2;
    } else if (in[i]=='+') out+=' ';
    else out+=in[i];
  }
  return true;
}
// seat map file. same rules as checkLayout() in dashboard/index.html
// {"cols":1..30, "rows":1..50 (optional), "seats":{"<id>":[col,row]},
//  "labels":[{"c","r","w","h","text"}] (optional, text required)}; no two blocks may overlap
static bool check_layout(const json&j,std::string&why) {
  auto is_int=[](const json&v,long lo,long hi){return v.is_number_integer()&&v.get<long>()>=lo&&v.get<long>()<=hi;};
  if (!j.is_object()) {why="not an object"; return false;}
  if (!j.contains("cols")||!is_int(j["cols"],1,30)) {why="cols must be 1-30"; return false;}
  long cols=j["cols"].get<long>(), rows=50;
  if (j.contains("rows")) {
    if (!is_int(j["rows"],1,50)) {why="rows must be 1-50"; return false;}
    rows=j["rows"].get<long>();
  }
  if (!j.contains("seats")||!j["seats"].is_object()) {why="seats must be an object"; return false;}
  struct box {long c,r,w,h;};
  std::vector<box> bs;
  for (auto&[id,p]:j["seats"].items()) {
    if (id.empty()||id.size()>64) {why="bad seat id"; return false;}
    if (!p.is_array()||p.size()!=2||!is_int(p[0],0,cols-1)||!is_int(p[1],0,rows-1)) {why="bad seat "+id; return false;}
    bs.push_back({p[0].get<long>(),p[1].get<long>(),1,1});
  }
  if (j.contains("labels")) {
    if (!j["labels"].is_array()||j["labels"].size()>1500) {why="labels must be a list"; return false;}
    for (auto&lb:j["labels"]) {
      if (!lb.is_object()||!lb.contains("c")||!lb.contains("r")||!lb.contains("w")||!lb.contains("h")) {why="bad label"; return false;}
      if (!is_int(lb["c"],0,cols-1)||!is_int(lb["r"],0,rows-1)||!is_int(lb["w"],1,cols)||!is_int(lb["h"],1,rows)) {why="bad label"; return false;}
      box b{lb["c"].get<long>(),lb["r"].get<long>(),lb["w"].get<long>(),lb["h"].get<long>()};
      if (b.c+b.w>cols||b.r+b.h>rows) {why="label outside the room"; return false;}
      if (!lb.contains("text")||!lb["text"].is_string()||lb["text"].get<std::string>().find_first_not_of(" \t")==std::string::npos
          ||lb["text"].get<std::string>().size()>160) {why="label needs text (up to 40 characters)"; return false;}
      bs.push_back(b);
    }
  }
  for (size_t a=0;a<bs.size();a++) for (size_t b=a+1;b<bs.size();b++)
    if (bs[a].c<bs[b].c+bs[b].w&&bs[b].c<bs[a].c+bs[a].w&&bs[a].r<bs[b].r+bs[b].h&&bs[b].r<bs[a].r+bs[a].h) {why="two blocks overlap"; return false;}
  return true;
}
// one seat or label per line, so the file stays easy to edit by hand
static std::string fmt_layout(const json&j) {
  auto d=[](const json&v){return v.dump(-1,' ',false,json::error_handler_t::replace);};
  std::string o="{\n  \"cols\": "+d(j["cols"]);
  if (j.contains("rows")) o+=",\n  \"rows\": "+d(j["rows"]);
  o+=",\n  \"seats\": {";
  bool first=true;
  for (auto&[id,p]:j["seats"].items()) {
    o+=std::string(first?"":",")+"\n    "+d(json(id))+": ["+d(p[0])+", "+d(p[1])+"]";
    first=false;
  }
  o+=first?"}":"\n  }";
  if (j.contains("labels")&&!j["labels"].empty()) {
    o+=",\n  \"labels\": [";
    first=true;
    for (auto&lb:j["labels"]) {
      o+=std::string(first?"":",")+"\n    {\"c\": "+d(lb["c"])+", \"r\": "+d(lb["r"])+", \"w\": "+d(lb["w"])+", \"h\": "+d(lb["h"])+", \"text\": "+d(lb["text"])+"}";
      first=false;
    }
    o+="\n  ]";
  }
  return o+"\n}\n";
}
static bool is_loopback(const std::string&ip) {
  return ip=="::1"||ip.rfind("127.",0)==0||ip.rfind("::ffff:127.",0)==0;
}
static json b_snp(const std::vector<mah>&snp,const std::vector<core::removed_pc>&rm,int64_t n,const history&h) {
  json arr=json::array();
  for (auto&m:snp) {
    arr.push_back({
      {"id",m.id},{"ip",m.ip},{"os",m.os},
      {"status",status_of(m,n)},
      {"cpu",m.cpu},{"ram",m.ram},{"disk",m.disk},
      {"rx",m.rx},{"tx",m.tx},
      {"age",n-m.l_s},
      {"abn",h.abnormal(m.id,n)},
      {"learn",h.learning(m.id,n)},
      {"name",m.name},{"desc",m.desc}
    });
  }
  json ra=json::array();
  for (auto&r:rm) ra.push_back({{"id",r.id},{"name",r.name}});
  return {{"type","snapshot"},{"ts",n},{"machines",arr},{"removed",ra}};
}

core::core()=default;
core::~core() {stop();}

void core::log(lvl l,const std::string&s) {if (on_log) on_log(l,s);}

// metrics from a signed-in agent connection; id is the one it signed in as, whatever the
// message says, so a pc cannot send data in another pc's name
void core::hmsg(const std::string&ip,const std::string&aid,const std::string&txt) {
  try {
    json j=json::parse(txt,nullptr,false);
    if (j==nlohmann::json::value_t::discarded)return;
    if (!j.is_object())return;
    if (j.value("type","")!="metrics")return;
    std::string id=aid;
    if (id.empty()) return;
    double cpu=j.value("cpu",0.0),ram=0;
    if (j.contains("ram")&&j["ram"].is_object())ram=j["ram"].value("per",0.0);
    double tx=0,rx=0;
    if (j.contains("net")&&j["net"].is_object()){tx=j["net"].value("tx_kbps",0.0);rx=j["net"].value("rx_kbps",0.00);}
    double disk=0;
    if (j.contains("disk")&&j["disk"].is_object())disk=j["disk"].value("per",0.0);
    std::string os=j.value("os","");
    if (!std::isfinite(cpu) || !std::isfinite(ram) || !std::isfinite(disk) || !std::isfinite(rx) || !std::isfinite(tx))return;
    if (cpu<0 || cpu>100 || ram<0 || ram>100 || disk<0 || disk>100 || rx<0 || tx<0) return;
    int64_t now=now_ms();
    {
    std::lock_guard<std::mutex> lock(mu_);
    if (removed_.count(id)) return;    // removed on the dashboard: ignored until added back
    mah&m=mah_[id];
    m.id=id;
    m.ip=ip;
    m.cpu=cpu;
    m.ram=ram;
    m.tx=tx;
    m.rx=rx;
    m.disk=disk;
    m.l_s=now;
    m.os=os;
    }
    hist_->add(id,now,cpu,rx,tx);
  } catch (json::exception&) {return;}
}

bool core::start(std::string&err) {
  if (run_) return true;
  if (!meta_loaded_) {load_meta(); meta_loaded_=true;}
  static bool tls_ok=tls_global_init();      // also feeds the random numbers for keys
  if (!keys_) {
    auto k=std::make_unique<agent_keys>(exe_dir()+"/agents.json");
    k->rotate_ms=rotate_ms;
    if (!tls_ok||!k->load(err)) {err="agent keys: "+(tls_ok?err:std::string("no random numbers")); log(lvl::err,err); return false;}
    keys_=std::move(k);
  }
  if (!auth_) {
    auto a=std::make_unique<auth>(exe_dir());
    if (!a->load(err)) {err="teacher login: "+err; log(lvl::err,err); return false;}
    auth_=std::move(a);
  }
  if (!auth_->has_password()) log(lvl::warn,"No teacher password yet: set it in the server window, with server --set-password, or in the dashboard on this computer");
  log(lvl::info,"Join key for new agents: "+keys_->join_key()+"  (agent --join-key ...; each pc then gets its own key)");
  if (!hist_) {
    hist_=std::make_unique<history>(hist);
    size_t pts=hist_->load();
    log(lvl::info,"History: "+std::to_string(pts)+" saved points loaded from "+(hist.dir.empty()?std::string("(memory only)"):hist.dir+"/")+", learning "+std::to_string(hist.learn_ms/60000)+" min");
  }
  {
    netif rst=ipvfour();
    std::lock_guard<std::mutex> lock(mu_);
    net_=rst;
  }
  if (net_.net_ip.empty()) log(lvl::warn,"Network IPv4 not found");
  else log(lvl::info,"Network interface: "+net_.net_inter+", local ip: "+net_.net_ip);

  // tls for both ports: agents use wss://, the dashboard https://
  static bool tls_ready=tls_global_init();
  if (!tls_ready) {err="TLS could not be initialised"; log(lvl::err,err); return false;}
  {
    tls_id t;
    if (!load_or_make_cert(exe_dir(),t,err)) {err="TLS certificate: "+err; log(lvl::err,err); return false;}
    std::lock_guard<std::mutex> lock(mu_);
    tls_=t;
  }
  log(lvl::info,"TLS certificate: "+tls_.cert);
  log(lvl::info,"Agent pin: "+fp_pin(tls_.fp)+"  (agent --pin "+fp_pin(tls_.fp)+")");
  ix::SocketTLSOptions tlso;
  tlso.tls=true;
  tlso.certFile=tls_.cert;
  tlso.keyFile=tls_.key;
  tlso.caFile="NONE";                // clients do not present certificates

  // backlog 128 (ixwebsocket's default is 5): a classroom of pcs reconnecting at once after
  // a restart must not be turned away
  wsr_=std::make_unique<ix::WebSocketServer>(ws_port,"0.0.0.0",128);
  wsr_->setTLSOptions(tlso);
  wsr_->setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState> state,ix::WebSocket&ws,const ix::WebSocketMessagePtr&msg){
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: {
        // every connection on this port is an agent and must sign in (the dashboard uses https)
        agent_open(ws);
        log(lvl::info,"agent connected: "+state->getRemoteIp());
        break;
      }
      case ix::WebSocketMessageType::Close: {
        {std::lock_guard<std::mutex> lock(mu_); conns_.erase(&ws);}
        log(lvl::info,"agent disconnected: "+state->getRemoteIp());
        break;
      }
      case ix::WebSocketMessageType::Error:
        log(lvl::err,"An error has occur: "+msg->errorInfo.reason);
        break;
      case ix::WebSocketMessageType::Message: {
        agent_msg(ws,state->getRemoteIp(),msg->str);
        break;
      }
      default: break;
    }
  });
  auto rc=wsr_->listen();
  if (!rc.first) {
    err="WebSocket port "+std::to_string(ws_port)+": "+rc.second;
    log(lvl::err,err);
    wsr_.reset();
    return false;
  }
  wsr_->start();
  log(lvl::info,"Agents can connect on port "+std::to_string(ws_port));

  http_=std::make_unique<ix::HttpServer>(http_port,"0.0.0.0",128);
  http_->setTLSOptions(tlso);
  http_->setOnConnectionCallback(
    [this](ix::HttpRequestPtr rq,std::shared_ptr<ix::ConnectionState> state)->ix::HttpResponsePtr {
      return http(rq,state->getRemoteIp());
    }
  );
  auto hr=http_->listen();
  if (!hr.first) {
    err="HTTP port "+std::to_string(http_port)+": "+hr.second;
    log(lvl::err,err);
    http_.reset();
    wsr_->stop();
    wsr_.reset();
    return false;
  }
  http_->start();
  log(lvl::info,"Dashboard is served at https://"+(net_.net_ip.empty()?std::string("127.0.0.1"):net_.net_ip)+":"+std::to_string(http_port));

  run_=true;
  th_=std::thread([this]{loop();});
  return true;
}

void core::loop() {
  int tick=0;
  while (run_) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!run_) break;
    {
      // connections that did not sign in within 10 s (refused, or never tried)
      std::vector<std::shared_ptr<ix::WebSocket>> late;
      int64_t t=now_ms();
      for (const auto&c:wsr_->getClients()) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it=conns_.find(c.get());
        if (it!=conns_.end()&&!it->second.authed&&t-it->second.opened>10000) late.push_back(c);
      }
      for (auto&w:late) w->close();
    }
    if (++tick%10==0) {
      // keys that got old while their pc stayed connected
      std::vector<std::pair<std::shared_ptr<ix::WebSocket>,std::string>> on;
      for (const auto&c:wsr_->getClients()) {
        std::lock_guard<std::mutex> lock(mu_);
        auto it=conns_.find(c.get());
        if (it!=conns_.end()&&it->second.authed&&!it->second.rekeying) on.push_back({c,it->second.id});
      }
      for (auto&[w,id]:on) offer_rotation(*w,id);
    }
    std::vector<mah> snp=machines();
    int64_t n=now_ms();
    try {
      std::string pl=b_snp(snp,removed(),n,*hist_).dump(-1,' ',false,json::error_handler_t::replace);
      std::lock_guard<std::mutex> lock(mu_);
      snap_=pl;
    } catch (const json::exception&e) {
      log(lvl::err,std::string("JSON EXCEPTION ERROR: ")+e.what());
    }
    if (on_tick) on_tick(snp,n);
  }
}

void core::stop() {
  if (!run_) return;
  run_=false;
  if (th_.joinable()) th_.join();
  if (http_) {http_->stop(); http_.reset();}
  if (wsr_) {wsr_->stop(); wsr_.reset();}
  log(lvl::info,"Server stopped");
}

std::string core::forget(const std::string&id) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!mah_.count(id)&&!meta_.count(id)) return "unknown";
    mah_.erase(id);
    removed_.insert(id);
  }
  save_meta();
  log(lvl::info,"Removed "+id+" from the list. Its data is ignored until it is added back on the dashboard.");
  return "";
}

std::string core::restore(const std::string&id) {
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!removed_.erase(id)) return "unknown";
  }
  save_meta();
  log(lvl::info,"Added "+id+" back. It shows again when its agent sends data.");
  return "";
}

std::string core::set_meta(const std::string&id,const std::string&name,const std::string&desc) {
  // bytes: 40 thai characters are 120 bytes in utf-8, 200 are 600
  if (name.size()>120||desc.size()>600) return "too long";
  {
    std::lock_guard<std::mutex> lock(mu_);
    if (!mah_.count(id)&&!meta_.count(id)&&!removed_.count(id)) return "unknown";
    if (name.empty()&&desc.empty()) meta_.erase(id);
    else meta_[id]={name,desc};
  }
  save_meta();
  return "";
}

std::vector<core::removed_pc> core::removed() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<removed_pc> v;
  for (auto&id:removed_) {
    auto it=meta_.find(id);
    v.push_back({id,it==meta_.end()?std::string():it->second.name});
  }
  return v;
}

bool core::is_local(const std::string&ip) const {
  if (is_loopback(ip)) return true;
  std::lock_guard<std::mutex> lock(mu_);
  return !net_.net_ip.empty()&&(ip==net_.net_ip||ip=="::ffff:"+net_.net_ip);
}

// machines.json: {"machines":{"<id>":{"name":"...","desc":"..."}},"removed":["<id>"]}
void core::load_meta() {
  std::string p=exe_dir()+"/machines.json";
  std::error_code ec;
  if (!std::filesystem::exists(p,ec)) return;
  json j=json::parse(read_f(p),nullptr,false);
  if (j.is_discarded()||!j.is_object()) {log(lvl::warn,"machines.json is not valid JSON, ignoring it");return;}
  std::lock_guard<std::mutex> lock(mu_);
  if (j.contains("machines")&&j["machines"].is_object())
    for (auto&[id,v]:j["machines"].items()) {
      if (!v.is_object()) continue;
      meta m{v.value("name",""),v.value("desc","")};
      if (!m.name.empty()||!m.desc.empty()) meta_[id]=m;
    }
  if (j.contains("removed")&&j["removed"].is_array())
    for (auto&id:j["removed"]) if (id.is_string()) removed_.insert(id.get<std::string>());
}

void core::save_meta() {
  json j={{"machines",json::object()},{"removed",json::array()}};
  {
    std::lock_guard<std::mutex> lock(mu_);
    for (auto&[id,m]:meta_) j["machines"][id]={{"name",m.name},{"desc",m.desc}};
    for (auto&id:removed_) j["removed"].push_back(id);
  }
  std::lock_guard<std::mutex> io(io_);
  std::string p=exe_dir()+"/machines.json", tmp=p+".tmp";
  {
    std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
    f<<j.dump(2,' ',false,json::error_handler_t::replace)<<'\n';
    if (!f) {log(lvl::err,"Cannot write "+tmp);return;}
  }
  if (!replace_file(tmp,p)) log(lvl::err,"Cannot replace "+p);
}

// ---------- agent sign-in (see README "Joining and keys") ----------
void core::agent_open(ix::WebSocket&ws) {
  std::string nonce=rand_hex(16);
  {std::lock_guard<std::mutex> lock(mu_); conns_[&ws]=conn{nonce,"",false,false,now_ms(),0};}
  ws.send(json{{"type","hello"},{"nonce",nonce}}.dump());
}

void core::agent_msg(ix::WebSocket&ws,const std::string&ip,const std::string&txt) {
  json j=json::parse(txt,nullptr,false);
  if (j.is_discarded()||!j.is_object()) return;
  std::string type=j.value("type","");
  conn c;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it=conns_.find(&ws);
    if (it==conns_.end()) return;
    c=it->second;
  }
  auto str=[&](const char*k){return j.contains(k)&&j[k].is_string()?j[k].get<std::string>():std::string();};
  if (c.authed) {
    if (type=="metrics") hmsg(ip,c.id,txt);
    else if (type=="rekeyed") {
      bool ok=keys_->finish_rotation(c.id,c.nonce,str("sig"),now_ms());
      {std::lock_guard<std::mutex> lock(mu_); auto it=conns_.find(&ws); if (it!=conns_.end()) it->second.rekeying=false;}
      if (ok) {log(lvl::info,"Key rotated: "+c.id); if (auth_) auth_->audit(ip,"key of "+c.id+" rotated");}
      else log(lvl::warn,"Key rotation for "+c.id+" was not confirmed; the old key stays valid and it is tried again");
    }
    return;
  }
  if (type!="join"&&type!="auth") return;     // nothing else counts before signing in
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it=conns_.find(&ws);
    if (it==conns_.end()||++it->second.tries>3) return;   // a few tries per connection
  }
  // the answer stays on the connection (closing at once could overtake it); the agent
  // stops or joins again, and the tick thread closes connections still not signed in
  auto deny=[&](const std::string&why,const std::string&what){
    log(lvl::warn,what+" ("+ip+")");
    if (auth_) auth_->audit(ip,"agent refused: "+what);
    ws.send(json{{"type","denied"},{"why",why}}.dump());
  };
  std::string id=str("id");
  if (id.empty()||id.size()>100) return deny("bad_id","An agent without a valid id was refused");
  std::string key;
  if (type=="join") {
    auto v=keys_->join(id,c.nonce,str("proof"),key);
    if (v==agent_keys::verdict::already_joined)
      return deny("already_joined",id+" tried to join again. If it is the same pc (reinstalled), revoke its key on the dashboard first");
    if (v!=agent_keys::verdict::ok) return deny("bad_join_key",id+" tried to join with a wrong join key");
    log(lvl::info,id+" joined and got its own key");
    if (auth_) auth_->audit(ip,"agent "+id+" joined");
  } else {
    auto v=keys_->auth(id,c.nonce,str("sig"));
    if (v==agent_keys::verdict::not_joined) return deny("not_joined",id+" is not joined (never joined, or its key was revoked)");
    if (v!=agent_keys::verdict::ok) return deny("bad_key",id+" signed in with a wrong key");
  }
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it=conns_.find(&ws);
    if (it==conns_.end()) return;
    it->second.authed=true;
    it->second.id=id;
  }
  if (type=="join") ws.send(json{{"type","welcome"},{"id",id},{"key",key}}.dump());
  else ws.send(json{{"type","ok"},{"id",id}}.dump());
  offer_rotation(ws,id);
}

void core::offer_rotation(ix::WebSocket&ws,const std::string&id) {
  if (!keys_->rotation_due(id,now_ms())) return;
  {
    std::lock_guard<std::mutex> lock(mu_);
    auto it=conns_.find(&ws);
    if (it==conns_.end()||it->second.rekeying) return;
    it->second.rekeying=true;
  }
  std::string k=keys_->start_rotation(id);
  if (k.empty()) return;
  ws.send(json{{"type","rekey"},{"key",k}}.dump());
}

std::string core::join_key() const {return keys_?keys_->join_key():std::string();}
std::string core::new_join_key() {
  if (!keys_) return std::string();
  std::string k=keys_->new_join_key();
  log(lvl::info,"New join key: "+k+" (the old one no longer works for new pcs)");
  if (auth_) auth_->audit("server","new join key made");
  return k;
}
bool core::joined(const std::string&id) const {return keys_&&keys_->joined(id);}

bool core::has_password() const {return auth_&&auth_->has_password();}
std::string core::set_password(const std::string&pw,const std::string&who) {
  if (!auth_) return "not started";
  std::string why=auth_->set_password(pw);
  if (why.empty()) {auth_->audit(who,"teacher password set"); log(lvl::info,"Teacher password set");}
  return why;
}

bool core::revoke_key(const std::string&id) {
  if (!keys_||!keys_->revoke(id)) return false;
  // disconnect it now; outside mu_, since closing calls back into the server
  std::vector<std::shared_ptr<ix::WebSocket>> drop;
  if (wsr_) for (const auto&c:wsr_->getClients()) {
    std::lock_guard<std::mutex> lock(mu_);
    auto it=conns_.find(c.get());
    if (it!=conns_.end()&&it->second.id==id) drop.push_back(c);
  }
  for (auto&w:drop) w->close();
  log(lvl::info,"Key of "+id+" revoked; it must join again with the join key");
  return true;
}

// ---------- dashboard http (see README "Teacher login and audit log") ----------
static std::string cookie_of(const ix::HttpRequestPtr&rq,const std::string&name) {
  auto it=rq->headers.find("Cookie");
  if (it==rq->headers.end()) return std::string();
  const std::string&c=it->second;
  for (size_t p=0;p<c.size();) {
    size_t e=c.find(';',p);
    std::string kv=c.substr(p,e==std::string::npos?std::string::npos:e-p);
    while (!kv.empty()&&kv[0]==' ') kv.erase(0,1);
    if (kv.rfind(name+"=",0)==0) return kv.substr(name.size()+1);
    if (e==std::string::npos) break;
    p=e+1;
  }
  return std::string();
}

ix::HttpResponsePtr core::http(ix::HttpRequestPtr rq,const std::string&ip) {
  ix::WebSocketHttpHeaders head;
  // the page may not be framed by other sites, and the browser must not guess types
  head["X-Frame-Options"]="DENY";
  head["X-Content-Type-Options"]="nosniff";
  head["Referrer-Policy"]="no-referrer";
  head["Cache-Control"]="no-store";
  auto reply=[&](int code,const char*msg,const json&body){
    head["Content-Type"]="application/json";
    return std::make_shared<ix::HttpResponse>(code,msg,ix::HttpErrorCode::Ok,head,body.dump(-1,' ',false,json::error_handler_t::replace));
  };
  const std::string ck="ci_session";
  auto set_cookie=[&](const std::string&t){
    head["Set-Cookie"]=ck+"="+t+"; HttpOnly; Secure; SameSite=Strict; Path=/; Max-Age="+(t.empty()?"0":std::to_string(auth::MAX_MS/1000));
  };
  const std::string&u=rq->uri;
  const bool post=rq->method=="POST";
  int64_t now=now_ms();
  std::string token=cookie_of(rq,ck);
  bool in=auth_->session(token,now);

  // ---- open to everyone: the page, who am i, first set-up, login ----
  if (!post&&(u=="/"||u=="/index.html")) {
    std::string by=read_f("dashboard/index.html");
    if (by.empty()) return reply(404,"Not Found",{{"error","dashboard/index.html not found (run the server from the project folder)"}});
    head["Content-Type"]="text/html; charset=utf-8";
    return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,by);
  }
  if (!post&&u=="/me") {
    json me={{"local",is_local(ip)},{"logged_in",in},{"setup_needed",!auth_->has_password()}};
    if (in) {me["pin"]=pin(); me["join_key"]=join_key();}
    return reply(200,"OK",me);
  }
  json j;
  if (post) {
    // a json body only: a browser will not send that cross-site without asking first (cors)
    auto ct=rq->headers.find("Content-Type");
    if (ct==rq->headers.end()||ct->second.rfind("application/json",0)!=0) return reply(415,"Unsupported Media Type",{{"error","needs application/json"}});
    if (rq->body.size()>256*1024) return reply(413,"Payload Too Large",{{"error","too large"}});
    j=json::parse(rq->body,nullptr,false);
    if (j.is_discarded()||!j.is_object()) return reply(400,"Bad Request",{{"error","not a JSON object"}});
  }
  auto str=[&](const char*k){return j.contains(k)&&j[k].is_string()?j[k].get<std::string>():std::string();};
  if (post&&u=="/setup") {
    // the very first password: only from the server computer itself
    if (auth_->has_password()) return reply(409,"Conflict",{{"error","already set"}});
    if (!is_local(ip)) return reply(403,"Forbidden",{{"error","local only"}});
    std::string why=auth_->set_password(str("password"));
    if (!why.empty()) return reply(400,"Bad Request",{{"error",why}});
    auth_->audit(ip,"teacher password set (first time, dashboard)");
    log(lvl::info,"Teacher password set");
    set_cookie(auth_->new_session());
    return reply(200,"OK",{{"ok",true}});
  }
  if (post&&u=="/login") {
    if (!auth_->has_password()) return reply(409,"Conflict",{{"error","setup"}});
    std::string why; int retry=0;
    std::string t=auth_->login(ip,str("password"),why,retry);
    if (t.empty()) {
      if (why=="locked") {
        if (retry>0&&str("password").size()) auth_->audit(ip,"login refused: locked out for "+std::to_string(retry)+" s");
        return reply(429,"Too Many Requests",{{"error","locked"},{"retry_s",retry}});
      }
      auth_->audit(ip,"login failed (wrong password)");
      log(lvl::warn,"Dashboard login failed from "+ip);
      return reply(401,"Unauthorized",{{"error","wrong"}});
    }
    auth_->audit(ip,"login");
    set_cookie(t);
    return reply(200,"OK",{{"ok",true}});
  }

  // ---- everything below needs a teacher session ----
  if (!in) return reply(401,"Unauthorized",{{"error","login"}});

  if (post&&u=="/logout") {
    auth_->logout(token);
    auth_->audit(ip,"logout");
    set_cookie("");
    return reply(200,"OK",{{"ok",true}});
  }
  if (post&&u=="/password") {
    if (!auth_->check_password(str("old"))) {auth_->audit(ip,"password change refused (wrong current password)"); return reply(403,"Forbidden",{{"error","wrong"}});}
    std::string why=auth_->set_password(str("new"));
    if (!why.empty()) return reply(400,"Bad Request",{{"error",why}});
    auth_->logout_others(token);
    auth_->audit(ip,"teacher password changed (other sessions logged out)");
    log(lvl::info,"Teacher password changed");
    return reply(200,"OK",{{"ok",true}});
  }
  if (post&&(u=="/forget"||u=="/restore"||u=="/meta"||u=="/revoke")) {
    std::string id=str("id"), why;
    if (id.empty()) return reply(400,"Bad Request",{{"error","needs {\"id\": \"...\"}"}});
    if (u=="/forget") why=forget(id);
    else if (u=="/restore") why=restore(id);
    else if (u=="/revoke") why=revoke_key(id)?"":"unknown";
    else why=set_meta(id,str("name"),str("desc"));
    if (why=="unknown") return reply(404,"Not Found",{{"error","unknown id"}});
    if (!why.empty()) return reply(400,"Bad Request",{{"error",why}});
    if (u=="/forget") auth_->audit(ip,"removed "+id+" from the dashboard");
    else if (u=="/restore") auth_->audit(ip,"added "+id+" back");
    else if (u=="/revoke") auth_->audit(ip,"revoked the key of "+id);
    else auth_->audit(ip,"renamed "+id+" to \""+str("name")+"\", description \""+str("desc")+"\"");
    return reply(200,"OK",{{"ok",true}});
  }
  if (post&&u=="/layout.json") {
    std::string why;
    if (!check_layout(j,why)) return reply(400,"Bad Request",{{"error",why}});
    std::string lp=exe_dir()+"/layout.json", tmp=lp+".tmp";
    {
      std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
      f<<fmt_layout(j);
      if (!f) return reply(500,"Internal Server Error",{{"error","cannot write "+tmp}});
    }
    if (!replace_file(tmp,lp)) return reply(500,"Internal Server Error",{{"error","cannot replace "+lp}});
    log(lvl::info,"Seat map saved: "+lp);
    auth_->audit(ip,"seat map saved");
    return reply(200,"OK",{{"ok",true}});
  }
  if (!post&&u=="/snapshot") {
    // the dashboard polls this once a second (same port and certificate as the page)
    std::string body;
    {std::lock_guard<std::mutex> lock(mu_); body=snap_;}
    if (body.empty()) body=json{{"type","snapshot"},{"ts",now},{"machines",json::array()},{"removed",json::array()}}.dump();
    head["Content-Type"]="application/json";
    return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,body);
  }
  const std::string hp="/history?id=";
  if (!post&&u.compare(0,hp.size(),hp)==0) {
    std::string id;
    // only ids the server has already seen; anything else is 404
    if (url_dec(u.substr(hp.size()),id)&&hist_->has(id))
      return reply(200,"OK",hist_->series(id));
    return reply(404,"Not Found",{{"error","unknown id"}});
  }
  if (!post&&u=="/layout.json") {
    // seat map: only this exact path, read from the folder that holds the server executable
    std::string lp=exe_dir()+"/layout.json";
    std::error_code ec;
    auto sz=std::filesystem::file_size(lp,ec);
    if (!ec&&sz>0&&sz<=256*1024) {
      head["Content-Type"]="application/json";
      return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,read_f(lp));
    }
  }
  return reply(404,"Not Found",{{"error","not found"}});
}

std::string core::pin() const {std::lock_guard<std::mutex> lock(mu_); return tls_.fp.empty()?std::string():fp_pin(tls_.fp);}

netif core::net() const {std::lock_guard<std::mutex> lock(mu_); return net_;}

std::vector<mah> core::machines() const {
  std::lock_guard<std::mutex> lock(mu_);
  std::vector<mah> v;
  for (auto&[id,m]:mah_) {
    v.push_back(m);
    auto it=meta_.find(id);
    if (it!=meta_.end()) {v.back().name=it->second.name; v.back().desc=it->second.desc;}
  }
  return v;
}

// dashboards: teacher sessions that asked for data in the last 5 s
int core::dash_count() const {return auth_?auth_->active(now_ms(),5000):0;}

int core::agent_count() const {
  if (!run_||!wsr_) return 0;
  std::lock_guard<std::mutex> lock(mu_);
  int n=0;
  for (const auto&c:wsr_->getClients()) {auto it=conns_.find(c.get()); if (it!=conns_.end()&&it->second.authed) n++;}
  return n;
}

std::vector<std::string> core::abnormal(const std::string&id,int64_t now) const {
  return hist_?hist_->abnormal(id,now):std::vector<std::string>{};
}
bool core::learning(const std::string&id,int64_t now) const {return hist_&&hist_->learning(id,now);}
