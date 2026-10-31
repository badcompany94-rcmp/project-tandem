#pragma once
#include "eos_config.h"
#include "peer_runtime.h"
#include <eos_sdk.h>
#include <eos_lobby.h>
#include <eos_p2p.h>
#include <functional>
#include <set>
#include <map>
#include <vector>

namespace mkdd::eos {
// Owned by one helper thread. Game processes communicate over loopback only;
// the helper owns persistent lobby membership and authenticated EOS P2P.
class Runtime : public PeerRuntime {
public:
    explicit Runtime(const Config&);
    ~Runtime() override;
    Runtime(const Runtime&)=delete;
    Runtime& operator=(const Runtime&)=delete;
    void login();
    void tick() override;
    void create(const std::string& code,const std::string& build) override;
    void join(const std::string& code,const std::string& build) override;
    void leave() override;
    void kick(const std::string& peer) override;
    void refresh_invitation(const std::string& code) override;
    std::set<std::string> members() const override;
    bool send(const std::string& peer,const std::string& socket,uint8_t channel,const std::vector<uint8_t>& data) override;
    void game_peers(std::string socket,std::set<std::string> peers) override;
    const std::string& self()const override{return self_;}
    const std::string& owner()const override{return owner_;}
    bool hosting()const override{return !owner_.empty() && owner_==self_;}
    const std::string& control_socket()const override{return control_socket_;}
    bool authorized(const std::string& peer,const std::string& socket)const override{return accepts(peer,socket);}
    std::string route()const override;
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
    std::map<std::string,std::string> routes_;
    bool initialized_=false;
    bool accepts(const std::string& peer,const std::string& socket) const;
    void destroy_claim(const std::string& id);
    static void EOS_CALL incoming(const EOS_P2P_OnIncomingConnectionRequestInfo*);
    static void EOS_CALL established(const EOS_P2P_OnPeerConnectionEstablishedInfo*);
    static void EOS_CALL membership(const EOS_Lobby_LobbyMemberStatusReceivedCallbackInfo*);
};
}
