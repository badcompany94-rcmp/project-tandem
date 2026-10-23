#include "online_room.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <cstdlib>

// Standalone room-protocol fixture: no game image, graphics or process launch.
namespace mkdd { uint64_t online_build_fingerprint(){return 0x1234;} void online_end_inputs(){} }
void input_log_end() {}
uint64_t host_ns(){return 0;} // timing diagnostics are inactive in this protocol-only fixture
void plat_relaunch(char**) { throw std::runtime_error("process launch is forbidden in the protocol fixture"); }

using mkdd::online::Json;
void require(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
template<class Function>void rejects(Function function) {
    bool failed=false;try{function();}catch(const std::exception&){failed=true;}require(failed,"invalid protocol input accepted");
}
int main(int argc,char** argv) {
    try {
        const std::string input="{\"name\":\"A\\\"B\\n\\uD83D\\uDE80\",\"seed\":4294967295,\"members\":[true,false,null,-9223372036854775808]}";
        auto json=Json::parse(input);
        require(json.at("seed").integer(0,UINT32_MAX)==UINT32_MAX,"seed truncated");
        require(json.at("name").str()=="A\"B\n\xf0\x9f\x9a\x80","Unicode escape decoded incorrectly");
        require(Json::parse(json.dump()).dump()==json.dump(),"JSON round trip changed data");
        for(const char* malformed:{"{\"a\":1,\"a\":2}","[1,]","true false","01","1.2","9223372036854775808","\"\\uD800\"","\"\\uDC00\"","{\"a\":}"})
            rejects([&]{Json::parse(malformed);});
        rejects([&]{Json::parse(std::string(65537,' '));});
        rejects([&]{Json::parse(std::string(26,'[')+"0"+std::string(26,']'));});
        if(argc==3 && std::string(argv[1])=="--manifest") {
            std::ifstream file(argv[2],std::ios::binary);require(bool(file),"missing fixture manifest");
            Json response=Json::parse(std::string(std::istreambuf_iterator<char>(file),{}));
            const std::string build=response.at("room").at("launch").at("compatibility").str();
            auto env=mkdd::online::launch_environment(response,build);
            require(env.at("MKDD_NET_PLAYERS")=="8","wrong player count");
            require(env.at("MKDD_NET_SEATS")=="70123456","reserved seats changed");
            require(env.at("MKDD_NET_ENGINE")=="mirror","engine class lost");
            require(env.at("MKDD_NET_CUP")=="flower","cup lost");
            auto mesh=response;mesh.object["connection"].object["mesh"]=true;
#ifdef _WIN32
            _putenv_s("MKDD_EXPECT_PREDICTION","1");
            bool refused_fallback=false;
            try{mkdd::online::enter_race(mesh);}catch(const std::exception& e){refused_fallback=std::string(e.what())=="rollback-not-enabled";}
            _putenv_s("MKDD_EXPECT_PREDICTION","");
            require(refused_fallback,"normal online launch silently fell back without negotiated rollback");
#endif
            auto mesh_env=mkdd::online::launch_environment(mesh,build);
            require(mesh_env.at("MKDD_NET_MESH")=="1" && mesh_env.count("MKDD_NET_CONNECT") && !mesh_env.count("MKDD_NET_LISTEN"),"host did not join its local mesh adapter");
            mesh.object["connection"].object["prediction"]=true;
            auto predictive=mkdd::online::launch_environment(mesh,build);
            require(predictive.at("MKDD_NET_PREDICTION")=="1" && predictive.at("GCN_WRITE_WATCH")=="1" &&
                    predictive.at("MKDD_PREDICTION_START")=="120","rollback launch configuration missing");
            mesh.object["connection"].object["mesh"]=false;
            rejects([&]{mkdd::online::launch_environment(mesh,build);});
            auto unlocked=response;
            auto& launch=unlocked.object["room"].object["launch"];
            launch.object["rules"].object["course"]=42;
            require(mkdd::online::launch_environment(unlocked,build).at("MKDD_NET_COURSE")=="42","online course choice lost");
            launch.object["rules"].object["course"]=49;
            rejects([&]{mkdd::online::launch_environment(unlocked,build);});
            launch.object["mode"]="balloon";launch.object["cpu_karts"]=0;
            for(int arena:{52,53,54,56,58,59}) {
                launch.object["rules"]=Json::dict({{"arena",arena},{"rounds",3}});
                require(mkdd::online::launch_environment(unlocked,build).at("MKDD_NET_ARENA")==std::to_string(arena),"online arena choice lost");
            }
            for(int arena:{55,57}) {
                launch.object["rules"].object["arena"]=arena;
                rejects([&]{mkdd::online::launch_environment(unlocked,build);});
            }
            auto wrong=response;wrong.object["connection"].object["slot"]=1;
            rejects([&]{mkdd::online::launch_environment(wrong,build);});
            wrong=response;wrong.object["connection"].object["endpoint"]="192.0.2.1:28121";
            rejects([&]{mkdd::online::launch_environment(wrong,build);});
            wrong=response;wrong.object["room"].object["launch"].object["roster"].array[0].object["game_seat"]=0;
            rejects([&]{mkdd::online::launch_environment(wrong,build);});
            wrong=response;wrong.object["room"].object["launch"].object["cpu_karts"]=7;
            rejects([&]{mkdd::online::launch_environment(wrong,build);});
            rejects([&]{mkdd::online::launch_environment(response,"other-build");});
        } else if(argc==2 && std::string(argv[1])=="--http") {
            Json created=mkdd::online::room_request(Json::dict({{"op","create"},{"name","Native fixture"},{"compatibility","fixture-build"}}));
            std::string token=created.at("token").str();
            Json state=mkdd::online::room_request(Json::dict({{"op","state"},{"token",token}}));
            auto ready=mkdd::online::room_request(Json::dict({{"op","ready"},{"token",token},
                {"revision",state.at("room").at("revision")},{"ready",true}}));
            require(ready.at("room").at("members").list()[0].at("ready").boolean(),"native ready operation failed");
            mkdd::online::room_request(Json::dict({{"op","leave"},{"token",token}}));
        }
        puts("PASS: bounded native JSON, Unicode, integer limits, invalid inputs and requested room integration fixture");
        return 0;
    } catch(const std::exception& error){fprintf(stderr,"FAIL: %s\n",error.what());return 1;}
}
