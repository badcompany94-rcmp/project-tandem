#include "time_trials_runtime.h"
#include "runtime.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <vector>

using namespace mkdd::trials;
extern "C" {
void hle_available__9GhostFileF9ECourseID(CPU*);
void hle_fn_801DC794(CPU*);
void hle_updateKartPadRecord__9GhostFileFRC13KartPadRecord(CPU*);
void hle_ctrlRace__7RaceAppFv(CPU*);
uint8_t* g_mem=nullptr;
uint32_t vm_translate(uint32_t,int){throw std::runtime_error("unexpected VM access");}
}
namespace {
constexpr uint32_t object=0x80400000,record=0x80500000,recorder=0x80600000;
constexpr uint32_t app=0x80700000,manager=0x80701000,controller=0x80702000;
constexpr uint32_t info=0x803b145c,pad_offset=0x1460,type_offset=0xa048;
bool replay=false;unsigned available_calls=0,set_calls=0,result_calls=0,race_calls=0;
Record original{},rival{},last_result{};
bool retry_overwrote=false;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void put(Record& value,unsigned offset,unsigned number){for(unsigned i=0;i<4;++i)value[offset+i]=uint8_t(number>>(24-i*8));}
Record fixture(unsigned time,unsigned first){
    Record value{};value[0]=7;value[1]=8;value[2]=0;value[3]=36;
    put(value,8,time);put(value,12,6000);put(value,16,time/3);put(value,20,time*2/3);
    value[40]=uint8_t(first);return value;
}
bool equals(uint32_t address,const Record& value){return !std::memcmp(mem_ptr(address),value.data(),value.size());}
CPU call(uint32_t r3,uint32_t r4=0,uint32_t r5=0){CPU cpu{};cpu.r[3]=r3;cpu.r[4]=r4;cpu.r[5]=r5;return cpu;}
void clear_env(const char* key){
#ifdef _WIN32
    _putenv_s(key,"");
#else
    unsetenv(key);
#endif
}
void setup(bool with_ghost=true){
    native_menu_boundary();native_session().leave();
    mem_w32(info+8,1);mem_w32(info+0x10,2);mem_w16(info+0x1c,4);mem_w16(info+0x1e,4);
    mem_w16(info+0x20,1);mem_w16(info+0x22,1);mem_w32(info+0x28,0);mem_w8(info+0x2c,0);
    original=fixture(80000,0);rival=fixture(90000,120);
    std::memcpy(mem_ptr(object+pad_offset),original.data(),record_size);mem_w32(object+type_offset,2);
    std::memcpy(mem_ptr(record),original.data(),record_size);
    mem_w32(app+0xc,manager);mem_w32(manager+0x18,controller);mem_w32(controller+0xc,0);
    std::string error;check(native_session().enter(true,error),error.c_str());
    check(native_session().choose(36,error),error.c_str());
    if(with_ghost)check(native_session().ghost(rival,error),error.c_str());
    retry_overwrote=false;
}
void start(){
    auto cpu=call(object,36);hle_available__9GhostFileF9ECourseID(&cpu);
    check(cpu.r[3]==1,"native available result lost");
    check(equals(object+pad_offset,rival),"constructor did not see downloaded kart/team");
    cpu=call(recorder,1,record);hle_fn_801DC794(&cpu);
    check(equals(record,rival),"private playback record not replaced");
    check(equals(object+pad_offset,original) && mem_r32(object+type_offset)==2,"personal ghost was changed");
    check(mem_r32(recorder+0xf0)==record,"native recorder slot lost");
}
void finish(unsigned time){
    const auto value=fixture(time,8);std::memcpy(mem_ptr(0x80510000),value.data(),record_size);
    auto cpu=call(object,0x80510000);hle_updateKartPadRecord__9GhostFileFRC13KartPadRecord(&cpu);
    check(native_session().result() && native_session().result_info().time_ms==time,"completed run not captured");
    check(last_result==value,"retail result handling changed");
}
}
bool input_replay_active(){return replay;}
extern "C" void orig_available__9GhostFileF9ECourseID(CPU* cpu){
    ++available_calls;const auto at=cpu->r[3];
    cpu->r[3]=mem_r32(at+type_offset)==2 && mem_r8(at+pad_offset+3)==cpu->r[4];
    if(!cpu->r[3])mem_w32(at+type_offset,0);
}
extern "C" void orig_fn_801DC794(CPU* cpu){
    ++set_calls;mem_w32(cpu->r[3]+0xec+cpu->r[4]*4,cpu->r[5]);
}
extern "C" void orig_updateKartPadRecord__9GhostFileFRC13KartPadRecord(CPU* cpu){
    ++result_calls;std::memcpy(last_result.data(),mem_ptr(cpu->r[4]),record_size);
    // Emulate a new personal best independently from the downloaded race copy.
    if(be32(last_result,8)<mem_r32(object+pad_offset+8)){
        std::memcpy(mem_ptr(object+pad_offset),last_result.data(),record_size);mem_w32(object+type_offset,3);
    }
    cpu->r[3]=0x5a;
}
extern "C" void orig_ctrlRace__7RaceAppFv(CPU* cpu){
    ++race_calls;
    if(mem_r32(controller+0xc)==4 && mem_r32(object+type_offset)==3){
        std::memcpy(mem_ptr(record),mem_ptr(object+pad_offset),record_size);retry_overwrote=true;
    }
    cpu->r[3]=0x6b;
}
int main()try{
    std::vector<uint8_t> ram(RAM_SIZE);g_mem=ram.data();
    for(const auto* key:{"TANDEM_ENGINE_CC","TANDEM_COURSE_INDEX","GCN_COURSE_OVERLAY","MKDD_NET_LISTEN","MKDD_NET_CONNECT"})clear_env(key);
    setup();native_session().leave();const auto untouched=ram;
    auto cpu=call(object,36);hle_available__9GhostFileF9ECourseID(&cpu);
    check(ram==untouched && cpu.r[3]==1,"disabled feature changed guest state");
    setup();start();mem_w32(controller+0xc,4);cpu=call(app);hle_ctrlRace__7RaceAppFv(&cpu);
    check(native_session().phase()==Session::Phase::Racing && !native_session().result() && equals(record,rival),"retry during unfinished run lost rival");
    finish(95000);
    check(equals(object+pad_offset,original),"slower uploaded run replaced personal best");
    mem_w32(controller+0xc,4);cpu=call(app);hle_ctrlRace__7RaceAppFv(&cpu);
    check(!native_session().result() && native_session().phase()==Session::Phase::Racing && cpu.r[3]==0x6b,"retry did not re-arm recording");
    finish(75000);const auto best=last_result;
    check(equals(object+pad_offset,best) && mem_r32(object+type_offset)==3,"new personal best lost");
    cpu=call(app);hle_ctrlRace__7RaceAppFv(&cpu);
    check(!retry_overwrote && equals(record,rival),"retry replaced selected rival");
    check(equals(object+pad_offset,best) && mem_r32(object+type_offset)==3,"retry damaged saved personal best");
    finish(85000); // Not a personal best, still a distinct upload opportunity.
    mem_w32(controller+0xc,5);cpu=call(app);hle_ctrlRace__7RaceAppFv(&cpu);
    check(native_session().result() && native_session().phase()==Session::Phase::Menu,"watching a replay discarded the completed recording");
    const auto replay_serial=native_session().result_serial();
    cpu=call(object,record);hle_updateKartPadRecord__9GhostFileFRC13KartPadRecord(&cpu);
    check(native_session().result_serial()==replay_serial,"replay captured a new uploadable result");
    setup();cpu=call(object,36);hle_available__9GhostFileF9ECourseID(&cpu);
    native_menu_boundary();check(equals(object+pad_offset,original),"interrupted constructor did not restore personal ghost");
    check(native_session().phase()==Session::Phase::Menu,"canceled race stayed active");
    setup();replay=true;cpu=call(object,36);hle_available__9GhostFileF9ECourseID(&cpu);
    check(equals(object+pad_offset,original) && native_session().phase()==Session::Phase::Menu,"input replay entered ranked recording");replay=false;
    for(unsigned gate=0;gate<4;++gate){
        setup();if(gate==0)mem_w32(info+8,2);if(gate==1)mem_w32(info+0x10,1);if(gate==2)mem_w16(info+0x20,2);if(gate==3)mem_w8(info+0x2c,1);
        cpu=call(object,36);hle_available__9GhostFileF9ECourseID(&cpu);
        check(equals(object+pad_offset,original) && native_session().phase()==Session::Phase::Menu,"nonstandard race admitted");
    }
    setup(false);cpu=call(object,36);hle_available__9GhostFileF9ECourseID(&cpu);
    check(equals(object+pad_offset,original) && native_session().phase()==Session::Phase::Racing,"solo recording did not start");finish(95000);
    native_menu_boundary();check(native_session().result()!=nullptr,"menu return lost completed upload opportunity");
    check(available_calls && set_calls && result_calls && race_calls,"retail hooks not delegated");
    std::puts("PASS native ghost hooks: default stock, selected rival, personal-best preservation, slower uploads, retry after finish/abort, replay exclusion, rule gates and canceled constructor restoration");
    return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
