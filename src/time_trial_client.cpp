#include "time_trial_client.h"
#include "online_json.h"
#include <chrono>
#include <fstream>
#include <set>
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#endif
namespace mkdd::trials {
namespace {
bool hex(const std::string& value,size_t length){return value.size()==length && value.find_first_not_of("0123456789abcdef")==std::string::npos;}
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool key_valid(const std::string& key){return key.size()==43 && key.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos;}
std::string failure(unsigned code){
    switch(code){case 401:return "Your trial identity is not valid.";case 403:return "Trial access is not available.";
    case 404:return "That ghost is no longer available. Refresh the list.";case 409:return "Update to a supported game build to use this leaderboard.";
    case 429:return "Upload limit reached. Wait a minute and try again.";case 503:return "The trial service is temporarily unavailable.";
    default:return "The trial service could not complete this request. Try again.";}
}
}
std::string ghost_digest(std::span<const uint8_t> bytes){
#ifdef _WIN32
    struct Algorithm {BCRYPT_ALG_HANDLE value=nullptr;~Algorithm(){if(value)BCryptCloseAlgorithmProvider(value,0);}} algorithm;
    check(BCryptOpenAlgorithmProvider(&algorithm.value,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"Cannot initialize ghost verification.");
    struct Hash {BCRYPT_HASH_HANDLE value=nullptr;~Hash(){if(value)BCryptDestroyHash(value);}} operation;
    check(BCryptCreateHash(algorithm.value,&operation.value,nullptr,0,nullptr,0,0)>=0,"Cannot initialize ghost hash.");
    uint8_t hash[32]{};
    check(BCryptHashData(operation.value,const_cast<PUCHAR>(bytes.data()),ULONG(bytes.size()),0)>=0 && BCryptFinishHash(operation.value,hash,sizeof(hash),0)>=0,"Cannot verify ghost integrity.");
    std::string result;for(auto b:hash){result+="0123456789abcdef"[b>>4];result+="0123456789abcdef"[b&15];}return result;
#else
    throw std::runtime_error("Private trial transfers are not available on this platform yet.");
#endif
}
TrialResponse trial_transfer(const TrialRequest& request,const std::string& key,const std::string& build,const TrialTransport& transport,const std::function<bool()>& cancelled,bool public_scope){
    TrialResponse result;result.action=request.action;result.course=request.course;
    try{
        check(laps(request.course),"Choose an original Time Trial course.");
        check(key_valid(key),"A trial identity is required.");check(hex(build,16),"Invalid game build identity.");
        check(!cancelled(),"Request cancelled.");
        TrialHttpRequest http;http.method="GET";http.path="/api/tandem/ghosts";http.key=key;http.build=build;http.public_scope=public_scope;
        Record canonical{};RecordInfo info;std::string error,expected_hash;
        if(request.action==TrialAction::List)http.path+="?course="+std::to_string(request.course);
        else if(request.action==TrialAction::Download){
            check(hex(request.entry.id,64) && hex(request.entry.digest,64),"Invalid leaderboard entry.");
            http.path+="?id="+request.entry.id;expected_hash=request.entry.digest;
        }else{
            check(!request.display_name.empty() && request.display_name.size()<=7 &&
                request.display_name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")==std::string::npos,
                "Choose an online name with 1 to 7 letters or numbers.");
            http.display_name=request.display_name;
            check(decode(request.record,canonical,info,error),"This run has no complete ghost recording.");
            check(info.course==request.course,"This run belongs to another course.");
            expected_hash=ghost_digest(canonical);http.method="POST";http.body.assign(canonical.begin(),canonical.end());
        }
        auto response=transport(http,cancelled);check(!cancelled(),"Request cancelled.");
        if(response.status!=200)throw std::runtime_error(failure(response.status));
        check(response.body.size()<=65536,"Trial response is too large.");
        if(request.action==TrialAction::Download){
            check(response.digest==expected_hash && ghost_digest(response.body)==expected_hash,"Downloaded ghost failed its integrity check.");
            check(decode(response.body,canonical,info,error),"Downloaded ghost is invalid.");
            check(info.course==request.course && info.time_ms==request.entry.time_ms,"Downloaded ghost does not match the selected run.");
            result.record=canonical;result.id=request.entry.id;return result;
        }
        const auto json=online::Json::parse(std::string(response.body.begin(),response.body.end()));
        check(json.at("course").integer(0,255)==request.course,"Trial response belongs to another course.");
        check(json.at("verification").str()=="record-checked","Unsupported trial verification format.");
        if(request.action==TrialAction::Upload){
            result.id=json.at("id").str();check(hex(result.id,64) && json.at("digest").str()==expected_hash && json.at("time_ms").integer(1,300000)==info.time_ms,"Upload receipt does not match this run.");
        }else{
            check(json.at("build").str()==build,"Leaderboard belongs to another game build.");
            const auto& rows=json.at("runs").list();check(rows.size()<=50,"Leaderboard is too large.");std::set<std::string> ids;
            for(const auto& row:rows){
                TrialEntry entry{row.at("id").str(),row.at("name").str(),row.at("digest").str(),unsigned(row.at("time_ms").integer(1,300000))};
                check(hex(entry.id,64) && hex(entry.digest,64) && ids.insert(entry.id).second,"Invalid leaderboard entry.");
                check(!entry.name.empty() && entry.name.size()<=32 && std::all_of(entry.name.begin(),entry.name.end(),[](unsigned char c){return c>=32 && c<127;}),"Invalid player display name.");
                result.entries.push_back(std::move(entry));
            }
        }
    }catch(const std::exception& e){result.entries.clear();result.id.clear();result.error=std::string(e.what())=="invalid-room-response"?"The trial service returned invalid data.":e.what();}
    return result;
}
TrialHttpResponse trial_https(const TrialHttpRequest& request,const std::function<bool()>& cancelled){
#ifdef _WIN32
    // The test endpoint is fixed; neither downloaded metadata nor a local key
    // file can redirect a credential to a different host. Normal TLS validation.
    struct Handle {HINTERNET value=nullptr;~Handle(){if(value)WinHttpCloseHandle(value);}};
    Handle session{WinHttpOpen(L"ProjectTandem-Trials/1",WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,WINHTTP_NO_PROXY_NAME,WINHTTP_NO_PROXY_BYPASS,0)};
    check(session.value,"Cannot start a secure trial request.");
    check(WinHttpSetTimeouts(session.value,5000,5000,5000,5000),"Cannot set trial timeouts.");
    Handle connection{WinHttpConnect(session.value,L"vowbi.com",INTERNET_DEFAULT_HTTPS_PORT,0)};check(connection.value,"Cannot reach the trial service.");
    const std::wstring method(request.method.begin(),request.method.end()),path(request.path.begin(),request.path.end());
    Handle operation{WinHttpOpenRequest(connection.value,method.c_str(),path.c_str(),nullptr,WINHTTP_NO_REFERER,WINHTTP_DEFAULT_ACCEPT_TYPES,WINHTTP_FLAG_SECURE)};
    check(operation.value,"Cannot open the trial request.");
    DWORD policy=WINHTTP_OPTION_REDIRECT_POLICY_NEVER;check(WinHttpSetOption(operation.value,WINHTTP_OPTION_REDIRECT_POLICY,&policy,sizeof(policy)),"Cannot secure trial redirects.");
    const std::string header="Authorization: Bearer "+request.key+"\r\nX-Tandem-Build: "+request.build+
        "\r\nContent-Type: application/octet-stream\r\n"+
        (request.public_scope?std::string("X-Tandem-Scope: public\r\n"):std::string{})+
        (request.display_name.empty()?std::string{}:"X-Tandem-Name: "+request.display_name+"\r\n");
    const std::wstring headers(header.begin(),header.end());
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(20);
    auto active=[&]{check(!cancelled(),"Request cancelled.");check(std::chrono::steady_clock::now()<deadline,"Trial request timed out. Try again.");};
    active();check(WinHttpSendRequest(operation.value,headers.c_str(),DWORD(headers.size()),request.body.empty()?nullptr:const_cast<uint8_t*>(request.body.data()),DWORD(request.body.size()),DWORD(request.body.size()),0),"Cannot send the trial request. Check your connection.");
    active();check(WinHttpReceiveResponse(operation.value,nullptr),"Trial service did not respond. Try again.");
    TrialHttpResponse result;DWORD status=0,size=sizeof(status);
    check(WinHttpQueryHeaders(operation.value,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,WINHTTP_HEADER_NAME_BY_INDEX,&status,&size,WINHTTP_NO_HEADER_INDEX),"Invalid trial response.");result.status=status;
    wchar_t hash[65]{};size=sizeof(hash);
    if(WinHttpQueryHeaders(operation.value,WINHTTP_QUERY_CUSTOM,L"X-Ghost-SHA256",hash,&size,WINHTTP_NO_HEADER_INDEX))for(wchar_t c:std::wstring(hash))result.digest+=c<=127?char(c):'?';
    uint8_t buffer[4096];DWORD read=0;
    for(;;){active();check(WinHttpReadData(operation.value,buffer,sizeof(buffer),&read),"Trial download was interrupted. Try again.");if(!read)break;
        check(result.body.size()+read<=65536,"Trial response is too large.");result.body.insert(result.body.end(),buffer,buffer+read);}
    return result;
#else
    throw std::runtime_error("Private trial transfers are not available on this platform yet.");
#endif
}
TrialClient::~TrialClient(){
    {std::lock_guard lock(mutex_);stop_=true;++generation_;job_.reset();}wake_.notify_one();if(worker_.joinable())worker_.join();
}
bool TrialClient::submit(TrialRequest request,std::string config_path,std::string build){
    std::lock_guard lock(mutex_);if(working_ || job_ || stop_)return false;
    response_.reset();job_=Job{std::move(request),std::move(config_path),std::move(build),generation_.load()};
    if(!worker_.joinable())try{worker_=std::thread([this]{run();});}catch(const std::system_error&){job_.reset();return false;}
    wake_.notify_one();return true;
}
std::optional<TrialResponse> TrialClient::take(){std::lock_guard lock(mutex_);auto result=std::move(response_);response_.reset();return result;}
void TrialClient::cancel(){std::lock_guard lock(mutex_);++generation_;job_.reset();response_.reset();}
bool TrialClient::busy()const{std::lock_guard lock(mutex_);return working_ || job_.has_value();}
void TrialClient::run(){
    for(;;){
        std::unique_lock lock(mutex_);wake_.wait(lock,[&]{return stop_ || job_.has_value();});if(stop_)return;
        auto job=std::move(*job_);job_.reset();working_=true;lock.unlock();
        TrialResponse result;result.action=job.request.action;result.course=job.request.course;
        try{
            std::ifstream file(job.config_path,std::ios::binary);check(bool(file),"Trial identity was not found. Launch through ProjectTandem.");
            char bytes[2049]{};file.read(bytes,sizeof(bytes));check(file.gcount()<sizeof(bytes),"Trial identity configuration is too large.");
            auto config=online::Json::parse(std::string(bytes,size_t(file.gcount())));
            result=trial_transfer(job.request,config.at("key").str(),job.build,transport_,[&]{return generation_.load()!=job.generation;},config.has("scope") && config.at("scope").str()=="public");
        }catch(const std::exception& e){result.error=e.what();}
        lock.lock();working_=false;if(generation_.load()==job.generation)response_=std::move(result);
    }
}
}
