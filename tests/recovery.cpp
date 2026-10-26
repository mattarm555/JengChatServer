#define main jengchat_server_main
#include "../server.cpp"
#undef main
#include <stdexcept>
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void reset(){clients.clear();chessGames.clear();blackjackGames.clear();rouletteGames.clear();pokerGames.clear();arenaGames.clear();sessions=SessionStore{};}
void add(Socket socket,const std::string& name){Client c;c.socket=socket;c.name=name;c.tlsReady=true;clients.push_back(std::move(c));}
int main(){try{
    using namespace std::chrono;
    SessionStore store;auto now=SessionStore::Clock::now();auto token=store.issue("Alice",now);auto digest=SessionStore::hash(token);
    require(token.size()==64&&digest!=token&&store.find(digest,now),"token generation/hash");
    require(SessionStore::hash("bad").empty(),"malformed token accepted");
    store.detach(digest,now);store.detach(digest,now+minutes(4));
    require(store.find(digest,now+minutes(4)),"session expired early");
    require(!store.find(digest,now+minutes(5)),"detach extended expiration");
    token=store.issue("Alice",now);digest=SessionStore::hash(token);
    require(!store.find(digest,now+hours(24)),"absolute expiry");
    token=store.issue("Alice",now);store.revokeAccount("Alice");require(!store.find(SessionStore::hash(token),now),"account revocation");
    for(int n=0;n<1024;++n)require(!store.issue("Alice",now).empty(),"capacity prematurely full");
    require(store.issue("Alice",now).empty(),"unbounded sessions");
    for(int kind=0;kind<9;++kind){
        reset();add(100,"Alice");add(101,"Bob");add(102,"Watcher");
        if(kind<=2){ChessGame g=makeChessGame(100,101);g.engineGame=kind==1;g.spectators={102};chessGames.push_back(g);}
        if(kind==3||kind==4){BlackjackGame g;g.host=100;BlackjackPlayer a,b;a.socket=100;b.socket=101;g.players={a,b};blackjackGames.push_back(g);}
        if(kind==5){RouletteGame g;g.host=100;RoulettePlayer a,b;a.socket=100;b.socket=101;g.players={a,b};rouletteGames.push_back(g);}
        if(kind==6){PokerGame g;g.host=100;PokerGame::Player a,b;a.socket=100;a.name="Alice";b.socket=101;b.name="Bob";g.players={a,b};pokerGames.push_back(g);}
        if(kind==7||kind==8){ArenaGame g;g.host=100;g.phase=kind==8?"PLAYING":"LOBBY";ArenaPlayer a,b;a.socket=100;b.socket=101;g.players={a,b};arenaGames.push_back(g);}
        int departing=kind==2?2:kind==4?1:0;Socket socket=clients[departing].socket;
        disconnectClient(departing);require(clients.size()==2&&!getClient(socket),"disconnect did not remove transport");
        if(kind!=2)require(chessGames.empty()&&blackjackGames.size()<=size_t(kind==4)&&rouletteGames.empty()&&pokerGames.empty()&&arenaGames.empty(),"game cleanup");
        else require(chessGames.size()==1&&chessGames[0].spectators.empty(),"spectator cleanup");
    }
    reset();add(100,"Alice");auto secret=sessions.issue("Alice");auto key=SessionStore::hash(secret);clients[0].sessionHash=key;
    add(101,"");clients[1].pendingResume=key;completeResume(101);
    require(clients.size()==1&&clients[0].socket==101&&clients[0].name=="Alice","stale connection not replaced");
    require(sessions.find(key)&&sessions.find(key)->active,"resume did not attach");
    handleRecovery(clients[0],"SESSION_LOGOUT|");require(!sessions.find(key),"logout did not revoke");
    std::promise<AccountResult> mutation;pendingAccountChanges.push_back({"Alice",mutation.get_future().share()});
    auto abandoned=sessions.issue("Alice");mutation.set_value({true,"Alice","changed"});
    clients.clear();processAccountRevocations();require(!sessions.find(SessionStore::hash(abandoned)),"abandoned account mutation failed to revoke");
    std::cout<<"PASS: session expiry, revocation, capacity, all game cleanup, spectator/engine cleanup, stale transport replacement and logout\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
