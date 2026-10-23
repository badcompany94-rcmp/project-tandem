// Standalone EOS helper. The SDK and credentials stay outside original sources.
#include "eos_config.h"
#include "eos_service.h"
#include <eos_sdk.h>
#include <eos_connect.h>
#include <eos_version.h>
#include <chrono>
#include <cstdio>
#include <thread>

namespace {
struct Result {
    bool done=false;
    EOS_EResult code=EOS_EResult::EOS_UnexpectedError;
    EOS_ProductUserId user=nullptr;
    EOS_ContinuanceToken continuation=nullptr;
};
template<class Callback>void EOS_CALL complete(const Callback* info) {
    auto& state=*static_cast<Result*>(info->ClientData);
    state.code=info->ResultCode;state.done=true;
}
void EOS_CALL logged_in(const EOS_Connect_LoginCallbackInfo* info) {
    auto& state=*static_cast<Result*>(info->ClientData);
    state.code=info->ResultCode;state.user=info->LocalUserId;
    state.continuation=info->ContinuanceToken;state.done=true;
}
void EOS_CALL created_user(const EOS_Connect_CreateUserCallbackInfo* info) {
    auto& state=*static_cast<Result*>(info->ClientData);
    state.code=info->ResultCode;state.user=info->LocalUserId;state.done=true;
}
void wait(EOS_HPlatform platform,Result& state) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(45);
    while(!state.done) {
        EOS_Platform_Tick(platform);
        if(std::chrono::steady_clock::now()>deadline) {
            // Async callbacks still own their context. The diagnostic process
            // exits immediately instead of unwinding through a live callback.
            fputs("EOS operation timed out\n",stderr);std::_Exit(2);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
void check(EOS_EResult result,const char* operation) {
    if(result!=EOS_EResult::EOS_Success)throw std::runtime_error(std::string(operation)+": "+EOS_EResult_ToString(result));
}
}

int main(int argc,char** argv) {
    setvbuf(stdout,nullptr,_IONBF,0);
    try {
        if(argc==5 && std::string(argv[1])=="--serve")
            return mkdd::eos::serve(argv[2],(unsigned)std::stoul(argv[3]),argv[4]);
        if(argc==5 && std::string(argv[1])=="--local-test")
            return mkdd::eos::serve("",(unsigned)std::stoul(argv[2]),argv[3],(unsigned)std::stoul(argv[4]),true);
        if(argc==3 && std::string(argv[1])=="--check-config") {
            mkdd::eos::Config::load(argv[2]);puts("PASS: EOS configuration fields are present (values withheld)");return 0;
        }
        if((argc!=2 || std::string(argv[1])!="--sdk-test") && (argc!=3 || std::string(argv[1])!="--login-test")) {
            fputs("Usage: mkdd_eos --sdk-test | --check-config <eos.json> | --login-test <eos.json> | --serve <eos.json> <upstream-port> <ready-file>\n",stderr);return 2;
        }
        EOS_InitializeOptions init{};init.ApiVersion=EOS_INITIALIZE_API_LATEST;
        init.ProductName="Project Tandem";init.ProductVersion="0.1";
        check(EOS_Initialize(&init),"EOS initialize");
        printf("EOS headers: %s; runtime: %s\n",EOS_VERSION_STRING,EOS_GetVersion());
        if(argc==2) {check(EOS_Shutdown(),"EOS shutdown");puts("PASS: real EOS SDK initializes and shuts down");return 0;}
        const auto config=mkdd::eos::Config::load(argv[2]);
        EOS_Platform_Options options{};options.ApiVersion=EOS_PLATFORM_OPTIONS_API_LATEST;
        options.ProductId=config.product_id.c_str();options.SandboxId=config.sandbox_id.c_str();options.DeploymentId=config.deployment_id.c_str();
        options.ClientCredentials.ClientId=config.client_id.c_str();options.ClientCredentials.ClientSecret=config.client_secret.c_str();
        options.CacheDirectory=config.cache.c_str();options.Flags=EOS_PF_DISABLE_OVERLAY|EOS_PF_DISABLE_SOCIAL_OVERLAY;
        EOS_HPlatform platform=EOS_Platform_Create(&options);
        if(!platform)throw std::runtime_error("EOS platform creation failed; check product configuration");
        EOS_HConnect connect=EOS_Platform_GetConnectInterface(platform);
        EOS_Connect_CreateDeviceIdOptions device{};device.ApiVersion=EOS_CONNECT_CREATEDEVICEID_API_LATEST;device.DeviceModel="Windows PC";
        Result created;EOS_Connect_CreateDeviceId(connect,&device,&created,complete<EOS_Connect_CreateDeviceIdCallbackInfo>);wait(platform,created);
        if(created.code!=EOS_EResult::EOS_DuplicateNotAllowed)check(created.code,"EOS device identity");
        EOS_Connect_Credentials credentials{};credentials.ApiVersion=EOS_CONNECT_CREDENTIALS_API_LATEST;credentials.Type=EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
        EOS_Connect_UserLoginInfo info{};info.ApiVersion=EOS_CONNECT_USERLOGININFO_API_LATEST;info.DisplayName="Playtester";
        EOS_Connect_LoginOptions login{};login.ApiVersion=EOS_CONNECT_LOGIN_API_LATEST;login.Credentials=&credentials;login.UserLoginInfo=&info;
        Result authenticated;EOS_Connect_Login(connect,&login,&authenticated,logged_in);wait(platform,authenticated);
        if(authenticated.code==EOS_EResult::EOS_InvalidUser && authenticated.continuation) {
            EOS_Connect_CreateUserOptions create{};create.ApiVersion=EOS_CONNECT_CREATEUSER_API_LATEST;create.ContinuanceToken=authenticated.continuation;
            Result user;EOS_Connect_CreateUser(connect,&create,&user,created_user);wait(platform,user);authenticated=user;
        }
        check(authenticated.code,"EOS Connect login");
        puts("PASS: EOS Connect authenticated this PC; no account password or user ID logged");
        EOS_Platform_Release(platform);check(EOS_Shutdown(),"EOS shutdown");return 0;
    }catch(const std::exception& error){fprintf(stderr,"ERROR: %s\n",error.what());return 1;}
}
