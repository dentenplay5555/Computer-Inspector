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
#include "exedir.hpp"
using json=nlohmann::json;

static std::string read_f(const std::string&pth) {
  std::ifstream f(pth,std::ios::binary);
  std::stringstream ss;
  ss<<f.rdbuf();
  return ss.str();
}
int64_t now_ms() {return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
const char* status_of(const mah&m,int64_t n) {
  if (n-m.l_s>30000) return "offline";
  else if (m.cpu>=90||m.ram>=90||m.disk>=95) return "crit";
  else if (m.cpu>=80||m.ram>=80||m.disk>=85) return "warn";
  return "ok";
}
// "%41-01" -> "A-01". Returns false on a malformed escape.
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
// Seat map file. Same rules as checkLayout() in dashboard/index.html:
// {"cols":1..30, "rows":1..50 (optional), "seats":{"<id>":[col,row]},
//  "labels":[{"c","r","w","h","text"}] (optional, text required)}; no two blocks may overlap.
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
// One seat or label per line, so the file stays easy to edit by hand.
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

void core::hmsg(const std::string&ip,const std::string&txt) {
  try {
    json j=json::parse(txt,nullptr,false);
    if (j==nlohmann::json::value_t::discarded)return;
    if (!j.is_object())return;
    if (j.value("type","")!="metrics")return;
    std::string id=j.value("id","");
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

  wsr_=std::make_unique<ix::WebSocketServer>(ws_port,"0.0.0.0");
  wsr_->setOnClientMessageCallback([this](std::shared_ptr<ix::ConnectionState> state,ix::WebSocket&ws,const ix::WebSocketMessagePtr&msg){
    switch (msg->type) {
      case ix::WebSocketMessageType::Open: {
        bool d=msg->openInfo.uri=="/dashboard";
        if (d) {std::lock_guard<std::mutex> lock(mu_); dash_.insert(&ws);}
        log(lvl::info,std::string(d?"dashboard":"agent")+" connected: "+state->getRemoteIp());
        break;
      }
      case ix::WebSocketMessageType::Close: {
        bool d;
        {std::lock_guard<std::mutex> lock(mu_); d=dash_.erase(&ws)>0;}
        log(lvl::info,std::string(d?"dashboard":"agent")+" disconnected: "+state->getRemoteIp());
        break;
      }
      case ix::WebSocketMessageType::Error:
        log(lvl::err,"An error has occur: "+msg->errorInfo.reason);
        break;
      case ix::WebSocketMessageType::Message: {
        bool is_d;
        {std::lock_guard<std::mutex> lock(mu_); is_d=dash_.count(&ws)>0;}
        if (!is_d) hmsg(state->getRemoteIp(),msg->str);
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

  http_=std::make_unique<ix::HttpServer>(http_port,"0.0.0.0");
  http_->setOnConnectionCallback(
    [this](ix::HttpRequestPtr rq,std::shared_ptr<ix::ConnectionState> state)->ix::HttpResponsePtr {
      ix::WebSocketHttpHeaders head;
      // Can this browser manage computers (rename, describe, remove, add back)?
      if (rq->uri=="/me"&&rq->method=="GET") {
        head["Content-Type"]="application/json";
        head["Cache-Control"]="no-store";
        return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,json{{"local",is_local(state->getRemoteIp())}}.dump());
      }
      // Managing computers from the dashboard. Only from the server computer: the dashboard has
      // no login, and a student must not be able to hide or rename their own PC.
      if ((rq->uri=="/forget"||rq->uri=="/restore"||rq->uri=="/meta")&&rq->method=="POST") {
        head["Content-Type"]="application/json";
        head["Cache-Control"]="no-store";
        auto reply=[&](int code,const char*msg,const json&body){
          return std::make_shared<ix::HttpResponse>(code,msg,ix::HttpErrorCode::Ok,head,body.dump(-1,' ',false,json::error_handler_t::replace));
        };
        if (!is_local(state->getRemoteIp())) return reply(403,"Forbidden",{{"error","local only"}});
        auto ct=rq->headers.find("Content-Type");
        if (ct==rq->headers.end()||ct->second.rfind("application/json",0)!=0) return reply(415,"Unsupported Media Type",{{"error","needs application/json"}});
        json j=json::parse(rq->body,nullptr,false);
        if (j.is_discarded()||!j.is_object()||!j.contains("id")||!j["id"].is_string()) return reply(400,"Bad Request",{{"error","needs {\"id\": \"...\"}"}});
        std::string id=j["id"].get<std::string>(), why;
        if (rq->uri=="/forget") why=forget(id);
        else if (rq->uri=="/restore") why=restore(id);
        else {
          auto str=[&](const char*k){return j.contains(k)&&j[k].is_string()?j[k].get<std::string>():std::string();};
          why=set_meta(id,str("name"),str("desc"));
        }
        if (why=="unknown") return reply(404,"Not Found",{{"error","unknown id"}});
        if (!why.empty()) return reply(400,"Bad Request",{{"error",why}});
        return reply(200,"OK",{{"ok",true}});
      }
      // Saving the seat map from the dashboard's editor. Only from the server computer:
      // the dashboard has no login, so other PCs on the LAN must not rewrite the room.
      // Requiring application/json also makes a browser refuse cross-site posts (preflight).
      if (rq->uri=="/layout.json"&&rq->method=="POST") {
        head["Content-Type"]="application/json";
        head["Cache-Control"]="no-store";
        auto reply=[&](int code,const char*msg,const std::string&err){
          return std::make_shared<ix::HttpResponse>(code,msg,ix::HttpErrorCode::Ok,head,json{{"error",err}}.dump(-1,' ',false,json::error_handler_t::replace));
        };
        if (!is_local(state->getRemoteIp())) return reply(403,"Forbidden","local only");
        auto ct=rq->headers.find("Content-Type");
        if (ct==rq->headers.end()||ct->second.rfind("application/json",0)!=0) return reply(415,"Unsupported Media Type","needs application/json");
        if (rq->body.size()>256*1024) return reply(413,"Payload Too Large","too large");
        json j=json::parse(rq->body,nullptr,false);
        std::string why;
        if (j.is_discarded()||!check_layout(j,why)) return reply(400,"Bad Request",why.empty()?"not JSON":why);
        std::string lp=exe_dir()+"/layout.json", tmp=lp+".tmp";
        {
          std::ofstream f(tmp,std::ios::binary|std::ios::trunc);
          f<<fmt_layout(j);
          if (!f) return reply(500,"Internal Server Error","cannot write "+tmp);
        }
        std::error_code ec;
        std::filesystem::rename(tmp,lp,ec);
        if (ec) {std::filesystem::remove(tmp,ec); return reply(500,"Internal Server Error","cannot replace "+lp);}
        log(lvl::info,"Seat map saved: "+lp);
        return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,"{\"ok\":true}");
      }
      const std::string hp="/history?id=";
      if (rq->uri.compare(0,hp.size(),hp)==0) {
        std::string id;
        // only ids the server has already seen; anything else is 404
        if (url_dec(rq->uri.substr(hp.size()),id)&&hist_->has(id)) {
          head["Content-Type"]="application/json";
          head["Cache-Control"]="no-store";
          return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,hist_->series(id).dump(-1,' ',false,json::error_handler_t::replace));
        }
      }
      // Seat map: only this exact path, read from the folder that holds the server executable.
      if (rq->uri=="/layout.json") {
        std::string lp=exe_dir()+"/layout.json";
        std::error_code ec;
        auto sz=std::filesystem::file_size(lp,ec);
        if (!ec&&sz>0&&sz<=256*1024) {
          head["Content-Type"]="application/json";
          head["Cache-Control"]="no-store";
          return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,read_f(lp));
        }
      }
      if (rq->uri=="/"||rq->uri=="/index.html") {
        std::string by=read_f("dashboard/index.html");
        if (!by.empty()) {
          head["Content-Type"]="text/html; charset=utf-8";
          return std::make_shared<ix::HttpResponse>(200,"OK",ix::HttpErrorCode::Ok,head,by);
        }
      }
      std::string nf="Not Found";
      head["Content-Type"]="text/html; charset=utf-8";
      return std::make_shared<ix::HttpResponse>(404,"Not Found",ix::HttpErrorCode::Ok,head,nf);
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
  log(lvl::info,"Dashboard is served at http://"+(net_.net_ip.empty()?std::string("127.0.0.1"):net_.net_ip)+":"+std::to_string(http_port));

  run_=true;
  th_=std::thread([this]{loop();});
  return true;
}

void core::loop() {
  while (run_) {
    std::this_thread::sleep_for(std::chrono::seconds(1));
    if (!run_) break;
    std::vector<mah> snp=machines();
    int64_t n=now_ms();
    try {
      std::string pl=b_snp(snp,removed(),n,*hist_).dump(-1,' ',false,json::error_handler_t::replace);
      std::lock_guard<std::mutex> lock(mu_);
      for (const auto&c:wsr_->getClients()) if (dash_.count(c.get())) c->send(pl);
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
  {std::lock_guard<std::mutex> lock(mu_); dash_.clear();}
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
  // bytes: 40 Thai characters are 120 bytes in UTF-8, 200 are 600
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
  std::error_code ec;
  std::filesystem::rename(tmp,p,ec);
  if (ec) {std::filesystem::remove(tmp,ec); log(lvl::err,"Cannot replace "+p);}
}

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

int core::dash_count() const {std::lock_guard<std::mutex> lock(mu_); return (int)dash_.size();}

int core::agent_count() const {
  if (!run_||!wsr_) return 0;
  std::lock_guard<std::mutex> lock(mu_);
  int n=0;
  for (const auto&c:wsr_->getClients()) if (!dash_.count(c.get())) n++;
  return n;
}

std::vector<std::string> core::abnormal(const std::string&id,int64_t now) const {
  return hist_?hist_->abnormal(id,now):std::vector<std::string>{};
}
bool core::learning(const std::string&id,int64_t now) const {return hist_&&hist_->learning(id,now);}
