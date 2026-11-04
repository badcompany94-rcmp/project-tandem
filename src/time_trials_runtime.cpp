// Native Time Trial recording/playback adapter. No disk/network calls in hooks.
// UI arms the session only after build/content/rule and download checks.
#include "time_trials_runtime.h"
#include "runtime.h"
#include "desktop_engine.h"
#include "input_log.h"
#include <cstring>
extern "C" {
void orig_available__9GhostFileF9ECourseID(CPU*);
void orig_fn_801DC794(CPU*);
void orig_updateKartPadRecord__9GhostFileFRC13KartPadRecord(CPU*);
void orig_ctrlRace__7RaceAppFv(CPU*);
}
namespace mkdd::trials {
namespace {
Session session;
constexpr unsigned pad_offset=0x1460,type_offset=0xa048;
Record original_record{};
uint32_t original_object=0,original_type=0;
uint32_t race_ghost_object=0;
bool valid(uint32_t at,unsigned size){return at>=0x80000000 && uint64_t(at)+size<=0x81800000 && !(at&3);}
bool eligible(){
#ifdef TANDEM_PLUS_PROBE
    return false;
#else
    constexpr uint32_t info=0x803b145c;
    return desktop_engine::requested()==0 && !input_replay_active() &&
        !getenv("TANDEM_COURSE_INDEX") && !getenv("GCN_COURSE_OVERLAY") &&
        !getenv("MKDD_NET_LISTEN") && !getenv("MKDD_NET_CONNECT") &&
        mem_r32(info+8)==1 && mem_r32(info+0x10)==2 &&
        // Retail TT counts the three ghosts as player-controlled karts. There
        // are FOUR kart/player slots, but one console and one scored human.
        mem_r16(info+0x1c)==4 && mem_r16(info+0x1e)==4 &&
        mem_r16(info+0x20)==1 && mem_r16(info+0x22)==1 &&
        !mem_r8(info+1) && !mem_r32(info+0x28) && !mem_r8(info+0x2c);
#endif
}
void restore_personal_ghost(){
    if(!original_object)return;
    std::memcpy(mem_ptr(original_object+pad_offset),original_record.data(),record_size);
    mem_w32(original_object+type_offset,original_type);original_object=0;
}
}
Session& native_session(){return session;}
void native_menu_boundary(){
    restore_personal_ghost();
    race_ghost_object=0;
    if(session.phase()==Session::Phase::Racing)session.cancel();
}
}
extern "C" void hle_available__9GhostFileF9ECourseID(CPU* cpu){
    using namespace mkdd::trials;
    const auto object=cpu->r[3],course=cpu->r[4];
    if(session.phase()==Session::Phase::Menu && eligible() && session.course()!=course){
        std::string error;session.choose(course,error);
    }
    if(session.phase()!=Session::Phase::Inactive && eligible() && session.begin(course,true)){
        race_ghost_object=object;
        if(!session.ghost()){orig_available__9GhostFileF9ECourseID(cpu);return;}
        restore_personal_ghost();
        if(!valid(object,type_offset+4)){session.cancel();orig_available__9GhostFileF9ECourseID(cpu);return;}
        original_object=object;original_type=mem_r32(object+type_offset);
        std::memcpy(original_record.data(),mem_ptr(object+pad_offset),record_size);
        std::memcpy(mem_ptr(object+pad_offset),session.ghost()->data(),record_size);
        mem_w32(object+type_offset,2);
    }
    orig_available__9GhostFileF9ECourseID(cpu);
}
extern "C" void hle_fn_801DC794(CPU* cpu){
    using namespace mkdd::trials;
    // RaceApp copies the old personal record BEFORE asking available(). By the
    // time slot 1 is assigned it has selected the downloaded kart/characters;
    // replace that private copy too, then put the personal record back intact.
    if(original_object && cpu->r[4]==1 && session.phase()==Session::Phase::Racing && session.ghost()){
        if(valid(cpu->r[5],record_size))std::memcpy(mem_ptr(cpu->r[5]),session.ghost()->data(),record_size);
        else session.cancel();
        restore_personal_ghost();
    }
    orig_fn_801DC794(cpu);
}
extern "C" void hle_updateKartPadRecord__9GhostFileFRC13KartPadRecord(CPU* cpu){
    using namespace mkdd::trials;
    restore_personal_ghost();
    if(session.phase()==Session::Phase::Racing && eligible() && valid(cpu->r[4],record_size)){
        std::string error;
        // This boundary occurs for every completed Time Trial, before retail
        // decides whether it is a new personal best. UI offers upload later.
        if(!session.finish({static_cast<const uint8_t*>(mem_ptr(cpu->r[4])),record_size},error))session.cancel();
    }
    orig_updateKartPadRecord__9GhostFileFRC13KartPadRecord(cpu);
}
extern "C" void hle_ctrlRace__7RaceAppFv(CPU* cpu){
    using namespace mkdd::trials;
#ifdef TANDEM_TIME_TRIAL_PROBE
    extern void time_trial_probe_race(uint32_t);
    if(session.result())time_trial_probe_race(cpu->r[3]);
#endif
    // Result controls live at PauseManager's persistent Result2D boundary.
    // RaceApp must keep advancing its normal finish/retry/quit transitions.
#ifdef TANDEM_TIME_TRIAL_PROBE
    const auto probe_app=cpu->r[3];
#endif
    uint32_t saved_type=0;
    bool restore_type=false;
    if(session.phase()!=Session::Phase::Inactive && race_ghost_object && eligible() && valid(cpu->r[3],0x10)){
        const auto manager=mem_r32(cpu->r[3]+0xc);
        const auto controller=valid(manager,0x1c)?mem_r32(manager+0x18):0;
        const auto phase=valid(controller,0x10)?mem_r32(controller+0xc):0;
        if(phase==4){ // Native "Retry", including restarting an unfinished run.
            session.restart(session.course(),true);
            if(session.ghost() && valid(race_ghost_object,type_offset+4)){
                saved_type=mem_r32(race_ghost_object+type_offset);
                // Retail replaces the SAME in-race buffer with the new personal
                // best on Retry and disables the previous rival. Keep the chosen
                // rival for this race; retain the personal best in GhostFile.
                if(saved_type==3){mem_w32(race_ghost_object+type_offset,2);restore_type=true;}
            }
        }else if(phase==5){ // Replay cannot record a new run; preserve the completed one.
            session.replay();
        }
    }
    orig_ctrlRace__7RaceAppFv(cpu);
    if(restore_type)mem_w32(race_ghost_object+type_offset,saved_type);
#ifdef TANDEM_TIME_TRIAL_PROBE
    extern void time_trial_probe_race(uint32_t);
    time_trial_probe_race(probe_app);
#endif
}
