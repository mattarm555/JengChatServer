#define main jengchat_server_main
#include "../server.cpp"
#undef main
#include <stdexcept>
void require(bool x,const char* why){if(!x)throw runtime_error(why);}
void reset(){clients.clear();blackjackGames.clear();rouletteGames.clear();pokerGames.clear();arenaGames.clear();chessGames.clear();}
void add(Socket socket,string name,bool hub=true){Client c;c.socket=socket;c.name=name;c.tlsReady=true;c.hubCapable=hub;clients.push_back(std::move(c));}
bool has(Socket socket,const string& text){auto* c=getClient(socket);for(auto& x:c->output)if(x.find(text)!=string::npos)return true;return false;}
void clearOutput(){for(auto& c:clients){c.output.clear();c.outputBytes=0;}}
HubEntry setup(int kind){
    reset();for(int i=0;i<9;++i)add(100+i,"Player"+to_string(i));
    if(kind==0){BlackjackGame g;g.host=100;g.startingChips=1000;g.totalHands=5;BlackjackPlayer a;a.socket=100;g.players.push_back(a);blackjackGames.push_back(g);}
    if(kind==1){RouletteGame g;g.host=100;g.startingChips=1000;g.totalRounds=5;RoulettePlayer a;a.socket=100;g.players.push_back(a);rouletteGames.push_back(g);}
    if(kind==2){PokerGame g;g.host=100;g.startingChips=1000;g.smallBlind=10;g.bigBlind=20;PokerGame::Player a;a.socket=100;a.name="Player0";a.chips=1000;g.players.push_back(a);pokerGames.push_back(g);}
    if(kind==3){ArenaGame g;g.host=100;g.maxPlayers=2;ArenaPlayer a;a.socket=100;g.players.push_back(a);arenaGames.push_back(g);}
    hubMaintain();return hubEntries()[0];
}
int main(){try{
    for(int kind=0;kind<4;++kind){
        auto e=setup(kind);string id=e.meta->hubId;
        hubSnapshot(*getClient(101));require(!has(101,"HUB_LOBBY|"),"invite-only leaked to browser");clearOutput();
        handleCommand(*getClient(101),"HUB_VISIBILITY|1|"+id+"|1");require(!e.meta->hubOpen,"nonhost changed visibility");
        handleCommand(*getClient(100),"HUB_VISIBILITY|1|"+id+"|1");require(e.meta->hubOpen,"host could not open lobby");
        hubSnapshot(*getClient(101));require(has(101,"HUB_LOBBY|"+id)&&has(101,e.settings),"snapshot missing settings");
        handleCommand(*getClient(101),"HUB_JOIN|2|"+id);require(isPlayerBusy(101),"browser join did not seat player");
        require(has(101,"HUB_RESULT|2|1|"+e.kind),"admission not acknowledged");
        int seats=hubEntries()[0].seats;handleCommand(*getClient(101),"HUB_JOIN|2|"+id);require(hubEntries()[0].seats==seats,"duplicate request changed seats");
        for(int n=2;n<e.capacity;++n)handleCommand(*getClient(100+n),"HUB_JOIN|1|"+id);
        require(hubEntries()[0].seats==e.capacity,"capacity not reached");
        handleCommand(*getClient(108),"HUB_JOIN|1|"+id);require(!isPlayerBusy(108)&&has(108,"Lobby full."),"last-seat race exceeded capacity");
        clearOutput();hubSnapshot(*getClient(108));require(has(108,"HUB_LOBBY|"+id),"full open lobby hidden");
        handleCommand(*getClient(100),"HUB_VISIBILITY|3|"+id+"|0");
        handleCommand(*getClient(108),"HUB_JOIN|2|"+id);require(has(108,"Lobby is invite-only."),"stale visibility allowed entry");
        // Phase changes never publish an open in-progress match.
        if(kind==0)blackjackGames[0].phase="BETTING";if(kind==1)rouletteGames[0].phase="BETTING";if(kind==2)pokerGames[0].phase="PLAYING";if(kind==3)arenaGames[0].phase="PLAYING";
        hubMaintain();clearOutput();hubSnapshot(*getClient(108));require(!has(108,"HUB_LOBBY|"),"started lobby remained listed");
        handleCommand(*getClient(108),"HUB_JOIN|3|"+id);require(has(108,"Match started."),"started match admitted player");
        e=setup(kind);id=e.meta->hubId;
        handleCommand(*getClient(100),"HUB_INVITE_PLAYER|1|"+e.kind+"|Player1");
        require(has(101,"HUB_INVITE|")&&has(100,"HUB_RESULT|1|1|"),"structured invitation missing");
        string invite=getClient(101)->hubInviteId;require(!invite.empty(),"no invite identifier");
        auto issued=getClient(101)->hubInviteAt;getClient(100)->name="RenamedHost";clearOutput();hubMaintain();
        require(getClient(101)->hubInviteId==invite&&getClient(101)->hubInviteAt==issued&&has(101,"RenamedHost"),"invite detail update extended/replaced invitation");
        handleCommand(*getClient(100),"HUB_INVITE_PLAYER|2|"+e.kind+"|Player1");require(getClient(101)->hubInviteId==invite&&has(100,"pending"),"incoming invitation replaced");
        handleCommand(*getClient(101),"HUB_ACCEPT|1|"+invite);require(isPlayerBusy(101),"invitation failed to admit");
        e=setup(kind);handleCommand(*getClient(100),"HUB_INVITE_PLAYER|1|"+e.kind+"|Player1");invite=getClient(101)->hubInviteId;
        hubMaintain(getClient(101)->hubInviteAt+chrono::minutes(2));require(getClient(101)->hubInviteId.empty()&&has(101,"Invitation expired."),"invitation did not expire");
        handleCommand(*getClient(101),"HUB_ACCEPT|1|"+invite);require(!isPlayerBusy(101),"expired invitation seated player");
        // Host replacement always resets visibility.
        e.meta->hubOpen=true;
        if(kind==0)blackjackGames[0].host=102;if(kind==1)rouletteGames[0].host=102;if(kind==2)pokerGames[0].host=102;if(kind==3)arenaGames[0].host=102;
        hubMaintain();require(!e.meta->hubOpen,"replacement host inherited public access");
        // Legacy /accept still routes through the same admission path.
        e=setup(kind);getClient(101)->hubCapable=false;getClient(101)->pendingChallenge=100;getClient(101)->pendingGame=e.kind;
        handleCommand(*getClient(101),"/accept");require(isPlayerBusy(101),"old client cannot accept invitation");
    }
    reset();add(100,"Host");add(101,"Guest");add(102,"Other");
    handleCommand(*getClient(100),"HUB_INVITE_PLAYER|1|chess|Guest");string id=getClient(101)->hubInviteId;
    handleCommand(*getClient(101),"HUB_ACCEPT|1|"+id);require(chessGames.size()==1,"chess direct challenge changed");
    handleCommand(*getClient(100),"HUB_INVITE_PLAYER|2|chess_spectate|Other");id=getClient(102)->hubInviteId;
    handleCommand(*getClient(102),"HUB_ACCEPT|1|"+id);require(chessGames[0].spectators.size()==1,"spectator invitation failed");
    clearOutput();hubSnapshot(*getClient(100));require(has(100,"Other|Spectating|chess")&&has(100,"Guest|Playing|chess"),"activity wrong");
    reset();add(100,"Probe",false);handleRecovery(clients[0],"CLIENT_CAPS|hub-v1");
    require(has(100,"HUB_CAPS|hub-v1")&&!clients[0].recoveryCapable,"hub capability coupled to session recovery");
    reset();add(100,"Legacy",false);handleCommand(clients[0],"USERS_REQUEST");require(has(100,"USERS_LIST|Legacy")&&!has(100,"HUB_PLAYER|"),"old roster changed");
    std::cout<<"PASS: all four admissions, visibility permissions/defaults, capacity races, duplicate requests, stale/expired invitations, host replacement, chess/spectators, legacy protocol\n";return 0;
}catch(const exception& e){cerr<<e.what()<<'\n';return 1;}}
