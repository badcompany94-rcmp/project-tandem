#include "online_room.h"
#include "online_content.h"
#include "runtime.h"
#include "platform.h"
#include "input_log.h"
#include "desktop_online_ui.h"
#include <future>
#include <chrono>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#endif

namespace mkdd { uint64_t online_build_fingerprint(); void online_end_inputs(); }
namespace mkdd::online {
namespace {
[[noreturn]] void invalid() { throw std::runtime_error("invalid-room-response"); }
std::string hex(const Json& value,size_t count) {
    std::string s=value.str();
    if(s.size()!=count || s.find_first_not_of("0123456789abcdef")!=std::string::npos)invalid();return s;
}
std::string one_of(const Json& value,std::initializer_list<const char*> choices) {
    std::string s=value.str();for(const char* choice:choices)if(s==choice)return s;invalid();
}
std::string decimal(const Json& value,int64_t low,int64_t high) { return std::to_string(value.integer(low,high)); }
void validate_reply(const Json& response,const std::string& op) {
    if(op=="leave") {
        if(!((response.has("closed") && response.at("closed").boolean()) ||
             (response.has("left") && response.at("left").boolean())))invalid();
        return;
    }
    const Json& room=response.at("room");
    std::string code=room.at("code").str();
    if(!((code.size()==4 && code.find_first_not_of("0123456789")==std::string::npos) ||
         (code.size()==8 && code.find_first_not_of("23456789ABCDEFGHJKLMNPQRSTUVWXYZ")==std::string::npos)))invalid();
    room.at("revision").integer(1,INT64_MAX);room.at("host").boolean();
    if(room.at("minimum_players").integer(2,2)!=2)invalid();
    room.at("human_karts").integer(1,8);room.at("cpu_karts").integer(0,7);
    std::string mode=one_of(room.at("mode"),{"grand-prix","balloon","shine","bob-omb"});
    const auto layout=one_of(room.at("layout"),{"solo","coop","mixed"});one_of(room.at("state"),{"forming","mode","characters","course","running"});
    const auto& members=room.at("members").list();if(members.empty() || members.size()>8)invalid();
    unsigned seats=0,you=0;
    for(const Json& member:members) {
        unsigned seat=(unsigned)member.at("seat").integer(0,layout=="mixed"?15:7);if(seats&(1u<<seat))invalid();seats|=1u<<seat;
        std::string name=member.at("name").str();if(name.empty() || name.size()>64)invalid();
        for(unsigned char c:name)if(c<32 || c==127)invalid();
        member.at("ready").boolean();member.at("host").boolean();if(member.at("you").boolean())++you;
        one_of(member.at("role"),{"solo","driver","passenger"});
    }
    if(you!=1)invalid();
    const auto& rules=room.at("rules");
    if(mode=="grand-prix") { one_of(rules.at("engine"),{"50cc","100cc","150cc","mirror"});one_of(rules.at("cup"),{"mushroom","flower","star","special","all-cup"}); }
    else { if(!content::arena((unsigned)rules.at("arena").integer(52,59)))invalid();if(!(rules.at("rounds").integer(1,9)&1))invalid(); }
    if(op=="create" || op=="join") {
        std::string token=response.at("token").str();if(token.empty() || token.size()>128 || token.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")!=std::string::npos)invalid();
    }
}
}

std::string executable_build() {
    char value[17];snprintf(value,sizeof(value),"%016llx",(unsigned long long)online_build_fingerprint());return value;
}

Json room_request(const Json& request) {
#ifdef _WIN32
    struct Handle {
        HINTERNET value;
        explicit Handle(HINTERNET v):value(v){if(!value)throw std::runtime_error("service-unavailable");}
        ~Handle(){WinHttpCloseHandle(value);}
    };
    unsigned port=28120;
    if(const char* setting=getenv("MKDD_ROOM_PORT")) {
        if(!*setting || strlen(setting)>5 || strspn(setting,"0123456789")!=strlen(setting))throw std::runtime_error("invalid-service-port");
        port=(unsigned)strtoul(setting,nullptr,10);if(!port || port>65535)throw std::runtime_error("invalid-service-port");
    }
    std::string body=request.dump();if(body.size()>4096)throw std::runtime_error("invalid-room-request");
    Handle session(WinHttpOpen(L"Native private-room development/1",WINHTTP_ACCESS_TYPE_NO_PROXY,
                              WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0));
    const char* backend=getenv("MKDD_ROOM_BACKEND");
    if(backend && strcmp(backend,"eos")!=0 && strcmp(backend,"local")!=0)throw std::runtime_error("invalid-room-backend");
    const int timeout=backend && strcmp(backend,"eos")==0?50000:2000;
    WinHttpSetTimeouts(session.value,2000,2000,timeout,timeout);
    Handle connection(WinHttpConnect(session.value,L"127.0.0.1",(INTERNET_PORT)port,0));
    Handle http(WinHttpOpenRequest(connection.value,L"POST",L"/v1/room",nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,0));
    DWORD redirect=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(http.value,WINHTTP_OPTION_REDIRECT_POLICY,&redirect,sizeof(redirect));
    if(!WinHttpSendRequest(http.value,L"Content-Type: application/json\r\n",(DWORD)-1L,
                           body.data(),(DWORD)body.size(),(DWORD)body.size(),0) || !WinHttpReceiveResponse(http.value,nullptr))
        throw std::runtime_error("service-unavailable");
    DWORD status=0,length=sizeof(status);
    if(!WinHttpQueryHeaders(http.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX,&status,&length,WINHTTP_NO_HEADER_INDEX))
        throw std::runtime_error("invalid-room-response");
    std::string reply;
    for(;;) {
        DWORD available=0,read=0;
        if(!WinHttpQueryDataAvailable(http.value,&available))throw std::runtime_error("service-unavailable");
        if(!available)break;
        if(available>65536-reply.size())invalid();
        size_t offset=reply.size();reply.resize(offset+available);
        if(!WinHttpReadData(http.value,reply.data()+offset,available,&read) || !read)throw std::runtime_error("service-unavailable");
        reply.resize(offset+read);
    }
    Json result=Json::parse(reply);
    if(status!=200)throw std::runtime_error(result.has("error")?result.at("error").str():"service-unavailable");
    validate_reply(result,request.at("op").str());
    return result;
#else
    throw std::runtime_error("Windows room client is unavailable on this platform");
#endif
}

std::map<std::string,std::string> launch_environment(const Json& response,const std::string& build) {
    const Json& room=response.at("room"),&launch=room.at("launch"),&connection=response.at("connection");
    if(room.at("state").str()!="running" || launch.at("compatibility").str()!=build)throw std::runtime_error("incompatible-build");
    const auto& roster=launch.at("roster").list();const unsigned count=(unsigned)roster.size();if(count<2 || count>8)invalid();
    const unsigned slot=(unsigned)connection.at("slot").integer(0,count-1);
    const std::string layout=one_of(launch.at("layout"),{"solo","coop","mixed"});
    const unsigned maximum_seat=layout=="mixed"?15:7;
    std::string seats;unsigned mask=0;
    for(unsigned i=0;i<count;++i) {
        if(roster[i].at("slot").integer(0,count-1)!=i)invalid();
        unsigned seat=(unsigned)roster[i].at("seat").integer(0,maximum_seat);
        if(mask&(1u<<seat))invalid();mask|=1u<<seat;
    }
    unsigned mapped[16]{},human_karts=0;
    std::string kart_map(8,'-'),role_map(8,'-');
    if(layout=="solo") {
        for(unsigned i=0;i<8;++i)if(mask&(1u<<i))mapped[i]=human_karts++;
    } else if(layout=="mixed") {
        if(!launch.has("seating") || launch.at("seating").str()!="garage")invalid();
        unsigned pad=0;
        for(unsigned kart=0;kart<8;++kart) {
            const bool front=mask&(1u<<kart),back=mask&(1u<<(kart+8));
            if(back && !front)invalid();if(!front)continue;
            for(unsigned rider=0;rider<(back?2u:1u);++rider) {
                const unsigned seat=kart+rider*8;mapped[seat]=pad;
                kart_map[pad]=char('0'+human_karts);role_map[pad]=rider?'P':'D';++pad;
            }
            ++human_karts;
        }
    } else for(unsigned pair=0;pair<4;++pair) {
        const bool numbered=launch.has("seating") && launch.at("seating").str()=="drivers-first";
        const unsigned front=numbered?pair:pair*2,back=numbered?pair+4:pair*2+1;
        const unsigned group=((mask>>front)&1)|(((mask>>back)&1)<<1);if(!group)continue;
        for(unsigned rider=0;rider<2;++rider)if(group&(1u<<rider))mapped[rider?back:front]=human_karts*2+(group==3?rider:0);
        ++human_karts;
    }
    for(const auto& member:roster) {
        const unsigned seat=(unsigned)member.at("seat").integer(0,maximum_seat);
        if(member.at("game_seat").integer(0,7)!=mapped[seat])invalid();
        if(layout=="mixed" && (member.at("kart").integer(0,7)!=kart_map[mapped[seat]]-'0' ||
            member.at("role").str()!=(seat>=8?"passenger":(mask&(1u<<(seat+8)))?"driver":"solo")))invalid();
        seats+=(char)('0'+mapped[seat]);
    }
    unsigned you=0;
    for(const auto& member:room.at("members").list()) if(member.at("you").boolean()) {
        ++you;if(member.at("seat").integer(0,maximum_seat)!=roster[slot].at("seat").integer(0,maximum_seat))invalid();
    }
    if(you!=1 || room.at("host").boolean()!=(slot==0))invalid();
    std::string endpoint=connection.at("endpoint").str();
    if(endpoint.rfind("127.0.0.1:",0)!=0)invalid();
    std::string port=endpoint.substr(10);
    if(port.empty() || port.size()>5 || port.find_first_not_of("0123456789")!=std::string::npos || std::stoul(port)==0 || std::stoul(port)>65535)invalid();
    std::map<std::string,std::string> env={
        {"MKDD_NET_FRESH","1"},{"MKDD_NET_KARTS","1"},{"MKDD_NET_PLAYERS",std::to_string(count)},
        {"MKDD_NET_ROOM_ID",hex(launch.at("id"),32)},
        {"MKDD_NET_SLOT",std::to_string(slot)},{"MKDD_NET_SEATS",seats},
        {"MKDD_NET_SEED",decimal(launch.at("seed"),0,UINT32_MAX)},
        {"MKDD_NET_EPOCH",decimal(launch.at("epoch"),946684800,4102444799LL)},
        {"MKDD_NET_MODE",one_of(launch.at("mode"),{"grand-prix","balloon","shine","bob-omb"})},
        {"MKDD_NET_LAYOUT",layout},
        {"MKDD_NET_ENGINE","150cc"},{"MKDD_NET_CUP","mushroom"},{"MKDD_NET_ARENA","52"},{"MKDD_NET_ROUNDS","3"},
        {"MKDD_NET_ADMISSION",hex(connection.at("admission"),32)}
    };
    if(layout=="mixed"){env["MKDD_NET_KART_MAP"]=kart_map;env["MKDD_NET_ROLE_MAP"]=role_map;}
    if(env["MKDD_NET_MODE"]!="grand-prix" && human_karts<2)invalid();
    if(connection.has("input_delay"))env["MKDD_NET_INPUT_DELAY"]=decimal(connection.at("input_delay"),0,12);
    if(launch.at("human_karts").integer(1,8)!=human_karts ||
       launch.at("cpu_karts").integer(0,7)!=(env["MKDD_NET_MODE"]=="grand-prix"?8-human_karts:0))invalid();
    const auto& karts=launch.at("karts").list();if(karts.size()!=8)invalid();
    const char* digits="0123456789abcdef";std::string packed;
    for(const auto& kart:karts) for(const char* field:{"kart","driver","partner"}) {
        unsigned byte=(unsigned)kart.at(field).integer(strcmp(field,"kart")==0?0:1,20);
        packed+=digits[byte>>4];packed+=digits[byte&15];
    }
    env["MKDD_NET_ROSTER"]=packed;
    std::string tags;
    for(const auto& member:roster) {
        if(!member.has("initials")){tags.clear();break;}
        const auto tag=member.at("initials").str();
        if(tag.empty() || tag.size()>7 || tag.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")!=std::string::npos)invalid();
        if(!tags.empty())tags+=',';tags+=tag;
    }
    if(!tags.empty())env["MKDD_NET_TAGS"]=tags;
    if(launch.has("seating"))env["MKDD_NET_SERIES"]="1";
    const Json& rules=launch.at("rules");
    if(env["MKDD_NET_MODE"]=="grand-prix") {
        env["MKDD_NET_ENGINE"]=one_of(rules.at("engine"),{"50cc","100cc","150cc","mirror"});
        env["MKDD_NET_CUP"]=one_of(rules.at("cup"),{"mushroom","flower","star","special","all-cup"});
        if(rules.has("course")) {
            unsigned course=(unsigned)rules.at("course").integer(33,51);
            const auto allowed=content::cup_courses(env["MKDD_NET_CUP"]);
            if(std::find(allowed.begin(),allowed.end(),course)==allowed.end())invalid();
            env["MKDD_NET_COURSE"]=std::to_string(course);
        }
    } else {
        env["MKDD_NET_ARENA"]=decimal(rules.at("arena"),52,59);
        if(!content::arena((unsigned)rules.at("arena").integer(52,59)))invalid();
        int64_t rounds=rules.at("rounds").integer(1,9);if(!(rounds&1))invalid();
        env["MKDD_NET_ROUNDS"]=std::to_string(rounds);
    }
    if(connection.has("mesh") && connection.at("mesh").boolean()) {
        env["MKDD_NET_MESH"]="1";env["MKDD_NET_CONNECT"]=endpoint;
    }else if(slot==0) {
        const auto& admissions=connection.at("admissions").list();if(admissions.size()!=count)invalid();
        std::set<std::string> unique;
        for(const auto& ticket:admissions){std::string value=hex(ticket,32);unique.insert(value);env["MKDD_NET_ADMISSIONS"]+=value;}
        if(unique.size()!=count || admissions[0].str()!=env["MKDD_NET_ADMISSION"])invalid();
        env["MKDD_NET_LISTEN"]=endpoint;
    } else env["MKDD_NET_CONNECT"]=endpoint;
    if(connection.has("prediction") && connection.at("prediction").boolean()) {
        if(!env.count("MKDD_NET_MESH"))invalid();
        env.insert({{"MKDD_NET_PREDICTION","1"},{"MKDD_PREDICTION_START","120"},
            {"GCN_OWNED_STACKS","1"},{"GCN_WRITE_WATCH","1"},
            {"MKDD_PREDICTION_ASYNC_RENDER","1"},{"MKDD_PREDICTION_ASYNC_RESTORE","1"},
            {"GCN_RENDER_TARGET_POOL","1"},{"GCN_REPLAY_QUEUE","4"}});
    }
    env["GCN_GAME_SCHEDULING"]="1";
    return env;
}

void enter_race(const Json& response) {
#ifdef _WIN32
    const auto& connection=response.at("connection");
    if(getenv("MKDD_EXPECT_PREDICTION") && (!connection.has("mesh") || !connection.at("mesh").boolean() || !connection.has("prediction") || !connection.at("prediction").boolean()))
        throw std::runtime_error("rollback-not-enabled");
    auto env=launch_environment(response,executable_build());
    const bool test=getenv("MKDD_MENU_TEST")!=nullptr;
    // Never retain an endpoint or admission from a previous event. Test input
    // injection stays opt-in and is not carried by the normal player handoff.
    const char* transient[]={"MKDD_NET_LISTEN","MKDD_NET_CONNECT","MKDD_NET_ADMISSIONS","MKDD_NET_ADMISSION","MKDD_NET_MESH",
        "MKDD_NET_PREDICTION","MKDD_PREDICTION_START","MKDD_PREDICTION_ASYNC_RENDER","MKDD_PREDICTION_ASYNC_RESTORE",
        "GCN_OWNED_STACKS","GCN_WRITE_WATCH","GCN_RENDER_TARGET_POOL","GCN_REPLAY_QUEUE","GCN_GAME_SCHEDULING","MKDD_NET_KART_MAP","MKDD_NET_ROLE_MAP"};
    for(const char* key:transient)_putenv_s(key,"");
    if(!test) for(const char* key:{"MKDD_NET_TICKS","MKDD_NET_LOCAL_INPUT","MKDD_NET_SWAP_AT","MKDD_NET_ITEM_PULSE","MKDD_NET_TEST_PASS","MKDD_NET_FAILURE_DUMP","MKDD_SYNC_TRACE","MKDD_PREDICTION_INPUT_DELAY"})_putenv_s(key,"");
    _putenv_s("GCN_INPUT","");_putenv_s("GCN_CLOCK","virtual");_putenv_s("GCN_CPU_SCALE","1");
    for(const auto& [key,value]:env) if(_putenv_s(key.c_str(),value.c_str()))throw std::runtime_error("native-launch-failed");
    char executable[32768];DWORD length=GetModuleFileNameA(nullptr,executable,sizeof(executable));
    if(!length || length>=sizeof(executable))throw std::runtime_error("native-launch-failed");
    std::vector<std::string> arguments={executable,"--no-input-log","--widescreen","--resolution=1920x1080"};
    if(test) { arguments.insert(arguments.end(),{"--hidden","--fast","--scale=1","--dump-output","--dump-every=2000","--dump-dir=frames"}); }
    else arguments.insert(arguments.end(),{"--fullscreen","--scale=4"});
    std::vector<char*> argv;for(auto& value:arguments)argv.push_back(value.data());argv.push_back(nullptr);
    input_log_end();plat_relaunch(argv.data());
    throw std::runtime_error("native-launch-failed");
#else
    throw std::runtime_error("native-launch-failed");
#endif
}

void finish_event(const Json& ranking) {
#ifdef _WIN32
    const char* token=getenv("MKDD_ROOM_TOKEN"),*event=getenv("MKDD_NET_ROOM_ID");
    if(!token || !*token || !event)throw std::runtime_error("invalid-session");
    auto state=room_request(Json::dict({{"op","state"},{"token",token}}));
    if(state.at("room").at("host").boolean()) {
        bool complete=false;
        for(unsigned attempt=0;attempt<3 && !complete;++attempt) {
            try {
                room_request(Json::dict({{"op","finish"},{"token",token},{"revision",state.at("room").at("revision")},{"event",event},{"results",ranking}}));
                complete=true;
            }catch(const std::exception&){if(attempt==2)throw;state=room_request(Json::dict({{"op","state"},{"token",token}}));}
        }
    }
    if(!state.at("room").at("host").boolean()) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(10);
        while(state.at("room").at("state").str()!="forming") {
            if(std::chrono::steady_clock::now()>=deadline)throw std::runtime_error("host-finish-timeout");
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            state=room_request(Json::dict({{"op","state"},{"token",token}}));
        }
    }
    return_to_room();
#endif
    throw std::runtime_error("native-launch-failed");
}
void recover_room_disconnect() {
    const char* token=getenv("MKDD_ROOM_TOKEN");if(!token || !*token)return;
    try{const auto state=room_request(Json::dict({{"op","state"},{"token",token}}));if(state.at("room").at("state").str()=="forming")return_to_room();}catch(const std::exception&){}
}
void race_ui_tick(bool fast_status) {
    const char* token=getenv("MKDD_ROOM_TOKEN");
    if(!token || !*token){desktop_online_ui::publish({});return;}
    struct Profile {
        uint64_t began=getenv("MKDD_PHASE_TRACE")?host_ns():0;
        double submit_ms=0;
        ~Profile(){if(began){const auto end=host_ns();const double elapsed=(end-began)/1e6;
            if(elapsed>2)fprintf(stderr,"[room-poll-profile] host_ns=%llu total_ms=%.3f submit_ms=%.3f\n",(unsigned long long)end,elapsed,submit_ms);}}
    } profile;
    static std::future<Json> pending;
    static Json latest;
    static auto next=std::chrono::steady_clock::time_point{};
    if(pending.valid() && pending.wait_for(std::chrono::milliseconds(0))==std::future_status::ready) {
        try{latest=pending.get();if(latest.at("room").at("state").str()=="forming")return_to_room();}catch(const std::exception& e){fprintf(stderr,"[online-ui] %s\n",e.what());}
    }
    desktop_online_ui::State ui;ui.available=true;ui.version="ALPHA 25 / "+executable_build().substr(0,8);
    ui.status="Event in progress. The host can return everyone to this room.";
    if(latest.has("room")){
        const auto& room=latest.at("room");ui.code=room.at("code").str();ui.can_return=room.at("host").boolean();
        for(const auto& member:room.at("members").list())if(member.at("you").boolean())ui.name=member.at("initials").str();
    }
    desktop_online_ui::publish(ui);
    static bool abort=false;
    if(auto command=desktop_online_ui::take())if(command->action==desktop_online_ui::Action::Return && ui.can_return)abort=true;
    if(!pending.valid() && (abort || std::chrono::steady_clock::now()>=next)) {
        const std::string session=token;const bool returning=abort;abort=false;
        const auto submitted_at=profile.began?host_ns():0;
        pending=std::async(std::launch::async,[session,returning]{
            auto state=room_request(Json::dict({{"op","state"},{"token",session}}));
            if(returning && state.at("room").at("state").str()=="running" && state.at("room").at("host").boolean())
                state=room_request(Json::dict({{"op","abort"},{"token",session},{"revision",state.at("room").at("revision")},{"event",state.at("room").at("launch").at("id")}}));
            return state;
        });
        if(submitted_at)profile.submit_ms=(host_ns()-submitted_at)/1e6;
        next=std::chrono::steady_clock::now()+std::chrono::milliseconds(fast_status?200:2000);
    }
}
void return_to_room() {
#ifdef _WIN32
    mkdd::online_end_inputs();
    _putenv_s("MKDD_ROOM_RESUME","1");
    const char* fields[]={"MKDD_NET_LISTEN","MKDD_NET_CONNECT","MKDD_NET_ADMISSIONS","MKDD_NET_ADMISSION","MKDD_NET_MESH","MKDD_NET_PREDICTION",
        "MKDD_NET_FRESH","MKDD_NET_KARTS","MKDD_NET_PLAYERS","MKDD_NET_ROOM_ID","MKDD_NET_SLOT","MKDD_NET_SEATS","MKDD_NET_SEED","MKDD_NET_EPOCH",
        "MKDD_NET_MODE","MKDD_NET_LAYOUT","MKDD_NET_ENGINE","MKDD_NET_CUP","MKDD_NET_COURSE","MKDD_NET_ARENA","MKDD_NET_ROUNDS","MKDD_NET_ROSTER","MKDD_NET_TAGS","MKDD_NET_SERIES","MKDD_NET_KART_MAP","MKDD_NET_ROLE_MAP",
        "MKDD_NET_TICKS","MKDD_NET_LOCAL_INPUT","MKDD_SYNC_TRACE","MKDD_PREDICTION_TRACE","MKDD_PREDICTION_INPUT_DELAY","GCN_INPUT"};
    for(const char* key:fields)_putenv_s(key,"");
    char executable[32768];const DWORD length=GetModuleFileNameA(nullptr,executable,sizeof(executable));
    if(!length || length>=sizeof(executable))throw std::runtime_error("native-launch-failed");
    const bool test=getenv("MKDD_MENU_TEST");
    std::vector<std::string> arguments={executable,"--no-input-log","--widescreen","--resolution=1920x1080"};
    if(test)arguments.insert(arguments.end(),{"--hidden","--fast","--frames=5000","--scale=1","--dump-output","--dump-every=1000","--dump-dir=frames"});
    else arguments.insert(arguments.end(),{"--fullscreen","--scale=4"});
    std::vector<char*> argv;for(auto& value:arguments)argv.push_back(value.data());argv.push_back(nullptr);
    input_log_end();plat_relaunch(argv.data());
#endif
    throw std::runtime_error("native-launch-failed");
}
}
