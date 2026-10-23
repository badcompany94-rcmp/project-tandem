#pragma once
#include "eos_config.h"
#include <eos_sdk.h>
#include <eos_lobby.h>
#include <eos_p2p.h>
#include <functional>
#include <set>
#include <vector>

namespace mkdd::eos {
// Owned by one helper thread. Game processes communicate over loopback only;
// the helper owns persistent lobby membership and authenticated EOS P2P.
class Runtime {
public:
    explicit Runtime(const Config&);
    ~Runtime();
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    void login();
    void tick();
    void create(const std::string& code,const std::string& build);
    void join(const std::string& code,const std::string& build);
    void leave();
    void kick(const std::string& peer);
    void refresh_invitation(const std::string& code);
    std::set<std::string> members() const;
    bool send(const std::string& peer,const std::string& socket,uint8_t channel,const std::vector<uint8_t>& data);
    void game_peers(std::string socket,std::set<std::string> peers);
    const std::string& self()const{return self_;}
    const std::string& owner()const{return owner_;}
    bool hosting()const{return !owner_.empty() && owner_==self_;}
    const std::string& control_socket()const{return control_socket_;}
    bool authorized(const std::string& peer,const std::string& socket)const{return accepts(peer,socket);}
    std::function<void(const std::string&,const std::string&,uint8_t,const std::vector<uint8_t>&)> packet;
    std::function<void()> idle;
private:
    Config config_;
    EOS_HPlatform platform_=nullptr;
    EOS_HLobby lobby_=nullptr;
    EOS_HP2P p2p_=nullptr;
    EOS_ProductUserId user_=nullptr;
    EOS_NotificationId incoming_=EOS_INVALID_NOTIFICATIONID,established_=EOS_INVALID_NOTIFICATIONID;
    EOS_NotificationId membership_=EOS_INVALID_NOTIFICATIONID;
    std::string self_,owner_,lobby_id_,game_socket_,control_socket_,invitation_claim_;
    std::set<std::string> game_peers_;
    bool initialized_=false;
    bool accepts(const std::string& peer,const std::string& socket) const;
    void destroy_claim(const std::string& id);
    static void EOS_CALL incoming(const EOS_P2P_OnIncomingConnectionRequestInfo*);
    static void EOS_CALL established(const EOS_P2P_OnPeerConnectionEstablishedInfo*);
    static void EOS_CALL membership(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo*);
};
}
