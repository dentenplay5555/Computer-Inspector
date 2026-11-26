// Native window for the Computer Inspector server (Dear ImGui + GLFW + OpenGL 3).
// Runs the same core as the console server and shows its state live.
#define GL_SILENCE_DEPRECATION
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <GLFW/glfw3.h>
#include <ixwebsocket/IXNetSystem.h>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif
#include "server/core.hpp"
#include "server/gui/i18n.hpp"
#include "server/gui/fonts.hpp"

// ---------- theme ----------
struct pal {
  ImVec4 bg, panel, focus_bg, text, muted, border, frame, frame_hov, frame_act;
  ImVec4 accent, accent_hov, on_accent, track, fill, ok, warn, crit, head, row_alt;
};
static ImVec4 hx(unsigned v) {return ImVec4(((v>>16)&255)/255.f,((v>>8)&255)/255.f,(v&255)/255.f,1.f);}
// Neutral greys, one teal accent (DESIGN.md). Status colors only mark machine state.
static const pal LIGHT={hx(0xF5F6F6),hx(0xFFFFFF),hx(0xE7F3F1),hx(0x1D2527),hx(0x56625F),hx(0xD7DCDD),hx(0xECEFEF),hx(0xE1E6E6),hx(0xD5DCDC),
  hx(0x0F766E),hx(0x0B5F58),hx(0xFFFFFF),hx(0xE3E7E8),hx(0x7A8689),hx(0x137535),hx(0xA84D06),hx(0xB91C1C),hx(0xEEF1F1),hx(0xF8F9F9)};
static const pal DARK={hx(0x111617),hx(0x192022),hx(0x12302D),hx(0xE5ECEC),hx(0x9AA8AA),hx(0x2B3537),hx(0x232C2E),hx(0x2C3739),hx(0x354244),
  hx(0x2DD4BF),hx(0x5EEAD4),hx(0x062B27),hx(0x2A3436),hx(0x8A989B),hx(0x4ADE80),hx(0xFBBF24),hx(0xF87171),hx(0x1E2729),hx(0x1C2325)};

static void apply_theme(const pal&p,float scale) {
  ImGuiStyle st;
  st.WindowPadding=ImVec2(20,18); st.FramePadding=ImVec2(12,7); st.ItemSpacing=ImVec2(10,10);
  st.ItemInnerSpacing=ImVec2(8,6); st.CellPadding=ImVec2(8,7);
  st.WindowRounding=0; st.ChildRounding=10; st.FrameRounding=6; st.PopupRounding=6; st.GrabRounding=6; st.TabRounding=6;
  st.ScrollbarRounding=6; st.ScrollbarSize=14; st.WindowBorderSize=0; st.ChildBorderSize=1; st.FrameBorderSize=0;
  ImVec4*c=st.Colors;
  c[ImGuiCol_Text]=p.text; c[ImGuiCol_TextDisabled]=p.muted;
  c[ImGuiCol_WindowBg]=p.bg; c[ImGuiCol_ChildBg]=p.panel; c[ImGuiCol_PopupBg]=p.panel;
  c[ImGuiCol_Border]=p.border; c[ImGuiCol_BorderShadow]=ImVec4(0,0,0,0);
  c[ImGuiCol_FrameBg]=p.frame; c[ImGuiCol_FrameBgHovered]=p.frame_hov; c[ImGuiCol_FrameBgActive]=p.frame_act;
  c[ImGuiCol_Button]=p.frame; c[ImGuiCol_ButtonHovered]=p.frame_hov; c[ImGuiCol_ButtonActive]=p.frame_act;
  c[ImGuiCol_Header]=p.frame; c[ImGuiCol_HeaderHovered]=p.frame_hov; c[ImGuiCol_HeaderActive]=p.frame_act;
  c[ImGuiCol_TableHeaderBg]=p.head; c[ImGuiCol_TableBorderStrong]=p.border; c[ImGuiCol_TableBorderLight]=p.border;
  c[ImGuiCol_TableRowBg]=p.panel; c[ImGuiCol_TableRowBgAlt]=p.row_alt;
  c[ImGuiCol_ScrollbarBg]=ImVec4(0,0,0,0); c[ImGuiCol_ScrollbarGrab]=p.frame_act; c[ImGuiCol_ScrollbarGrabHovered]=p.fill; c[ImGuiCol_ScrollbarGrabActive]=p.fill;
  c[ImGuiCol_Separator]=p.border; c[ImGuiCol_TextSelectedBg]=ImVec4(p.accent.x,p.accent.y,p.accent.z,0.30f);
  c[ImGuiCol_NavCursor]=p.accent; c[ImGuiCol_CheckMark]=p.accent;
  st.ScaleAllSizes(scale);
  ImGui::GetStyle()=st;
}

// ---------- log buffer (written from server threads) ----------
struct logline {std::string t; lvl l; std::string s;};
static std::mutex g_logmu;
static std::deque<logline> g_log;
static void push_log(lvl l,const std::string&s) {
  std::time_t tt=std::time(nullptr);
  char b[16]; std::strftime(b,sizeof b,"%H:%M:%S",std::localtime(&tt));
  std::lock_guard<std::mutex> lock(g_logmu);
  g_log.push_back({b,l,s});
  if (g_log.size()>500) g_log.pop_front();
}

// ---------- app state ----------
enum class srvstate {starting,running,failed};
struct app {
  core srv;
  srvstate st=srvstate::starting;
  std::string err;
  int lang=TH;
  bool dark=false;
  char find[64]={};
  std::string copied_key; double copied_at=-10;
  ImFont*body=nullptr,*bold=nullptr,*title=nullptr;
  float scale=1;
};
static const char* T(app&a,sid s) {return STR[a.lang][s];}

static void open_url(const std::string&url) {
#ifdef _WIN32
  ShellExecuteA(nullptr,"open",url.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
#elif defined(__APPLE__)
  std::system(("open '"+url+"'").c_str());
#else
  std::system(("xdg-open '"+url+"' >/dev/null 2>&1 &").c_str());
#endif
}

static int status_idx(const char*s) {
  std::string v=s;
  return v=="ok"?0:v=="warn"?1:v=="crit"?2:3;
}
static ImVec4 status_col(const pal&p,int i) {return i==0?p.ok:i==1?p.warn:i==2?p.crit:p.muted;}

// A dot drawn with the draw list, then the label in the same color.
static void status_dot(const ImVec4&col,const char*label) {
  float h=ImGui::GetTextLineHeight(),r=h*0.22f;
  ImVec2 p=ImGui::GetCursorScreenPos();
  ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x+r,p.y+h*0.5f),r,ImGui::GetColorU32(col));
  ImGui::Dummy(ImVec2(r*2,h));
  ImGui::SameLine(0,r*1.6f);
  ImGui::TextColored(col,"%s",label);
}

// Usage bar: grey while normal, warn/crit color past the server's thresholds.
static void usage_bar(const pal&p,double v,double warn_at,double crit_at,float sc) {
  float w=ImGui::GetContentRegionAvail().x,h=ImGui::GetTextLineHeight();
  char txt[16]; std::snprintf(txt,sizeof txt,"%.0f%%",v);
  float tw=ImGui::CalcTextSize("100%").x,gap=8*sc,bw=std::max(20.f,w-tw-gap),bh=7*sc;
  ImVec2 o=ImGui::GetCursorScreenPos();
  ImDrawList*dl=ImGui::GetWindowDrawList();
  float y0=o.y+(h-bh)*0.5f;
  ImVec4 fc=v>=crit_at?p.crit:v>=warn_at?p.warn:p.fill;
  dl->AddRectFilled(ImVec2(o.x,y0),ImVec2(o.x+bw,y0+bh),ImGui::GetColorU32(p.track),bh*0.5f);
  float f=(float)std::clamp(v,0.0,100.0)/100.f;
  if (f>0) dl->AddRectFilled(ImVec2(o.x,y0),ImVec2(o.x+std::max(bh,bw*f),y0+bh),ImGui::GetColorU32(fc),bh*0.5f);
  float tx=o.x+bw+gap+(tw-ImGui::CalcTextSize(txt).x);
  dl->AddText(ImVec2(tx,o.y),ImGui::GetColorU32(v>=warn_at?fc:p.text),txt);
  ImGui::Dummy(ImVec2(w,h));
}

static bool primary_button(const pal&p,const char*label) {
  ImGui::PushStyleColor(ImGuiCol_Button,p.accent);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered,p.accent_hov);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive,p.accent_hov);
  ImGui::PushStyleColor(ImGuiCol_Text,p.on_accent);
  bool r=ImGui::Button(label);
  ImGui::PopStyleColor(4);
  return r;
}

static void copy_button(app&a,const char*key,const std::string&text) {
  bool done=a.copied_key==key&&ImGui::GetTime()-a.copied_at<1.5;
  std::string lbl=std::string(done?T(a,S_COPIED):T(a,S_COPY))+"###"+key;
  if (ImGui::Button(lbl.c_str())) {
    ImGui::SetClipboardText(text.c_str());
    a.copied_key=key; a.copied_at=ImGui::GetTime();
  }
}

// One address line: label, value, hint, buttons.
static void address_row(app&a,const pal&p,sid label,sid hint,const char*key,const std::string&value,bool with_open) {
  ImGui::PushFont(a.bold); ImGui::TextUnformatted(T(a,label)); ImGui::PopFont();
  ImGui::PushStyleColor(ImGuiCol_Text,p.accent);
  ImGui::PushFont(a.title); ImGui::TextUnformatted(value.c_str()); ImGui::PopFont();
  ImGui::PopStyleColor();
  ImGui::TextDisabled("%s",T(a,hint));
  copy_button(a,key,value);
  if (with_open) {
    ImGui::SameLine();
    if (ImGui::Button((std::string(T(a,S_OPEN))+"###open_"+key).c_str())) open_url(value);
  }
}

// Icon buttons. ImGui cannot show SVG, so the icons are drawn with simple shapes,
// in the text color, matching the web dashboard (globe + language code, moon / sun).
static void icon_globe(ImDrawList*dl,ImVec2 c,float r,ImU32 col,float th) {
  dl->AddCircle(c,r,col,0,th);
  dl->AddLine(ImVec2(c.x-r,c.y),ImVec2(c.x+r,c.y),col,th);
  dl->AddEllipse(c,ImVec2(r*0.45f,r),col,0.f,0,th);
}
// Crescent: a full disc, then a disc in the button's own color cut out of it.
static void icon_moon(ImDrawList*dl,ImVec2 c,float r,ImU32 col,ImU32 bg) {
  dl->AddCircleFilled(c,r,col);
  dl->AddCircleFilled(ImVec2(c.x+r*0.5f,c.y-r*0.42f),r*0.82f,bg);
}
static void icon_sun(ImDrawList*dl,ImVec2 c,float r,ImU32 col,float th) {
  dl->AddCircleFilled(c,r*0.42f,col);
  for (int i=0;i<8;i++) {
    float an=i*3.14159265f/4.f,dx=std::cos(an),dy=std::sin(an);
    dl->AddLine(ImVec2(c.x+dx*r*0.66f,c.y+dy*r*0.66f),ImVec2(c.x+dx*r,c.y+dy*r),col,th);
  }
}
static ImU32 item_bg() {
  return ImGui::GetColorU32(ImGui::IsItemActive()?ImGuiCol_ButtonActive:ImGui::IsItemHovered()?ImGuiCol_ButtonHovered:ImGuiCol_Button);
}

static void draw_header(app&a,const pal&p) {
  (void)p;
  ImGui::PushFont(a.title); ImGui::TextUnformatted(T(a,S_TITLE)); ImGui::PopFont();
  ImGui::SameLine(0,12*a.scale);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s",T(a,S_SUBTITLE));

  ImGuiStyle&st=ImGui::GetStyle();
  float h=ImGui::GetFrameHeight(),r=h*0.24f,gap=7*a.scale,th=std::max(1.f,1.6f*a.scale);
  const char*code=a.lang==TH?"EN":"TH";            // the language you switch to
  ImVec2 code_sz=a.bold->CalcTextSizeA(ImGui::GetFontSize(),FLT_MAX,0.f,code);
  ImVec2 lsz(st.FramePadding.x*2+r*2+gap+code_sz.x,h),tsz(h,h);
  float x=ImGui::GetWindowContentRegionMax().x-(lsz.x+st.ItemSpacing.x+tsz.x);
  ImGui::SameLine(std::max(x,ImGui::GetCursorPosX()));
  ImU32 fg=ImGui::GetColorU32(ImGuiCol_Text);
  ImDrawList*dl=ImGui::GetWindowDrawList();

  bool lang_hit=ImGui::Button("###lang",lsz);
  ImVec2 mn=ImGui::GetItemRectMin();
  ImVec2 c(mn.x+st.FramePadding.x+r,mn.y+h*0.5f);
  icon_globe(dl,c,r,fg,th);
  dl->AddText(a.bold,ImGui::GetFontSize(),ImVec2(c.x+r+gap,mn.y+(h-code_sz.y)*0.5f),fg,code);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",T(a,S_LANG_SWITCH));

  ImGui::SameLine();
  bool theme_hit=ImGui::Button("###theme",tsz);
  ImVec2 tc((ImGui::GetItemRectMin().x+ImGui::GetItemRectMax().x)*0.5f,(ImGui::GetItemRectMin().y+ImGui::GetItemRectMax().y)*0.5f);
  if (a.dark) icon_sun(dl,tc,r*1.05f,fg,th);       // shows the mode you switch to
  else icon_moon(dl,tc,r*0.95f,fg,item_bg());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",a.dark?T(a,S_LIGHT):T(a,S_DARK));

  if (lang_hit) a.lang=a.lang==TH?EN:TH;
  if (theme_hit) {a.dark=!a.dark; apply_theme(a.dark?DARK:LIGHT,a.scale);}
}

// The focal panel: is the server up, and where do agents and teachers connect.
static void draw_connect(app&a,const pal&p) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg,p.focus_bg);
  ImGui::PushStyleColor(ImGuiCol_Border,p.focus_bg);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(18*a.scale,16*a.scale));
  ImGui::BeginChild("connect",ImVec2(0,0),ImGuiChildFlags_Borders|ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysUseWindowPadding|ImGuiChildFlags_NavFlattened);
  if (a.st==srvstate::starting) {
    status_dot(p.muted,T(a,S_STARTING));
  } else if (a.st==srvstate::failed) {
    status_dot(p.crit,T(a,S_FAILED));
    ImGui::TextWrapped("%s",a.err.c_str());
    if (primary_button(p,T(a,S_RETRY))) {
      a.err.clear();
      a.st=a.srv.start(a.err)?srvstate::running:srvstate::failed;
    }
  } else {
    netif n=a.srv.net();
    std::string ip=n.net_ip.empty()?"127.0.0.1":n.net_ip;
    status_dot(p.ok,T(a,S_RUNNING));
    ImGui::SameLine(0,16*a.scale);
    if (!n.net_inter.empty()) {ImGui::TextDisabled("%s: %s",T(a,S_NETWORK),n.net_inter.c_str()); ImGui::SameLine(0,16*a.scale);}
    ImGui::TextDisabled("%s: %d",T(a,S_AGENTS_ON),a.srv.agent_count());
    ImGui::SameLine(0,16*a.scale);
    ImGui::TextDisabled("%s: %d",T(a,S_DASH_OPEN),a.srv.dash_count());
    if (n.net_ip.empty()) ImGui::TextColored(p.warn,"%s",T(a,S_NO_NETWORK));
    ImGui::Spacing();
    std::string agent="ws://"+ip+":"+std::to_string(a.srv.ws_port)+"/agent";
    std::string dash="http://"+ip+":"+std::to_string(a.srv.http_port);
    float avail=ImGui::GetContentRegionAvail().x;
    bool two=avail>760*a.scale;
    if (two && ImGui::BeginTable("addr",2,ImGuiTableFlags_SizingStretchSame)) {
      ImGui::TableNextColumn(); address_row(a,p,S_AGENT_ADDR,S_AGENT_ADDR_HINT,"agent",agent,false);
      ImGui::TableNextColumn(); address_row(a,p,S_DASH_ADDR,S_DASH_ADDR_HINT,"dash",dash,true);
      ImGui::EndTable();
    } else {
      address_row(a,p,S_AGENT_ADDR,S_AGENT_ADDR_HINT,"agent",agent,false);
      ImGui::Spacing();
      address_row(a,p,S_DASH_ADDR,S_DASH_ADDR_HINT,"dash",dash,true);
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

static std::string ago(app&a,int64_t ms) {
  char b[64];
  int s=(int)(ms/1000);
  if (s<120) std::snprintf(b,sizeof b,T(a,S_SEC_AGO),s);
  else std::snprintf(b,sizeof b,T(a,S_MIN_AGO),s/60);
  return b;
}

static bool contains_ci(const std::string&h,const char*n) {
  std::string a=h,b=n;
  for (auto&c:a) c=(char)std::tolower((unsigned char)c);
  for (auto&c:b) c=(char)std::tolower((unsigned char)c);
  return a.find(b)!=std::string::npos;
}

static void centered_note(const pal&p,const char*txt,ImVec4 col) {
  (void)p;
  float w=ImGui::GetContentRegionAvail().x,tw=ImGui::CalcTextSize(txt).x;
  if (tw<w) ImGui::SetCursorPosX(ImGui::GetCursorPosX()+(w-tw)*0.5f);
  ImGui::PushStyleColor(ImGuiCol_Text,col);
  ImGui::TextWrapped("%s",txt);
  ImGui::PopStyleColor();
}

static void draw_machines(app&a,const pal&p,float height) {
  std::vector<mah> ms=a.srv.machines();
  int64_t now=now_ms();
  int cnt[4]={0,0,0,0};
  for (auto&m:ms) cnt[status_idx(status_of(m,now))]++;

  ImGui::PushFont(a.bold); ImGui::TextUnformatted(T(a,S_COMPUTERS)); ImGui::PopFont();
  ImGui::SameLine(0,8*a.scale);
  ImGui::TextDisabled("(%d)",(int)ms.size());
  const sid lab[4]={S_OK,S_WARN,S_CRIT,S_OFFLINE};
  for (int i=0;i<4;i++) {
    ImGui::SameLine(0,18*a.scale);
    char b[64]; std::snprintf(b,sizeof b,"%s %d",T(a,lab[i]),cnt[i]);
    status_dot(cnt[i]>0?status_col(p,i):p.muted,b);
  }
  float fw=240*a.scale;
  ImGui::SameLine(0,18*a.scale);
  float room=ImGui::GetContentRegionAvail().x;
  if (room>=fw) ImGui::SetCursorPosX(ImGui::GetCursorPosX()+room-fw);
  else ImGui::NewLine();
  ImGui::SetNextItemWidth(fw);
  ImGui::InputTextWithHint("##find",T(a,S_FIND_HINT),a.find,sizeof a.find);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",T(a,S_FIND));

  // NavFlattened: Tab moves through every panel instead of staying inside one.
  ImGui::BeginChild("tablewrap",ImVec2(0,height),ImGuiChildFlags_Borders|ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_HorizontalScrollbar);
  if (a.st!=srvstate::running) {
    ImGui::Dummy(ImVec2(0,height*0.3f));
    centered_note(p,a.st==srvstate::starting?T(a,S_WAIT_START):T(a,S_FAILED_TABLE),a.st==srvstate::failed?p.crit:p.muted);
    ImGui::EndChild();
    return;
  }
  if (ms.empty()) {
    ImGui::Dummy(ImVec2(0,height*0.22f));
    ImGui::PushFont(a.bold); centered_note(p,T(a,S_EMPTY),p.text); ImGui::PopFont();
    centered_note(p,T(a,S_EMPTY_HINT),p.muted);
    netif n=a.srv.net();
    std::string cmd=std::string(
#ifdef _WIN32
      "agent.exe"
#else
      "./agent"
#endif
      )+" --host "+(n.net_ip.empty()?"127.0.0.1":n.net_ip);
    ImGui::PushFont(a.title);
    centered_note(p,cmd.c_str(),p.accent);
    ImGui::PopFont();
    float bw=ImGui::CalcTextSize(T(a,S_COPIED)).x+ImGui::GetStyle().FramePadding.x*2;
    ImGui::SetCursorPosX(std::max(0.f,(ImGui::GetContentRegionAvail().x-bw)*0.5f));
    copy_button(a,"cmd",cmd);
    ImGui::EndChild();
    return;
  }

  std::vector<const mah*> rows;
  for (auto&m:ms) if (!a.find[0]||contains_ci(m.id,a.find)||contains_ci(m.ip,a.find)) rows.push_back(&m);
  if (rows.empty()) {
    ImGui::Dummy(ImVec2(0,height*0.3f));
    char b[160]; std::snprintf(b,sizeof b,T(a,S_NO_MATCH),a.find);
    centered_note(p,b,p.muted);
    ImGui::EndChild();
    return;
  }

  // No ScrollX/ScrollY on the table itself: a scrolling table gets its own inner window, which traps Tab.
  // The surrounding panel scrolls instead, and the table is given its full natural width.
  ImGuiTableFlags tf=ImGuiTableFlags_Sortable|ImGuiTableFlags_RowBg|ImGuiTableFlags_BordersInnerH|ImGuiTableFlags_SizingFixedFit|ImGuiTableFlags_Resizable;
  float s=a.scale;
  float natural=(120+105+110*3+90*2+115+70+110)*s+ImGui::GetStyle().CellPadding.x*2*10;
  if (ImGui::BeginTable("machines",10,tf,ImVec2(std::max(natural,ImGui::GetContentRegionAvail().x),0))) {
    ImGui::TableSetupColumn(T(a,S_COL_PC),ImGuiTableColumnFlags_DefaultSort|ImGuiTableColumnFlags_WidthFixed,120*s);
    ImGui::TableSetupColumn(T(a,S_COL_STATUS),ImGuiTableColumnFlags_WidthFixed,105*s);
    ImGui::TableSetupColumn(T(a,S_COL_CPU),ImGuiTableColumnFlags_WidthFixed|ImGuiTableColumnFlags_PreferSortDescending,110*s);
    ImGui::TableSetupColumn(T(a,S_COL_RAM),ImGuiTableColumnFlags_WidthFixed|ImGuiTableColumnFlags_PreferSortDescending,110*s);
    ImGui::TableSetupColumn(T(a,S_COL_DISK),ImGuiTableColumnFlags_WidthFixed|ImGuiTableColumnFlags_PreferSortDescending,110*s);
    ImGui::TableSetupColumn(T(a,S_COL_DOWN),ImGuiTableColumnFlags_WidthFixed|ImGuiTableColumnFlags_PreferSortDescending,90*s);
    ImGui::TableSetupColumn(T(a,S_COL_UP),ImGuiTableColumnFlags_WidthFixed|ImGuiTableColumnFlags_PreferSortDescending,90*s);
    ImGui::TableSetupColumn(T(a,S_COL_IP),ImGuiTableColumnFlags_WidthFixed,115*s);
    ImGui::TableSetupColumn(T(a,S_COL_OS),ImGuiTableColumnFlags_WidthFixed,70*s);
    ImGui::TableSetupColumn(T(a,S_COL_SEEN),ImGuiTableColumnFlags_WidthStretch,110*s);
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs*ss=ImGui::TableGetSortSpecs(); ss&&ss->SpecsCount>0) {
      const ImGuiTableColumnSortSpecs&sp=ss->Specs[0];
      int col=sp.ColumnIndex; bool asc=sp.SortDirection==ImGuiSortDirection_Ascending;
      std::stable_sort(rows.begin(),rows.end(),[&](const mah*x,const mah*y){
        auto key=[&](const mah*m)->double{
          switch(col){case 1:return status_idx(status_of(*m,now));case 2:return m->cpu;case 3:return m->ram;case 4:return m->disk;
            case 5:return m->rx;case 6:return m->tx;case 9:return (double)(now-m->l_s);default:return 0;}
        };
        int c;
        if (col==0) c=x->id.compare(y->id);
        else if (col==7) c=x->ip.compare(y->ip);
        else if (col==8) c=x->os.compare(y->os);
        else {double kx=key(x),ky=key(y); c=kx<ky?-1:kx>ky?1:0;}
        return asc?c<0:c>0;
      });
    }

    for (const mah*m:rows) {
      int si=status_idx(status_of(*m,now));
      bool off=si==3;
      ImGui::TableNextRow(ImGuiTableRowFlags_None,ImGui::GetTextLineHeight()+ImGui::GetStyle().CellPadding.y*2);
      ImGui::PushStyleColor(ImGuiCol_Text,off?p.muted:p.text);
      ImGui::TableNextColumn(); ImGui::PushFont(a.bold); ImGui::TextUnformatted(m->id.c_str()); ImGui::PopFont();
      ImGui::TableNextColumn(); status_dot(status_col(p,si),T(a,(sid)(S_OK+si)));
      ImGui::TableNextColumn(); usage_bar(p,m->cpu,off?999:80,off?999:90,a.scale);
      ImGui::TableNextColumn(); usage_bar(p,m->ram,off?999:80,off?999:90,a.scale);
      ImGui::TableNextColumn(); usage_bar(p,m->disk,off?999:85,off?999:95,a.scale);
      ImGui::TableNextColumn(); ImGui::Text("%.1f",m->rx);
      ImGui::TableNextColumn(); ImGui::Text("%.1f",m->tx);
      ImGui::TableNextColumn(); ImGui::TextUnformatted(m->ip.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(m->os.empty()?"-":m->os.c_str());
      ImGui::TableNextColumn(); ImGui::TextUnformatted(ago(a,now-m->l_s).c_str());
      ImGui::PopStyleColor();
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}

static void draw_log(app&a,const pal&p,float height) {
  ImGui::PushFont(a.bold); ImGui::TextUnformatted(T(a,S_LOG)); ImGui::PopFont();
  float bw=ImGui::CalcTextSize(T(a,S_CLEAR)).x+ImGui::GetStyle().FramePadding.x*2;
  ImGui::SameLine(ImGui::GetWindowContentRegionMax().x-bw);
  if (ImGui::Button((std::string(T(a,S_CLEAR))+"###log").c_str())) {std::lock_guard<std::mutex> lock(g_logmu); g_log.clear();}
  ImGui::BeginChild("log",ImVec2(0,height),ImGuiChildFlags_Borders|ImGuiChildFlags_NavFlattened,ImGuiWindowFlags_HorizontalScrollbar);
  std::deque<logline> lines;
  {std::lock_guard<std::mutex> lock(g_logmu); lines=g_log;}
  if (lines.empty()) ImGui::TextDisabled("%s",T(a,S_LOG_EMPTY));
  bool at_bottom=ImGui::GetScrollY()>=ImGui::GetScrollMaxY()-4;
  for (auto&l:lines) {
    ImGui::TextDisabled("%s",l.t.c_str());
    ImGui::SameLine();
    ImVec4 c=l.l==lvl::err?p.crit:l.l==lvl::warn?p.warn:p.text;
    ImGui::TextColored(c,"%s",l.s.c_str());
  }
  if (at_bottom) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
}

static void load_fonts(app&a,float px_scale) {
  ImGuiIO&io=ImGui::GetIO();
  static const ImWchar latin[]={0x0020,0x00FF,0x2010,0x205E,0x2190,0x2193,0};
  auto add=[&](const unsigned char*lat,unsigned lat_n,const unsigned char*th,unsigned th_n,float size)->ImFont*{
    ImFontConfig c; c.FontDataOwnedByAtlas=false; c.OversampleH=2;
    ImFont*f=io.Fonts->AddFontFromMemoryTTF((void*)lat,(int)lat_n,size*px_scale,&c,latin);
    // Noto Sans Thai draws smaller than Noto Sans at the same pixel size; enlarge it so mixed lines match.
    ImFontConfig m; m.FontDataOwnedByAtlas=false; m.MergeMode=true; m.OversampleH=2;
    m.GlyphOffset.y=-0.08f*size*px_scale;
    io.Fonts->AddFontFromMemoryTTF((void*)th,(int)th_n,size*1.18f*px_scale,&m,io.Fonts->GetGlyphRangesThai());
    return f;
  };
  a.body=add(font_latin_reg,font_latin_reg_len,font_thai_reg,font_thai_reg_len,17);
  a.bold=add(font_latin_bold,font_latin_bold_len,font_thai_bold,font_thai_bold_len,17);
  a.title=add(font_latin_bold,font_latin_bold_len,font_thai_bold,font_thai_bold_len,22);
  io.FontDefault=a.body;
}

int main(int,char**) {
  ix::initNetSystem();
  if (!glfwInit()) {std::fprintf(stderr,"Could not start GLFW\n"); return 1;}
#ifdef __APPLE__
  const char*glsl="#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,2);
  glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE); glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
#else
  const char*glsl="#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3); glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,0);
#endif
  glfwWindowHint(GLFW_SCALE_TO_MONITOR,GLFW_TRUE);
  GLFWwindow*win=glfwCreateWindow(1280,800,"Computer Inspector · Server",nullptr,nullptr);
  if (!win) {std::fprintf(stderr,"Could not open a window (OpenGL 3 is required)\n"); glfwTerminate(); return 1;}
  glfwMakeContextCurrent(win);
  glfwSwapInterval(1);

  // macOS reports a framebuffer larger than the window (points vs pixels); Windows/Linux scale the window itself.
  int ww,wh,fw,fh; glfwGetWindowSize(win,&ww,&wh); glfwGetFramebufferSize(win,&fw,&fh);
  float xs,ys; glfwGetWindowContentScale(win,&xs,&ys);
  bool fb_scaled=fw>ww;
  float ui_scale=fb_scaled?1.f:xs;
  float px_scale=fb_scaled?(float)fw/ww:xs;
  glfwSetWindowSizeLimits(win,(int)(640*ui_scale),(int)(480*ui_scale),GLFW_DONT_CARE,GLFW_DONT_CARE);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO&io=ImGui::GetIO();
  io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename=nullptr;

  app a;
  a.scale=ui_scale;
  load_fonts(a,px_scale);
  if (fb_scaled) io.FontGlobalScale=1.f/px_scale;
  apply_theme(LIGHT,ui_scale);

  ImGui_ImplGlfw_InitForOpenGL(win,true);
  ImGui_ImplOpenGL3_Init(glsl);

  a.srv.on_log=push_log;
  int frame=0;
  while (!glfwWindowShouldClose(win)) {
    glfwWaitEventsTimeout(0.25);   // redraw at least 4x a second so "last update" stays current
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    const pal&p=a.dark?DARK:LIGHT;
    const ImGuiViewport*vp=ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos); ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("main",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBringToFrontOnFocus);
    draw_header(a,p);
    ImGui::Spacing();
    draw_connect(a,p);
    ImGui::Spacing();
    float line=ImGui::GetFrameHeightWithSpacing();
    float log_h=std::max(90*a.scale,ImGui::GetContentRegionAvail().y*0.22f);
    float tbl_h=std::max(140*a.scale,ImGui::GetContentRegionAvail().y-log_h-line*2-ImGui::GetStyle().ItemSpacing.y*2);
    draw_machines(a,p,tbl_h);
    draw_log(a,p,std::max(60*a.scale,ImGui::GetContentRegionAvail().y-line));
    ImGui::End();

    ImGui::Render();
    int dw,dh; glfwGetFramebufferSize(win,&dw,&dh);
    glViewport(0,0,dw,dh);
    glClearColor(p.bg.x,p.bg.y,p.bg.z,1);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(win);

    // Start after the first frame so the window shows the starting state instead of a blank screen.
    if (++frame==2&&a.st==srvstate::starting) a.st=a.srv.start(a.err)?srvstate::running:srvstate::failed;
  }
  a.srv.stop();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(win);
  glfwTerminate();
  ix::uninitNetSystem();
  return 0;
}
