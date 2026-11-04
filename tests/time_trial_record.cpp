#include "time_trial_record.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>
using namespace mkdd::trials;
static void check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static void put(Record& r,unsigned at,unsigned value){for(unsigned i=0;i<4;++i)r[at+i]=uint8_t(value>>(24-8*i));}
static Record fixture(unsigned course=36,unsigned total=90000){
    Record r{};r[0]=7;r[1]=8;r[2]=0;r[3]=uint8_t(course);put(r,8,total);put(r,12,6000);
    for(unsigned i=0;i<laps(course)-1;++i)put(r,16+4*i,total*(i+1)/laps(course));
    for(unsigned i=0;i<6001;++i){r[40+2*i]=120;r[41+2*i]=1;}return r;
}
int main(int argc,char** argv)try{
    std::string error;Record decoded;RecordInfo info;const auto source=fixture();
    check(decode(source,decoded,info,error),error.c_str());check(info.frames==6001 && info.time_ms==90000,"inclusive final input");
    auto bad=source;put(bad,12,max_frames);const auto saved=decoded;
    check(!decode(bad,decoded,info,error) && decoded==saved,"overlong recording preserved output");
    bad=source;bad[40]=128;check(!decode(bad,decoded,info,error),"bad stick accepted");
    for(auto course:{33u,43u}){check(decode(fixture(course),decoded,info,error),error.c_str());check(info.lap_count==laps(course),"stock lap count");}
    Session session;
    check(!session.enter(false,error) && session.phase()==Session::Phase::Inactive,"nonstandard rules admitted");
    check(session.enter(true,error) && session.choose(36,error) && session.ghost(source,error),error.c_str());
    check(!session.ghost(fixture(34),error) && session.ghost()!=nullptr,"wrong course replaced valid ghost");
    check(session.begin(36,true),"begin");check(!session.choose(34,error) && !session.ghost(source,error),"running race changed");
    check(session.finish(source,error) && session.result(),"completed run lost");
    check(session.begin(36,true) && !session.result(),"retry retained old upload result");
    check(session.finish(fixture(36,95000),error) && session.result_info().time_ms==95000,"slower race not offered for upload");
    check(session.choose(34,error) && !session.ghost() && !session.result(),"course switch retained incompatible ghost/result");
    check(session.begin(34,true),"new course");session.cancel();check(!session.result(),"aborted run uploadable");session.leave();
    for(int i=1;i<argc;++i){
        std::ifstream in(argv[i],std::ios::binary);std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)),{});
        check(decode(data,decoded,info,error),error.c_str());
        std::printf("Real ghost course=%u laps=%u frames=%u time_ms=%u\n",info.course,info.lap_count,info.frames,info.time_ms);
    }
    std::puts("PASS native ghost validation and session: stock rules, course matching, immutable active run, every completed result, retry/cancel, inclusive frame bounds and variable lap counts");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}
