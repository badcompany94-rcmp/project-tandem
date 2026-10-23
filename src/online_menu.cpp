// Original desktop online-menu additions over the retail J2D title screen.
// Native layouts, fonts and animation assets remain loaded from the user's disc.
#include "runtime.h"
#include "online_room.h"
#include "online_content.h"
#include "display_config.h"
#include "desktop_pointer.h"
#include "desktop_online_ui.h"
#include "race.h"
#include "gx/texture.h"
#include "replay_audit.h"
#include "hw/pad.h"
extern "C" {
#include "recomp_funcs.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <algorithm>
#include <future>
#include <chrono>
#include <fstream>
#include <cmath>

namespace {
bool enabled() { return getenv("MKDD_ONLINE_MENU")!=nullptr; }
enum class Page { Title, Type, HostJoin, Code, Lobby, Rules, Kart, Initials, Native, Battle, LeaveConfirm, Engine, KickConfirm };
bool mode_advance=false;
unsigned kick_seat=0;
std::string kick_name,kick_id;
const char* keyboard_keys="1234567890QWERTYUIOPASDFGHJKLZXCVBNM";
std::vector<desktop_pointer::Region> clickable;
std::string release_label(){return "ALPHA 25  /  "+mkdd::online::executable_build().substr(0,8);}
void hit(float x,float y,float w,float h,int action){clickable.push_back({x/608,y/448,w/608,h/448,action});}
std::pair<float,float> key_position(unsigned i) {
    unsigned row=i<10?0:i<20?1:i<29?2:3,first=row==0?0:row==1?10:row==2?20:29;
    unsigned count=row<2?10:row==2?9:7;
    return {304-count*23.f+(i-first)*46.f,193+row*38.f};
}
unsigned lobby_selection=0;
Page page=Page::Title;
bool title_hint_hidden=false;
unsigned selection;
int online_focus=0;
void online_focus_move(CPU* c,int dx,int dy) {
    const desktop_pointer::Region* origin=nullptr;
    for(const auto& area:clickable)if(area.action==online_focus){origin=&area;break;}
    if(!origin){if(!clickable.empty())online_focus=clickable.front().action;return;}
    const float x=origin->x+origin->w/2,y=origin->y+origin->h/2;
    float best=1e9f;int next=online_focus;
    for(const auto& area:clickable){
        const float vx=area.x+area.w/2-x,vy=area.y+area.h/2-y;
        const float forward=vx*dx+vy*dy,cross=std::abs(vx*dy-vy*dx);
        if(forward<=.001f)continue;
        const float score=forward+cross*2.5f;
        if(score<best){best=score;next=area.action;}
    }
    if(next!=online_focus){online_focus=next;if(getenv("MKDD_MENU_TEST"))fprintf(stderr,"[online-focus] action=%d\n",next);}
}

std::string mode="grand-prix",code;
const char* alphabet="23456789ABCDEFGHJKLMNPQRSTUVWXYZ";
std::string message;
using mkdd::online::Json;
Json room_reply;
std::string token,build;
struct Response { Json value;std::string error; };
enum class Work { None, Create, Join, Poll, Change, Leave, Start, Select, Character };
std::future<Response> pending;
Work work=Work::None;
Work queued_work=Work::None;
Json queued_request;
bool cancelled=false;
bool leave_pending=false;
std::chrono::steady_clock::time_point next_poll;
std::string draft_mode,draft_layout,draft_engine,draft_cup;
int draft_rounds=3;
unsigned draft_arena=52,draft_course=36,draft_kart=0,draft_driver=7,draft_partner=8;
std::string initials="P1",draft_initials="P1";
Page profile_return=Page::Type;
bool profile_loaded=false,native_selection=false;
void native_room_state(CPU*);
void native_scene(CPU*,unsigned);
void native_character_reply(const std::string&);
void restore_menu_unlocks();
namespace content=mkdd::online::content;

std::string friendly(const std::string& error) {
    if(error=="room-not-found")return "ROOM NOT FOUND";
    if(error=="room-full")return "ROOM IS FULL";
    if(error=="event-started")return "THIS EVENT HAS ALREADY STARTED";
    if(error=="incompatible-build")return "PLAYERS NEED THE SAME GAME BUILD";
    if(error=="rollback-not-enabled")return "ONLINE HELPER NEEDS ROLLBACK. RESTART TANDEM";
    if(error=="players-not-ready")return "NEED 2-8 PLAYERS, ALL READY";
    if(error=="battle-needs-opponents")return "BATTLE NEEDS TWO OPPOSING KARTS";
    if(error=="seat-occupied")return "THAT SEAT IS TAKEN";
    if(error=="stale-room")return "ROOM CHANGED. PLEASE TRY AGAIN";
    if(error=="player-changed")return "THAT PLAYER MOVED. PLEASE SELECT THEM AGAIN";
    if(error=="room-busy")return "ROOM IS UPDATING. PLEASE TRY AGAIN";
    if(error=="invalid-session")return "THIS ROOM HAS CLOSED";
    if(error=="service-unavailable")return "ROOM SERVICE IS UNAVAILABLE";
    if(error=="invalid-kart")return "CHOOSE TWO RIDERS THAT FIT THE KART";
    if(error=="driver-required")return "A BACK SEAT NEEDS ITS DRIVER";
    if(error=="character-taken")return "THAT CHARACTER IS ALREADY CHOSEN";
    if(error=="characters-incomplete")return "WAITING FOR CHARACTER SELECTION";
    if(error=="kart-not-selected")return "THE DRIVER NEEDS TO CHOOSE A KART";
    return "COULD NOT UPDATE THE ROOM. TRY AGAIN";
}
bool busy() { return pending.valid(); }
bool can_act() { return queued_work==Work::None && (!busy() || work==Work::Poll); }
bool submit(Work kind,Json request) {
    if(busy()) {
        if(work==Work::Poll && queued_work==Work::None && (kind==Work::Change || kind==Work::Start || kind==Work::Select || kind==Work::Character)) {
            queued_work=kind;queued_request=std::move(request);
            message="UPDATING ROOM...";
            fprintf(stderr,"[online-menu] queued action during room poll\n");
            return true;
        }
        return false;
    }
    if(!token.empty())request.object["token"]=token;
    if(kind==Work::Change || kind==Work::Start || kind==Work::Select || kind==Work::Character)request.object["revision"]=room_reply.at("room").at("revision");
    work=kind;
    pending=std::async(std::launch::async,[request] {
        try{return Response{mkdd::online::room_request(request),{}};}
        catch(const std::exception& error){return Response{Json(),error.what()};}
    });
    return true;
}
void create_or_join(bool host) {
    if(busy())return;
    if(build.empty())build=mkdd::online::executable_build();
    const char* name=getenv("MKDD_ONLINE_NAME");
    std::string player=name?std::string(name).substr(0,16):"Player";
    Json request=Json::dict({{"op",host?"create":"join"},{"name",player},{"initials",initials},{"compatibility",build}});
    request.object["name"]=initials;
    if(host){request.object["mode"]="grand-prix";request.object["layout"]="mixed";request.object["flow"]="garage";}
    else request.object["code"]=code;
    message=host?"CREATING ROOM...":"JOINING ROOM...";
    cancelled=false;submit(host?Work::Create:Work::Join,std::move(request));
}
bool valid(uint32_t p,unsigned size=0x100) {
    return p>=0x80000000u && p<=0x80000000u+RAM_SIZE-size && !(p&3);
}
float read_float(uint32_t address) {
    uint32_t bits=mem_r32(address);float value;memcpy(&value,&bits,4);return value;
}
void write_float(uint32_t address,float value) {
    uint32_t bits;memcpy(&bits,&value,4);mem_w32(address,bits);
}
void fp(CPU* c,unsigned index,float value) { c->f[index].d=value;c->ps1[index]=value; }
void text(CPU* c,const char* label,float x,float y,float size=24,uint32_t colour=0xffffffff,bool center=false) {
    const CPU saved=*c;
    c->r[1]-=0x400;
    const uint32_t object=c->r[1]+0x80, string=c->r[1]+0x100;
    if (strlen(label)>=256) fatal("Online menu label too long");
    memcpy(mem_ptr(string),label,strlen(label)+1);
    c->r[3]=object;c->r[4]=mem_r32(c->r[13]-22112);fp(c,1,0);
    fn_8002E484(c); // native J2DPrint with FontMgr's kart font
    write_float(object+0x50,size);write_float(object+0x54,size);
    mem_w32(object+0x40,colour);mem_w32(object+0x44,colour);
    c->r[3]=object;c->r[4]=string;c->r[5]=center?0:2;c->r[6]=2;c->r[7]=255;
    fp(c,1,608);fp(c,2,448);fp(c,3,x);fp(c,4,y);
    fn_8002EB74(c); // native J2DPrint::printReturn, no variadic format string
    *c=saved;
}
void label(CPU* c,const std::string& value,float y,float size=24,uint32_t colour=0xffffffff) {
    text(c,value.c_str(),1,y+1,size,0x000000ff,true);
    text(c,value.c_str(),0,y,size,colour,true);
}
void flat_polygon(CPU* c,const float (*points)[2],unsigned count,uint32_t colour) {
    const CPU saved=*c;
    fn_800BCA18(c);
    for(unsigned attr:{9u,11u}){c->r[3]=attr;c->r[4]=1;fn_800BC100(c);}
    c->r[3]=0;c->r[4]=9;c->r[5]=1;c->r[6]=4;c->r[7]=0;fn_800BCA50(c);
    c->r[3]=0;c->r[4]=11;c->r[5]=1;c->r[6]=5;c->r[7]=0;fn_800BCA50(c);
    c->r[3]=0;fn_800BD5D8(c);c->r[3]=1;fn_800BF040(c);
    c->r[3]=4;c->r[4]=0;c->r[5]=0;c->r[6]=1;c->r[7]=0;c->r[8]=0;c->r[9]=2;fn_800BF07C(c);
    c->r[3]=1;fn_800C0D94(c);
    c->r[3]=0;c->r[4]=255;c->r[5]=255;c->r[6]=4;fn_800C0BF8(c);
    c->r[3]=0;c->r[4]=4;fn_800C0658(c);
    c->r[3]=0;c->r[4]=7;c->r[5]=0;fn_800C11B0(c);
    c->r[3]=0;fn_800BE0E4(c);
    c->r[3]=1;c->r[4]=4;c->r[5]=5;c->r[6]=15;fn_800C1104(c);
    c->r[3]=0xa0;c->r[4]=0;c->r[5]=count;fn_800BDEC4(c);
    for(unsigned i=0;i<count;++i){uint32_t a,b;memcpy(&a,&points[i][0],4);memcpy(&b,&points[i][1],4);gp_write32(a);gp_write32(b);gp_write32(0);gp_write32(colour);}
    *c=saved;
}
void box(CPU* c,float x,float y,float w,float h,uint32_t colour) {
    const float points[][2]={{x,y},{x+w,y},{x+w,y+h},{x,y+h}};flat_polygon(c,points,4,colour);
}
void rounded(CPU* c,float x,float y,float w,float h,float r,uint32_t colour) {
    const float points[][2]={{x+r,y},{x+w-r,y},{x+w,y+r},{x+w,y+h-r},{x+w-r,y+h},{x+r,y+h},{x,y+h-r},{x,y+r}};flat_polygon(c,points,8,colour);
}
void oval(CPU* c,float x,float y,float w,float h,uint32_t colour) {
    float points[20][2];for(unsigned i=0;i<20;++i){const float t=i*6.2831853f/20;points[i][0]=x+w*.5f+std::cos(t)*w*.5f;points[i][1]=y+h*.5f+std::sin(t)*h*.5f;}flat_polygon(c,points,20,colour);
}
void kart_icon(CPU* c,float x,float y,uint32_t colour) {
    box(c,x,y+7,8,13,0x131c34ff);box(c,x+35,y+7,8,13,0x131c34ff);
    box(c,x,y+36,8,14,0x131c34ff);box(c,x+35,y+36,8,14,0x131c34ff);
    box(c,x+8,y+2,27,50,colour);box(c,x+12,y+13,19,12,0x172442ff);box(c,x+12,y+30,19,12,0x172442ff);
    box(c,x+7,y,29,5,0xffe95cff);
}
bool native_picture(CPU* c,uint32_t resource,float x,float y,float width,float height,uint32_t tint=0xffffffff,bool stretch=false) {
    if(!valid(resource,0x20))return false;
    const auto line=mem_r32(c->r[13]-21896);if(!valid(line,0x124))return false;
    const auto pane=mem_r32(line+0x120);if(!valid(pane,0x168))return false;
    const CPU saved=*c;const auto table=mem_r32(pane);
    uint32_t colors[6];for(unsigned i=0;i<6;++i){colors[i]=mem_r32(pane+0x148+i*4);mem_w32(pane+0x148+i*4,i==1?0:i>=2?tint:0xffffffff);}
    const auto alpha=mem_r8(pane+0xb2);mem_w8(pane+0xb2,255);
    c->r[3]=pane;c->r[4]=resource;c->r[5]=0;call_indirect(c,mem_r32(table+0x110));const auto previous=c->r[3];
    const float w=mem_r16(resource+2),h=mem_r16(resource+4),scale=std::min(width/std::max(1.0f,w),height/std::max(1.0f,h));
    c->r[3]=pane;c->r[4]=0;c->r[5]=0;c->r[6]=0;fp(c,1,stretch?x:x+(width-w*scale)/2);fp(c,2,stretch?y:y+(height-h*scale)/2);fp(c,3,stretch?width:w*scale);fp(c,4,stretch?height:h*scale);
    call_indirect(c,mem_r32(table+0xec));
    if(valid(previous,0x20)){c->r[3]=pane;c->r[4]=previous;c->r[5]=0;call_indirect(c,mem_r32(table+0x110));}
    for(unsigned i=0;i<6;++i)mem_w32(pane+0x148+i*4,colors[i]);mem_w8(pane+0xb2,alpha);*c=saved;return true;
}
void sound(CPU* c,unsigned id) {
    const CPU saved=*c;c->r[3]=mem_r32(c->r[13]-24008);c->r[4]=id;fn_80115E74(c);*c=saved;
}
bool backgrounds(CPU* c) {
    const CPU saved=*c;
    const uint32_t factory=mem_r32(c->r[13]-23784);
    if (!valid(factory,0x48) || !valid(mem_r32(factory+0x24)) || !valid(mem_r32(factory+0x20))) return false;
    if (!mem_r32(c->r[13]-21904)) {
        c->r[3]=0x120;fn_800155A8(c);
        if (!c->r[3]) fatal("Cannot allocate native online menu background");
        c->r[4]=mem_r32(factory+0x24);fn_801E7154(c);mem_w32(saved.r[13]-21904,c->r[3]);
    }
    if (!mem_r32(saved.r[13]-21896)) {
        c->r[3]=0x160;fn_800155A8(c);
        if (!c->r[3]) fatal("Cannot allocate native online title line");
        c->r[4]=mem_r32(factory+0x20);fn_801E7240(c);mem_w32(saved.r[13]-21896,c->r[3]);
    }
    *c=saved;return true;
}
void open(CPU* c,Page next) {
    title_hint_hidden=next!=Page::Title;display_set_settings_hint(false);
    page=next;selection=next==Page::Engine?2:0;online_focus=(int)selection;message.clear();sound(c,0x20002);
    desktop_pointer::publish("online-"+std::to_string((int)page),{});
    desktop_pointer::take_text();
    if (next!=Page::Title && backgrounds(c)) {
        const CPU saved=*c;c->r[3]=mem_r32(c->r[13]-21896);c->r[4]=2;fn_801E73D0(c);*c=saved;
    }
    fprintf(stderr,"[online-menu] page=%u\n",(unsigned)page);
}
void animate(CPU* c,uint32_t scene) {
    const CPU saved=*c;
    mem_w32(scene+0x16c,1);c->r[3]=scene;fn_8012E304(c);
    if (page!=Page::Title && backgrounds(c)) {
        c->r[3]=mem_r32(saved.r[13]-21904);fn_801E71D8(c);
        c->r[3]=mem_r32(saved.r[13]-21896);fn_801E74BC(c);
    }
    *c=saved;
}
uint32_t authored_kart(CPU* c) {
    static bool attempted=false;static uint32_t resource=0;if(attempted)return resource;attempted=true;
    const char* path=getenv("MKDD_MENU_KART_ASSET");if(!path)return 0;
    std::ifstream file(path,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(file)),{});
    if(bytes.size()!=32+256*448*4 || bytes[0]!=6 || (unsigned char)bytes[31]!=32)return 0;
    const CPU saved=*c;c->r[3]=(uint32_t)bytes.size()+31;fn_800155A8(c);resource=c->r[3]?(c->r[3]+31)&~31u:0;*c=saved;
    if(!valid(resource,(unsigned)bytes.size())){resource=0;return 0;}memcpy(mem_ptr(resource),bytes.data(),bytes.size());return resource;
}
#include "online_classic.inc"
#include "classic_production.inc"
void draw_online(CPU* c,uint32_t scene) {
    clickable.clear();
    if (!backgrounds(c)) { label(c,"LOADING...",350);return; }
    const CPU saved=*c;
    const uint32_t ortho=mem_r32(scene+4),back=mem_r32(c->r[13]-21904),line=mem_r32(c->r[13]-21896);
    c->r[3]=ortho;call_indirect(c,mem_r32(mem_r32(ortho)+0x14));
    c->r[3]=back;c->r[4]=ortho;fp(c,1,0);fp(c,2,0);fn_80030F4C(c);
    const uint32_t title=mem_r32(line+0x120);
    const uint8_t visible=valid(title)?mem_r8(title+0xb0):0;
    if (valid(title)) mem_w8(title+0xb0,0);
    c->r[3]=line+8;c->r[4]=ortho;fp(c,1,0);fp(c,2,0);fn_80030F4C(c);
    if (valid(title)) mem_w8(title+0xb0,visible);
    c->r[3]=ortho;call_indirect(c,mem_r32(mem_r32(ortho)+0x14));
    if(classic_draw(c)){*c=saved;return;}
    if(page==Page::Lobby)classic_heading(c,"ONLINE PLAY","CHOOSE YOUR KART");
    else if(page==Page::Type)classic_heading(c,"CHOOSE GAME MODE","HOST CHOOSES THE MODE");
    else if(page==Page::Engine)classic_heading(c,"GRAND PRIX","CHOOSE ENGINE CLASS");
    else if(page==Page::Battle)classic_heading(c,"BATTLE MODE","CHOOSE YOUR BATTLE");
    else label(c,"ONLINE PLAY",22,30,0xffe95cff);
    if (page==Page::Type || page==Page::HostJoin) {
        if(page==Page::Type) {
            for(unsigned i=0;i<2;++i){const float x=22+i*292;classic_card(c,x,135,272,206,selection==i);
                if(i==0)classic_trophy(c,x+105,164);else classic_balloons(c,x+82,154);
                text(c,i?"BATTLE MODE":"GRAND PRIX",x+35,268,25,0xffe95cff);
                text(c,i?"COMPETE IN AN ARENA":"RACE A FULL CUP",x+37,305,12,0xfff0c4ff);hit(x,135,272,206,i);}
            rounded(c,195,363,218,31,7,0xdf142aff);label(c,"CONTINUE  >",369,21,0xffe95cff);
            hit(195,363,218,31,selection);
        }else {
        label(c,"PLAY WITH FRIENDS",114,20);
        const char* types[]={"GRAND PRIX","BATTLE MODE"};
        const char* actions[]={"HOST ROOM","JOIN ROOM"};
        const unsigned count=2;
        for (unsigned i=0;i<count;++i) label(c,(selection==i?"> ":"  ")+std::string(page==Page::Type?types[i]:actions[i]),
            (page==Page::Type?166:185)+i*(page==Page::Type?47:64),page==Page::Type?25:28,selection==i?0xffe95cff:0xffffffff);
        for(unsigned i=0;i<2;++i)hit(80,(page==Page::Type?157:176)+i*(page==Page::Type?47:64),448,40,i);
        }
        if(page==Page::HostJoin)label(c,"NAME: "+initials+"  /  N OR MENU TO EDIT",329,17,0xbbcde4ff);
        if(page==Page::Type && room_reply.has("room") && !room_reply.at("room").at("host").boolean())label(c,"HOST IS CHOOSING THE GAME MODE",329,15,0xbbcde4ff);
    } else if (page==Page::Code || page==Page::Initials) {
        const bool naming=page==Page::Initials;
        label(c,naming?"YOUR NAME":"ENTER ROOM CODE",94,24,0xffe95cff);
        std::string shown=naming?draft_initials:code;while(shown.size()<(naming?7u:8u))shown+='-';
        label(c,shown,130,30);
        for(unsigned i=0;i<36;++i) {
            auto [x,y]=key_position(i);const bool allowed=naming || strchr(alphabet,keyboard_keys[i]);
            box(c,x-4,y-5,39,31,selection==i?0x967c28ff:0x193c6cff);
            char key[2]={keyboard_keys[i],0};text(c,key,x+5,y,21,allowed?(selection==i?0xffe95cff:0xffffffff):0x65738aff);
            if(allowed)hit(x-4,y-5,39,31,i);
        }
        text(c,selection==36?"> DELETE":"  DELETE",93,359,20,selection==36?0xffe95cff:0xffffffff);
        text(c,((selection==37?"> ":"  ")+std::string(naming?"SAVE":"JOIN")).c_str(),377,359,20,selection==37?0xffe95cff:0xffffffff);
        hit(80,350,160,30,36);hit(360,350,160,30,37);
        label(c,"TYPE DIRECTLY, CLICK KEYS, OR USE YOUR CONTROLLER",392,11,0xbbcde4ff);
    } else if (page==Page::LeaveConfirm && room_reply.has("room")) {
        label(c,"LEAVE ROOM?",111,30,0xffe95cff);
        label(c,"ARE YOU SURE?",165,23);
        label(c,room_reply.at("room").at("host").boolean()?"THIS WILL CLOSE THE ROOM FOR EVERYONE":"YOU WILL NEED THE ROOM CODE TO REJOIN",207,15,0xbbcde4ff);
        label(c,std::string(selection==0?"> ":"  ")+"NO, STAY",274,25,selection==0?0xffe95cff:0xffffffff);
        label(c,std::string(selection==1?"> ":"  ")+"YES, LEAVE",325,25,selection==1?0xffe95cff:0xffffffff);
        hit(100,266,408,35,0);hit(100,317,408,35,1);
        label(c,"A: CONFIRM    B: STAY IN ROOM",410,15);
    } else if (page==Page::Lobby && room_reply.has("room")) {
        const Json& room=room_reply.at("room");
        const auto common=mem_r32(c->r[13]-21928);
        const uint32_t colors[]={0xd93942ff,0x34b658ff,0x3773dcff,0xf1c52aff,0x9955d7ff,0xea70b7ff,0x36b9dcff,0xf18b29ff};
        for(unsigned kart=0;kart<8;++kart) {
            const float x=10+(kart%4)*149,y=95+(kart/4)*136;
            const Json* riders[2]{};
            for(const auto& m:room.at("members").list()){const auto seat=m.at("seat").integer(0,15);if(seat==kart)riders[0]=&m;if(seat==kart+8)riders[1]=&m;}
            if(selection%8==kart){rounded(c,x,y,142,131,8,0xffd23aff);rounded(c,x+3,y+3,136,125,6,0x09458bff);}
            text(c,std::to_string(kart+1).c_str(),x+4,y+3,20,0xffe95cff);
            classic_kart(c,x+6,y+23,colors[kart],riders[0],riders[1]);
            for(unsigned rider=0;rider<2;++rider) {
                if(rider && !riders[0])continue;
                const unsigned seat=kart+rider*8;const float ry=y+30+rider*49;
                rounded(c,x+72,ry,65,28,4,selection==seat?0xffed89ff:riders[rider]?0xffe58aff:0xb1d8ffff);
                if(!riders[rider])rounded(c,x+74,ry+2,61,24,3,0x114b9aff);
                std::string name=riders[rider]?riders[rider]->at("initials").str():rider?"JOIN BACK":"TAKE WHEEL";
                text(c,name.c_str(),x+76,ry+6,riders[rider]?9:8,riders[rider]?0x09266aff:0xffffffff);
                hit(x+70,ry-3,69,39,seat);
                if(riders[rider]) {
                    const auto& m=*riders[rider];std::string detail=(m.at("you").boolean()?"YOU ":"")+std::string(m.at("ready").boolean()?"READY":"");
                    oval(c,x+76,ry+19,5,5,m.at("ready").boolean()?0x5aff63ff:0x7e8592ff);
                    text(c,detail.c_str(),x+84,ry+19,6,0x082359ff);
                    if(valid(common,0x190))native_picture(c,mem_r32(common+0x18c),x+79,ry+29,11,11);
                    text(c,std::to_string(m.at("stars").integer(0,INT64_MAX)).c_str(),x+93,ry+30,9,0xffe95cff);
                }
            }
        }
        const char* actions[]={"X: READY","Y: PLAY","NAME","LEAVE"};
        for(unsigned i=0;i<4;++i){float x=18+i*146;box(c,x,370,134,26,0x244c7fff);text(c,actions[i],x+9,375,14);if(i!=1 || room.at("host").boolean())hit(x,370,134,26,32+i);}
        label(c,room.at("host").boolean()?"SELECT A PLAYER + RB TO KICK  /  F1: ROOM SETTINGS":"A: TAKE SEAT  /  N OR MENU: EDIT NAME",410,11,0xbbcde4ff);
    } else if(page==Page::Battle) {
        const char* names[]={"BALLOON BATTLE","SHINE THIEF","BOB-OMB BLAST"};
        const auto common=mem_r32(c->r[13]-21928);
        for(unsigned i=0;i<3;++i) {
            const float x=13+i*199;classic_card(c,x,143,184,190,selection==i);
            if(valid(common,0x1a4))native_picture(c,mem_r32(common+0x198+i*4),x+43,164,98,98);
            text(c,names[i],x+10,284,15,0xffe95cff);hit(x,143,184,190,i);
        }
        label(c,"ONE BATTLE, THEN BACK TO YOUR ROOM",345,17);
        label(c,"A: CHOOSE CHARACTERS",381,14);
    } else if(page==Page::Engine) {
        const char* names[]={"50cc","100cc","150cc","MIRROR"};
        for(unsigned i=0;i<4;++i){float x=13+i*149;classic_card(c,x,161,136,157,selection==i);
            oval(c,x+39,183,58,58,0xc7dcf0ff);oval(c,x+45,189,46,46,0x18478bff);
            text(c,i==0?"1":i==1?"2":i==2?"3":"M",x+56,200,28,0xffe95cff);
            text(c,names[i],x+18,270,i==3?21:26,0xffe95cff);hit(x,161,136,157,i);}
    } else if(page==Page::KickConfirm) {
        label(c,"REMOVE "+kick_name+"?",120,27,0xffe95cff);
        label(c,"THEY CAN REJOIN WITH THE ROOM CODE",182,16);
        label(c,selection==0?"> KEEP PLAYER":"  KEEP PLAYER",258,23);hit(120,250,368,35,0);
        label(c,selection==1?"> REMOVE PLAYER":"  REMOVE PLAYER",318,23);hit(120,310,368,35,1);
    } else if(page==Page::Rules) {
        label(c,"ROOM RULES",101,24,0xffe95cff);
        const bool gp=draft_mode=="grand-prix";
        const std::string competition=gp?"GRAND PRIX":draft_mode=="balloon"?"BALLOON BATTLE":draft_mode=="shine"?"SHINE THIEF":"BOB-OMB BLAST";
        const std::string rows[]={"LAYOUT: "+std::string(draft_layout=="coop"?"CO-OP":"SOLO"),"MODE: "+competition,
            gp?"CLASS: "+draft_engine:"ARENA: "+std::string(content::name(draft_arena,true)),gp?"CUP: "+draft_cup:"ROUNDS: "+std::to_string(draft_rounds),
            "START COURSE: "+std::string(content::name(draft_course,false))};
        for(unsigned i=0;i<(gp?5u:4u);++i)label(c,(selection==i?"> ":"  ")+rows[i],151+i*39,18,selection==i?0xffe95cff:0xffffffff);
        label(c,"ALL ONLINE CONTENT AVAILABLE",348,12,0xbbcde4ff);
        label(c,room_reply.at("room").at("host").boolean()?"LEFT / RIGHT: CHANGE    A: APPLY    B: BACK":"HOST CHOOSES RULES    A / B: BACK",375,15);
    } else if(page==Page::Kart) {
        label(c,room_reply.at("room").at("layout").str()=="coop"?"YOUR SHARED KART":"YOUR KART",101,24,0xffe95cff);
        const std::string rows[]={"KART: "+std::string(content::karts[draft_kart]),
            "DRIVER: "+std::string(content::characters[draft_driver]),"PARTNER: "+std::string(content::characters[draft_partner])};
        for(unsigned i=0;i<3;++i)label(c,(selection==i?"> ":"  ")+rows[i],177+i*47,20,selection==i?0xffe95cff:0xffffffff);
        label(c,"ALL 21 KARTS / ALL 20 CHARACTERS",326,14,0xbbcde4ff);
        label(c,"RIDERS AND KART FOLLOW NORMAL WEIGHT LIMITS",350,12,0xbbcde4ff);
        label(c,"LEFT / RIGHT: CHANGE    A: APPLY    B: BACK",405,15);
    }
    if (!message.empty()) label(c,message,page==Page::Lobby?400:381,11,0xffe95cff);
    if(page!=Page::Lobby && page!=Page::Rules && page!=Page::Kart && page!=Page::LeaveConfirm)label(c,page==Page::Code?"A: SELECT    X: DELETE    B: BACK":"A: CONFIRM    B: BACK",410,15);
    text(c,release_label().c_str(),12,435,10,0xbbcde4ff);
    desktop_pointer::publish("online-"+std::to_string((int)page),clickable);
    *c=saved;
}
void tree(uint32_t pane,unsigned depth=0) {
    if (!valid(pane) || depth>12) return;
    char tag[9]{};memcpy(tag,HOST(pane+0x10),8);
    for (unsigned i=0;i<8;++i) if (!tag[i]) tag[i]='.';
    fprintf(stderr,"[menu-pane] %*s%08x %.8s visible=%u xy=%.1f,%.1f bounds=%.1f,%.1f,%.1f,%.1f\n",
            depth*2,"",pane,tag,mem_r8(pane+0xb0),read_float(pane+0x8c),read_float(pane+0x9c),
            read_float(pane+0x20),read_float(pane+0x24),read_float(pane+0x28),read_float(pane+0x2c));
    uint32_t link=mem_r32(pane+0xdc);
    for (unsigned i=0;link && i<200;++i) {
        if (!valid(link,16)) break;
        tree(mem_r32(link),depth+1);link=mem_r32(link+0xc);
    }
}
void network_updates(CPU* c) {
    if(busy() && pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        Response result=pending.get();Work completed=work;work=Work::None;
        if(leave_pending) {
            leave_pending=false;queued_work=Work::None;submit(Work::Leave,Json::dict({{"op","leave"}}));return;
        }
        if(cancelled && (completed==Work::Create || completed==Work::Join)) {
            cancelled=false;
            if(result.error.empty()) { token=result.value.at("token").str();submit(Work::Leave,Json::dict({{"op","leave"}})); }
            return;
        }
        if(completed==Work::Leave) { restore_menu_unlocks();token.clear();room_reply=Json();return; }
        if(!result.error.empty()) {
            mode_advance=false;
            fprintf(stderr,"[online-menu] room action failed: %s\n",result.error.c_str());
            message=friendly(result.error);
            if(result.error=="invalid-session") {
                restore_menu_unlocks();
                if(native_selection){native_selection=false;native_scene(c,0);}
                token.clear();room_reply=Json();queued_work=Work::None;
                open(c,Page::HostJoin);message="THIS ROOM HAS CLOSED";
            }
        } else {
            try {
                if(completed==Work::Create || completed==Work::Join) {
                    token=result.value.at("token").str();open(c,Page::Lobby);message.clear();
                }
                room_reply=std::move(result.value);
                if(completed==Work::Change){if(page!=Page::LeaveConfirm && page!=Page::Lobby)open(c,Page::Lobby);message.clear();}
                if(completed==Work::Poll)message.clear();
                const bool returning=getenv("MKDD_ROOM_RESUME")!=nullptr;
                if(returning && room_reply.at("room").at("state").str()=="forming")_putenv_s("MKDD_ROOM_RESUME","");
                if(room_reply.at("room").at("state").str()=="running" && !returning) {
                    _putenv_s("MKDD_ROOM_TOKEN",token.c_str());
                    message="STARTING EVENT...";mkdd::online::enter_race(room_reply);
                }
                native_room_state(c);
            } catch(const std::exception& error){message=friendly(error.what());}
        }
        if(completed==Work::Character)native_character_reply(result.error);
        // A failed mutation must refresh its revision immediately. Repeated
        // button presses otherwise postpone every poll and retain stale seats.
        next_poll=std::chrono::steady_clock::now()+
            (result.error=="stale-room"?std::chrono::milliseconds(0):native_selection?std::chrono::milliseconds(75):std::chrono::milliseconds(500));
        if(completed==Work::Poll && queued_work!=Work::None) {
            const Work next=queued_work;queued_work=Work::None;
            const auto state=room_reply.has("room")?room_reply.at("room").at("state").str():std::string();
            if(result.error.empty() && !token.empty() && state!="running" && (next!=Work::Character || state=="characters") && (next!=Work::Select || state=="mode" || state=="characters" || state=="course"))
                submit(next,std::move(queued_request));
        }
        // Automatic selection uploads can otherwise immediately retry the old
        // revision forever, preventing the state poll that would refresh it.
        const bool phase_changed=native_selection && (result.error=="host-only" || result.error=="event-started" || result.error=="invalid-preview" || result.error=="invalid-character");
        if((result.error=="stale-room" || result.error=="character-taken" || phase_changed) && !busy() && !token.empty())submit(Work::Poll,Json::dict({{"op","state"}}));
    }
}
void edit_rules() {
    const Json& room=room_reply.at("room");draft_mode=room.at("mode").str();draft_layout=room.at("layout").str();
    const Json& rules=room.at("rules");draft_engine="150cc";draft_cup="mushroom";draft_rounds=3;draft_arena=52;draft_course=36;
    if(draft_mode=="grand-prix"){draft_engine=rules.at("engine").str();draft_cup=rules.at("cup").str();draft_course=rules.has("course")?(unsigned)rules.at("course").integer(33,51):content::cup_courses(draft_cup)[0];}
    else {draft_rounds=(int)rules.at("rounds").integer(1,9);draft_arena=(unsigned)rules.at("arena").integer(52,59);}
}
void edit_kart() {
    for(const auto& member:room_reply.at("room").at("members").list())if(member.at("you").boolean()) {
        const auto& choice=member.at("choice");
        draft_kart=(unsigned)choice.at("kart").integer(0,20);
        draft_driver=(unsigned)choice.at("driver").integer(1,20);draft_partner=(unsigned)choice.at("partner").integer(1,20);
    }
}
void cycle_id(unsigned& value,const std::vector<unsigned>& list,bool backwards) {
    auto found=std::find(list.begin(),list.end(),value);size_t at=found==list.end()?0:found-list.begin();
    value=list[(at+list.size()+(backwards?-1:1))%list.size()];
}
void cycle(std::string& value,std::initializer_list<const char*> values,bool backwards) {
    std::vector<std::string> list;for(const char* s:values)list.emplace_back(s);
    auto found=std::find(list.begin(),list.end(),value);size_t at=found==list.end()?0:found-list.begin();
    value=list[(at+list.size()+(backwards?-1:1))%list.size()];
}
Json native_course_preview(CPU*,uint32_t);
void native_follow_course(CPU*,uint32_t);
#include "online_native_selection.inc"
#include "native_pointer.inc"
bool native_dialog_regions(CPU* c) {
    const auto state=mem_r32(c->r[13]-0x5644);
    if(state!=3 && state!=4)return false;
    const auto dialog=mem_r32(c->r[13]-0x561c);if(!valid(dialog,0x30))return false;
    clickable.clear();
    if(state==4){hit(115,325,150,53,91);hit(355,325,150,53,92);}
    else hit(90,310,430,85,91);
    desktop_pointer::context(true);desktop_pointer::publish("memory-card-"+std::to_string(mem_r32(dialog+0x10)),clickable);return true;
}
void desktop_online(CPU* c) {
    using namespace desktop_online_ui;
    State state;state.available=enabled();state.can_join=token.empty() && !native_selection;
    state.can_edit=token.empty() || (room_reply.has("room") && room_reply.at("room").at("state").str()=="forming");
    state.name=initials;state.version=release_label();state.status=message;
    if(room_reply.has("room")) {
        const auto& room=room_reply.at("room");state.code=room.at("code").str();
        state.can_kick=room.at("host").boolean() && room.at("state").str()!="running";
        state.can_refresh=room.at("host").boolean() && room.at("state").str()=="forming";
        for(const auto& m:room.at("members").list())state.members.push_back({m.at("initials").str(),(unsigned)m.at("seat").integer(0,15),m.at("you").boolean(),m.at("host").boolean(),m.at("id").str()});
    }
    publish(state);
    static std::optional<Command> command;
    if(!command)command=take();
    if(!command || !can_act())return;
    const auto action=*command;command.reset();
    if(action.action==Action::Name && state.can_edit) {
        if(action.text.empty() || action.text.size()>7 || action.text.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")!=std::string::npos){message="USE 1 TO 7 LETTERS OR NUMBERS";return;}
        initials=action.text;std::ofstream("online-initials.txt",std::ios::trunc)<<initials<<'\n';
        if(!token.empty())submit(Work::Change,Json::dict({{"op","profile"},{"initials",initials}}));
    }else if(action.action==Action::Join && state.can_join) {
        if(action.text.size()!=8 || action.text.find_first_not_of(alphabet)!=std::string::npos){message="ENTER AN 8 CHARACTER ROOM CODE";return;}
        code=action.text;native_scene(c,0);open(c,Page::HostJoin);create_or_join(false);
    }else if(action.action==Action::Kick && state.can_kick)submit(Work::Change,Json::dict({{"op","kick"},{"seat",(int)action.seat},{"member",action.text}}));
    else if(action.action==Action::Refresh && state.can_refresh)submit(Work::Change,Json::dict({{"op","refresh-code"}}));
}
}

extern "C" bool mkdd_online_menu_wide(){return enabled() && page!=Page::Title && page!=Page::Native && !native_selection && !mkdd::race_read().exists;}
extern "C" void hle_fn_8012D88C(CPU* c) {
    const uint32_t scene=c->r[3];
    display_set_settings_hint(page==Page::Title && !title_hint_hidden && valid(scene,0x174) && !mem_r8(scene+0x169));
    if (enabled() && valid(scene,0x174) && page!=Page::Title) { draw_online(c,scene);return; }
    uint8_t visible[4]{};
    const bool replace=enabled() && valid(scene,0x174) && mem_r32(scene+0x15c)>=4;
    if (replace) for (unsigned i=0;i<4;++i) {
        const uint32_t pane=mem_r32(scene+0x10+12*(8+i));
        visible[i]=mem_r8(pane+0xb0);mem_w8(pane+0xb0,0);
    }
    orig_fn_8012D88C(c);
    if(native_dialog_regions(c))return;
    if (!replace) {
        if(valid(scene,0x174) && mem_r32(scene+0x15c)>=4) {
            clickable.clear();for(unsigned i=0;i<3;++i){const auto pane=mem_r32(scene+0x10+12*(8+i));if(valid(pane,0xb4))hit(120,read_float(pane+0x9c)-18,368,32,i);}
            desktop_pointer::publish("offline-title",clickable);
            const CPU saved=*c;c->r[3]=mem_r32(scene+4);call_indirect(c,mem_r32(mem_r32(c->r[3])+0x14));text(c,release_label().c_str(),12,430,10,0xbbcde4ff);*c=saved;
        }
        return;
    }
    const CPU saved=*c;
    c->r[3]=mem_r32(scene+4);call_indirect(c,mem_r32(mem_r32(c->r[3])+0x14));*c=saved;
    static bool dumped;
    if (!dumped && getenv("MKDD_MENU_TRACE")) {
        dumped=true;
        fprintf(stderr,"[menu] title=%08x screen=%08x\n",scene,mem_r32(scene+0x158));
        tree(mem_r32(scene+0x158));
    }
    const char* labels[]={"START GAME","ONLINE PLAY","RECORDS","OPTIONS"};
    clickable.clear();
    for (unsigned i=0;i<4;++i) {
        const uint32_t pane=mem_r32(scene+0x10+12*(8+i));
        mem_w8(pane+0xb0,visible[i]);
        label(c,labels[i],read_float(pane+0x9c)-14,mem_r8(scene+0x168)==i?28:24,mem_r8(scene+0x168)==i?0xffe95cff:0xddd09cff);
        hit(120,read_float(pane+0x9c)-18,368,32,i);
    }
    text(c,release_label().c_str(),12,430,10,0xbbcde4ff);
    desktop_pointer::publish("title",clickable);
}

extern "C" void hle_fn_8012DCC4(CPU* c) {
    const uint32_t scene=c->r[3];
    const auto card_state=mem_r32(c->r[13]-0x5644);if(card_state==3 || card_state==4){orig_fn_8012DCC4(c);return;}
    struct RestoreButtons {uint32_t pressed=mem_r32(0x803b442c),repeat=mem_r32(0x803b4440);~RestoreButtons(){mem_w32(0x803b442c,pressed);mem_w32(0x803b4440,repeat);}} restore_buttons;
    if(valid(scene,0x174) && mem_r32(scene+0x15c)>=4 && page==Page::Title && (mem_r32(0x803b4410+0x1c)&0x1100)) {title_hint_hidden=true;display_set_settings_hint(false);}
    if (!enabled() || mem_r8(scene+0x169)) {
        if(!enabled() && !mem_r8(scene+0x169)) {
            const auto pointer=desktop_pointer::read();if(pointer.hover>=0 && pointer.hover<3)mem_w8(scene+0x168,(uint8_t)pointer.hover);
            if(auto click=desktop_pointer::take())if(*click>=0 && *click<3){mem_w8(scene+0x168,(uint8_t)*click);native_menu_button(0x100);}
        }
        orig_fn_8012DCC4(c);return;
    }
    if(!profile_loaded) {
        profile_loaded=true;std::ifstream file("online-initials.txt");std::string saved;file>>saved;
        if(!saved.empty() && saved.size()<=7 && saved.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")==std::string::npos)initials=saved;
    }
    uint32_t pressed=mem_r32(0x803b4410+0x1c),repeat=mem_r32(0x803b4410+0x30);
    desktop_pointer::text_enabled=page==Page::Code || page==Page::Initials;
    const auto pointer=desktop_pointer::read();
    const unsigned choices=page==Page::HostJoin || page==Page::Type || page==Page::LeaveConfirm || page==Page::KickConfirm?2:page==Page::Engine?4:page==Page::Battle?3:page==Page::Lobby?16:38;
    const bool navigable=page==Page::HostJoin || page==Page::Type || page==Page::Engine || page==Page::Battle || page==Page::Lobby || page==Page::Code || page==Page::Initials || page==Page::LeaveConfirm || page==Page::KickConfirm;
    static desktop_pointer::State last_pointer;
    const bool moved=pointer.hover!=last_pointer.hover || pointer.x!=last_pointer.x || pointer.y!=last_pointer.y;
    last_pointer=pointer;
    if(moved && (pointer.hover>=0 || pointer.hover==-2))online_focus=pointer.hover;
    if(navigable) {
        const int dx=(repeat&(0x01000000|1))?-1:(repeat&(0x02000000|2))?1:0;
        const int dy=(repeat&(0x08000000|8))?-1:(repeat&(0x04000000|4))?1:0;
        if(dx || dy){
            bool footer=false;
            if(page==Page::Lobby && dy>0 && online_focus>=0 && online_focus<16 && online_focus%8>=4 && room_reply.has("room")) {
                bool backseat=false;for(const auto& m:room_reply.at("room").at("members").list())if(m.at("seat").integer(0,15)==online_focus)backseat=true;
                if(online_focus>=8 || !backseat){const int actions[]={32,35,34,33};online_focus=actions[online_focus%4];if(online_focus==33 && !room_reply.at("room").at("host").boolean())online_focus=34;footer=true;if(getenv("MKDD_MENU_TEST"))fprintf(stderr,"[online-focus] action=%d\n",online_focus);}
            }
            if(!footer)online_focus_move(c,dx,dy);
            sound(c,0x20000);repeat&=~0x0f00000fu;
        }
        if(online_focus>=0 && (unsigned)online_focus<choices)selection=(unsigned)online_focus;
    }else if(pointer.hover>=0 && (unsigned)pointer.hover<choices){if(page==Page::Title && pointer.hover<4)mem_w8(scene+0x168,(uint8_t)pointer.hover);else selection=pointer.hover;}
    auto activated=desktop_pointer::take();
    if(!activated && navigable && (pressed&0x100))activated=online_focus;
    if(activated) {
        const int action=*activated;
        if(navigable)pressed&=~0x100u;
        if(action==-2)pressed|=0x200;
        else if(page==Page::HostJoin && action==34)pressed|=0x1000;
        else if(page==Page::Lobby && action>=32){const uint32_t buttons[]={0x400,0x800,0x1000,0x200};if(action<36)pressed|=buttons[action-32];}
        else if(action==38 && (page==Page::Type || page==Page::Engine || page==Page::Battle))pressed|=0x100;
        else if(action>=0 && (unsigned)action<choices){online_focus=action;selection=(unsigned)action;if(page==Page::Title)mem_w8(scene+0x168,(uint8_t)selection);pressed|=0x100;}
    }
    for(unsigned char ch:desktop_pointer::take_text())if(page==Page::Code || page==Page::Initials) {
        auto& draft=page==Page::Code?code:draft_initials;const unsigned maximum=page==Page::Code?8:7;
        if(ch=='\b'){if(!draft.empty())draft.pop_back();}
        else if(ch=='\r'){selection=37;pressed|=0x100;}
        else {if(ch>='a' && ch<='z')ch-=32;if(draft.size()<maximum && strchr(page==Page::Code?alphabet:"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789",ch))draft+=(char)ch;}
    }
    mem_w32(0x803b442c,pressed);mem_w32(0x803b4440,repeat);
    const bool up=(repeat&(0x08000000|8))!=0,down=(repeat&(0x04000000|4))!=0;
    const bool left=(repeat&(0x01000000|1))!=0,right=(repeat&(0x02000000|2))!=0;
    network_updates(c);
    if (page==Page::Title) {
        unsigned selected=mem_r8(scene+0x168);
        if (up || down) { selected=(selected+(up?3:1))%4;mem_w8(scene+0x168,(uint8_t)selected);sound(c,0x20000); }
        if (pressed&0x1100) {
            if (selected==1) open(c,Page::HostJoin);
            else {
                // Retain the original offline transition and sound behaviour.
                classic_release(c);
                mem_w8(scene+0x168,(uint8_t)(selected?selected-1:0));
                orig_fn_8012DCC4(c);mem_w8(scene+0x168,(uint8_t)selected);
                return; // the native confirm path already advances animation
            }
        }
    } else if (page==Page::Type || page==Page::HostJoin || page==Page::Engine || page==Page::Battle) {
        const bool choosing=page!=Page::HostJoin;
        const bool host=!choosing || (room_reply.has("room") && room_reply.at("room").at("host").boolean());
        const unsigned count=page==Page::Engine?4:page==Page::Battle?3:2;
        if(host && (up || down || left || right)) {
            selection=(selection+((up || left)?count-1:1))%count;
            sound(c,0x20000);
        }
        if(page==Page::HostJoin && (pressed&0x1000)){profile_return=Page::HostJoin;draft_initials=initials;open(c,Page::Initials);animate(c,scene);return;}
        if ((pressed&0x200) && host) {
            if(busy())cancelled=true;
            if(page==Page::HostJoin)open(c,Page::Title);
            else if(page==Page::Type && can_act())submit(Work::Select,Json::dict({{"op","back"}}));
            else if(page!=Page::Type)open(c,Page::Type);
        } else if((pressed&0x1100) && host && can_act()) {
            if(page==Page::HostJoin) {if(selection){code.clear();open(c,Page::Code);}else create_or_join(true);}
            else if(page==Page::Type) {mode=selection?"balloon":"grand-prix";open(c,selection?Page::Battle:Page::Engine);}
            else {
                Json rules;
                if(page==Page::Engine){const char* engines[]={"50cc","100cc","150cc","mirror"};mode="grand-prix";rules=Json::dict({{"engine",engines[selection]},{"cup","mushroom"}});}
                else {const char* modes[]={"balloon","shine","bob-omb"};mode=modes[selection];rules=Json::dict({{"arena",52},{"rounds",1}});}
                mode_advance=submit(Work::Select,Json::dict({{"op","rules"},{"mode",mode},{"rules",rules}}));
            }
        }
    } else if (page==Page::Code) {
        if(up || down || left || right){selection=(selection+38+(up?-10:down?10:left?-1:1))%38;sound(c,0x20000);}
        if(pressed&0x200){if(busy())cancelled=true;open(c,Page::HostJoin);}
        else if((pressed&0x400) || ((pressed&0x100) && selection==36)){if(!code.empty())code.pop_back();}
        else if((pressed&0x1000) || ((pressed&0x100) && selection==37)){if(code.size()==8)create_or_join(false);else message="ENTER ALL 8 CHARACTERS";}
        else if((pressed&0x100) && selection<36 && strchr(alphabet,keyboard_keys[selection]) && code.size()<8){code+=keyboard_keys[selection];if(code.size()==8)selection=37;}
    } else if(page==Page::Lobby && room_reply.has("room")) {
        const Json& room=room_reply.at("room");
        if(up || down || left || right) {
            unsigned kart=selection%8,rider=selection/8;
            if(left || right)kart=(kart+(left?7:1))%8;
            else {unsigned row=(kart/4)*2+rider;row=(row+(up?3:1))%4;kart=(row/2)*4+kart%4;rider=row%2;}
            selection=kart+rider*8;sound(c,0x20000);
        }
        if(pressed&0x200) {
            queued_work=Work::None;
            lobby_selection=selection;open(c,Page::LeaveConfirm);
        }
        else if((pressed&0x400) && can_act()) {
            for(const auto& member:room.at("members").list())if(member.at("you").boolean())
                submit(Work::Change,Json::dict({{"op","ready"},{"ready",!member.at("ready").boolean()}}));
        } else if((pressed&0x100) && can_act()) {
            bool driver=true;
            if(selection>=8){driver=false;for(const auto& m:room.at("members").list())if(m.at("seat").integer(0,15)==selection-8)driver=true;}
            if(driver)submit(Work::Change,Json::dict({{"op","seat"},{"seat",(int)selection}}));else message="CHOOSE A DRIVER BEFORE JOINING THE BACK";
        }else if((pressed&0x10) && can_act() && room.at("host").boolean()) {
            for(const auto& member:room.at("members").list())if(member.at("seat").integer(0,15)==selection && !member.at("host").boolean()){kick_seat=selection;kick_name=member.at("initials").str();kick_id=member.at("id").str();open(c,Page::KickConfirm);break;}
        }
        else if((pressed&0x1000) && can_act()) {
            profile_return=Page::Lobby;draft_initials=initials;open(c,Page::Initials);
        }
        else if((pressed&0x800) && can_act() && room.at("host").boolean())
            submit(Work::Start,Json::dict({{"op","start"},{"endpoint","127.0.0.1:28121"}}));
    } else if(page==Page::LeaveConfirm && room_reply.has("room")) {
        if(up || down || left || right){selection^=1;sound(c,0x20000);}
        if((pressed&0x200) || ((pressed&0x100) && selection==0)) {
            open(c,Page::Lobby);selection=lobby_selection;online_focus=(int)selection;
        }else if((pressed&0x100) && selection==1) {
            queued_work=Work::None;
            if(busy())leave_pending=true;else submit(Work::Leave,Json::dict({{"op","leave"}}));
            open(c,Page::HostJoin);
        }
    } else if(page==Page::KickConfirm) {
        if(up || down || left || right)selection^=1;
        if((pressed&0x200) || ((pressed&0x100) && selection==0))open(c,Page::Lobby);
        else if((pressed&0x100) && selection==1 && can_act())submit(Work::Change,Json::dict({{"op","kick"},{"seat",(int)kick_seat},{"member",kick_id}}));
    } else if(page==Page::Initials) {
        if(up || down || left || right){selection=(selection+38+(up?-10:down?10:left?-1:1))%38;sound(c,0x20000);}
        if(pressed&0x200)open(c,profile_return);
        else if((pressed&0x400) || ((pressed&0x100) && selection==36)){if(!draft_initials.empty())draft_initials.pop_back();}
        else if(((pressed&0x100) && selection==37) || (pressed&0x1000)) {
            if(draft_initials.empty())message="CHOOSE 1 TO 7 LETTERS OR NUMBERS";
            else if(can_act()) {
                initials=draft_initials;std::ofstream("online-initials.txt",std::ios::trunc)<<initials<<'\n';
                if(token.empty())open(c,profile_return);else submit(Work::Change,Json::dict({{"op","profile"},{"initials",initials}}));
            }
        }else if((pressed&0x100) && selection<36 && draft_initials.size()<7)draft_initials+=keyboard_keys[selection];
    } else if(page==Page::Rules) {
        const unsigned count=draft_mode=="grand-prix"?5:4;
        if(up || down){selection=(selection+(up?count-1:1))%count;sound(c,0x20000);}
        const bool host=room_reply.at("room").at("host").boolean();
        if((left || right) && host) {
            if(selection==0)cycle(draft_layout,{"solo","coop"},left);
            if(selection==1)cycle(draft_mode,{"grand-prix","balloon","shine","bob-omb"},left);
            if(selection==2 && draft_mode=="grand-prix")cycle(draft_engine,{"50cc","100cc","150cc","mirror"},left);
            if(selection==3 && draft_mode=="grand-prix"){cycle(draft_cup,{"mushroom","flower","star","special","all-cup"},left);draft_course=content::cup_courses(draft_cup)[0];}
            if(selection==4 && draft_mode=="grand-prix")cycle_id(draft_course,content::cup_courses(draft_cup),left);
            if(selection==2 && draft_mode!="grand-prix")cycle_id(draft_arena,{52,53,54,56,58,59},left);
            if(selection==3 && draft_mode!="grand-prix")draft_rounds=(((draft_rounds-1)/2+(left?4:1))%5)*2+1;
            sound(c,0x20000);
        }
        if(pressed&0x200)open(c,Page::Lobby);
        else if((pressed&0x100) && can_act()) {
            if(!host)open(c,Page::Lobby);
            else {
                Json rules=draft_mode=="grand-prix"?Json::dict({{"engine",draft_engine},{"cup",draft_cup},{"course",(int)draft_course}}):Json::dict({{"arena",(int)draft_arena},{"rounds",draft_rounds}});
                submit(Work::Change,Json::dict({{"op","rules"},{"mode",draft_mode},{"layout",draft_layout},{"rules",rules}}));
            }
        }
    } else if(page==Page::Kart) {
        if(up || down){selection=(selection+(up?2:1))%3;sound(c,0x20000);}
        if(left || right) {
            if(selection==0) {
                draft_kart=(draft_kart+(left?20:1))%21;
                if(!content::team(draft_kart,draft_driver,draft_partner)){draft_driver=1;draft_partner=2;}
            } else {
                unsigned& rider=selection==1?draft_driver:draft_partner;
                do {rider=(rider-1+(left?19:1))%20+1;}while(draft_driver==draft_partner);
                if(!content::team(draft_kart,draft_driver,draft_partner))draft_kart=20;
            }
            sound(c,0x20000);
        }
        if(pressed&0x200)open(c,Page::Lobby);
        else if((pressed&0x100) && can_act())submit(Work::Change,Json::dict({{"op","kart"},
            {"choice",Json::dict({{"kart",(int)draft_kart},{"driver",(int)draft_driver},{"partner",(int)draft_partner}})}}));
    }
    if((page==Page::Lobby || page==Page::Rules || page==Page::Kart || page==Page::Initials || page==Page::LeaveConfirm || page==Page::KickConfirm || page==Page::Type || page==Page::Engine || page==Page::Battle) && !token.empty() && !busy() && std::chrono::steady_clock::now()>=next_poll)
        submit(Work::Poll,Json::dict({{"op","state"}}));
    animate(c,scene);
}

extern "C" void hle___ct__7GameAppFUlPCcPv(CPU* c) {
    // Direct lobby resume retains the title's preloaded menus while exposing
    // every unlocked character and cup. Leave 768 KiB more room for the course
    // archive and its J2D panes; the sequence heap is freed before racing.
    // Other applications and the normal offline boot retain their retail size.
    if(c->lr==0x801d93ec && c->r[4]==0x680000 && getenv("MKDD_ROOM_RESUME"))c->r[4]=0x740000;
    orig___ct__7GameAppFUlPCcPv(c);
}

extern "C" void hle_fn_80015F40(CPU* c) {
    // CharacterSelect3D reserves the largest free block for each model heap.
    // Its retail getFreeSize() excludes the padding needed for 16-byte alignment.
    // A different allocation order on lobby resume can therefore make that exact
    // size unallocatable. JKRSolidHeap's native -1 convention computes the largest
    // aligned allocation instead, in the same parent heap. Both model heaps need
    // this; other solid-heap allocations retain their original requested size.
    if(c->lr==0x801669d8 || c->lr==0x80166e98)c->r[3]=0xffffffffu;
    orig_fn_80015F40(c);
}

extern "C" void hle_calc__11SequenceAppFv(CPU* c) {
    const uint32_t app=c->r[3];
    desktop_pointer::context(true);
    desktop_pointer::text_enabled=page==Page::Code || page==Page::Initials;
    if(enabled())desktop_online(c);
    if(enabled() && getenv("MKDD_ROOM_RESUME")) {
        if(token.empty() && getenv("MKDD_ROOM_TOKEN")) {
            token=getenv("MKDD_ROOM_TOKEN");open(c,Page::Lobby);submit(Work::Poll,Json::dict({{"op","state"}}));
        }
        const auto scene=mem_r32(app+0xc);
        if(mem_r32(c->r[13]-30528)==0 && valid(scene,0x174))mem_w32(scene+0x15c,4);
    }
    if(native_selection)native_selection_before(c,app);
    uint32_t buttons[5]{};
    const unsigned offsets[]={0x18,0x1c,0x20,0x2c,0x30};
    bool hold=false;
    if(native_selection && room_reply.has("room")) {
        const auto& own=own_member();const auto scene=mem_r32(app+0xc),kind=mem_r32(c->r[13]-30528);
        hold=native_ui.battle_menu || native_ui.chosen || native_ui.claim.state!=NativeSelection::Claim::Idle || (busy() && work==Work::Select) ||
            (native_ui.state=="course" && !room_reply.at("room").at("host").boolean());
        if(kind==1 && valid(scene,0x2200)) {
            const auto role=own.at("role").str();
            const bool first=mem_r32(scene+0x2138)<20,second=mem_r32(scene+0x213c)<20;
            hold|=native_ui.finished || (role=="passenger" && first) || (role=="driver" && first && !second);
        }
    }
    if(hold)for(unsigned i=0;i<5;++i){buttons[i]=mem_r32(0x803b4410+offsets[i]);mem_w32(0x803b4410+offsets[i],0);}
    {PointerPadRestore restore;native_character_pointer(c,app,hold);orig_calc__11SequenceAppFv(c);}
    if(valid(app,0x4c)) {
        const auto title_scene=mem_r32(app+0xc),kind=mem_r32(c->r[13]-30528);
        if(kind!=0){title_hint_hidden=false;display_set_settings_hint(false);}
        else display_set_settings_hint(page==Page::Title && !title_hint_hidden && !native_selection && valid(title_scene,0x174) && !mem_r8(title_scene+0x169));
    }
    if(hold)for(unsigned i=0;i<5;++i)mem_w32(0x803b4410+offsets[i],buttons[i]);
    if(native_selection)native_selection_after(c,app);
    if(!getenv("MKDD_NATIVE_MENU_TRACE") || !valid(app,0x4c))return;
    const uint32_t scene=mem_r32(app+0xc),kind=mem_r32(c->r[13]-30528);
    static uint32_t previous=~0u;
    if(scene && kind!=previous && valid(scene,0x10)) {
        previous=kind;const uint32_t table=mem_r32(scene+8);
        fprintf(stderr,"[native-selection] scene=%u object=%08x init=%08x draw=%08x calc=%08x\n",kind,scene,
            mem_r32(table+0xc),mem_r32(table+0x10),mem_r32(table+0x14));
        if(kind==1 && getenv("MKDD_NATIVE_CHAR_START")) {
            mem_w32(0x803b0f34+0x350,1);
            mem_w16(0x803b0f34+0x368,0xffff);mem_w16(0x803b0f34+0x36a,0xffff);
            mem_w32(scene+0x20e0,12);
        }
    }
    if(kind==1 && valid(scene,0x2100)) {
        static uint32_t phase=~0u;const uint32_t next=mem_r32(scene+0x20e0);
        if(next!=phase){phase=next;fprintf(stderr,"[native-selection] menu phase=%u values=",phase);
            for(unsigned i=0x20e4;i<0x2140;i+=4)fprintf(stderr," %x:%08x",i,mem_r32(scene+i));fputc('\n',stderr);}
    }
}

extern "C" void hle_draw__11SequenceAppFv(CPU* c) {
    const uint32_t app=c->r[3];
    if(native_selection && native_ui.battle_menu){draw_online(c,mem_r32(app+0xc));return;}
    orig_draw__11SequenceAppFv(c);
    native_character_regions(c,app);
    if(!native_selection || !room_reply.has("room"))return;
    const uint32_t scene=mem_r32(app+0xc);if(!valid(scene,0x10))return;
    const CPU saved=*c;c->r[3]=mem_r32(scene+4);call_indirect(c,mem_r32(mem_r32(c->r[3])+0x14));
    native_shared_characters(c,app);
    const auto& own=own_member();
    std::string status=own.at("initials").str()+"  -  ";
    if(native_ui.state=="course")status+=room_reply.at("room").at("host").boolean()?"CHOOSE THE EVENT":"HOST CHOOSING THE EVENT";
    else if(own.at("selected").boolean())status+="READY - WAITING FOR FRIENDS";
    else if(own.at("role").str()=="passenger")status+="CHOOSE YOUR BACKSEAT CHARACTER";
    else if(own.at("role").str()=="driver")status+="CHOOSE YOUR DRIVER AND KART";
    else status+="CHOOSE BOTH CHARACTERS AND KART";
    if(native_ui.state=="characters") {
        if(native_ui.claim.state!=NativeSelection::Claim::Idle)status="CONFIRMING CHARACTER CHOICE...";
        else if(!native_ui.notice.empty() && std::chrono::steady_clock::now()<native_ui.notice_until)status=native_ui.notice;
        else status+="  /  B: RESELECT";
    }
    box(c,15,66,578,13,0x12243aee);label(c,message.empty()?status:message,68,9,0xffe95cff);*c=saved;
}

extern "C" void hle_fn_8015394C(CPU* c) {
    static unsigned confirm_delay=0;static uint32_t waiting_dialog=0;
    const auto dialog=c->r[3];PointerPadRestore restore;
    if(native_dialog_regions(c)) {
        if(auto click=desktop_pointer::take()) {
            if(*click==91 || *click==92) {
                if(getenv("MKDD_NATIVE_MENU_TRACE"))fprintf(stderr,"[native-dialog] mouse choice=%d\n",*click-91);
                native_menu_button(0,*click==91?0x01000000u:0x02000000u);
                waiting_dialog=dialog;confirm_delay=3;
            }else if(*click==-2)native_menu_button(0x200);
        }
        if(confirm_delay && waiting_dialog==dialog && --confirm_delay==0)native_menu_button(0x100);
    }else confirm_delay=0;
    orig_fn_8015394C(c);
}
extern "C" bool mkdd_online_menu_race_selected(CPU* c) {return native_race_selected(c);}

namespace {bool pause_open=false,pause_confirm=false;unsigned pause_choice=0,pause_row=0;bool pause_returning=false;}
extern "C" bool mkdd_online_pause_open(){return pause_open;}
extern "C" bool mkdd_online_pause_capture(uint64_t tick) {
    static uint64_t last=0;static uint16_t previous=0;
    if(tick<=last)return pause_open;
    last=tick;
    const auto pad=pad_capture_states()[0];
    uint16_t buttons=pad.buttons;
    if(pad.stick_x<70)buttons|=PAD_LEFT;else if(pad.stick_x>185)buttons|=PAD_RIGHT;
    if(pad.stick_y<70)buttons|=PAD_DOWN;else if(pad.stick_y>185)buttons|=PAD_UP;
    const uint16_t pressed=buttons&~previous;previous=buttons;
    if(pressed&PAD_START){pause_open=!pause_open;pause_confirm=false;pause_choice=0;pause_row=0;fprintf(stderr,"[online-menu] local online menu %s\n",pause_open?"opened":"closed");}
    if(!pause_open){pause_confirm=false;pause_returning=false;return false;}
    desktop_pointer::context(true);
    const auto ui=desktop_online_ui::read();
    const auto click=desktop_pointer::take();
    const auto hover=desktop_pointer::read().hover;
    if(!pause_confirm){if(hover==73)pause_row=0;else if(hover==70 && ui.can_return)pause_row=1;}
    else if(hover==71 || hover==72)pause_choice=hover-71;
    if(!pause_confirm) {
        if(pressed&(PAD_UP|PAD_DOWN))pause_row=ui.can_return?1-pause_row:0;
        if((pressed&PAD_B) || (click && *click==-2) || (click && *click==73)){pause_open=false;return true;}
        if(ui.can_return && ((pressed&PAD_Y) || (click && *click==70) || ((pressed&PAD_A) && pause_row==1))){pause_confirm=true;pause_choice=0;return true;}
        if((pressed&PAD_A) && pause_row==0){pause_open=false;return true;}
    }
    if(pause_confirm) {
        if(pressed&(PAD_LEFT|PAD_RIGHT))pause_choice^=1;
        if(click && (*click==71 || *click==72))pause_choice=*click-71;
        if((pressed&0x200) || (click && *click==-2))pause_confirm=false;
        else if((pressed&0x100) || (click && (*click==71 || *click==72))) {
            if(pause_choice==0)pause_confirm=false;
            else if(!pause_returning)pause_returning=desktop_online_ui::submit({desktop_online_ui::Action::Return,{}});
        }
    }
    return true;
}
extern "C" void hle_draw__7RaceAppFv(CPU* c) {
    orig_draw__7RaceAppFv(c);
    if((g_replay_guard && !g_replay_guard->publish_outputs) || !getenv("MKDD_ROOM_TOKEN") || !pause_open)return;
    const auto ui=desktop_online_ui::read();
    const CPU saved=*c;clickable.clear();
    if(pause_confirm) {
        box(c,80,165,448,165,0x102d58f5);box(c,85,170,438,4,0xffdf58ff);
        label(c,pause_returning?"RETURNING TO ROOM...":"RETURN EVERYONE TO THE LOBBY?",192,17,0xffe95cff);
        label(c,"CURRENT EVENT PROGRESS WILL NOT BE SAVED",224,11);
        text(c,"KEEP PLAYING",113,277,17,pause_choice==0?0xffe95cff:0xffffffff);
        text(c,"RETURN",372,277,17,pause_choice==1?0xffe95cff:0xffffffff);
        hit(100,264,210,38,71);hit(350,264,158,38,72);classic_outline(c,pause_choice?350:100,264,pause_choice?158:210,38);
    }else {
        box(c,110,135,388,190,0x102d58f5);box(c,115,140,378,4,0xffdf58ff);
        label(c,"ONLINE MENU",155,23,0xffe95cff);
        label(c,"THE RACE CONTINUES WHILE THIS MENU IS OPEN",192,10);
        label(c,"CONTINUE",228,21,pause_row==0?0xffe95cff:0xffffffff);hit(140,222,328,31,73);
        if(ui.can_return){label(c,"RETURN TO LOBBY",275,19,pause_row==1?0xffe95cff:0xffffffff);hit(140,267,328,32,70);}
        else label(c,"THE HOST CAN RETURN THE ROOM TO THE LOBBY",278,10,0xbbcde4ff);
        classic_outline(c,140,pause_row?267:222,328,pause_row?32:31);
    }
    desktop_pointer::publish(pause_confirm?"pause-return-confirm":"pause-online",clickable);*c=saved;
}

extern "C" void hle_fn_80162AE4(CPU* c) {
    const uint32_t scene=c->r[3],pad=c->r[4];
    if(native_character_confirm(c,scene,pad))return;
    const unsigned stage=native_selection && pad==0?mem_r32(scene+0x2144):~0u;
    const unsigned selected=stage==0?native_character(mem_r32(scene+0x212c)):stage==1?native_character(mem_r32(scene+0x2130)):0;
    orig_fn_80162AE4(c);
    if(stage<=2 && mem_r32(scene+0x2144)!=stage) {
        if(stage==0)native_ui.first=selected;
        else if(stage==1)native_ui.second=selected;
        fprintf(stderr,"[online-menu] native selection accepted stage=%u character=%u\n",stage,selected);
    }
}
extern "C" void hle_fn_80162CC0(CPU* c) {
    if(native_selection && native_ui.state=="characters" && !c->r[4]) {native_character_cancel(c->r[3]);return;}
    orig_fn_80162CC0(c);
}
