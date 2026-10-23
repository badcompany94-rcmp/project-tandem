#include "eos_protocol.h"
#include <cstdio>

void require(bool ok){if(!ok)throw std::runtime_error("EOS control framing assertion failed");}
template<class F>void rejects(F f){bool rejected=false;try{f();}catch(const std::exception&){rejected=true;}require(rejected);}
int main() {
    try {
        using namespace mkdd::eos;
        for(size_t n:{1u,999u,1000u,1001u,65536u}) {
            const std::string body(n,'x');Reassembler receiver;
            auto packets=fragments(1,0x80000003,body);
            for(size_t i=0;i<packets.size();++i) {
                auto message=receiver.add("peer-a",packets[i]);
                require(bool(message)==(i+1==packets.size()));
                if(message)require(message->body==body && message->id==0x80000003 && message->kind==1);
            }
        }
        rejects([]{fragments(1,0,"body");});rejects([]{fragments(3,1,"body");});
        rejects([]{fragments(1,1,std::string(65537,'x'));});
        auto p=fragments(2,17,std::string(1500,'z'));
        Reassembler receiver;require(!receiver.add("peer-a",p[0]));
        rejects([&]{receiver.add("peer-b",p[1]);});
        auto completed=receiver.add("peer-a",p[1]);require(completed && completed->body.size()==1500);
        rejects([&]{receiver.add("peer-a",p[1]);});
        require(!receiver.add("peer-a",p[0]));rejects([&]{receiver.add("peer-a",p[0]);});
        for(auto change:{0,2,3,8,10}) {
            auto invalid=p[0];invalid[change]=255;
            rejects([&]{Reassembler r;r.add("peer-a",invalid);});
        }
        for(unsigned i=1;i<=32;++i)require(!receiver.add("peer-a",fragments(1,i,std::string(1500,'x'))[0]));
        rejects([&]{receiver.add("peer-a",fragments(1,33,std::string(1500,'x'))[0]);});
        puts("PASS: EOS control framing boundaries, peer isolation, ordering and bounded reassembly");return 0;
    }catch(const std::exception& e){fprintf(stderr,"FAIL: %s\n",e.what());return 1;}
}
