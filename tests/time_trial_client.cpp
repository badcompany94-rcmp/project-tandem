#include "time_trial_client.h"
#include "online_json.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace mkdd::trials;
using mkdd::online::Json;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void word(Record& r,unsigned at,unsigned n){r[at]=n>>24;r[at+1]=n>>16;r[at+2]=n>>8;r[at+3]=n;}
Record ghost(){Record r{};r[0]=7;r[1]=8;r[3]=36;word(r,8,3000);word(r,12,359);word(r,16,1000);word(r,20,2000);return r;}
TrialHttpResponse json_reply(const Json& value){auto text=value.dump();return {200,{},std::vector<uint8_t>(text.begin(),text.end())};}
int main()try{
 const std::string key(43,'a'),build(16,'b'),id(64,'c');const auto record=ghost();const auto hash=ghost_digest(record);
 const auto not_cancelled=[] {return false;};TrialRequest request;request.course=36;
 Json rows;rows.kind=Json::Array;rows.array.push_back(Json::dict({{"id",id},{"name","Johnny"},{"digest",hash},{"time_ms",3000}}));
 auto board=Json::dict({{"course",36},{"build",build},{"verification","record-checked"},{"runs",rows}});
 auto transfer=[&](TrialHttpResponse reply){return trial_transfer(request,key,build,[&](const TrialHttpRequest& http,const auto&){require(http.key==key && http.build==build,"Request lost identity");return reply;},not_cancelled);};
 require(transfer(json_reply(board)).entries.size()==1,"Valid board rejected");
 require(trial_transfer(request,key,build,[&](const TrialHttpRequest& h,const auto&){require(h.public_scope,"Public scope lost");return json_reply(board);},not_cancelled,true).entries.size()==1,"Public board rejected");
 require(trial_transfer(request,key,build,[&](const TrialHttpRequest& h,const auto&){require(!h.public_scope,"Private request became public");return json_reply(board);},not_cancelled).entries.size()==1,"Private board rejected");
 board.object["build"]=std::string(16,'d');require(!transfer(json_reply(board)).error.empty(),"Wrong build accepted");board.object["build"]=build;
 board.object["runs"].array.push_back(rows.array[0]);require(!transfer(json_reply(board)).error.empty(),"Duplicate ID accepted");board.object["runs"]=rows;
 request.action=TrialAction::Download;request.entry={id,"Johnny",hash,3000};TrialHttpResponse download{200,hash,std::vector<uint8_t>(record.begin(),record.end())};
 auto accepted=transfer(download);require(accepted.error.empty() && accepted.record==record,"Valid download changed");
 download.body[80]^=1;require(!transfer(download).error.empty(),"Damaged download accepted");download.body.assign(record.begin(),record.end());
 request.entry.time_ms=3001;require(!transfer(download).error.empty(),"Wrong finish accepted");request.entry.time_ms=3000;
 download.body.pop_back();require(!transfer(download).error.empty(),"Truncated download accepted");
 request.action=TrialAction::Upload;request.record=record;request.display_name="NEWNAME";
 auto receipt=Json::dict({{"id",id},{"digest",hash},{"course",36},{"time_ms",3000},{"verification","record-checked"}});
 require(trial_transfer(request,key,build,[&](const TrialHttpRequest& http,const auto&){require(http.display_name=="NEWNAME","Upload lost current name");return json_reply(receipt);},not_cancelled).error.empty(),"Named upload rejected");
 for(auto invalid:{"","TooLong8","A\r\nX:E","<NAME>","lower"}){request.display_name=invalid;bool sent=false;
   require(!trial_transfer(request,key,build,[&](const auto&,const auto&){sent=true;return json_reply(receipt);},not_cancelled).error.empty() && !sent,"Invalid name reached transport");}
 request.display_name="NEWNAME";
 require(transfer(json_reply(receipt)).error.empty(),"Valid receipt rejected");receipt.object["digest"]=std::string(64,'e');require(!transfer(json_reply(receipt)).error.empty(),"Wrong receipt accepted");
 for(auto status:{401u,403u,404u,409u,429u,503u,302u})require(!transfer({status,{},{}}).error.empty(),"Failure or redirect accepted");
 require(!transfer({200,{},std::vector<uint8_t>(65537)}).error.empty(),"Oversized response accepted");
 require(!trial_transfer(request,key,build,[](const auto&,const auto&)->TrialHttpResponse{throw std::runtime_error("interrupted");},not_cancelled).error.empty(),"Interrupted transfer accepted");
 bool called=false;require(!trial_transfer(request,key,build,[&](const auto&,const auto&){called=true;return TrialHttpResponse{};},[]{return true;}).error.empty() && !called,"Cancelled request was sent");
 // Hold a real worker at the transport boundary. Cancel must return without
 // joining, and an in-flight completion must never reappear in the next menu.
 const auto path=std::filesystem::temp_directory_path()/"tandem-trial-client-fixture.json";
 struct Cleanup{std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove(path,ec);}} cleanup{path};
 {std::ofstream out(path);out<<Json::dict({{"key",key}}).dump();}
 std::atomic<bool> entered=false,release=false;
 TrialClient client([&](const auto&,const auto&){entered=true;const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!release.load() && std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));return json_reply(receipt);});
 require(client.submit(request,path.string(),build),"Worker refused first job");
 const auto timeout=std::chrono::steady_clock::now()+std::chrono::seconds(3);
 while(!entered.load() && std::chrono::steady_clock::now()<timeout)std::this_thread::sleep_for(std::chrono::milliseconds(1));
 if(!entered){release=true;throw std::runtime_error("Worker did not start");}
 require(!client.submit(request,path.string(),build),"Worker accepted overlapping jobs");
 const auto begin=std::chrono::steady_clock::now();client.cancel();const bool fast=std::chrono::steady_clock::now()-begin<std::chrono::milliseconds(50);release=true;require(fast,"Cancel blocked the menu");
 while(client.busy() && std::chrono::steady_clock::now()<timeout)std::this_thread::sleep_for(std::chrono::milliseconds(1));require(!client.busy() && !client.take(),"Late cancelled result escaped");
 require(client.submit(request,path.string()+".missing",build),"Worker did not recover after cancel");
 std::optional<TrialResponse> missing;while(!(missing=client.take()) && std::chrono::steady_clock::now()<timeout)std::this_thread::sleep_for(std::chrono::milliseconds(1));require(missing && !missing->error.empty(),"Missing config accepted");
 std::cout<<"PASS: bounded board/ghost/receipt validation, failures, cancellation and worker recovery\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}
