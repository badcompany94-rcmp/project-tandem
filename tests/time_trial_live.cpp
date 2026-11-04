// Explicit private service check; never run by default and never linked to game.
#include "time_trial_client.h"
#include "online_json.h"
#include <fstream>
#include <iostream>
#include <iterator>
using namespace mkdd::trials;
std::string key(const char* path){std::ifstream f(path);return mkdd::online::Json::parse(std::string(std::istreambuf_iterator<char>(f),{})).at("key").str();}
int main(int argc,char** argv)try{
    if(argc!=6 && argc!=7)throw std::runtime_error("usage: time_trial_live upload-key-file download-key-file build ghost-file output-file [public]");
    const bool public_scope=argc==7 && std::string(argv[6])=="public";
    std::ifstream f(argv[4],std::ios::binary);std::vector<uint8_t> bytes(std::istreambuf_iterator<char>(f),{});
    Record record;RecordInfo info;std::string error;if(!decode(bytes,record,info,error))throw std::runtime_error(error);
    TrialRequest q;q.action=TrialAction::Upload;q.course=info.course;q.record=record;q.display_name="VERIFY";
    auto upload=trial_transfer(q,key(argv[1]),argv[3],trial_https,[]{return false;},public_scope);if(!upload.error.empty())throw std::runtime_error(upload.error);
    q.action=TrialAction::List;auto board=trial_transfer(q,key(argv[2]),argv[3],trial_https,[]{return false;},public_scope);if(!board.error.empty())throw std::runtime_error(board.error);
    bool found=false;for(const auto& entry:board.entries)if(entry.id==upload.id){if(entry.name!=q.display_name)throw std::runtime_error("Service did not use the latest display name");q.entry=entry;found=true;break;}
    if(!found)throw std::runtime_error("Uploaded record missing from second tester's board");
    q.action=TrialAction::Download;auto ghost=trial_transfer(q,key(argv[2]),argv[3],trial_https,[]{return false;},public_scope);if(!ghost.error.empty())throw std::runtime_error(ghost.error);
    if(ghost.record!=record)throw std::runtime_error("Downloaded inputs differ");
    std::ofstream out(argv[5],std::ios::binary);out.write(reinterpret_cast<const char*>(ghost.record.data()),ghost.record.size());out.close();if(!out)throw std::runtime_error("Cannot save verification ghost");
    std::cout<<"PASS live HTTPS: tester A uploaded, tester B listed and downloaded identical canonical ghost; "<<info.time_ms<<" ms\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
