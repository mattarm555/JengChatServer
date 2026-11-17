#define main jengchat_server_main
#include "../server.cpp"
#undef main
#include <stdexcept>
void require(bool x,const char* why){if(!x)throw runtime_error(why);}
chrono::steady_clock::time_point fakeNow=chrono::steady_clock::time_point{}+chrono::hours(1);
chrono::steady_clock::time_point fakeClock(){return fakeNow;}
void advance(long long ms){fakeNow+=chrono::milliseconds(ms);}
void reset(){clients.clear();blackjackGames.clear();rouletteGames.clear();pokerGames.clear();arenaGames.clear();chessGames.clear();}
void add(Socket socket,string name,bool clock){Client c;c.socket=socket;c.name=name;c.tlsReady=true;c.chessClockCapable=clock;clients.push_back(std::move(c));}
bool has(Socket socket,const string& text){auto* c=getClient(socket);for(auto& x:c->output)if(x.find(text)!=string::npos)return true;return false;}
int count(Socket socket,const string& prefix){int n=0;for(auto& x:getClient(socket)->output)if(x.rfind(prefix,0)==0)++n;return n;}
string last(Socket socket,const string& prefix){
    string found;for(auto& x:getClient(socket)->output)if(x.rfind(prefix,0)==0)found=x;
    while(!found.empty()&&(found.back()=='\n'||found.back()=='\r'))found.pop_back();
    return found;
}
void clearOutput(){for(auto& c:clients){c.output.clear();c.outputBytes=0;}}
void cmd(Socket s,const string& line){handleCommand(*getClient(s),line);}
// Starts White=100 vs Black=101 with an accepted /chess challenge.
void start(const string& tc,bool whiteClock=true,bool blackClock=true){
    reset();add(100,"Alice",whiteClock);add(101,"Bob",blackClock);add(102,"Watcher",true);add(103,"OldWatcher",false);
    cmd(100,"/chess Bob"+(tc.empty()?"":" "+tc));require(getClient(101)->pendingGame=="chess","challenge not pending");
    cmd(101,"/accept");require(chessGames.size()==1,"challenge did not start a game");
}
int main(){try{
    chessClockNow=fakeClock;

    // Capability probe: answered, flagged, unrelated caps untouched.
    reset();add(100,"Probe",false);handleRecovery(clients[0],"CLIENT_CAPS|chess-clock-v1");
    require(has(100,"CHESS_CAPS|clock-v1")&&clients[0].chessClockCapable,"chess clock cap not acknowledged");
    require(!clients[0].recoveryCapable&&!clients[0].hubCapable&&!clients[0].loungeCapable,"clock cap coupled to other caps");
    clearOutput();handleRecovery(clients[0],"CLIENT_CAPS|chess-clock-v9");require(clients[0].output.empty(),"unknown cap answered");

    // Bad time arguments are rejected and leave no pending challenge.
    for(string bad:{"0+0","61+0","5+61","5","+3","5+","a+b","5+3+1","-1+2","100+1","5+3 extra"}){
        reset();add(100,"Alice",true);add(101,"Bob",true);cmd(100,"/chess Bob "+bad);
        require(getClient(101)->pendingChallenge==INVALID_SOCK&&has(100,"Time control must be"),"bad time control accepted");
    }
    for(string good:{"1+0","60+60","5+3","05+03"}){
        reset();add(100,"Alice",true);add(101,"Bob",true);cmd(100,"/chess Bob "+good);
        require(getClient(101)->pendingGame=="chess"&&getClient(101)->pendingClockMinutes>0,"valid time control rejected");
    }

    // Untimed challenge is unchanged: no CHESS_CLOCK for anybody.
    start("");require(!chessGames[0].timed,"untimed game became timed");
    cmd(100,"CHESS_MOVE|e2|e4");cmd(101,"CHESS_MOVE|e7|e5");
    for(Socket s:{100,101,102,103})require(count(s,"CHESS_CLOCK|")==0,"untimed game sent CHESS_CLOCK");

    // Timed challenge: starting clock, nothing running before both first moves.
    start("1+2");ChessGame* g=&chessGames[0];
    require(g->timed&&g->baseMs==60000&&g->incMs==2000&&g->whiteMs==60000&&g->blackMs==60000,"timed fields");
    require(has(100,"Time control 1+2")&&last(100,"CHESS_CLOCK|")=="CHESS_CLOCK|60000|60000|-|60|2","start clock");
    getClient(102)->pendingChallenge=100;getClient(102)->pendingGame="chess_spectate";cmd(102,"/accept");
    getClient(103)->pendingChallenge=100;getClient(103)->pendingGame="chess_spectate";cmd(103,"/accept");
    g=&chessGames[0];require(g->spectators.size()==2,"spectators not seated");
    require(last(102,"CHESS_CLOCK|")=="CHESS_CLOCK|60000|60000|-|60|2","capable spectator join missed clock");
    require(count(103,"CHESS_CLOCK|")==0,"non-capable spectator got CHESS_CLOCK");
    clearOutput();advance(10000);cmd(100,"CHESS_MOVE|e2|e4");g=&chessGames[0];
    require(g->whiteMs==60000&&!g->clockRunning,"white charged before both first moves");
    require(last(101,"CHESS_CLOCK|")=="CHESS_CLOCK|60000|60000|-|60|2","clock after white's first move");
    advance(15000);cmd(101,"CHESS_MOVE|e7|e5");g=&chessGames[0];
    require(g->blackMs==60000&&g->clockRunning&&g->clockWhite,"white clock not started after black's first move");
    require(last(100,"CHESS_CLOCK|")=="CHESS_CLOCK|60000|60000|WHITE|60|2","running clock packet");
    require(last(102,"CHESS_CLOCK|")=="CHESS_CLOCK|60000|60000|WHITE|60|2","spectator missed running clock");
    require(count(103,"CHESS_CLOCK|")==0,"non-capable spectator got running clock");

    // Running clock and increment: white spends 3s and gains 2s.
    advance(3000);cmd(100,"CHESS_MOVE|g1|f3");g=&chessGames[0];
    require(g->whiteMs==59000&&g->clockRunning&&!g->clockWhite,"increment/elapsed wrong for white");
    require(last(101,"CHESS_CLOCK|")=="CHESS_CLOCK|59000|60000|BLACK|60|2","clock after increment");
    advance(500);processChessClocks();require(chessGames.size()==1,"flagged too early");
    advance(1500);cmd(101,"CHESS_MOVE|b8|c6");g=&chessGames[0];
    require(g->blackMs==60000&&g->clockWhite,"black increment wrong");
    // A wrong-turn move does not touch the clock.
    cmd(101,"CHESS_MOVE|g8|f6");require(chessGames[0].blackMs==60000&&chessGames[0].clockWhite,"wrong-turn move changed clock");

    // Flag via the loop tick: white's 59s run out.
    clearOutput();advance(58999);processChessClocks();require(chessGames.size()==1,"flagged with time left");
    advance(1);processChessClocks();require(chessGames.empty(),"flag did not end the game");
    for(Socket s:{100,101,102,103})require(has(s,"CHESS_END|TIME - Bob wins on time.|"),"flag end text missing");
    require(last(100,"CHESS_CLOCK|")=="CHESS_CLOCK|0|60000|-|60|2"&&last(102,"CHESS_CLOCK|")=="CHESS_CLOCK|0|60000|-|60|2","final clock");
    require(count(103,"CHESS_CLOCK|")==0,"final clock reached non-capable spectator");
    require(has(100,"Alice ran out of time."),"flag notice");

    // A move arriving after the flag fell loses on time rather than playing.
    start("1+0");cmd(100,"CHESS_MOVE|e2|e4");cmd(101,"CHESS_MOVE|e7|e5");advance(61000);
    cmd(100,"CHESS_MOVE|g1|f3");require(chessGames.empty()&&has(101,"TIME - Bob wins on time."),"late move was not flagged");

    // Resignation freezes and sends the final clock.
    start("1+0");cmd(100,"CHESS_MOVE|e2|e4");cmd(101,"CHESS_MOVE|e7|e5");clearOutput();advance(4000);cmd(100,"/resign");
    require(chessGames.empty()&&last(101,"CHESS_CLOCK|")=="CHESS_CLOCK|56000|60000|-|60|0"&&has(101,"wins by resignation"),"resign clock");

    // Untimed fallback with a non-capable peer.
    for(int side=0;side<2;++side){
        start("5+3",side!=0,side!=1);require(!chessGames[0].timed,"timed game with non-capable peer");
        string who=side==0?"Alice":"Bob";
        require(has(100,"CHESS_NOTICE|"+who+"'s client doesn't support clocks; playing untimed.")&&
                has(101,"CHESS_NOTICE|"+who+"'s client doesn't support clocks; playing untimed."),"fallback notice missing");
        cmd(100,"CHESS_MOVE|e2|e4");cmd(101,"CHESS_MOVE|e7|e5");advance(400000);processChessClocks();
        require(chessGames.size()==1,"untimed game flagged");
        for(Socket s:{100,101})require(count(s,"CHESS_CLOCK|")==0,"fallback game sent CHESS_CLOCK");
    }

    // Engine games stay untimed even for capable clients.
    reset();add(100,"Alice",true);cmd(100,"CHESS_ENGINE|1");require(chessGames.size()==1&&!chessGames[0].timed&&count(100,"CHESS_CLOCK|")==0,"engine game timed");

    // Hub invites carry the time control; the 4-field form stays untimed.
    reset();add(100,"Alice",true);add(101,"Bob",true);getClient(100)->hubCapable=getClient(101)->hubCapable=true;
    cmd(100,"HUB_INVITE_PLAYER|1|chess|Bob|3+2");require(has(101,"HUB_INVITE|")&&has(101,"|Chess 3+2|play|"),"hub invite lost time control");
    cmd(101,"HUB_ACCEPT|1|"+getClient(101)->hubInviteId);require(chessGames.size()==1&&chessGames[0].timed&&chessGames[0].baseMs==180000,"hub accept untimed");
    reset();add(100,"Alice",true);add(101,"Bob",true);getClient(100)->hubCapable=getClient(101)->hubCapable=true;
    cmd(100,"HUB_INVITE_PLAYER|1|chess|Bob|99+0");require(getClient(101)->pendingChallenge==INVALID_SOCK&&has(100,"HUB_RESULT|1|0|chess|Time control must be"),"hub bad time accepted");
    cmd(100,"HUB_INVITE_PLAYER|2|blackjack|Bob|5+3");require(has(100,"HUB_RESULT|2|0|none|Invalid hub request.")&&getClient(101)->pendingChallenge==INVALID_SOCK,"time field accepted for non-chess invite");
    cmd(100,"HUB_INVITE_PLAYER|3|chess|Bob");require(has(101,"|Standard chess|play|"),"plain hub chess invite changed");
    cmd(101,"HUB_ACCEPT|1|"+getClient(101)->hubInviteId);require(chessGames.size()==1&&!chessGames[0].timed,"plain hub invite timed");

    // Declining clears the pending time control.
    reset();add(100,"Alice",true);add(101,"Bob",true);cmd(100,"/chess Bob 5+0");cmd(101,"/decline");
    require(getClient(101)->pendingClockMinutes==0&&getClient(101)->pendingClockIncrement==0,"decline kept time control");

    std::cout<<"PASS: caps, bad/valid time controls, untimed unchanged, first-move rule, increment, tick and late-move flags, resign clock, "
               "non-capable fallback, capable/non-capable spectators, engine untimed, hub time controls, decline cleanup\n";return 0;
}catch(const exception& e){cerr<<e.what()<<'\n';return 1;}}
