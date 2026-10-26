// Included after the legacy game handlers so all admission rules stay shared.
#pragma once
using HubClock=chrono::steady_clock;
struct HubEntry {
    HubLobby* meta; Socket host; string kind,phase,settings; int seats,capacity;
};
vector<HubEntry> hubEntries() {
    vector<HubEntry> out;
    for(auto& g:blackjackGames)out.push_back({&g,g.host,"blackjack",g.phase,
        to_string(g.startingChips)+" chips; "+to_string(g.totalHands)+" hands",(int)g.players.size(),6});
    for(auto& g:rouletteGames)out.push_back({&g,g.host,"roulette",g.phase,
        to_string(g.startingChips)+" chips; "+to_string(g.totalRounds)+" rounds",(int)g.players.size(),MAX_ROULETTE_PLAYERS});
    for(auto& g:pokerGames)out.push_back({&g,g.host,"poker",g.phase,
        to_string(g.startingChips)+" chips; blinds "+to_string(g.smallBlind)+"/"+to_string(g.bigBlind),seatedPokerPlayers(g),6});
    for(auto& g:arenaGames)out.push_back({&g,g.host,"arena",g.phase,
        g.mode+"; "+g.map+"; score "+to_string(g.scoreLimit)+"; "+to_string(g.timeLimitSeconds)+" seconds",(int)g.players.size(),g.maxPlayers});
    return out;
}
vector<string> hubSplit(const string& s) {
    vector<string> out;string x;istringstream in(s);while(getline(in,x,'|'))out.push_back(x);return out;
}
bool hubIdentifier(const string& s) {
    return !s.empty()&&s.size()<=32&&all_of(s.begin(),s.end(),[](unsigned char c){return isalnum(c)||c=='-';});
}
void hubInvalidate(Client& c,const string& why) {
    if(c.hubCapable&&!c.hubInviteId.empty())sendPacket(c.socket,"HUB_INVALID",c.hubInviteId+"|"+why);
    if(auto* from=getClient(c.hubInviteFrom);from&&from->hubCapable)
        sendPacket(from->socket,"HUB_NOTICE","Invitation to "+c.name+": "+why);
    if(!c.hubCapable&&c.pendingChallenge!=INVALID_SOCK)sendPacket(c.socket,"GAME","Challenge cancelled: "+why);
    clearPendingChallenge(c);c.hubInviteId.clear();c.hubInviteView.clear();c.hubInviteLobby.clear();c.hubInviteFrom=INVALID_SOCK;
}
void hubMaintain(HubClock::time_point now=HubClock::now()) {
    auto entries=hubEntries();
    for(auto& e:entries) {
        if(e.meta->hubOwner!=e.host){e.meta->hubOpen=false;e.meta->hubOwner=e.host;}
        if(e.phase!="LOBBY")e.meta->hubOpen=false;
    }
    for(auto& c:clients) {
        if(c.pendingChallenge==INVALID_SOCK) {
            if(!c.hubInviteId.empty())hubInvalidate(c,isPlayerBusy(c.socket)?"Invitation accepted.":"Invitation closed.");
            continue;
        }
        auto* from=getClient(c.pendingChallenge);
        bool valid=from&&!from->name.empty()&&!from->closeRequested&&!c.closeRequested&&!isPlayerBusy(c.socket);
        string lobby,settings="Standard chess",kind=c.pendingGame;int seats=1,capacity=2;
        if(kind=="chess_spectate") {
            int i=findChessGame(c.pendingChallenge);valid=valid&&i>=0;
            settings="Watch the current chess game";seats=0;capacity=0;
        } else if(kind=="chess")valid=valid&&!isPlayerBusy(c.pendingChallenge);
        else {
            bool found=false;
            for(auto& e:entries)if(e.host==c.pendingChallenge&&e.kind==kind) {
                found=true;lobby=e.meta->hubId;settings=e.settings;seats=e.seats;capacity=e.capacity;
                // Existing Poker invitations also admit between hands.
                valid=valid&&(e.phase=="LOBBY"||(kind=="poker"&&e.phase=="RESULT"))&&seats<capacity;
            }
            valid=valid&&found;
        }
        if(!c.hubInviteId.empty()) {
            valid=valid&&c.hubInviteFrom==c.pendingChallenge&&c.hubInviteLobby==lobby;
            if(now-c.hubInviteAt>=chrono::minutes(2)){hubInvalidate(c,"Invitation expired.");continue;}
        }
        if(!valid){hubInvalidate(c,"Player or lobby unavailable.");continue;}
        if(c.hubInviteId.empty()) {
            c.hubInviteId=to_string(nextHubId++);c.hubInviteLobby=lobby;c.hubInviteFrom=c.pendingChallenge;c.hubInviteAt=now;
        }
        string view=kind+"|"+from->name+"|"+to_string(seats)+"|"+to_string(capacity)+"|"+settings+"|"+(kind=="chess_spectate"?"spectate":"play");
        if(c.hubCapable&&view!=c.hubInviteView) {
            auto remaining=120-chrono::duration_cast<chrono::seconds>(now-c.hubInviteAt).count();
            sendPacket(c.socket,"HUB_INVITE",c.hubInviteId+"|"+view+"|"+to_string(remaining));
            c.hubInviteView=view;
        }
    }
}
void hubSnapshot(Client& c) {
    sendPacket(c.socket,"HUB_BEGIN","");
    auto entries=hubEntries();
    for(auto& user:clients)if(!user.name.empty()&&!user.closeRequested) {
        string activity="Available",kind="none";
        for(auto& e:entries) {
            int i=e.kind=="blackjack"?findBlackjackGame(user.socket):e.kind=="roulette"?findRouletteGame(user.socket):e.kind=="poker"?findPokerGame(user.socket):findArenaGame(user.socket);
            bool member=i>=0&&(e.kind=="blackjack"?blackjackGames[i].hubId:e.kind=="roulette"?rouletteGames[i].hubId:e.kind=="poker"?pokerGames[i].hubId:arenaGames[i].hubId)==e.meta->hubId;
            if(member){activity=e.phase=="LOBBY"?"In lobby":"Playing";kind=e.kind;break;}
        }
        int chess=findChessGame(user.socket);
        if(chess>=0){auto& g=chessGames[chess];activity=find(g.spectators.begin(),g.spectators.end(),user.socket)!=g.spectators.end()?"Spectating":"Playing";kind="chess";}
        sendPacket(c.socket,"HUB_PLAYER",user.name+"|"+activity+"|"+kind);
    }
    sendPacket(c.socket,"HUB_OWN","none");
    for(auto& e:entries) {
        if(e.phase=="LOBBY"&&e.meta->hubOpen)sendPacket(c.socket,"HUB_LOBBY",e.meta->hubId+"|"+e.kind+"|"+getName(e.host)+"|"+to_string(e.seats)+"|"+to_string(e.capacity)+"|"+e.settings);
        int idx=e.kind=="blackjack"?findBlackjackGame(c.socket):e.kind=="roulette"?findRouletteGame(c.socket):e.kind=="poker"?findPokerGame(c.socket):findArenaGame(c.socket);
        if(idx>=0) {
            string id=e.kind=="blackjack"?blackjackGames[idx].hubId:e.kind=="roulette"?rouletteGames[idx].hubId:e.kind=="poker"?pokerGames[idx].hubId:arenaGames[idx].hubId;
            if(id==e.meta->hubId)sendPacket(c.socket,"HUB_OWN",id+"|"+e.kind+"|"+(e.meta->hubOpen?"1":"0")+"|"+(e.host==c.socket?"1":"0")+"|"+e.phase);
        }
    }
    sendPacket(c.socket,"HUB_END","");
}
void hubResult(Client& c,const string& request,bool ok,const string& game,const string& message) {
    string body=request+"|"+(ok?"1":"0")+"|"+game+"|"+message;
    c.hubResults.emplace_back(request,body);if(c.hubResults.size()>16)c.hubResults.pop_front();
    sendPacket(c.socket,"HUB_RESULT",body);
}
string hubMembership(Socket socket) {
    int i=findBlackjackGame(socket);if(i>=0){auto& g=blackjackGames[i];return g.hubId+g.phase+to_string(g.host);}
    i=findRouletteGame(socket);if(i>=0){auto& g=rouletteGames[i];return g.hubId+g.phase+to_string(g.host);}
    i=findPokerGame(socket);if(i>=0){auto& g=pokerGames[i];return g.hubId+g.phase+to_string(g.host);}
    i=findArenaGame(socket);if(i>=0){auto& g=arenaGames[i];return g.hubId+g.phase+to_string(g.host);}
    return findChessGame(socket)>=0?"chess":"none";
}
void handleCommand(Client& c,const string& line) {
    hubMaintain();
    if(line=="HUB_HELLO|hub-v1") {if(!c.hubCapable){c.hubCapable=true;c.hubInviteId.clear();hubMaintain();hubSnapshot(c);}return;}
    if(line.rfind("HUB_",0)!=0) {
        auto before=hubMembership(c.socket);
        handleLegacyCommand(c,line);hubMaintain();
        if(c.hubCapable&&before!=hubMembership(c.socket))hubSnapshot(c);
        return;
    }
    if(!c.hubCapable)return;
    auto now=HubClock::now();
    if(line=="HUB_REQUEST") {
        if(now-c.hubSnapshotAt<chrono::seconds(1))return;
        c.hubSnapshotAt=now;hubSnapshot(c);return;
    }
    auto f=hubSplit(line);
    if(f.size()<2||!hubIdentifier(f[1]))return;
    string request=f[1];
    for(auto& cached:c.hubResults)if(cached.first==request){sendPacket(c.socket,"HUB_RESULT",cached.second);return;}
    while(!c.hubCommands.empty()&&now-c.hubCommands.front()>=chrono::minutes(1))c.hubCommands.pop_front();
    if(c.hubCommands.size()>=20){hubResult(c,request,false,"none","Too many requests. Wait a minute.");return;}
    c.hubCommands.push_back(now);c.hubError.clear();
    if(f[0]=="HUB_VISIBILITY"&&f.size()==4&&(f[3]=="0"||f[3]=="1")) {
        for(auto& e:hubEntries())if(e.meta->hubId==f[2]) {
            bool ok=e.host==c.socket&&e.phase=="LOBBY";
            if(ok)e.meta->hubOpen=f[3]=="1";
            hubResult(c,request,ok,e.kind,ok?"Lobby visibility updated.":"Only the host of a waiting lobby can change access.");hubSnapshot(c);return;
        }
        hubResult(c,request,false,"none","Lobby closed.");return;
    }
    if(f[0]=="HUB_INVITE_PLAYER"&&f.size()==4) {
        Client* target=getClientByName(f[3]);
        string cmd=f[2]=="arena"?"ARENA_INVITE|":f[2]=="chess_spectate"?"CHESS_SPECTATE_INVITE|":
            f[2]=="chess"?"/chess ":f[2]=="blackjack"?"/blackjack ":f[2]=="roulette"?"/roulette ":f[2]=="poker"?"/poker ":"";
        if(!target||target->name.empty()||target->closeRequested||cmd.empty()) {
            hubResult(c,request,false,"none","Player unavailable.");return;
        }
        // Legacy commands retain authority over host, capacity, and busy checks.
        handleLegacyCommand(c,cmd+target->name);hubMaintain();
        bool ok=target->pendingChallenge==c.socket&&target->pendingGame==f[2]&&c.hubError.empty();
        hubResult(c,request,ok,f[2],ok?"Invitation sent to "+target->name+".":c.hubError.empty()?"Invitation unavailable.":c.hubError);
        hubSnapshot(c);return;
    }
    if(f[0]=="HUB_JOIN"&&f.size()==3) {
        if(isPlayerBusy(c.socket)){hubResult(c,request,false,"none","Leave your current game first.");return;}
        for(auto& e:hubEntries())if(e.meta->hubId==f[2]) {
            if(e.phase!="LOBBY"||!e.meta->hubOpen||e.seats>=e.capacity) {
                hubResult(c,request,false,e.kind,e.phase!="LOBBY"?"Match started.":!e.meta->hubOpen?"Lobby is invite-only.":"Lobby full.");return;
            }
            // Transient internal invitation; the exact legacy admission handler owns seating.
            if(c.pendingChallenge!=INVALID_SOCK)hubInvalidate(c,"Invitation replaced by joining a lobby.");
            c.pendingChallenge=e.host;c.pendingGame=e.kind;
            handleLegacyCommand(c,"/accept");
            bool ok=isPlayerBusy(c.socket);
            hubResult(c,request,ok,e.kind,ok?"Joined lobby.":c.hubError.empty()?"Lobby unavailable.":c.hubError);
            hubMaintain();hubSnapshot(c);return;
        }
        hubResult(c,request,false,"none","Lobby closed.");return;
    }
    if((f[0]=="HUB_ACCEPT"||f[0]=="HUB_DECLINE")&&f.size()==3) {
        if(c.hubInviteId.empty()||c.hubInviteId!=f[2]){hubResult(c,request,false,"none","Invitation expired or unavailable.");return;}
        string kind=c.pendingGame;
        bool decline=f[0]=="HUB_DECLINE";
        handleLegacyCommand(c,decline?"/decline":"/accept");
        bool ok=decline||isPlayerBusy(c.socket);
        hubResult(c,request,ok,kind,ok?(decline?"Invitation declined.":"Joined game."):c.hubError.empty()?"Player or lobby unavailable.":c.hubError);
        hubMaintain();hubSnapshot(c);return;
    }
    hubResult(c,request,false,"none","Invalid hub request.");
}
