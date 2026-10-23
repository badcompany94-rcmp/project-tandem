#include "diagnostic_log.h"
// Retail logical-controller boundary and diagnostic simulation checksums.
// The normal offline path calls the untouched original routines.
#include "race.h"
#include "adaptive_input_delay.h"
#include "runtime.h"
#include "platform.h"
#include "input_log.h"
#include "net/peer_link.h"
#include "build_identity.h"
#include "online_content.h"
#include "online_room.h"
#include "replay_audit.h"
#include "device_checkpoint.h"
#include "native_checkpoint.h"
#include "gx/render.h"
#include "performance_stats.h"
#include "desktop_pointer.h"
#include "display_config.h"
#include "hw/pad.h"
extern "C" bool mkdd_online_pause_capture(uint64_t);
extern "C" bool mkdd_online_pause_open();
#include <deque>
#include <map>
#include <thread>
extern "C" {
#include "recomp_funcs.h"
}
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <cerrno>
#include <chrono>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
uint64_t pad_tick;
uint8_t logical_pads[16][2];
std::unique_ptr<net::PeerLink> link;
net::Inputs network_pads{};
bool network_override;
const bool prediction_enabled=getenv("MKDD_NET_PREDICTION")!=nullptr;
constexpr unsigned prediction_window=8;
// Queued local inputs need no rewind checkpoint. A poor link may need more
// in-flight inputs than speculative simulation frames; keep checksums at least
// 32 ticks behind and leave that confirmation distance larger than this budget.
constexpr unsigned prediction_inflight=24;
static_assert(prediction_inflight<32);
static_assert(mkdd::online::AdaptiveInputDelay::maximum+prediction_window<=prediction_inflight);
unsigned configured_input_delay=0;
std::map<uint64_t,uint64_t> prediction_hashes;
bool fresh_session() {
    static const bool enabled=[] {
        const char* value=getenv("MKDD_NET_FRESH");
        if (value && strcmp(value,"1")!=0) fatal("MKDD_NET_FRESH must be 1 when set");
        return value!=nullptr;
    }();
    return enabled;
}
uint64_t number(const char* name, uint64_t minimum, uint64_t maximum) {
    const char* value=getenv(name);
    if (!value || !*value || strspn(value,"0123456789")!=strlen(value)) fatal("Missing or invalid %s",name);
    char* end=nullptr;
    errno=0;
    const uint64_t result=strtoull(value,&end,10);
    if (errno || *end || result<minimum || result>maximum) fatal("Out-of-range %s",name);
    return result;
}
uint64_t prediction_start_tick() {
    static const uint64_t start=getenv("MKDD_PREDICTION_START")?number("MKDD_PREDICTION_START",64,1000000):120;
    return start;
}
uint32_t fresh_seed;
uint64_t fresh_epoch;
unsigned fresh_level=2, fresh_cup=0, fresh_arena=0x34, fresh_rounds=3, fresh_course=0;
unsigned seats[8]={0,1,2,3,4,5,6,7};
unsigned seat_karts[8]={0,1,2,3,4,5,6,7},seat_riders[8]{};
bool mixed_layout() {const char* value=getenv("MKDD_NET_LAYOUT");return value && !strcmp(value,"mixed");}
unsigned requested_battle();
bool coop_layout() {
    const char* value=getenv("MKDD_NET_LAYOUT");return value && strcmp(value,"coop")==0;
}
unsigned kart_for_seat(unsigned seat){return mixed_layout()?seat_karts[seat]:coop_layout()?seat/2:seat;}
unsigned human_karts() {
    unsigned mask=0;
    for(unsigned i=0;i<link->players();++i)mask|=1u<<kart_for_seat(seats[i]);
    unsigned count=0;for(unsigned bits=mask;bits;bits>>=1)count+=bits&1;
    if(mask!=((1u<<count)-1))fatal("Native human karts must be packed without empty kart positions");
    return count;
}
uint32_t kart_pad(unsigned kart,unsigned rider) {
    if(mixed_layout()) {
        for(unsigned i=0;i<link->players();++i)if(seat_karts[seats[i]]==kart && seat_riders[seats[i]]==rider)return mem_r32(0x80352808+4*seats[i]);
        return 0;
    }
    if(!coop_layout() && rider)return 0;
    const unsigned wanted=coop_layout()?2*kart+rider:kart;
    for(unsigned i=0;i<link->players();++i)if(seats[i]==wanted)return mem_r32(0x80352808+4*wanted);
    return 0;
}
struct KartChoice { unsigned kart,driver,partner; };
KartChoice choices[8]={{0,7,8},{4,5,6},{1,13,14},{7,15,16},{2,11,12},{3,4,3},{5,1,2},{6,9,10}};
unsigned hex_digit(char c) {
    if (c>='0' && c<='9') return c-'0';
    if (c>='a' && c<='f') return c-'a'+10;
    fatal("Room launch contains invalid hexadecimal data");
}
void hex_bytes(const char* value, uint8_t* out, size_t count) {
    if (!value || strlen(value)!=count*2) fatal("Invalid room launch hexadecimal length");
    for (size_t i=0;i<count;++i) out[i]=(uint8_t)((hex_digit(value[i*2])<<4)|hex_digit(value[i*2+1]));
}
unsigned choice(const char* name, const char* const* options, unsigned count, unsigned fallback) {
    const char* value=getenv(name);
    if (!value) return fallback;
    for (unsigned i=0;i<count;++i) if (strcmp(value,options[i])==0) return i;
    fatal("Invalid %s",name);
}
void configure_boot() {
    if (!fresh_session()) return;
    if (input_replay_active() || !clock_is_virtual() || clock_cpu_scale()!=1.0 ||
        (!getenv("MKDD_NET_LISTEN") && !getenv("MKDD_NET_CONNECT")) || !getenv("MKDD_NET_KARTS"))
        fatal("Fresh online development launch needs a live endpoint, virtual time, standard CPU scale and no replay");
    fresh_seed=(uint32_t)number("MKDD_NET_SEED",0,UINT32_MAX);
    fresh_epoch=number("MKDD_NET_EPOCH",946684800ULL,4102444799ULL);
    const unsigned players=(unsigned)number("MKDD_NET_PLAYERS",2,8);
    const char* const layouts[]={"solo","coop","mixed"};
    choice("MKDD_NET_LAYOUT",layouts,3,0);
    const char* const engines[]={"50cc","100cc","150cc","mirror"};
    const char* const cups[]={"mushroom","flower","star","special","all-cup"};
    fresh_level=choice("MKDD_NET_ENGINE",engines,4,2);
    fresh_cup=choice("MKDD_NET_CUP",cups,5,0);
    if (getenv("MKDD_NET_ARENA")) fresh_arena=(unsigned)number("MKDD_NET_ARENA",52,59);
    if(!mkdd::online::content::arena(fresh_arena))fatal("Invalid online arena");
    if(getenv("MKDD_NET_COURSE")) {
        fresh_course=(unsigned)number("MKDD_NET_COURSE",33,51);
        const auto allowed=mkdd::online::content::cup_courses(cups[fresh_cup]);
        if(std::find(allowed.begin(),allowed.end(),fresh_course)==allowed.end())fatal("Course does not belong to selected cup");
    }
    if (getenv("MKDD_NET_ROUNDS")) fresh_rounds=(unsigned)number("MKDD_NET_ROUNDS",1,9);
    if (!(fresh_rounds&1)) fatal("Battle rounds must be an odd number");
    if (const char* roster=getenv("MKDD_NET_ROSTER")) {
        uint8_t bytes[24]; hex_bytes(roster,bytes,sizeof(bytes));
        for (unsigned i=0;i<8;++i) {
            choices[i]={bytes[3*i],bytes[3*i+1],bytes[3*i+2]};
            if (choices[i].kart>20 || choices[i].driver<1 || choices[i].driver>20 ||
                choices[i].partner<1 || choices[i].partner>20 || choices[i].driver==choices[i].partner)
                fatal("Invalid room kart/character roster");
        }
    }
    if (const char* mapping=getenv("MKDD_NET_SEATS")) {
        if (strlen(mapping)!=players) fatal("Invalid room seat count");
        unsigned used=0;
        for (unsigned i=0;i<players;++i) {
            const unsigned seat=(unsigned)(mapping[i]-'0');
            if (seat>=8 || (used&(1u<<seat))) fatal("Invalid or duplicate room seat");
            seats[i]=seat; used|=1u<<seat;
        }
    }
    if(mixed_layout()) {
        const char* karts=getenv("MKDD_NET_KART_MAP"),*roles=getenv("MKDD_NET_ROLE_MAP");
        if(!karts || !roles || strlen(karts)!=8 || strlen(roles)!=8)fatal("Missing mixed-kart mapping");
        unsigned occupied[8]{};
        for(unsigned i=0;i<players;++i) {
            const unsigned seat=seats[i],kart=unsigned(karts[seat]-'0');
            if(kart>=8 || (roles[seat]!='D' && roles[seat]!='P'))fatal("Invalid mixed-kart mapping");
            const unsigned rider=roles[seat]=='P';
            if(occupied[kart]&(1u<<rider))fatal("Duplicate mixed-kart rider");
            occupied[kart]|=1u<<rider;seat_karts[seat]=kart;seat_riders[seat]=rider;
        }
        for(unsigned group:occupied)if(group==2)fatal("Mixed-kart passenger has no driver");
    }
    // No personal card is opened or copied. Calendar and clock policy must not
    // depend on the host computer's date, timezone, rendering speed or saves.
    exi_set_card_enabled(false);
    rtc_set_base((time_t)fresh_epoch);
    rtc_set_local_time(false);
    clock_set_catchup(false);
    diagnostic_log("[net] fresh boot: seed=%u epoch=%llu UTC; memory cards absent\n",
            fresh_seed,(unsigned long long)fresh_epoch);
}
const bool boot_installed=[] { set_boot_hook(configure_boot); return true; }();
uint64_t compatibility_fingerprint() {
    static const uint64_t value=[] {
        // Keep the marker intact in the executable so developer launch tools
        // can inspect it without booting a game or trusting a sidecar file.
        static const char identity[]="MKDD_BUILD_ID:" MKDD_BUILD_ID;
        diagnostic_log("[net] source/compiler identity %s\n",identity);
        return (uint64_t)strtoull(identity+14,nullptr,16);
    }();
    return value;
}
unsigned requested_battle() {
    const char* mode=getenv("MKDD_NET_MODE");
    if (!mode || strcmp(mode,"grand-prix")==0) return 0;
    if (strcmp(mode,"balloon")==0) return 4;
    if (strcmp(mode,"bob-omb")==0) return 6;
    if (strcmp(mode,"shine")==0) return 7;
    fatal("Unknown development competition mode");
}
uint64_t session_compatibility() {
    uint64_t h=compatibility_fingerprint();
    // The experiment must reject mismatched rules before accepting any input.
    // Include boot identity, but never local input or local rendering options.
    const char* fields[]={"MKDD_NET_PLAYERS","MKDD_NET_LAYOUT","MKDD_NET_MODE","MKDD_NET_KARTS","MKDD_NET_FRESH","MKDD_NET_SEED","MKDD_NET_EPOCH",
                          "MKDD_NET_ENGINE","MKDD_NET_CUP","MKDD_NET_COURSE","MKDD_NET_ARENA","MKDD_NET_ROUNDS","MKDD_NET_SEATS","MKDD_NET_ROSTER","MKDD_NET_ROOM_ID","MKDD_NET_INPUT_DELAY","MKDD_NET_MESH","MKDD_PREDICTION_START","MKDD_NET_TAGS","MKDD_NET_SERIES"};
    for (const char* name:fields) {
        const char* value=getenv(name);
        for (const unsigned char* p=(const unsigned char*)(value?value:"");*p;++p) { h^=*p; h*=1099511628211ULL; }
        h^=255; h*=1099511628211ULL;
    }
    return h;
}
bool valid_ptr(uint32_t p, uint32_t size) {
    return p >= 0x80000000u && size <= RAM_SIZE && p - 0x80000000u <= RAM_SIZE - size && !(p & 3);
}
uint64_t hash_bytes(uint64_t h, uint32_t address, uint32_t size) {
    const uint8_t* p = HOST(address);
    for (uint32_t i = 0; i < size; ++i) { h ^= p[i]; h *= 1099511628211ULL; }
    return h;
}
uint64_t simulation_hash() {
    const auto race=mkdd::race_read();
    if (!race.exists) return 0;
    const uint32_t mgr=mem_r32(0x803CB7E8), info=mem_r32(mgr+0x38), ctrl=mem_r32(0x803CC588);
    const unsigned karts=mem_r16(info+0x1c);
    if (karts>8 || !valid_ptr(ctrl,0x220)) return 0;
    uint64_t h=14695981039346656037ULL;
    h=hash_bytes(h,info+4,0x2c);
    h=hash_bytes(h,info+0xf0,0x24);
    for (unsigned i=0;i<karts;++i) {
        const uint32_t body=mem_r32(ctrl+0xa0+4*i);
        if (!valid_ptr(body,0x600)) continue;
        h=hash_bytes(h,body+0x110,0x30);
        h=hash_bytes(h,body+0x444,0x10);
        // KartSound::DoSlipSound identifies bit 37 as local tyre-slip audio
        // state. Exclude that presentation flag from shared-state diagnostics.
        const uint32_t high=mem_r32(body+0x570)&~0x20u;
        for (int shift=24;shift>=0;shift-=8) { h^=(high>>shift)&255; h*=1099511628211ULL; }
        h=hash_bytes(h,body+0x574,4);
        h=hash_bytes(h,body+0x5b2,2); // active driver and kart index
    }
    return h;
}
void trace_tick() {
    static FILE* out = [] {
        const char* path = getenv("MKDD_SYNC_TRACE");
        if (!path || !*path) return (FILE*)nullptr;
        FILE* f = fopen(path, "w");
        if (!f) fatal("Cannot create MKDD_SYNC_TRACE at %s", path);
        fprintf(f, "tick,time,phase,run,mode,karts,hash,pads,drivers,level,cup,course,view,items,humans,cpu_positions\n");
        return f;
    }();
    if (!out) return;
    const auto race = mkdd::race_read();
    if (!race.exists) return;
    const uint32_t mgr = mem_r32(0x803CB7E8), info = mem_r32(mgr + 0x38);
    const unsigned karts = mem_r16(info + 0x1c);
    const uint64_t h=simulation_hash();
    fprintf(out, "%llu,%llu,%d,%d,%d,%u,%016llx,", (unsigned long long)pad_tick,
            (unsigned long long)now_ticks(), race.phase, race.run_state, race.mode, karts,
            (unsigned long long)h);
    for (const auto& p : logical_pads) fprintf(out, "%02x%02x", p[0], p[1]);
    fputc(',',out);
    const uint32_t ctrl=mem_r32(0x803cc588);
    if (valid_ptr(ctrl,0x220)) {
        for (unsigned i=0;i<karts && i<8;++i) {
            const uint32_t body=mem_r32(ctrl+0xa0+i*4);
            if (valid_ptr(body,0x600)) fprintf(out,"%u",mem_r8(body+0x5b2));
        }
    }
    // The retail course loader stores its course ID at r13-22236, with the
    // verified retail small-data base 0x803D1420. Record applied launch rules.
    fprintf(out,",%u,%u,%u,%u,",mem_r32(info+0x10),mem_r32(info+0xc),
            mem_r32(0x803cbd44),mem_r16(info+0x114));
    const uint32_t items=mem_r32(0x803d1420-21728);
    if(valid_ptr(items,0x310))for(unsigned i=0;i<karts && i<8;++i)
        fprintf(out,"%u",(mem_r32(items+0x2cc+i*8)?1:0)|(mem_r32(items+0x2d0+i*8)?2:0));
    fprintf(out,",%u,",mem_r16(info+0x1e));
    for(unsigned i=0;i<karts && i<8;++i)if(!mem_r32(info+0x30+i*24+4)) {
        const uint32_t body=mem_r32(ctrl+0xa0+4*i);
        if(valid_ptr(body,0x600))fprintf(out,"%u:%08x:%08x:%08x;",i,mem_r32(body+0x11c),mem_r32(body+0x12c),mem_r32(body+0x13c));
    }
    fputc('\n',out);
    fflush(out);
}
void test_item_passing(CPU* c) {
    const char* button=getenv("MKDD_NET_TEST_PASS");
    if(!button)return;
    if(!fresh_session() || !getenv("MKDD_NET_LAYOUT") || strcmp(getenv("MKDD_NET_LAYOUT"),"coop") ||
        (strcmp(button,"X") && strcmp(button,"Y")))fatal("Item-passing fixture requires fresh co-op and X or Y");
    const uint32_t items=mem_r32(c->r[13]-21728);
    static uint32_t object;
    if(pad_tick==1200) {
        if(!valid_ptr(items,0x310))fatal("Item fixture has no native item manager");
        const CPU saved=*c;
        c->r[3]=items;c->r[4]=0;c->r[5]=0;c->r[6]=0;c->r[7]=0;c->r[8]=0;
        fn_80209120(c); // native equipItemToKart: give the front rider a green shell
        const bool equipped=c->r[3]!=0;*c=saved;
        object=mem_r32(items+0x2cc);
        if(!equipped || !valid_ptr(object,0x100))fatal("Native test item could not be equipped");
        diagnostic_log("[net-controls] equipped front rider; awaiting %s input\n",button);
    }
    // The fixture only equips an item. Passing must happen through the real
    // logical pad and native race/item logic, never a direct transfer call.
    if(pad_tick==1400) {
        if(mem_r32(items+0x2cc) || mem_r32(items+0x2d0)!=object)fatal("Driver item did not pass to the rear rider");
        diagnostic_log("[net-controls] %s passed the same item to the rear rider\n",button);
    }
    if(pad_tick==2000) {
        if(mem_r32(items+0x2cc)!=object || mem_r32(items+0x2d0))fatal("Item pass after swapping riders failed");
        diagnostic_log("[net-controls] %s passed the same item after the RB/Z swap\n",button);
    }
}
std::array<uint8_t,2> capture_local(CPU* c,uint64_t tick) {
    // Call the verified retail compressor on this computer's local JUT pad.
    // Its temporary output lives in a reserved guest stack frame, below the
    // caller and above the stack frames used by the original function.
    const CPU saved=*c;
    const bool menu=getenv("MKDD_ROOM_TOKEN") && mkdd_online_pause_capture(tick);
    constexpr unsigned offsets[]={0x18,0x1c,0x20,0x2c,0x30};uint32_t buttons[5]{};
    for(unsigned i=0;i<5;++i){buttons[i]=mem_r32(0x803b4410+offsets[i]);mem_w32(0x803b4410+offsets[i],buttons[i]&~0x1000u);}
    c->r[1]-=0x100;
    c->r[3]=0; c->r[4]=0; c->r[5]=c->r[1]+0x80;
    const uint32_t temp=c->r[5];
    orig_fn_801DC2AC(c);
    std::array<uint8_t,2> local{mem_r8(temp),mem_r8(temp+1)};
    for(unsigned i=0;i<5;++i)mem_w32(0x803b4410+offsets[i],buttons[i]);
    *c=saved;
    if(menu)return {0,0};
    if (mkdd::race_read().exists) {
        if(getenv("MKDD_NET_TEST_PATTERN")) {
            local={uint8_t((tick/7+link->slot()*5)%31),uint8_t(1|(tick%89==0?4:0)|(tick%257<2?0x40:0))};
        }
        if (const char* fixed=getenv("MKDD_NET_LOCAL_INPUT")) {
            unsigned packed;
            if (strlen(fixed)!=4 || sscanf(fixed,"%4x",&packed)!=1) fatal("Invalid development input word");
            local[0]=(uint8_t)(packed>>8); local[1]=(uint8_t)packed;
        }
        if (const char* at=getenv("MKDD_NET_SWAP_AT")) {
            const uint64_t swap_tick=strtoull(at,nullptr,10);
            if (tick>=swap_tick && tick<swap_tick+5) local[1]|=0x40;
        }
        if (getenv("MKDD_NET_ITEM_PULSE") && tick%120<3) local[1]|=4;
        if(const char* test=getenv("MKDD_NET_TEST_PASS")) {
            const unsigned seat=seats[link->slot()];
            if((seat==0 && tick>=1250 && tick<1253) || (seat==1 && tick>=1800 && tick<1803))
                local[1]|=strcmp(test,"X")==0?4:8;
        }
    }
    return local;
}
void exchange_tick(CPU* c) {
    static const char* host=getenv("MKDD_NET_LISTEN");
    static const char* join=getenv("MKDD_NET_CONNECT");
    if (!host && !join) return;
    if (!link) {
        if ((host && join) || (!input_replay_active() && !fresh_session()) || !clock_is_virtual())
            fatal("Development net test requires exactly one endpoint, a canonical replay or fresh launch, and virtual time");
        link=std::make_unique<net::PeerLink>();
        const bool mesh=getenv("MKDD_NET_MESH")!=nullptr;
        if(mesh && (!join || host || !link->local_mesh(true)))fatal("Invalid local mesh adapter configuration");
        const unsigned delay=getenv("MKDD_NET_INPUT_DELAY")?(unsigned)number("MKDD_NET_INPUT_DELAY",0,12):0;
        configured_input_delay=delay;
        if(!link->input_delay(delay))fatal("[net] %s",link->error().c_str());
        if(prediction_enabled) {
            if(!threads_have_owned_stacks() || getenv("MKDD_REPLAY_FRAMES") || getenv("MKDD_REPLAY_WINDOW") || getenv("MKDD_NET_TEST_PASS"))
                fatal("Native prediction needs owned stacks without isolated probes or item-pass fixtures");
            if(!link->prediction_window(prediction_inflight))fatal("[net] %s",link->error().c_str());
        }
        const uint64_t compatibility=session_compatibility();
        const char* count=getenv("MKDD_NET_PLAYERS");
        const unsigned players=count?(unsigned)atoi(count):2;
        if (players<2 || players>8) fatal("Development game sessions support 2..8 people");
        net::Admission admissions[8]{};
        unsigned slot=net::kMaxPeers;
        const bool room=getenv("MKDD_NET_ROOM_ID")!=nullptr;
        if (room) {
            if (!fresh_session()) fatal("Room launches require a fresh event");
            slot=(unsigned)number("MKDD_NET_SLOT",0,players-1);
            if (host) {
                if (slot!=0) fatal("The room host must occupy transport slot zero");
                uint8_t packed[128];
                hex_bytes(getenv("MKDD_NET_ADMISSIONS"),packed,players*16);
                for (unsigned i=0;i<players;++i) memcpy(admissions[i].data(),packed+16*i,16);
            } else {
                if (!slot && !mesh) fatal("A joining member cannot claim the host slot");
                hex_bytes(getenv("MKDD_NET_ADMISSION"),admissions[slot].data(),16);
            }
        }
        const bool ok=host ? link->host(host,players,compatibility,room?admissions:nullptr)
                           : link->join(join,compatibility,slot,room?&admissions[slot]:nullptr);
        if (!ok) fatal("[net] %s",link->error().c_str());
        if (link->players()>8) fatal("Unsupported game player count");
        diagnostic_log("[net] connected slot %u of %u\n",link->slot(),link->players());
    }
    test_item_passing(c);
    auto local=capture_local(c,pad_tick);
    if(prediction_enabled) {
        prediction_hashes[pad_tick]=simulation_hash();
        while(prediction_hashes.size()>128)prediction_hashes.erase(prediction_hashes.begin());
    }
    const auto wait_started=std::chrono::steady_clock::now();
    if (!link->exchange(pad_tick,simulation_hash(),local.data(),network_pads)) {
        diagnostic_log("[net] local state=%016llx virtual-time=%llu\n",
                (unsigned long long)simulation_hash(),(unsigned long long)now_ticks());
        if (const char* path=getenv("MKDD_NET_FAILURE_DUMP")) {
            if (FILE* dump=fopen(path,"wb")) { fwrite(g_mem,1,RAM_SIZE,dump); fclose(dump); }
        }
        mkdd::online::recover_room_disconnect();
        fatal("[net] tick %llu: %s",(unsigned long long)pad_tick,link->error().c_str());
    }
    const auto wait_finished=std::chrono::steady_clock::now();
    static auto performance_at=wait_started;
    static unsigned performance_ticks=0;
    static double blocked_ms=0;
    ++performance_ticks;
    blocked_ms+=std::chrono::duration<double,std::milli>(wait_finished-wait_started).count();
    const double elapsed=std::chrono::duration<double>(wait_finished-performance_at).count();
    if(elapsed>=2) {
        diagnostic_log("[net-performance] %.1f input ticks/s; %.1f ms/tick waiting for network; delay=%s\n",
                performance_ticks/elapsed,blocked_ms/performance_ticks,
                getenv("MKDD_NET_INPUT_DELAY")?getenv("MKDD_NET_INPUT_DELAY"):"0");
        performance_at=wait_finished;performance_ticks=0;blocked_ms=0;
    }
    // Transport slots identify room members; game seats identify karts/roles.
    // Never let connection arrival order decide someone's selected partner.
    const net::Inputs contributors=network_pads;
    network_pads={};
    for (unsigned i=0;i<link->players();++i) {
        network_pads[seats[i]*2]=contributors[i*2];
        network_pads[seats[i]*2+1]=contributors[i*2+1];
    }
    network_override=true;
}
#include "native_prediction.inc"
}

namespace {
bool intro_pointer() {
    desktop_pointer::context(true);desktop_pointer::text_enabled=false;display_set_settings_hint(true);
    desktop_pointer::publish("startup-intro",{{0,0,1,1,90}});
    const auto click=desktop_pointer::take();return click && *click==90;
}
}
extern "C" void hle_calc__8MovieAppFv(CPU* c) {
    const bool clicked=intro_pointer();const auto pressed=mem_r32(0x803b442c),repeat=mem_r32(0x803b4440);
    if(clicked){mem_w32(0x803b442c,pressed|0x1100);mem_w32(0x803b4440,repeat|0x1100);}
    orig_calc__8MovieAppFv(c);mem_w32(0x803b442c,pressed);mem_w32(0x803b4440,repeat);
}
extern "C" void hle_calc__7LogoAppFv(CPU* c) {
    if(getenv("MKDD_ROOM_RESUME")) {
        const CPU saved=*c;static bool requested=false,called=false;
        if(!requested){fn_801CEBFC(c);requested=true;}
        if(!called) {
            bool ready=true;for(unsigned archive:{0u,1u,2u,5u}){c->r[3]=archive;fn_801CEE1C(c);ready&=c->r[3]!=0;}
            c->r[3]=mem_r32(saved.r[13]-24008);c->r[4]=1;fn_80114AA8(c);ready&=c->r[3]!=0;
            if(ready){c->r[3]=0;orig_call__11SequenceAppFQ25Scene9SceneType(c);called=true;diagnostic_log("[online-menu] resume directly to persistent room\n");}
        }
        *c=saved;return;
    }
    if (!fresh_session()) {
        if(intro_pointer()) {
            const auto state=mem_r32(c->r[3]+0x14);
            if(state==5 || state==7)mem_w32(c->r[3]+0x18,91);
        }
        orig_calc__7LogoAppFv(c);return;
    }
    // A room launch takes over only this startup application. System::init has
    // already created heaps, pads, audio, resource managers and the display.
    // Keep the original asynchronous loaders; do not reconstruct game assets.
    const CPU saved=*c;
    static unsigned stage;
    if (stage==0) {
        fn_801CEBFC(c); // request persistent race assets
        stage=1;
        diagnostic_log("[net] fresh launch: requesting race resources\n");
    } else if (stage==1) {
        bool ready=true;
        for (unsigned archive:{0u,1u,2u,5u}) {
            c->r[3]=archive; fn_801CEE1C(c);
            ready=ready && c->r[3]!=0;
        }
        c->r[3]=mem_r32(saved.r[13]-24008); c->r[4]=1;
        fn_80114AA8(c); // native SE wave readiness, also checked by LogoApp
        ready=ready && c->r[3]!=0;
        if (ready) {
            if (!link) fatal("Fresh launch reached resource loading without a link");
            constexpr uint32_t info=0x803b145c;
            c->r[3]=info; fn_8018C408(c); // native RaceInfo reset
            const unsigned mode=requested_battle();
            const unsigned humans=human_karts(),karts=mode?humans:8;
            if(mode && humans<2)fatal("Battle needs at least two opposing human karts");
            mem_w32(info+4,fresh_seed);
            mem_w32(info+8,mode?mode:2);
            mem_w32(info+0xc,fresh_cup);
            mem_w32(info+0x10,fresh_level);
            mem_w8(info+0x2c,fresh_level==3);
            mem_w16(info+0x18,3); mem_w16(info+0x1a,3);
            mem_w16(info+0x1c,(uint16_t)karts); mem_w16(info+0x1e,(uint16_t)humans);
            mem_w16(info+0x20,1); mem_w16(info+0x22,1);
            for (unsigned i=0;i<karts;++i) {
                c->r[3]=info; c->r[4]=i; c->r[5]=choices[i].kart; c->r[6]=choices[i].driver;
                c->r[7]=kart_pad(i,0); c->r[8]=choices[i].partner;
                c->r[9]=kart_pad(i,1);
                fn_8018C904(c); // retail RaceInfo::setKart
            }
            if(getenv("MKDD_NET_SERIES")) {
                c->r[3]=0x803b0f34;fn_8013FB04(c); // native sequence reset
                mem_w32(0x803b0f34+0x90,fresh_cup);mem_w32(0x803b0f34+0x94,0);
                mem_w32(0x803b0f34+0x358,fresh_level);
                mem_w32(0x803b0f34+0x350,std::min(humans,2u));
                mem_w32(0x803b0f34+0x24,mem_r32(0x80352808+4*seats[0]));
                for(unsigned i=0;i<std::min(humans,2u);++i) {
                    const uint32_t record=0x803b0f34+0x318+0x1c*i;
                    mem_w32(record,choices[i].driver);mem_w32(record+4,choices[i].partner);mem_w32(record+8,choices[i].kart);
                    mem_w32(record+0x18,kart_pad(i,0));
                }
            }
            constexpr unsigned first_courses[]={0x24,0x28,0x33,0x2b,0x24};
            c->r[3]=mode?fresh_arena:(fresh_course?fresh_course:first_courses[fresh_cup]); c->r[4]=0;
            orig_loadCourseData__6ResMgrF9ECourseIDQ26ResMgr11CourseOrder(c);
            stage=2;
            diagnostic_log("[net] fresh launch: requesting course, %u karts\n",karts);
        }
    } else if (stage==2) {
        c->r[3]=3; fn_801CEE1C(c);
        if (c->r[3]) {
            // The normal menu route releases its large application heap before
            // constructing RaceApp. Startup pre-created the sequence/movie apps;
            // retire both idle apps too, or the race heap cannot be allocated.
            c->r[3]=mem_r32(saved.r[13]-22456);
            if (c->r[3]) { c->r[4]=1; fn_801B0C10(c); }
            c->r[3]=mem_r32(saved.r[13]-22016);
            if (c->r[3]) { c->r[4]=1; fn_801D9494(c); }
            mem_w32(saved.r[13]-22124,mem_r32(saved.r[13]-22124)|1u);
            // Reuse the verified pre-construction controller/camera setup.
            extern void hle_call__7RaceAppFv(CPU*);
            hle_call__7RaceAppFv(c);
            stage=3;
            diagnostic_log("[net] fresh launch: entering race at tick %llu\n",(unsigned long long)pad_tick);
        }
    }
    *c=saved;
}

extern "C" void hle_processKartPad__6PadMgrFv(CPU* c) {
    if(prediction_enabled && link && pad_tick>=prediction_start_tick()) {
        if(pad_tick<prediction_start_tick()+configured_input_delay) {
            ++pad_tick;prediction_hashes[pad_tick]=simulation_hash();
            net::Inputs contributors{};
            if(!link->drain_startup(pad_tick,contributors))fatal("[native-rollback] startup drain: %s",link->error().c_str());
            network_pads={};
            for(unsigned i=0;i<link->players();++i) {
                network_pads[2*seats[i]]=contributors[2*i];network_pads[2*seats[i]+1]=contributors[2*i+1];
            }
            network_override=true;orig_processKartPad__6PadMgrFv(c);network_override=false;trace_tick();return;
        }
        prediction_boundary(c);
        ++pad_tick;
        orig_processKartPad__6PadMgrFv(c);network_override=false;return;
    }
    if(g_replay_guard && g_replay_guard->runtime_graphics) {
        extern void mkdd_frame_probe_pads(uint8_t*);
        mkdd_frame_probe_pads(network_pads.data());network_override=true;
        orig_processKartPad__6PadMgrFv(c);network_override=false;return;
    }
    ++pad_tick;
    exchange_tick(c);
    orig_processKartPad__6PadMgrFv(c);
    network_override=false;
    trace_tick();
    static const uint64_t limit=[] {
        const char* value=getenv("MKDD_NET_TICKS");
        return value ? strtoull(value,nullptr,10) : 0ULL;
    }();
    if (link && limit && pad_tick==limit) {
        diagnostic_log("[net] completed %llu synchronized input ticks\n",(unsigned long long)pad_tick);
        plat_exit_now(0);
    }
}

extern "C" void hle_fn_801DC2AC(CPU* c) {
    const unsigned slot = c->r[3];
    const uint32_t dest = c->r[5];
    if (network_override && slot<16) {
        mem_w8(dest,network_pads[slot*2]);
        mem_w8(dest+1,network_pads[slot*2+1]);
    } else orig_fn_801DC2AC(c);
    if (slot < 16 && valid_ptr(dest & ~3u, 4)) {
        logical_pads[slot][0] = mem_r8(dest);
        logical_pads[slot][1] = mem_r8(dest + 1);
    }
}

extern "C" void hle_call__7RaceAppFv(CPU* c) {
    extern bool mkdd_online_menu_race_selected(CPU*);
    if(mkdd_online_menu_race_selected(c))return;
    extern void mkdd_online_tags_begin_race();mkdd_online_tags_begin_race();
    if (link && getenv("MKDD_NET_KARTS") && mem_r32(0x803b145c+8)!=8) {
        // RaceApp::call occurs after character/course selection and before
        // race construction. Retail setAudioRaceMode confirms this global;
        // setKart confirms the 24-byte kart entries and pad-pointer offsets.
        constexpr uint32_t info=0x803b145c, pads=0x80352808;
        if (const unsigned mode=requested_battle()) mem_w32(info+8,mode);
        const char* layout=getenv("MKDD_NET_LAYOUT");
        const bool coop=layout && strcmp(layout,"coop")==0;
        const unsigned humans=human_karts();
        const unsigned karts=requested_battle()?humans:8;
        if(requested_battle() && humans<2)fatal("Battle needs at least two opposing human karts");
        mem_w8(info+1,1); // use the game's LAN simulation rules, not offline culling/AI rules
        mem_w16(info+0x1c,(uint16_t)karts);
        mem_w16(info+0x1e,(uint16_t)humans);
        mem_w16(info+0x20,1); // one full-screen camera on each client
        mem_w16(info+0x22,1); // one local HUD/recording view
        const unsigned local_seat=seats[link->slot()];
        mem_w16(info+0x114,(uint16_t)kart_for_seat(local_seat));
        for (unsigned i=0;i<16;++i) {
            const uint32_t pad=mem_r32(pads+i*4);
            if (!valid_ptr(pad,0xb4)) fatal("Invalid retail logical pad table");
            mem_w32(pad+0xa8,1); // network-expanded logical controller
            mem_w32(pad+0xac,i==local_seat?0:0xfffffffeu);
            mem_w32(pad+0xb0,0);
        }
        const uint32_t manager=mem_r32(c->r[13]-22024);
        if (!valid_ptr(manager,0x1308)) fatal("Invalid retail network manager");
        mem_w8(manager+0xc58,(uint8_t)local_seat);
        mem_w8(manager+0x12e8,(uint8_t)link->players());
        for (unsigned i=0;i<16;++i) mem_w8(manager+0x12f4+i,(uint8_t)(i*4));
        for (unsigned kart=0;kart<karts;++kart) {
            // SequenceInfo keeps each kart's previous finish position. The
            // transport/seat order is unrelated to its next starting grid.
            if(mem_r32(info+8)==2 && getenv("MKDD_NET_SERIES")) {
                const unsigned position=mem_r32(0x803b0f34+4+kart*4);
                if(position>=karts)fatal("Invalid next Grand Prix starting position");
                mem_w16(info+0xf0+kart*2,(uint16_t)position);
            }
            const uint32_t entry=info+0x30+kart*24;
            mem_w32(entry+4,kart_pad(kart,0));
            mem_w32(entry+12,kart_pad(kart,1));
        }
        diagnostic_log("[net] race setup: %u humans, %u human karts, %u total, view %u\n",
                link->players(),humans,karts,kart_for_seat(local_seat));
        diagnostic_log("[net] starting grid by kart: ");
        for(unsigned kart=0;kart<karts;++kart)diagnostic_log("%u",mem_r16(info+0xf0+kart*2));
        diagnostic_log("\n");
    }
    orig_call__7RaceAppFv(c);
}

namespace {
void grand_prix_item_tables(CPU* c,void (*original)(CPU*)) {
    // LAN mode keeps view-dependent AI/collision culling consistent, but its
    // VS roulette path never initializes the enemy tables used by GP CPUs.
    // Use the native GP tables only inside the two item-table routines.
    constexpr uint32_t info=0x803b145c;
    const uint8_t lan=mem_r8(info+1);
    const bool adapt=link && mem_r32(info+8)==2;
    if(adapt)mem_w8(info+1,0);
    original(c);
    if(adapt)mem_w8(info+1,lan);
}
}
extern "C" void hle_loadSlotData__14ItemShuffleMgrFv(CPU* c) {
    grand_prix_item_tables(c,orig_loadSlotData__14ItemShuffleMgrFv);
    if(getenv("MKDD_ITEM_TABLE_AUDIT")) {
        // Compare the tables actually loaded by the original executable with
        // probabilities independently read from the owner's disc in tests.
        for(unsigned enemy=0;enemy<2;++enemy) {
            const uint32_t table=c->r[13]-21716+8*enemy;
            const unsigned ranks=mem_r8(table),items=mem_r8(table+2);
            const uint32_t rows=mem_r32(table+4);
            if(!ranks || ranks>8 || !items || items>27 || !valid_ptr(rows,ranks*8))fatal("Invalid native roulette table");
            uint64_t hash=14695981039346656037ULL;
            for(unsigned rank=0;rank<ranks;++rank) {
                const uint32_t weights=mem_r32(rows+rank*8);
                if(!valid_ptr(weights,items))fatal("Invalid native roulette weights");
                hash=hash_bytes(hash,weights,items);
            }
            diagnostic_log("[item-audit] mode=%u level=%u enemy=%u ranks=%u items=%u hash=%016llx\n",
                mem_r32(0x803b145c+8),mem_r32(0x803b145c+0x10),enemy,ranks,items,(unsigned long long)hash);
        }
    }
}
extern "C" void hle_setAllDynamicRate__14ItemShuffleMgrFv(CPU* c) {
    grand_prix_item_tables(c,orig_setAllDynamicRate__14ItemShuffleMgrFv);
}

extern "C" void hle_fn_80225F20(CPU* c) {
    constexpr uint32_t info=0x803b145c;
    const unsigned karts=mem_r16(info+0x1c);
    if(!link || mem_r32(info+8)!=2 || mem_r16(info+0x1e)>=karts) {
        orig_fn_80225F20(c);return;
    }
    // Retail CPU item/object collision pruning measures distance from this
    // console's local human kart. Different views would accept different hits.
    // All online GP karts must participate in collisions, including offscreen
    // CPUs. The callers retain their normal active/invulnerability checks.
    const uint32_t output=c->r[3];
    if(karts>8 || output<0x80000000u || output>0x80000000u+RAM_SIZE-karts)
        fatal("Invalid native collision participation array");
    for(unsigned i=0;i<karts;++i)mem_w8(output+i,1);
}

extern "C" void hle_loadCourseData__6ResMgrF9ECourseIDQ26ResMgr11CourseOrder(CPU* c) {
    if (link && getenv("MKDD_NET_KARTS")) {
        if (const unsigned mode=requested_battle()) {
            mem_w32(0x803b145c+8,mode);
            c->r[3]=fresh_session()?fresh_arena:0x34;
            c->r[4]=0;
        }
    }
    orig_loadCourseData__6ResMgrF9ECourseIDQ26ResMgr11CourseOrder(c);
}

extern "C" void hle_fn_801781D8(CPU* c) {
    // BombStart2D has no one-console initialization path in the retail game.
    // Reuse its native two-console animation for the experimental single-view
    // session. Hide a second-view pane if the loaded asset has one. The
    // single-view visual treatment still needs dedicated verification.
    constexpr uint32_t info=0x803b145c;
    const bool adapt=link && requested_battle()==6 && mem_r16(info+0x20)==1;
    if (adapt) mem_w16(info+0x20,2);
    orig_fn_801781D8(c);
    if (!adapt) return;
    mem_w16(info+0x20,1);
    const CPU result=*c;
    const uint32_t object=c->r[3];
    if (!valid_ptr(object,0x24)) fatal("Invalid native bomb intro object");
    const uint32_t screen=mem_r32(object+0x1c);
    if (!valid_ptr(screen,0xb4)) fatal("Invalid native bomb intro screen");
    const uint32_t vtable=mem_r32(screen);
    if (!valid_ptr(vtable,0x40)) fatal("Invalid native bomb intro screen methods");
    c->r[3]=screen; c->r[5]=0; c->r[6]=0x004e3250; // N2P, the other native panel
    call_indirect(c,mem_r32(vtable+0x3c));
    if (valid_ptr(c->r[3],0xb4)) mem_w8(c->r[3]+0xb0,0);
    else diagnostic_log("[net] using shared native bomb start animation\n");
    *c=result;
}

namespace mkdd {
uint64_t online_build_fingerprint() { return compatibility_fingerprint(); }
void online_end_inputs() {
    // Close sockets before the Windows relaunch can inherit their handles.
    // A live inherited socket otherwise leaves the lobby bridge waiting for
    // race input while the replacement process is still loading the menus.
    link.reset();
}
}

// Candidate replay boundary: RaceApp::calc in this retail revision. Audit is
// opt-in and observational; do not enable rollback until external state is owned.
extern "C" void hle_fn_80126804(CPU* c) {
    const auto race=mkdd::race_read();
    if(getenv("MKDD_ROOM_TOKEN") && race.exists && race.phase<19)return;
    orig_fn_80126804(c);
}
extern "C" void hle_fn_801CD92C(CPU* c) {
    // Keep the configured race bindings through results, next-course prompts and
    // the award ceremony. SequenceApp owns the switch back to menu bindings.
    if(!g_replay_guard || g_replay_guard->publish_outputs){const auto current=mkdd::race_read();const bool menu=mkdd_online_pause_open();desktop_pointer::context(menu);desktop_pointer::navigation_enabled=!menu && (current.paused || current.phase>=19 || current.mode==8);desktop_pointer::text_enabled=false;mkdd::online::race_ui_tick(menu || current.phase>=19 || current.mode==8);}
    struct PauseMenuInput {
        unsigned count=0;uint32_t addresses[16]{},values[16][5]{};
        const unsigned offsets[5]={0x18,0x1c,0x20,0x2c,0x30};
        PauseMenuInput(){if(!getenv("MKDD_ROOM_TOKEN"))return;for(unsigned p=0;p<16;++p){const auto address=mem_r32(0x80352808+4*p);if(!valid_ptr(address,0x34))continue;addresses[count]=address;for(unsigned i=0;i<5;++i){values[count][i]=mem_r32(address+offsets[i]);mem_w32(address+offsets[i],values[count][i]&~0x1000u);}++count;}}
        ~PauseMenuInput(){for(unsigned p=0;p<count;++p)for(unsigned i=0;i<5;++i)mem_w32(addresses[p]+offsets[i],values[p][i]);}
    } pause_menu_input;
    struct CeremonyInput {
        bool active=false;uint32_t saved[5]{};
        const uint32_t primary=0x803b4410;
        CeremonyInput() {
            if(!link || !getenv("MKDD_NET_SERIES") || mem_r32(0x803b145c+8)!=8)return;
            const auto host=mem_r32(0x80352808+4*seats[0]);if(!valid_ptr(host,0x34))fatal("Invalid ceremony controller");
            active=true;
            constexpr unsigned offsets[]={0x18,0x1c,0x20,0x2c,0x30};
            for(unsigned i=0;i<5;++i){saved[i]=mem_r32(primary+offsets[i]);mem_w32(primary+offsets[i],mem_r32(host+offsets[i]));}
        }
        ~CeremonyInput(){if(active){constexpr unsigned offsets[]={0x18,0x1c,0x20,0x2c,0x30};for(unsigned i=0;i<5;++i)mem_w32(primary+offsets[i],saved[i]);}}
    } ceremony_input;
    static const bool audit=getenv("MKDD_REPLAY_AUDIT")!=nullptr;
    static const bool probe=getenv("MKDD_REPLAY_PROBE")!=nullptr;
    static unsigned probes=0;
    const auto prior_race=mkdd::race_read();
    if(link && getenv("MKDD_BATTLE_TEST_FINISH") && prior_race.run_state==2 && prior_race.mode==4) {
        const auto manager=mem_r32(0x803cb7e8);
        if(valid_ptr(manager,0x68) && mem_r32(manager+0x24)>1200) {
            const CPU saved=*c;
            for(unsigned kart=1;kart<human_karts();++kart) {
                const auto checker=mem_r32(manager+0x48+4*kart);
                if(valid_ptr(checker,0xc0) && mem_r16(checker+0x90)>0){mem_w16(checker+0x8e,0);c->r[3]=checker;fn_8018706C(c);}
            }
            *c=saved;
        }
    }
    if(!g_replay_guard && link && getenv("MKDD_NET_SERIES") && prior_race.mode==8) {
        static bool logged=false;if(!logged){logged=true;diagnostic_log("[online-series] award ceremony active\n");}
    }
    if(((link && getenv("MKDD_NET_SERIES") && getenv("MKDD_SERIES_TEST_FINISH")) || (!link && getenv("MKDD_OFFLINE_TEST_FINISH"))) && prior_race.run_state==2 && prior_race.mode==2) {
        const auto manager=mem_r32(0x803cb7e8);
        if(mem_r32(manager+0x24)>1200) {
            const CPU saved=*c;
            for(unsigned kart=0;kart<8;++kart) {
                const auto checker=mem_r32(manager+0x48+4*kart);
                if(valid_ptr(checker,0xc0) && !mem_r8(checker+0x29)) {
                    c->r[3]=checker;fn_80186A84(c);
                    static constexpr unsigned finishes[4][8]={{3,0,1,2,4,5,6,7},{0,4,1,2,3,5,6,7},{1,0,3,4,2,5,6,7},{2,1,0,3,4,5,6,7}};
                    const unsigned rank=getenv("MKDD_SERIES_TEST_GRID")?finishes[mem_r32(0x803b0f34+0x94)%4][kart]:kart;
                    mem_w32(checker+0x84,10000+rank*1000);
                    mem_w32(checker+0x88,rank+1);
                }
            }
            *c=saved;
        }
    }
    if(link) {
        extern void mkdd_full_frame_probe(CPU*,unsigned,unsigned,const unsigned*,uint64_t,bool);
        mkdd_full_frame_probe(c,link->players(),link->slot(),seats,pad_tick,prior_race.run_state==2);
        if(g_replay_guard && g_replay_guard->runtime_graphics){orig_fn_801CD92C(c);return;}
    }
    if(link && prior_race.run_state==2) {
        extern void mkdd_rollback_window_probe(CPU*,unsigned,unsigned,const unsigned*,uint64_t);
        mkdd_rollback_window_probe(c,link->players(),link->slot(),seats,pad_tick);
    }
    if(probe && prior_race.run_state==2 && probes<120) {
        if(g_replay_guard || !clock_is_virtual())fatal("Invalid replay probe context");
        const CPU caller=*c;
        fn_800EB1B8(c); // OSDisableScheduler: defer audio-thread handoff for this probe
        *c=caller;
        const CPU before_cpu=*c;
        const uint64_t before_count=g_vcount,before_limit=g_vlimit;
        const uint32_t pipe_length=g_gp_len;
        std::array<uint8_t,64> pipe{};memcpy(pipe.data(),g_gp_buf,pipe.size());
        std::vector<uint8_t> before(g_mem,g_mem+RAM_SIZE),cache(g_mem+LC_BASE,g_mem+LC_BASE+LC_SIZE);
        ReplayGuard first;first.maximum_edges=g_vcount+1000000;
        g_replay_guard=&first;orig_fn_801CD92C(c);g_replay_guard=nullptr;
        if(g_gp_len!=pipe_length || memcmp(g_gp_buf,pipe.data(),pipe.size()))fatal("Replay probe changed unowned graphics pipe state");
        const CPU after_cpu=*c;const uint64_t after_count=g_vcount;
        std::vector<uint8_t> after(g_mem,g_mem+RAM_SIZE),after_cache(g_mem+LC_BASE,g_mem+LC_BASE+LC_SIZE);
        memcpy(g_mem,before.data(),RAM_SIZE);memcpy(g_mem+LC_BASE,cache.data(),LC_SIZE);
        *c=before_cpu;g_vcount=before_count;g_vlimit=before_limit;
        ReplayGuard second;second.maximum_edges=g_vcount+1000000;
        g_replay_guard=&second;orig_fn_801CD92C(c);g_replay_guard=nullptr;
        if(g_gp_len!=pipe_length || memcmp(g_gp_buf,pipe.data(),pipe.size()))fatal("Replay probe changed unowned graphics pipe state");
        if(memcmp(g_mem,after.data(),RAM_SIZE) || memcmp(g_mem+LC_BASE,after_cache.data(),LC_SIZE) ||
           memcmp(c,&after_cpu,sizeof(CPU)) || g_vcount!=after_count)
            fatal("Replay probe did not reproduce complete frame state at tick %llu",(unsigned long long)pad_tick);
        if(first.outputs.size()!=second.outputs.size())fatal("Replay output journal size differs");
        for(size_t i=0;i<first.outputs.size();++i) {
            const auto a=first.outputs[i],b=second.outputs[i];
            if(a.address!=b.address || a.value!=b.value)fatal("Replay output journal differs");
            mmio_write32(a.address,a.value);
        }
        clock_update_limit();
        const CPU result=*c;
        fn_800EB1F8(c); // OSEnableScheduler; pending work can resume outside replay
        *c=result;
        if(++probes==120)diagnostic_log("[rollback-probe] PASS: 120 isolated race-call RAM/CPU/cache/output replay comparisons\n");
        return;
    }
    if(!audit){orig_fn_801CD92C(c);return;}
    if(g_replay_audit)fatal("Nested replay boundary audit");
    ReplayAudit counters;
    g_replay_audit=&counters;
    const uint64_t begin=now_ticks();
    orig_fn_801CD92C(c);
    g_replay_audit=nullptr;
    const auto race=mkdd::race_read();
    static FILE* output=[] {
        FILE* file=fopen(getenv("MKDD_REPLAY_AUDIT"),"w");
        if(!file)fatal("Cannot open replay audit");
        fputs("tick,phase,run,virtual_ticks,context_saves,context_loads,interrupts,events,mmio_reads,mmio_writes,gpu_flushes,spr_writes,first_mmio,first_context,quiet\n",file);
        return file;
    }();
    fprintf(output,"%llu,%d,%d,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%llu,%08x,%08x,%d\n",
        (unsigned long long)pad_tick,race.phase,race.run_state,(unsigned long long)(now_ticks()-begin),
        (unsigned long long)counters.context_saves,(unsigned long long)counters.context_loads,
        (unsigned long long)counters.interrupts,(unsigned long long)counters.events,
        (unsigned long long)counters.mmio_reads,(unsigned long long)counters.mmio_writes,
        (unsigned long long)counters.gpu_flushes,(unsigned long long)counters.spr_writes,
        counters.first_mmio,counters.first_context,counters.externally_quiet());
    fflush(output);
}

DeviceFragment mkdd_checkpoint_online_input() {
    return checkpoint_fields("logical online input",pad_tick,logical_pads,network_pads,network_override);
}

// Native GP presentation supports two local record slots. Online karts keep
// all eight standings, while calls that touch those record slots stay bounded.
extern "C" void hle_fn_8013FCE4(CPU* c) {
    if(!link || !getenv("MKDD_NET_SERIES")){orig_fn_8013FCE4(c);return;}
    const auto humans=mem_r16(0x803b145c+0x1e);
    mem_w16(0x803b145c+0x1e,std::min<unsigned>(humans,2));
    orig_fn_8013FCE4(c);
    mem_w16(0x803b145c+0x1e,humans);
    diagnostic_log("[online-series] completed course %u; standings=",mem_r32(0x803b0f34+0x94));
    for(unsigned i=0;i<8;++i)diagnostic_log("%u",mem_r32(0x803b0f34+0x2d8+4*i));fputc('\n',stderr);
}
extern "C" void hle_fn_801400CC(CPU* c) {
    if(!link || !getenv("MKDD_NET_SERIES") || mem_r32(0x803b145c+8)!=2){orig_fn_801400CC(c);return;}
    const auto humans=human_karts();c->r[3]=7;
    for(unsigned rank=0;rank<8;++rank)if(mem_r32(0x803b0f34+0x2d8+4*rank)<humans){c->r[3]=rank;break;}
}
extern "C" void hle_fn_801401C8(CPU* c) {
    if(!link || !getenv("MKDD_NET_SERIES"))orig_fn_801401C8(c);
}
extern "C" void hle_fn_8019FE64(CPU* c) {
    if(link && getenv("MKDD_NET_SERIES") && mem_r32(0x803b145c+8)==2)
        mem_w8(0x803b145c+1,0); // native GP results and next-course presentation
    orig_fn_8019FE64(c);
}
extern "C" void hle_call__11SequenceAppFQ25Scene9SceneType(CPU* c) {
    if(link && getenv("MKDD_NET_SERIES")) {
        diagnostic_log("[online-series] sequence=%u mode=%u course=%u\n",c->r[3],mem_r32(0x803b145c+8),mem_r32(0x803b0f34+0x94));
        if(requested_battle() && (c->r[3]==0 || c->r[3]==1 || c->r[3]==4)) {
            const auto manager=mem_r32(0x803cb7e8);std::vector<std::pair<unsigned,unsigned>> standings;
            for(unsigned kart=0;kart<human_karts();++kart) {
                const auto checker=mem_r32(manager+0x48+4*kart);
                const unsigned rank=valid_ptr(checker,0x90)?mem_r32(checker+0x88):0;
                standings.emplace_back(rank?rank:9,kart);
            }
            std::stable_sort(standings.begin(),standings.end());mkdd::online::Json ranking;ranking.kind=mkdd::online::Json::Array;
            for(const auto& [rank,kart]:standings)ranking.array.emplace_back((int)kart);
            diagnostic_log("[online-series] battle complete; returning to room\n");mkdd::online::finish_event(ranking);
        }
        if((c->r[3]==0 || (c->r[3]==6 && mem_r32(0x803b145c+8)==8)) && mem_r32(0x803b0f34+0x94)>=(fresh_cup==4?16u:4u)) {
            mkdd::online::Json ranking;ranking.kind=mkdd::online::Json::Array;
            for(unsigned rank=0;rank<8;++rank)ranking.array.emplace_back((int)mem_r32(0x803b0f34+0x2d8+4*rank));
            mkdd::online::finish_event(ranking);
        }
    }
    orig_call__11SequenceAppFQ25Scene9SceneType(c);
}
