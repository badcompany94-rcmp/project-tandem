#include "eos_runtime.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <eos_connect.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <algorithm>

namespace mkdd::eos {
namespace {
using Clock=std::chrono::steady_clock;
int64_t micros(){return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now().time_since_epoch()).count();}
std::string invite_hash(const std::string& domain,const std::string& code) {
    const std::string input=domain+":"+code;unsigned char digest[32];
    if(BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,(PUCHAR)input.data(),(ULONG)input.size(),digest,sizeof(digest))<0)
        throw std::runtime_error("invitation-hash-failed");
    const char* digits="0123456789abcdef";std::string result;
    for(unsigned byte:digest){result+=digits[byte>>4];result+=digits[byte&15];}return result;
}
void check(EOS_EResult result,const char* operation) {
    if(result!=EOS_EResult::EOS_Success)throw std::runtime_error(std::string(operation)+": "+EOS_EResult_ToString(result));
}
std::string id(EOS_ProductUserId user) {
    char value[EOS_PRODUCTUSERID_MAX_LENGTH+1]{};int32_t size=sizeof(value);
    check(EOS_ProductUserId_ToString(user,value,&size),"EOS identity");return value;
}
struct ResultState {
    bool done=false;EOS_EResult code=EOS_EResult::EOS_UnexpectedError;
    std::string lobby;EOS_ProductUserId user=nullptr;EOS_ContinuanceToken continuation=nullptr;
};
struct Result {
    std::shared_ptr<ResultState> state=std::make_shared<ResultState>();
    bool& done=state->done;EOS_EResult& code=state->code;std::string& lobby=state->lobby;
    EOS_ProductUserId& user=state->user;EOS_ContinuanceToken& continuation=state->continuation;
    Result& operator=(const Result& other){*state=*other.state;return *this;}
    ResultState* context(Runtime& runtime){std::weak_ptr<ResultState> weak=state;runtime.retain(state,[weak]{auto p=weak.lock();return !p || p->done;});return state.get();}
};
template<class T>void EOS_CALL completed(const T* info) {
    auto& r=*static_cast<ResultState*>(info->ClientData);r.code=info->ResultCode;r.done=true;
}
void EOS_CALL lobby_created(const EOS_Lobby_CreateLobbyCallbackInfo* info) {
    completed(info);if(info->LobbyId)static_cast<ResultState*>(info->ClientData)->lobby=info->LobbyId;
}
void EOS_CALL logged_in(const EOS_Connect_LoginCallbackInfo* info) {
    completed(info);auto& r=*static_cast<ResultState*>(info->ClientData);r.user=info->LocalUserId;r.continuation=info->ContinuanceToken;
}
void EOS_CALL user_created(const EOS_Connect_CreateUserCallbackInfo* info) {
    completed(info);static_cast<ResultState*>(info->ClientData)->user=info->LocalUserId;
}
void wait(Runtime& runtime,Result& r,const char* operation) {
    const auto deadline=Clock::now()+std::chrono::seconds(15);
    while(!r.done) {
        runtime.tick();
        // Runtime retains incomplete callback state through SDK shutdown. A
        // directory timeout cannot kill a live LAN room or free SDK-owned data.
        if(Clock::now()>deadline){fprintf(stderr,"EOS timeout: %s\n",operation);throw std::runtime_error("network-timeout");}
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
template<class T,void(EOS_CALL* Release)(T)>struct Owned {
    T value=nullptr;~Owned(){if(value)Release(value);}
};
using Details=Owned<EOS_HLobbyDetails,EOS_LobbyDetails_Release>;
EOS_P2P_SocketId socket_id(const std::string& name) {
    if(name.empty() || name.size()>32 || name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789")!=std::string::npos)
        throw std::runtime_error("invalid-eos-socket");
    EOS_P2P_SocketId result{};result.ApiVersion=EOS_P2P_SOCKETID_API_LATEST;memcpy(result.SocketName,name.data(),name.size());return result;
}
}

Runtime::Runtime(const Config& config):config_(config) {
    EOS_InitializeOptions init{};init.ApiVersion=EOS_INITIALIZE_API_LATEST;init.ProductName=config_.name.c_str();init.ProductVersion=config_.version.c_str();
    check(EOS_Initialize(&init),"EOS initialize");initialized_=true;
    EOS_Platform_Options options{};options.ApiVersion=EOS_PLATFORM_OPTIONS_API_LATEST;
    options.ProductId=config_.product_id.c_str();options.SandboxId=config_.sandbox_id.c_str();options.DeploymentId=config_.deployment_id.c_str();
    options.ClientCredentials.ClientId=config_.client_id.c_str();options.ClientCredentials.ClientSecret=config_.client_secret.c_str();
    options.Flags=EOS_PF_DISABLE_OVERLAY|EOS_PF_DISABLE_SOCIAL_OVERLAY;options.CacheDirectory=config_.cache.c_str();
    platform_=EOS_Platform_Create(&options);
    if(!platform_){EOS_Shutdown();initialized_=false;throw std::runtime_error("EOS platform configuration failed");}
    lobby_=EOS_Platform_GetLobbyInterface(platform_);p2p_=EOS_Platform_GetP2PInterface(platform_);
}
Runtime::~Runtime() {
    if(membership_!=EOS_INVALID_NOTIFICATIONID)EOS_Lobby_RemoveNotifyLobbyMemberStatusReceived(lobby_,membership_);
    if(incoming_!=EOS_INVALID_NOTIFICATIONID)EOS_P2P_RemoveNotifyPeerConnectionRequest(p2p_,incoming_);
    if(established_!=EOS_INVALID_NOTIFICATIONID)EOS_P2P_RemoveNotifyPeerConnectionEstablished(p2p_,established_);
    if(platform_)EOS_Platform_Release(platform_);
    if(initialized_)EOS_Shutdown();
}
void Runtime::login() {
    auto connect=EOS_Platform_GetConnectInterface(platform_);
    EOS_Connect_CreateDeviceIdOptions device{};device.ApiVersion=EOS_CONNECT_CREATEDEVICEID_API_LATEST;device.DeviceModel="Windows PC";
    Result made;EOS_Connect_CreateDeviceId(connect,&device,made.context(*this),completed<EOS_Connect_CreateDeviceIdCallbackInfo>);wait(*this,made,"device identity");
    if(made.code!=EOS_EResult::EOS_DuplicateNotAllowed)check(made.code,"EOS device identity");
    EOS_Connect_Credentials credentials{};credentials.ApiVersion=EOS_CONNECT_CREDENTIALS_API_LATEST;credentials.Type=EOS_EExternalCredentialType::EOS_ECT_DEVICEID_ACCESS_TOKEN;
    EOS_Connect_UserLoginInfo info{};info.ApiVersion=EOS_CONNECT_USERLOGININFO_API_LATEST;info.DisplayName="Playtester";
    EOS_Connect_LoginOptions login{};login.ApiVersion=EOS_CONNECT_LOGIN_API_LATEST;login.Credentials=&credentials;login.UserLoginInfo=&info;
    Result auth;EOS_Connect_Login(connect,&login,auth.context(*this),logged_in);wait(*this,auth,"Connect login");
    if(auth.code==EOS_EResult::EOS_InvalidUser && auth.continuation) {
        EOS_Connect_CreateUserOptions options{};options.ApiVersion=EOS_CONNECT_CREATEUSER_API_LATEST;options.ContinuanceToken=auth.continuation;
        Result created;EOS_Connect_CreateUser(connect,&options,created.context(*this),user_created);wait(*this,created,"create product user");auth=created;
    }
    check(auth.code,"EOS Connect login");user_=auth.user;self_=id(user_);
    if(getenv("TANDEM_BROWSER_TRACE"))fprintf(stderr,"[directory] local identity fingerprint=%s\n",invite_hash("test-local-identity",self_).substr(0,8).c_str());
    EOS_P2P_SetRelayControlOptions relay{};relay.ApiVersion=EOS_P2P_SETRELAYCONTROL_API_LATEST;relay.RelayControl=EOS_ERelayControl::EOS_RC_AllowRelays;
    check(EOS_P2P_SetRelayControl(p2p_,&relay),"EOS relay fallback");
    EOS_P2P_AddNotifyPeerConnectionRequestOptions request{};request.ApiVersion=EOS_P2P_ADDNOTIFYPEERCONNECTIONREQUEST_API_LATEST;request.LocalUserId=user_;
    incoming_=EOS_P2P_AddNotifyPeerConnectionRequest(p2p_,&request,this,incoming);
    EOS_P2P_AddNotifyPeerConnectionEstablishedOptions established_options{};established_options.ApiVersion=EOS_P2P_ADDNOTIFYPEERCONNECTIONESTABLISHED_API_LATEST;established_options.LocalUserId=user_;
    established_=EOS_P2P_AddNotifyPeerConnectionEstablished(p2p_,&established_options,this,established);
    EOS_Lobby_AddNotifyLobbyMemberStatusReceivedOptions membership_options{};membership_options.ApiVersion=EOS_LOBBY_ADDNOTIFYLOBBYMEMBERSTATUSRECEIVED_API_LATEST;
    membership_=EOS_Lobby_AddNotifyLobbyMemberStatusReceived(lobby_,&membership_options,this,membership);
    if(incoming_==EOS_INVALID_NOTIFICATIONID || established_==EOS_INVALID_NOTIFICATIONID || membership_==EOS_INVALID_NOTIFICATIONID)throw std::runtime_error("EOS connection notifications failed");
    puts("EOS Connect ready; direct P2P with automatic relay fallback enabled");
}
void Runtime::tick() {
    EOS_Platform_Tick(platform_);
    contexts_.erase(std::remove_if(contexts_.begin(),contexts_.end(),[](const auto& c){return c.second();}),contexts_.end());
    if(user_)for(unsigned i=0;i<256;++i) {
        uint8_t bytes[EOS_P2P_MAX_PACKET_SIZE],channel=0;uint32_t size=0;EOS_ProductUserId peer=nullptr;EOS_P2P_SocketId socket{};
        EOS_P2P_ReceivePacketOptions options{};options.ApiVersion=EOS_P2P_RECEIVEPACKET_API_LATEST;options.LocalUserId=user_;options.MaxDataSizeBytes=sizeof(bytes);
        const auto result=EOS_P2P_ReceivePacket(p2p_,&options,&peer,&socket,&channel,bytes,&size);
        if(result==EOS_EResult::EOS_NotFound)break;
        check(result,"EOS receive");
        if(!memchr(socket.SocketName,0,sizeof(socket.SocketName)))continue;
        const std::string sender=id(peer),name=socket.SocketName;
        if(name=="TandemBrowseV1" && channel==2 && size==9) {
            const auto now=micros();
            if(now-browse_window_>1000000){browse_window_=now;browse_replies_=0;}
            if(bytes[0]==0x50 && hosting() && listing_.has("visibility") && listing_.at("visibility").str()=="public" && browse_replies_++<64){
                bytes[0]=0x51;browse_packet(sender,{bytes,bytes+size});
            }else if(bytes[0]==0x51){auto probe=browsing_.find(sender);
                if(probe!=browsing_.end() && probe->second.nonce==std::vector<uint8_t>(bytes+1,bytes+9) && probe->second.sent>0)
                    probe->second.elapsed=std::max<int64_t>(0,now-probe->second.sent);
            }
            continue;
        }
        if(accepts(sender,name) && packet)packet(sender,name,channel,{bytes,bytes+size});
    }
    if(idle)idle();
}
std::set<std::string> Runtime::members() const {
    std::set<std::string> result;if(lobby_id_.empty())return result;
    EOS_Lobby_CopyLobbyDetailsHandleOptions copy{};copy.ApiVersion=EOS_LOBBY_COPYLOBBYDETAILSHANDLE_API_LATEST;copy.LocalUserId=user_;copy.LobbyId=lobby_id_.c_str();
    Details details;if(EOS_Lobby_CopyLobbyDetailsHandle(lobby_,&copy,&details.value)!=EOS_EResult::EOS_Success)return result;
    EOS_LobbyDetails_GetMemberCountOptions count{};count.ApiVersion=EOS_LOBBYDETAILS_GETMEMBERCOUNT_API_LATEST;
    const auto n=EOS_LobbyDetails_GetMemberCount(details.value,&count);if(n>8)return result;
    for(unsigned i=0;i<n;++i) {
        EOS_LobbyDetails_GetMemberByIndexOptions index{};index.ApiVersion=EOS_LOBBYDETAILS_GETMEMBERBYINDEX_API_LATEST;index.MemberIndex=i;
        auto member=EOS_LobbyDetails_GetMemberByIndex(details.value,&index);if(member)result.insert(id(member));
    }
    return result;
}
bool Runtime::accepts(const std::string& peer,const std::string& socket) const {
    if(socket=="TandemBrowseV1")return browsing_.count(peer) ||
        (hosting() && listing_.has("visibility") && listing_.at("visibility").str()=="public");
    if(!control_socket_.empty() && socket==control_socket_)return peer!=self_ && members().count(peer);
    return !game_socket_.empty() && socket==game_socket_ && game_peers_.count(peer);
}
void EOS_CALL Runtime::incoming(const EOS_P2P_OnIncomingConnectionRequestInfo* info) {
    auto& r=*static_cast<Runtime*>(info->ClientData);
    try {
        if(!info->SocketId || !memchr(info->SocketId->SocketName,0,33) || !r.accepts(id(info->RemoteUserId),info->SocketId->SocketName))return;
        EOS_P2P_AcceptConnectionOptions accept{};accept.ApiVersion=EOS_P2P_ACCEPTCONNECTION_API_LATEST;accept.LocalUserId=r.user_;accept.RemoteUserId=info->RemoteUserId;accept.SocketId=info->SocketId;
        check(EOS_P2P_AcceptConnection(r.p2p_,&accept),"EOS accept");
    }catch(const std::exception&){fputs("EOS rejected a connection request\n",stderr);}
}
void EOS_CALL Runtime::established(const EOS_P2P_OnPeerConnectionEstablishedInfo* info) {
    auto& runtime=*static_cast<Runtime*>(info->ClientData);
    if(info->RemoteUserId){
        const auto route=info->NetworkType==EOS_ENetworkConnectionType::EOS_NCT_DirectConnection?"direct":
            info->NetworkType==EOS_ENetworkConnectionType::EOS_NCT_RelayedConnection?"relay":"unknown";
        try{runtime.routes_[id(info->RemoteUserId)]=route;}catch(const std::exception&){}
    }
    // Diagnostics reveal the route type, never credentials, codes or peer IDs.
    if(info->NetworkType==EOS_ENetworkConnectionType::EOS_NCT_DirectConnection)puts("EOS route: direct peer-to-peer");
    else if(info->NetworkType==EOS_ENetworkConnectionType::EOS_NCT_RelayedConnection)puts("EOS route: Epic relay fallback");
}
std::string Runtime::route()const {
    const auto relevant=game_peers_.empty()?members():game_peers_;
    std::string result;
    for(const auto& peer:relevant)if(peer!=self_){
        auto found=routes_.find(peer);if(found==routes_.end())return "unknown";
        if(result.empty())result=found->second;else if(result!=found->second)return "mixed";
    }
    return result.empty()?"unknown":result;
}
void EOS_CALL Runtime::membership(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo* info) {
    auto& r=*static_cast<Runtime*>(info->ClientData);
    if(!info->LobbyId || r.lobby_id_!=info->LobbyId || info->TargetUserId!=r.user_)return;
    const auto status=info->CurrentStatus;
    if(status==EOS_ELobbyMemberStatus::EOS_LMS_KICKED || status==EOS_ELobbyMemberStatus::EOS_LMS_CLOSED ||
       status==EOS_ELobbyMemberStatus::EOS_LMS_DISCONNECTED || status==EOS_ELobbyMemberStatus::EOS_LMS_LEFT) {
        // Losing membership invalidates only this room, not the device identity.
        // Clear it immediately so a fresh join does not require a helper restart.
        r.lobby_id_.clear();r.owner_.clear();r.control_socket_.clear();r.game_socket_.clear();r.game_peers_.clear();
    }
}
bool Runtime::send(const std::string& peer,const std::string& socket,uint8_t channel,const std::vector<uint8_t>& data) {
    if(data.empty() || data.size()>EOS_P2P_MAX_PACKET_SIZE || peer==self_ || !accepts(peer,socket))return false;
    auto name=socket_id(socket);auto remote=EOS_ProductUserId_FromString(peer.c_str());if(!remote)return false;
    EOS_P2P_SendPacketOptions options{};options.ApiVersion=EOS_P2P_SENDPACKET_API_LATEST;options.LocalUserId=user_;options.RemoteUserId=remote;
    options.SocketId=&name;options.Channel=channel;options.Data=data.data();options.DataLengthBytes=(uint32_t)data.size();
    const bool redundant=channel==1 && !data.empty() && (data[0]==14 || (data.size()>66 && data[0]==15 && data[66]==14));
    options.bAllowDelayedDelivery=EOS_TRUE;
    options.Reliability=redundant?EOS_EPacketReliability::EOS_PR_UnreliableUnordered:EOS_EPacketReliability::EOS_PR_ReliableOrdered;
    return EOS_P2P_SendPacket(p2p_,&options)==EOS_EResult::EOS_Success;
}
void Runtime::game_peers(std::string socket,std::set<std::string> peers) {
    if(!socket.empty())socket_id(socket);
    peers.erase(self_);if(peers.size()>7 || (socket.empty() && !peers.empty()))throw std::runtime_error("invalid-eos-roster");
    game_socket_=std::move(socket);game_peers_=std::move(peers);
}
void Runtime::create(const std::string& code,const std::string& build) {
    if(!lobby_id_.empty())throw std::runtime_error("already-in-room");
    EOS_Lobby_CreateLobbyOptions options{};options.ApiVersion=EOS_LOBBY_CREATELOBBY_API_LATEST;options.LocalUserId=user_;
    options.MaxLobbyMembers=8;options.PermissionLevel=EOS_ELobbyPermissionLevel::EOS_LPL_PUBLICADVERTISED;
    options.BucketId="ProjectTandemV1";options.bDisableHostMigration=EOS_TRUE;
    // Reserve invitations atomically, including across hosts. Search alone has
    // a race in which two hosts can publish the same short invitation.
    const auto reserved=invite_hash("tandem-room-v2",code).substr(0,EOS_LOBBY_MAX_LOBBYIDOVERRIDE_LENGTH);
    options.LobbyId=reserved.c_str();
    Result created;EOS_Lobby_CreateLobby(lobby_,&options,created.context(*this),lobby_created);wait(*this,created,"create lobby");
    if(created.code==EOS_EResult::EOS_Lobby_LobbyAlreadyExists || created.code==EOS_EResult::EOS_DuplicateNotAllowed)throw std::runtime_error("room-code-in-use");
    check(created.code,"EOS create lobby");
    lobby_id_=created.lobby;owner_=self_;control_socket_=invite_hash("tandem-control-v2",lobby_id_).substr(0,32);
    try {
        EOS_Lobby_UpdateLobbyModificationOptions modification{};modification.ApiVersion=EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;modification.LobbyId=lobby_id_.c_str();modification.LocalUserId=user_;
        Owned<EOS_HLobbyModification,EOS_LobbyModification_Release> handle;
        check(EOS_Lobby_UpdateLobbyModification(lobby_,&modification,&handle.value),"EOS lobby attributes");
        for(auto entry:{std::make_pair("dd_invite",invite_hash("tandem-discovery-v1",code)),std::make_pair("dd_build",build)}) {
            EOS_Lobby_AttributeData data{};data.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;data.Key=entry.first;data.Value.AsUtf8=entry.second.c_str();data.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;
            EOS_LobbyModification_AddAttributeOptions attribute{};attribute.ApiVersion=EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;attribute.Attribute=&data;attribute.Visibility=EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
            check(EOS_LobbyModification_AddAttribute(handle.value,&attribute),"EOS publish room code");
        }
        EOS_Lobby_UpdateLobbyOptions update{};update.ApiVersion=EOS_LOBBY_UPDATELOBBY_API_LATEST;update.LobbyModificationHandle=handle.value;
        Result published;EOS_Lobby_UpdateLobby(lobby_,&update,published.context(*this),completed<EOS_Lobby_UpdateLobbyCallbackInfo>);wait(*this,published,"publish lobby");check(published.code,"EOS publish lobby");
    }catch(...){leave();throw;}
}
void Runtime::join(const std::string& code,const std::string& build) {
    if(!lobby_id_.empty())throw std::runtime_error("already-in-room");
    EOS_Lobby_CreateLobbySearchOptions options{};options.ApiVersion=EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;options.MaxResults=2;
    Owned<EOS_HLobbySearch,EOS_LobbySearch_Release> search;check(EOS_Lobby_CreateLobbySearch(lobby_,&options,&search.value),"EOS lobby search");
    for(auto entry:{std::make_pair("dd_invite",invite_hash("tandem-discovery-v1",code))}) {
        EOS_Lobby_AttributeData data{};data.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;data.Key=entry.first;data.Value.AsUtf8=entry.second.c_str();data.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;
        EOS_LobbySearch_SetParameterOptions parameter{};parameter.ApiVersion=EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;parameter.Parameter=&data;parameter.ComparisonOp=EOS_EComparisonOp::EOS_CO_EQUAL;
        check(EOS_LobbySearch_SetParameter(search.value,&parameter),"EOS room search parameter");
    }
    EOS_LobbySearch_FindOptions find{};find.ApiVersion=EOS_LOBBYSEARCH_FIND_API_LATEST;find.LocalUserId=user_;
    Result found;EOS_LobbySearch_Find(search.value,&find,found.context(*this),completed<EOS_LobbySearch_FindCallbackInfo>);wait(*this,found,"find room");check(found.code,"EOS find room");
    EOS_LobbySearch_GetSearchResultCountOptions count{};count.ApiVersion=EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
    if(EOS_LobbySearch_GetSearchResultCount(search.value,&count)!=1)throw std::runtime_error("room-not-found");
    EOS_LobbySearch_CopySearchResultByIndexOptions index{};index.ApiVersion=EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;
    Details details;check(EOS_LobbySearch_CopySearchResultByIndex(search.value,&index,&details.value),"EOS room result");
    EOS_LobbyDetails_CopyAttributeByKeyOptions attr{};attr.ApiVersion=EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;attr.AttrKey="dd_build";
    EOS_Lobby_Attribute* build_attribute=nullptr;
    check(EOS_LobbyDetails_CopyAttributeByKey(details.value,&attr,&build_attribute),"EOS room build");
    bool compatible=build_attribute && build_attribute->Data && build_attribute->Data->ValueType==EOS_ELobbyAttributeType::EOS_AT_STRING && build_attribute->Data->Value.AsUtf8 && build==build_attribute->Data->Value.AsUtf8;
    if(build_attribute)EOS_Lobby_Attribute_Release(build_attribute);if(!compatible)throw std::runtime_error("incompatible-build");
    EOS_LobbyDetails_GetLobbyOwnerOptions owner{};owner.ApiVersion=EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;
    const std::string host=id(EOS_LobbyDetails_GetLobbyOwner(details.value,&owner));
    EOS_LobbyDetails_CopyInfoOptions copy{};copy.ApiVersion=EOS_LOBBYDETAILS_COPYINFO_API_LATEST;
    EOS_LobbyDetails_Info* info=nullptr;check(EOS_LobbyDetails_CopyInfo(details.value,&copy,&info),"EOS room information");
    const std::string lobby_id=info->LobbyId;EOS_LobbyDetails_Info_Release(info);
    EOS_Lobby_JoinLobbyOptions join{};join.ApiVersion=EOS_LOBBY_JOINLOBBY_API_LATEST;join.LocalUserId=user_;join.LobbyDetailsHandle=details.value;
    Result joined;EOS_Lobby_JoinLobby(lobby_,&join,joined.context(*this),completed<EOS_Lobby_JoinLobbyCallbackInfo>);wait(*this,joined,"join lobby");check(joined.code,"EOS join lobby");
    lobby_id_=lobby_id;owner_=host;control_socket_=invite_hash("tandem-control-v2",lobby_id_).substr(0,32);
}
void Runtime::join_target(const std::string& code,const std::string& build,const online::Json& row){
    join(code,build);if(owner_!=row.at("id").str()){leave();throw std::runtime_error("room-not-found");}
}
void Runtime::destroy_claim(const std::string& claim) {
    if(claim.empty())return;
    EOS_Lobby_DestroyLobbyOptions options{};options.ApiVersion=EOS_LOBBY_DESTROYLOBBY_API_LATEST;options.LocalUserId=user_;options.LobbyId=claim.c_str();
    Result result;EOS_Lobby_DestroyLobby(lobby_,&options,result.context(*this),completed<EOS_Lobby_DestroyLobbyCallbackInfo>);wait(*this,result,"release invitation");
    if(result.code!=EOS_EResult::EOS_Success && result.code!=EOS_EResult::EOS_NotFound)fputs("EOS invitation cleanup will complete on disconnect\n",stderr);
}
void Runtime::refresh_invitation(const std::string& code) {
    if(!hosting())throw std::runtime_error("host-only");
    const std::string claim=invite_hash("tandem-room-v2",code).substr(0,EOS_LOBBY_MAX_LOBBYIDOVERRIDE_LENGTH);std::string reserved;
    if(claim!=lobby_id_ && claim!=invitation_claim_) {
        EOS_Lobby_CreateLobbyOptions options{};options.ApiVersion=EOS_LOBBY_CREATELOBBY_API_LATEST;options.LocalUserId=user_;
        options.MaxLobbyMembers=2;options.PermissionLevel=EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY;
        options.BucketId="TandemInviteV2";options.bDisableHostMigration=EOS_TRUE;options.LobbyId=claim.c_str();
        Result result;EOS_Lobby_CreateLobby(lobby_,&options,result.context(*this),lobby_created);wait(*this,result,"reserve invitation");
        if(result.code==EOS_EResult::EOS_Lobby_LobbyAlreadyExists || result.code==EOS_EResult::EOS_DuplicateNotAllowed)throw std::runtime_error("room-code-in-use");
        check(result.code,"EOS reserve invitation");reserved=result.lobby;
    }
    try {
        EOS_Lobby_UpdateLobbyModificationOptions options{};options.ApiVersion=EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;options.LocalUserId=user_;options.LobbyId=lobby_id_.c_str();
        Owned<EOS_HLobbyModification,EOS_LobbyModification_Release> handle;
        check(EOS_Lobby_UpdateLobbyModification(lobby_,&options,&handle.value),"EOS refresh invitation");
        const std::string hash=invite_hash("tandem-discovery-v1",code);
        EOS_Lobby_AttributeData data{};data.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;data.Key="dd_invite";data.Value.AsUtf8=hash.c_str();data.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;
        EOS_LobbyModification_AddAttributeOptions attribute{};attribute.ApiVersion=EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;attribute.Attribute=&data;attribute.Visibility=EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
        check(EOS_LobbyModification_AddAttribute(handle.value,&attribute),"EOS refresh invitation attribute");
        EOS_Lobby_UpdateLobbyOptions update{};update.ApiVersion=EOS_LOBBY_UPDATELOBBY_API_LATEST;update.LobbyModificationHandle=handle.value;
        Result result;EOS_Lobby_UpdateLobby(lobby_,&update,result.context(*this),completed<EOS_Lobby_UpdateLobbyCallbackInfo>);wait(*this,result,"refresh invitation");check(result.code,"EOS refresh invitation");
    }catch(...){destroy_claim(reserved);throw;}
    if(invitation_claim_!=claim) {
        const auto previous=invitation_claim_;invitation_claim_=reserved;
        destroy_claim(previous);
    }
}
void Runtime::kick(const std::string& peer) {
    if(!hosting() || peer==self_)throw std::runtime_error("host-only");
    EOS_Lobby_KickMemberOptions options{};options.ApiVersion=EOS_LOBBY_KICKMEMBER_API_LATEST;
    options.LobbyId=lobby_id_.c_str();options.LocalUserId=user_;options.TargetUserId=EOS_ProductUserId_FromString(peer.c_str());
    Result result;EOS_Lobby_KickMember(lobby_,&options,result.context(*this),completed<EOS_Lobby_KickMemberCallbackInfo>);
    wait(*this,result,"kick member");check(result.code,"EOS kick member");game_peers_.erase(peer);
}
void Runtime::leave() {
    listing_=online::Json();published_listing_.clear();listing_build_.clear();browsing_.clear();
    if(!name_claim_.empty()){auto claim=name_claim_;name_claim_.clear();allocated_name_.clear();destroy_claim(claim);}
    if(!invitation_claim_.empty()){const auto claim=invitation_claim_;invitation_claim_.clear();destroy_claim(claim);}
    if(lobby_id_.empty())return;
    Result left;
    if(hosting()) {
        EOS_Lobby_DestroyLobbyOptions options{};options.ApiVersion=EOS_LOBBY_DESTROYLOBBY_API_LATEST;options.LocalUserId=user_;options.LobbyId=lobby_id_.c_str();
        EOS_Lobby_DestroyLobby(lobby_,&options,left.context(*this),completed<EOS_Lobby_DestroyLobbyCallbackInfo>);
    }else {
        EOS_Lobby_LeaveLobbyOptions options{};options.ApiVersion=EOS_LOBBY_LEAVELOBBY_API_LATEST;options.LocalUserId=user_;options.LobbyId=lobby_id_.c_str();
        EOS_Lobby_LeaveLobby(lobby_,&options,left.context(*this),completed<EOS_Lobby_LeaveLobbyCallbackInfo>);
    }
    wait(*this,left,"leave lobby");lobby_id_.clear();owner_.clear();game_socket_.clear();control_socket_.clear();game_peers_.clear();
}

bool Runtime::browse_packet(const std::string& peer,const std::vector<uint8_t>& bytes){
    auto name=socket_id("TandemBrowseV1");auto remote=EOS_ProductUserId_FromString(peer.c_str());if(!remote || bytes.size()!=9)return false;
    EOS_P2P_SendPacketOptions options{};options.ApiVersion=EOS_P2P_SENDPACKET_API_LATEST;options.LocalUserId=user_;options.RemoteUserId=remote;
    options.SocketId=&name;options.Channel=2;options.Data=bytes.data();options.DataLengthBytes=uint32_t(bytes.size());options.bAllowDelayedDelivery=EOS_TRUE;options.Reliability=EOS_EPacketReliability::EOS_PR_UnreliableUnordered;
    return EOS_P2P_SendPacket(p2p_,&options)==EOS_EResult::EOS_Success;
}
std::string Runtime::reserve_name(const std::string& requested){
    if(!hosting())throw std::runtime_error("host-only");if(!allocated_name_.empty())return allocated_name_;
    for(unsigned suffix=0;suffix<256;++suffix){
        auto ending=suffix?" ("+std::to_string(suffix)+")":std::string();auto name=requested.substr(0,24-ending.size())+ending;
        auto folded=name;for(char& c:folded)if(c>='A' && c<='Z')c+=32;
        auto claim=invite_hash("tandem-lobby-name-v1",folded).substr(0,EOS_LOBBY_MAX_LOBBYIDOVERRIDE_LENGTH);
        EOS_Lobby_CreateLobbyOptions options{};options.ApiVersion=EOS_LOBBY_CREATELOBBY_API_LATEST;options.LocalUserId=user_;
        options.MaxLobbyMembers=2;options.PermissionLevel=EOS_ELobbyPermissionLevel::EOS_LPL_INVITEONLY;
        options.BucketId="TandemNamesV1";options.bDisableHostMigration=EOS_TRUE;options.LobbyId=claim.c_str();
        Result result;EOS_Lobby_CreateLobby(lobby_,&options,result.context(*this),lobby_created);wait(*this,result,"reserve lobby name");
        if(result.code==EOS_EResult::EOS_Lobby_LobbyAlreadyExists || result.code==EOS_EResult::EOS_DuplicateNotAllowed)continue;
        check(result.code,"EOS reserve lobby name");name_claim_=result.lobby;allocated_name_=name;return name;
    }
    throw std::runtime_error("lobby-name-unavailable");
}
void Runtime::publish(const online::Json& room,const std::string& build){
    using online::Json;if(!hosting())return;if(!build.empty())listing_build_=build;
    listing_=Json::dict({{"id",self_},{"name",room.at("lobby_name")},{"code",room.at("code")},{"build",listing_build_},
        {"visibility",room.at("visibility")},{"locked",room.at("locked")},{"players",(int)room.at("members").list().size()},{"state",room.at("state")}});
    const bool visible=room.at("visibility").str()=="public";
    const auto serialized=visible?listing_.dump():std::string("{}");
    const auto signature=std::string(visible?"1":"0")+serialized;if(signature==published_listing_)return;
    EOS_Lobby_UpdateLobbyModificationOptions modification{};modification.ApiVersion=EOS_LOBBY_UPDATELOBBYMODIFICATION_API_LATEST;modification.LocalUserId=user_;modification.LobbyId=lobby_id_.c_str();
    Owned<EOS_HLobbyModification,EOS_LobbyModification_Release> handle;check(EOS_Lobby_UpdateLobbyModification(lobby_,&modification,&handle.value),"EOS lobby listing");
    for(const auto& pair:{std::make_pair(std::string("dd_public"),std::string(visible?"1":"0")),std::make_pair(std::string("dd_listing"),serialized)}){
        EOS_Lobby_AttributeData data{};data.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;data.Key=pair.first.c_str();data.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;data.Value.AsUtf8=pair.second.c_str();
        EOS_LobbyModification_AddAttributeOptions attr{};attr.ApiVersion=EOS_LOBBYMODIFICATION_ADDATTRIBUTE_API_LATEST;attr.Attribute=&data;attr.Visibility=EOS_ELobbyAttributeVisibility::EOS_LAT_PUBLIC;
        check(EOS_LobbyModification_AddAttribute(handle.value,&attr),"EOS listing attribute");
    }
    published_listing_=signature;
    try{EOS_Lobby_UpdateLobbyOptions update{};update.ApiVersion=EOS_LOBBY_UPDATELOBBY_API_LATEST;update.LobbyModificationHandle=handle.value;
        Result result;EOS_Lobby_UpdateLobby(lobby_,&update,result.context(*this),completed<EOS_Lobby_UpdateLobbyCallbackInfo>);wait(*this,result,"update public listing");check(result.code,"EOS update public listing");
    }catch(...){published_listing_.clear();throw;}
}
online::Json Runtime::browse(const std::string& build){
    using online::Json;Json rows;rows.kind=Json::Array;
    EOS_Lobby_CreateLobbySearchOptions options{};options.ApiVersion=EOS_LOBBY_CREATELOBBYSEARCH_API_LATEST;options.MaxResults=100;
    Owned<EOS_HLobbySearch,EOS_LobbySearch_Release> search;check(EOS_Lobby_CreateLobbySearch(lobby_,&options,&search.value),"EOS public lobby search");
    EOS_Lobby_AttributeData data{};data.ApiVersion=EOS_LOBBY_ATTRIBUTEDATA_API_LATEST;data.Key="dd_public";data.Value.AsUtf8="1";data.ValueType=EOS_ELobbyAttributeType::EOS_AT_STRING;
    EOS_LobbySearch_SetParameterOptions param{};param.ApiVersion=EOS_LOBBYSEARCH_SETPARAMETER_API_LATEST;param.Parameter=&data;param.ComparisonOp=EOS_EComparisonOp::EOS_CO_EQUAL;
    check(EOS_LobbySearch_SetParameter(search.value,&param),"EOS public lobby parameter");
    EOS_LobbySearch_FindOptions find{};find.ApiVersion=EOS_LOBBYSEARCH_FIND_API_LATEST;find.LocalUserId=user_;
    Result result;EOS_LobbySearch_Find(search.value,&find,result.context(*this),completed<EOS_LobbySearch_FindCallbackInfo>);wait(*this,result,"browse public lobbies");check(result.code,"EOS browse public lobbies");
    EOS_LobbySearch_GetSearchResultCountOptions count{};count.ApiVersion=EOS_LOBBYSEARCH_GETSEARCHRESULTCOUNT_API_LATEST;
    browsing_.clear();const auto n=std::min<uint32_t>(100,EOS_LobbySearch_GetSearchResultCount(search.value,&count));
    if(getenv("TANDEM_BROWSER_TRACE"))fprintf(stderr,"[directory] search results=%u\n",n);
    for(unsigned i=0;i<n;++i)try{
        EOS_LobbySearch_CopySearchResultByIndexOptions index{};index.ApiVersion=EOS_LOBBYSEARCH_COPYSEARCHRESULTBYINDEX_API_LATEST;index.LobbyIndex=i;
        Details details;check(EOS_LobbySearch_CopySearchResultByIndex(search.value,&index,&details.value),"EOS listing result");
        EOS_LobbyDetails_GetLobbyOwnerOptions owner{};owner.ApiVersion=EOS_LOBBYDETAILS_GETLOBBYOWNER_API_LATEST;auto who=id(EOS_LobbyDetails_GetLobbyOwner(details.value,&owner));
        if(who==self_ && !getenv("TANDEM_BROWSER_SELF_TEST"))continue;
        EOS_LobbyDetails_CopyAttributeByKeyOptions copy{};copy.ApiVersion=EOS_LOBBYDETAILS_COPYATTRIBUTEBYKEY_API_LATEST;copy.AttrKey="dd_listing";
        EOS_Lobby_Attribute* attribute=nullptr;check(EOS_LobbyDetails_CopyAttributeByKey(details.value,&copy,&attribute),"EOS listing information");
        struct Release{EOS_Lobby_Attribute* a;~Release(){EOS_Lobby_Attribute_Release(a);}}release{attribute};
        if(!attribute || !attribute->Data || attribute->Data->ValueType!=EOS_ELobbyAttributeType::EOS_AT_STRING || !attribute->Data->Value.AsUtf8)continue;
        auto text=std::string(attribute->Data->Value.AsUtf8);if(text.size()>1024)continue;
        auto row=Json::parse(text);if(row.at("id").str()!=who || row.at("visibility").str()!="public")continue;
        row.object["network"]="internet";row.object["compatible"]=row.at("build").str()==build;row.object["ping_us"]=-1;
        Probe probe;probe.nonce.resize(8);if(BCryptGenRandom(nullptr,probe.nonce.data(),8,BCRYPT_USE_SYSTEM_PREFERRED_RNG))continue;
        probe.sent=micros();auto wire=probe.nonce;wire.insert(wire.begin(),0x50);browsing_[who]=std::move(probe);browse_packet(who,wire);
        rows.array.push_back(std::move(row));
    }catch(const std::exception& error){if(getenv("TANDEM_BROWSER_TRACE"))fprintf(stderr,"[directory] invalid result: %s\n",error.what());}
    const auto until=Clock::now()+std::chrono::milliseconds(300);
    while(Clock::now()<until){tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    for(auto& row:rows.array){auto found=browsing_.find(row.at("id").str());if(found!=browsing_.end())row.object["ping_us"]=found->second.elapsed;}
    return rows;
}
}
