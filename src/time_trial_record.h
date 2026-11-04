#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>

namespace mkdd::trials {
constexpr unsigned record_size=0x8bb8, max_frames=17864, input_offset=40;
using Record=std::array<uint8_t,record_size>;
struct RecordInfo {
    unsigned course=0,kart=0,driver=0,partner=0,time_ms=0,frames=0,lap_count=0;
    std::array<unsigned,6> splits{};
};
inline unsigned laps(unsigned course){
    switch(course){
    case 33:return 7;case 43:return 2;
    case 36:case 34:case 50:case 40:case 37:case 35:case 42:case 51:
    case 41:case 38:case 45:case 44:case 47:case 49:return 3;
    default:return 0;
    }
}
inline uint32_t be32(std::span<const uint8_t> b,unsigned offset){
    return uint32_t(b[offset])<<24|uint32_t(b[offset+1])<<16|uint32_t(b[offset+2])<<8|b[offset+3];
}
// Validate before any bytes can reach guest memory. Failure preserves outputs.
// The final stored frame is inclusive. Copy only initialized input/split data.
inline bool decode(std::span<const uint8_t> bytes,Record& output,RecordInfo& info,std::string& error){
    error.clear();
    if(bytes.size()!=record_size){error="Invalid ghost size.";return false;}
    RecordInfo next;next.driver=bytes[0];next.partner=bytes[1];next.kart=bytes[2];next.course=bytes[3];next.lap_count=laps(next.course);
    if(!next.lap_count || next.driver<1 || next.driver>20 || next.partner<1 || next.partner>20 || next.driver==next.partner || next.kart>20){error="Invalid ghost course or team.";return false;}
    const auto last=be32(bytes,12);next.time_ms=be32(bytes,8);
    if(last==0 || last>=max_frames){error="Ghost recording is incomplete or too long.";return false;}
    next.frames=last+1;
    if(!next.time_ms || next.time_ms>next.frames*1000/60+1){error="Invalid ghost finish time.";return false;}
    unsigned prior=0;
    for(unsigned i=0;i<next.lap_count-1;++i){
        next.splits[i]=be32(bytes,16+4*i);
        if(next.splits[i]<=prior || next.splits[i]>=next.time_ms){error="Invalid ghost lap times.";return false;}
        prior=next.splits[i];
    }
    for(unsigned i=0;i<next.frames;++i){
        const auto stick=bytes[input_offset+i*2];
        if((stick>>3)==16 || (stick&7)==4){error="Invalid ghost controller data.";return false;}
    }
    Record clean{};
    std::copy_n(bytes.begin(),16+4*(next.lap_count-1),clean.begin());
    std::fill_n(clean.begin()+4,4,0); // Display name belongs to the authenticated profile.
    std::copy_n(bytes.begin()+input_offset,next.frames*2,clean.begin()+input_offset);
    output=clean;info=next;return true;
}

// Owned by the guest/menu thread. Workers return immutable downloads to that
// thread; they never mutate a running race or touch guest memory.
class Session {
public:
    enum class Phase {Inactive,Menu,Racing,Result};
    bool enter(bool stock_rules,std::string& error){
        if(phase_==Phase::Racing){error="Return to the main menu first.";return false;}
        if(!stock_rules){error="Online Time Trials currently requires original courses and standard 150cc rules.";return false;}
        phase_=Phase::Menu;course_=0;has_ghost_=has_result_=false;error.clear();return true;
    }
    bool choose(unsigned course,std::string& error){
        if(phase_!=Phase::Menu && phase_!=Phase::Result){error="Return to course selection first.";return false;}
        if(!laps(course)){error="Unsupported Time Trial course.";return false;}
        if(course_!=course)has_ghost_=false;
        course_=course;has_result_=false;phase_=Phase::Menu;error.clear();return true;
    }
    bool ghost(std::span<const uint8_t> bytes,std::string& error){
        if(phase_!=Phase::Menu || !course_){error="Choose a course before downloading a ghost.";return false;}
        Record next;RecordInfo info;
        if(!decode(bytes,next,info,error))return false;
        if(info.course!=course_){error="This ghost belongs to another course.";return false;}
        ghost_=next;has_ghost_=true;return true;
    }
    bool begin(unsigned course,bool stock_rules){
        if((phase_!=Phase::Menu && phase_!=Phase::Result) || course_!=course || !stock_rules)return false;
        has_result_=false;phase_=Phase::Racing;return true;
    }
    bool restart(unsigned course,bool stock_rules){
        if(phase_==Phase::Inactive || course_!=course || !stock_rules)return false;
        has_result_=false;phase_=Phase::Racing;return true;
    }
    bool finish(std::span<const uint8_t> bytes,std::string& error){
        if(phase_!=Phase::Racing){error="No Online Time Trial is running.";return false;}
        Record next;RecordInfo info;
        if(!decode(bytes,next,info,error))return false;
        if(info.course!=course_){error="The finished run belongs to another course.";return false;}
        result_=next;result_info_=info;has_result_=true;++result_serial_;phase_=Phase::Result;return true;
    }
    // A replay is never a new timed run. Retain the already completed recording
    // so returning from Replay can still offer its original upload.
    void replay(){phase_=Phase::Menu;}
    void cancel(){phase_=Phase::Menu;has_result_=false;}
    void leave(){phase_=Phase::Inactive;has_ghost_=has_result_=false;course_=0;}
    Phase phase()const{return phase_;}
    unsigned course()const{return course_;}
    const Record* ghost()const{return has_ghost_?&ghost_:nullptr;}
    const Record* result()const{return has_result_?&result_:nullptr;}
    const RecordInfo& result_info()const{return result_info_;}
    uint64_t result_serial()const{return result_serial_;}
private:
    Phase phase_=Phase::Inactive;
    unsigned course_=0;
    bool has_ghost_=false,has_result_=false;
    uint64_t result_serial_=0;
    Record ghost_{},result_{};RecordInfo result_info_{};
};
}
