// Test real server serialization and output queues; do not run the server main.
#define main jengchat_server_main
#include "../server.cpp"
#undef main
#include <stdexcept>
#include <fstream>

static void Require(bool value, const char* message)
{
    if(!value)throw std::runtime_error(message);
}
static bool Has(const Client& client, const std::string& packet)
{
    return std::find(client.output.begin(),client.output.end(),packet+"\n")!=client.output.end();
}
static void ClearOutput(){for(auto& client:clients){client.output.clear();client.outputBytes=0;}}

int main()
{
    try
    {
        RouletteGame game;game.phase="BETTING";game.host=100;game.currentRound=1;game.totalRounds=2;
        for(int i=0;i<6;++i)
        {
            Client client;client.socket=100+i;client.name="Player"+std::to_string(i);client.tlsReady=true;
            clients.push_back(std::move(client));
            RoulettePlayer player;player.socket=100+i;player.chips=1000;
            player.bets.push_back({"NUMBER",17,10*(i+1)});
            if(i==0)player.bets.push_back({"RED",0,25});
            game.players.push_back(player);
        }
        auto encoded=encodeRouletteTableBets(game);
        Require(encoded.find("Player0~NUMBER~17~10")!=std::string::npos,"first player's number bet missing");
        Require(encoded.find("Player5~NUMBER~17~60")!=std::string::npos,"sixth player's overlapping number bet missing");
        Require(encoded.find("Player0~RED~0~25")!=std::string::npos,"outside bet missing");
        sendRouletteState(game);
        std::ofstream snapshot("build/roulette-snapshot.txt");
        for(const auto& packet:clients[0].output)snapshot<<packet;
        snapshot.close();
        for(const auto& client:clients)
        {
            Require(Has(client,"RLT_TABLE_BETS|"+encoded),"seated recipient did not receive identical shared snapshot");
            Require(client.output.size()==3,"expected shared snapshot, state and private bets");
            Require(client.output.front()=="RLT_TABLE_BETS|"+encoded+"\n","shared snapshot must precede new round state");
        }
        Require(Has(clients[1],"RLT_BETS|NUMBER~17~20"),"existing private bet packet changed");
        game.players[0].bets.clear();ClearOutput();sendRouletteState(game);
        encoded=encodeRouletteTableBets(game);
        Require(encoded.find("Player0~")==std::string::npos,"cleared player's markers remained");
        Require(encoded.find("Player1~NUMBER~17~20")!=std::string::npos,"clearing removed another player's bets");
        game.players.erase(game.players.begin()+1);ClearOutput();sendRouletteState(game);
        Require(encodeRouletteTableBets(game).find("Player1~")==std::string::npos,"departed player's markers remained");
        ClearOutput();startRouletteBetting(game);
        for(const auto& player:game.players)
        {
            auto client=getClient(player.socket);
            Require(client&&Has(*client,"RLT_TABLE_BETS|-"),"next round failed to clear shared markers");
            Require(client&&Has(*client,"RLT_BETS|-"),"next round failed to clear private bets");
        }
        std::cout<<"PASS: six recipients, overlapping/number/outside bets, private compatibility, clearing, departure and next-round reset.\n";
        return 0;
    }
    catch(const std::exception& error){std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}
}
