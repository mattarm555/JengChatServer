#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cctype>
#include <random>
#include <csignal>
#include <chrono>
#include <cmath>

#ifdef _WIN32

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

using Socket = SOCKET;

#define CLOSE_SOCKET closesocket
#define INVALID_SOCK INVALID_SOCKET
#define SOCKET_ERR SOCKET_ERROR

#else

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <signal.h>

using Socket = int;

#define CLOSE_SOCKET close
#define INVALID_SOCK -1
#define SOCKET_ERR -1

#endif

using namespace std;

const int PORT = 54000;
const int BUFFER_SIZE = 2048;
const int MAX_BLACKJACK_HANDS = 50;
const int MAX_STARTING_CHIPS = 1000000;
const int MAX_POKER_CHIPS = 1000000;
const int MAX_ROULETTE_ROUNDS = 100;
const int MAX_ROULETTE_PLAYERS = 6;
const int MAX_ARENA_PLAYERS = 6;
const int ARENA_COLOR_COUNT = 16;
const int ARENA_MAX_HEALTH = 100;
const int ARENA_SHOT_DAMAGE = 25;
const float ARENA_HALF = 25.0f;
const float ARENA_TANK_RADIUS = 0.82f;
const float ARENA_PLAYER_SPEED = 7.0f;
const float ARENA_BULLET_SPEED = 36.0f;
const float ARENA_BULLET_RADIUS = 0.16f;
const float ARENA_MINE_RADIUS = 0.42f;
const float ARENA_FIRE_COOLDOWN = 0.16f;
const float ARENA_MINE_COOLDOWN = 0.50f;
const float ARENA_RESPAWN_TIME = 2.0f;
const int ARENA_MINE_DAMAGE = 100;
const int ARENA_MAX_MINES_PER_PLAYER = 3;
const float ARENA_PI = 3.14159265358979323846f;


bool initializeSocketLibrary() {
#ifdef _WIN32
    WSADATA wsaData;

    if (
        WSAStartup(
            MAKEWORD(2, 2),
            &wsaData
        ) != 0
    ) {
        return false;
    }
#else
    // Prevent a disconnected client from terminating the whole server
    // when send() writes to a closed socket.
    signal(SIGPIPE, SIG_IGN);
#endif

    return true;
}

void cleanupSocketLibrary() {
#ifdef _WIN32
    WSACleanup();
#endif
}

struct Client {
    Socket socket;
    string name;
    string inputBuffer;

    Socket pendingChallenge = INVALID_SOCK;
    string pendingGame;
    int pendingChips = 0;
    int pendingHands = 0;
};

struct TicTacToeGame {
    Socket playerX;
    Socket playerO;

    char board[9] = {
        ' ', ' ', ' ',
        ' ', ' ', ' ',
        ' ', ' ', ' '
    };

    Socket turn;
};

struct Card {
    string rank;
    char suit;
    int value;
};

struct BlackjackHand {
    vector<Card> cards;
    int bet = 0;
    bool stood = false;
    bool busted = false;
    bool doubled = false;
    bool fromSplit = false;
};

struct BlackjackPlayer {
    Socket socket = INVALID_SOCK;
    int chips = 0;
    int pendingBet = 0;
    bool betPlaced = false;
    vector<BlackjackHand> hands;
};

struct BlackjackGame {
    Socket host = INVALID_SOCK;
    int startingChips = 0;
    int totalHands = 1;
    int currentHand = 1;

    vector<BlackjackPlayer> players;

    vector<Card> deck;
    vector<Card> dealerHand;

    bool handInProgress = false;
    bool dealerRevealed = false;
    bool awaitingNextHand = false;

    // When the final active player places a bet, keep the table in
    // BETTING for one second so everyone can actually see that bet
    // before cards begin dealing.
    bool dealScheduled = false;
    chrono::steady_clock::time_point dealAt{};

    Socket turn = INVALID_SOCK;
    int turnHandIndex = 0;

    string phase = "LOBBY";
    string status = "Invite players to a Blackjack match.";
};


struct RouletteBet {
    string type;
    int value = 0;
    int amount = 0;
};

struct RoulettePlayer {
    Socket socket = INVALID_SOCK;
    int chips = 0;
    bool ready = false;
    vector<RouletteBet> bets;
};

struct RouletteGame {
    Socket host = INVALID_SOCK;
    int startingChips = 0;
    int totalRounds = 1;
    int currentRound = 1;
    int lastResult = -1;

    vector<RoulettePlayer> players;

    string phase = "LOBBY";
    string status = "Invite players to a Roulette table.";
};

struct ChessGame {
    Socket white;
    Socket black;
    Socket turn;
    vector<Socket> spectators;

    // Uppercase pieces are White, lowercase pieces are Black.
    // Rows: 0 = rank 8, 7 = rank 1.
    char board[8][8];

    bool whiteKingMoved = false;
    bool blackKingMoved = false;
    bool whiteARookMoved = false;
    bool whiteHRookMoved = false;
    bool blackARookMoved = false;
    bool blackHRookMoved = false;

    // En-passant destination square, or -1/-1 when unavailable.
    int enPassantRow = -1;
    int enPassantCol = -1;
};


enum class PokerStage {
    PREFLOP,
    FLOP,
    TURN,
    RIVER,
    SHOWDOWN
};

struct PokerGame {
    Socket host = INVALID_SOCK;

    struct Player {
        Socket socket = INVALID_SOCK;
        string name;
        int chips = 0;
        int roundBet = 0;
        int handContribution = 0;
        bool acted = false;
        bool folded = false;
        bool left = false;
        vector<Card> hole;
    };

    struct Standing {
        string name;
        int chips = 0;
        bool left = false;
    };

    vector<Player> players;
    vector<Standing> departedStandings;

    int startingChips = 0;
    string phase = "LOBBY";
    string status = "Table created. Invite up to five players.";

    int smallBlind = 0;
    int bigBlind = 0;

    Socket dealer = INVALID_SOCK;
    Socket smallBlindPlayer = INVALID_SOCK;
    Socket bigBlindPlayer = INVALID_SOCK;
    Socket turn = INVALID_SOCK;

    vector<Card> deck;
    vector<Card> community;

    int pot = 0;
    int currentBet = 0;
    int lastRaiseSize = 0;

    bool handActive = false;
    int handNumber = 0;
    PokerStage stage = PokerStage::PREFLOP;
};


struct ArenaProjectile {
    float x = 0.0f;
    float y = 1.10f;
    float z = 0.0f;
    float vx = 0.0f;
    float vz = 0.0f;
    Socket owner = INVALID_SOCK;
    bool active = true;
};

struct ArenaMine {
    float x = 0.0f;
    float z = 0.0f;
    Socket owner = INVALID_SOCK;
    int colorIndex = -1;
    int team = -1;
    bool active = true;
};

struct ArenaPlayer {
    Socket socket = INVALID_SOCK;
    bool ready = false;
    int colorIndex = -1;
    int team = -1; // -1 FFA/duel, 0 red, 1 blue

    // Authoritative realtime movement state.
    float x = 0.0f;
    float z = 0.0f;
    float spawnX = 0.0f;
    float spawnZ = 0.0f;
    float bodyYaw = 180.0f;
    float aimYaw = 180.0f;

    int health = ARENA_MAX_HEALTH;
    bool alive = true;
    int kills = 0;
    int deaths = 0;
    int damageDealt = 0;
    int damageTaken = 0;
    float respawnTimer = 0.0f;

    int lastInputSequence = -1;
    bool inputClockReady = false;
    chrono::steady_clock::time_point lastInputAt{};

    bool fireClockReady = false;
    chrono::steady_clock::time_point lastFireAt{};

    bool mineClockReady = false;
    chrono::steady_clock::time_point lastMineAt{};
};

struct ArenaGame {
    Socket host = INVALID_SOCK;
    string mode = "SCORE_FFA";
    string map = "REACTOR_YARD";
    int maxPlayers = 4;
    int scoreLimit = 5;
    int timeLimitSeconds = 180;

    vector<ArenaPlayer> players;
    vector<ArenaProjectile> projectiles;
    vector<ArenaMine> mines;

    string phase = "LOBBY";
    string status = "Invite players to JENG Arena.";

    int worldSequence = 0;
    float timeRemainingSeconds = 180.0f;

    bool broadcastClockReady = false;
    chrono::steady_clock::time_point lastBroadcastAt{};

    bool simClockReady = false;
    chrono::steady_clock::time_point lastSimAt{};
};

vector<Client> clients;
vector<TicTacToeGame> ticTacToeGames;
vector<BlackjackGame> blackjackGames;
vector<ChessGame> chessGames;
vector<PokerGame> pokerGames;
vector<RouletteGame> rouletteGames;
vector<ArenaGame> arenaGames;

static mt19937 rng(random_device{}());


// ============================================================
// NETWORK HELPERS
// ============================================================

bool sendAll(Socket socket, const string& data) {
    int total = 0;

    while (total < (int)data.size()) {
        int sent = send(
            socket,
            data.c_str() + total,
            (int)data.size() - total,
            0
        );

        if (sent == SOCKET_ERR || sent == 0)
            return false;

        total += sent;
    }

    return true;
}

void sendPacket(
    Socket socket,
    const string& type,
    const string& text
) {
    sendAll(
        socket,
        type + "|" + text + "\n"
    );
}

void sendReady(Socket socket) {
    sendPacket(
        socket,
        "READY",
        ""
    );
}

Client* getClient(Socket socket) {
    for (Client& c : clients) {
        if (c.socket == socket)
            return &c;
    }

    return nullptr;
}

string lowerCopy(string value) {
    transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char ch) {
            return (char)tolower(ch);
        }
    );

    return value;
}

Client* getClientByName(const string& name) {
    string wanted = lowerCopy(name);

    for (Client& c : clients) {
        if (lowerCopy(c.name) == wanted)
            return &c;
    }

    return nullptr;
}

string getName(Socket socket) {
    Client* c = getClient(socket);

    if (c)
        return c->name;

    return "Unknown";
}

void clearPendingChallenge(Client& client) {
    client.pendingChallenge = INVALID_SOCK;
    client.pendingGame.clear();
    client.pendingChips = 0;
    client.pendingHands = 0;
}

void broadcastSystem(const string& message) {
    for (Client& c : clients) {
        if (!c.name.empty()) {
            sendPacket(
                c.socket,
                "SYS",
                message
            );

            sendReady(c.socket);
        }
    }
}

void broadcastChat(
    const string& name,
    const string& message
) {
    string packet =
        "CHAT|" +
        name +
        "|" +
        message +
        "\n";

    for (Client& c : clients) {
        if (!c.name.empty()) {
            sendAll(
                c.socket,
                packet
            );

            sendReady(c.socket);
        }
    }
}


// ============================================================
// GAME LOOKUPS
// ============================================================

int findTicTacToeGame(Socket socket) {
    for (int i = 0; i < (int)ticTacToeGames.size(); i++) {
        if (
            ticTacToeGames[i].playerX == socket ||
            ticTacToeGames[i].playerO == socket
        ) {
            return i;
        }
    }

    return -1;
}

int findBlackjackGame(Socket socket) {
    for (int i = 0; i < (int)blackjackGames.size(); i++) {
        for (const BlackjackPlayer& player : blackjackGames[i].players) {
            if (player.socket == socket)
                return i;
        }
    }

    return -1;
}

int findChessGame(Socket socket) {
    for (int i = 0; i < (int)chessGames.size(); i++) {
        if (
            chessGames[i].white == socket ||
            chessGames[i].black == socket
        ) {
            return i;
        }

        for (Socket spectator : chessGames[i].spectators)
            if (spectator == socket)
                return i;
    }

    return -1;
}

bool chessIsPlayer(
    const ChessGame& game,
    Socket socket
) {
    return game.white == socket || game.black == socket;
}

bool chessIsSpectator(
    const ChessGame& game,
    Socket socket
) {
    return find(
        game.spectators.begin(),
        game.spectators.end(),
        socket
    ) != game.spectators.end();
}

int findRouletteGame(Socket socket) {
    for (int i = 0; i < (int)rouletteGames.size(); i++) {
        for (const RoulettePlayer& player : rouletteGames[i].players) {
            if (player.socket == socket)
                return i;
        }
    }

    return -1;
}

int findPokerGame(Socket socket) {
    for (int i = 0; i < (int)pokerGames.size(); i++) {
        for (const PokerGame::Player& player : pokerGames[i].players)
            if (!player.left && player.socket == socket)
                return i;
    }

    return -1;
}

int findArenaGame(Socket socket) {
    for (int i = 0; i < (int)arenaGames.size(); i++) {
        for (const ArenaPlayer& player : arenaGames[i].players) {
            if (player.socket == socket)
                return i;
        }
    }

    return -1;
}

bool isPlayerBusy(Socket socket) {
    return
        findTicTacToeGame(socket) != -1 ||
        findBlackjackGame(socket) != -1 ||
        findChessGame(socket) != -1 ||
        findPokerGame(socket) != -1 ||
        findRouletteGame(socket) != -1 ||
        findArenaGame(socket) != -1;
}


// ============================================================
// TIC-TAC-TOE
// ============================================================

void sendTicTacToeLine(
    TicTacToeGame& game,
    const string& text
) {
    sendPacket(
        game.playerX,
        "GAME",
        text
    );

    sendPacket(
        game.playerO,
        "GAME",
        text
    );
}

char displayCell(TicTacToeGame& game, int index) {
    if (game.board[index] != ' ')
        return game.board[index];

    return '1' + index;
}

void showTicTacToeBoard(
    TicTacToeGame& game,
    bool showTurn = true
) {
    sendTicTacToeLine(game, "");
    sendTicTacToeLine(game, "========== TIC-TAC-TOE ==========");

    sendTicTacToeLine(
        game,
        getName(game.playerX) +
        " (X) vs " +
        getName(game.playerO) +
        " (O)"
    );

    sendTicTacToeLine(game, "");

    string row1;
    row1 += " ";
    row1 += displayCell(game, 0);
    row1 += " | ";
    row1 += displayCell(game, 1);
    row1 += " | ";
    row1 += displayCell(game, 2);

    string row2;
    row2 += " ";
    row2 += displayCell(game, 3);
    row2 += " | ";
    row2 += displayCell(game, 4);
    row2 += " | ";
    row2 += displayCell(game, 5);

    string row3;
    row3 += " ";
    row3 += displayCell(game, 6);
    row3 += " | ";
    row3 += displayCell(game, 7);
    row3 += " | ";
    row3 += displayCell(game, 8);

    sendTicTacToeLine(game, row1);
    sendTicTacToeLine(game, "---+---+---");
    sendTicTacToeLine(game, row2);
    sendTicTacToeLine(game, "---+---+---");
    sendTicTacToeLine(game, row3);
    sendTicTacToeLine(game, "");

    if (showTurn) {
        string symbol =
            game.turn == game.playerX
            ? "X"
            : "O";

        sendTicTacToeLine(
            game,
            "Turn: " +
            getName(game.turn) +
            " (" +
            symbol +
            ")"
        );
    }

    sendTicTacToeLine(game, "================================");
    sendTicTacToeLine(game, "");
}

bool ticTacToeWinner(
    TicTacToeGame& game,
    char symbol
) {
    int combinations[8][3] = {
        {0,1,2},
        {3,4,5},
        {6,7,8},
        {0,3,6},
        {1,4,7},
        {2,5,8},
        {0,4,8},
        {2,4,6}
    };

    for (auto& combo : combinations) {
        if (
            game.board[combo[0]] == symbol &&
            game.board[combo[1]] == symbol &&
            game.board[combo[2]] == symbol
        ) {
            return true;
        }
    }

    return false;
}

bool ticTacToeBoardFull(TicTacToeGame& game) {
    for (char cell : game.board) {
        if (cell == ' ')
            return false;
    }

    return true;
}


// ============================================================
// CHESS HELPERS
// ============================================================

bool chessInside(int row, int col) {
    return row >= 0 && row < 8 && col >= 0 && col < 8;
}

bool chessWhitePiece(char piece) {
    return piece >= 'A' && piece <= 'Z';
}

bool chessBlackPiece(char piece) {
    return piece >= 'a' && piece <= 'z';
}

bool chessSameColor(char a, char b) {
    if (a == ' ' || b == ' ')
        return false;

    return
        (chessWhitePiece(a) && chessWhitePiece(b)) ||
        (chessBlackPiece(a) && chessBlackPiece(b));
}

string chessPieceSymbol(char piece) {
    switch (piece) {
        case 'K': return "♔";
        case 'Q': return "♕";
        case 'R': return "♖";
        case 'B': return "♗";
        case 'N': return "♘";
        case 'P': return "♙";
        case 'k': return "♚";
        case 'q': return "♛";
        case 'r': return "♜";
        case 'b': return "♝";
        case 'n': return "♞";
        case 'p': return "♟";
        default:  return " ";
    }
}

bool parseChessSquare(
    string square,
    int& row,
    int& col
) {
    if (square.size() != 2)
        return false;

    char file = (char)tolower((unsigned char)square[0]);
    char rank = square[1];

    if (file < 'a' || file > 'h' || rank < '1' || rank > '8')
        return false;

    col = file - 'a';
    row = 8 - (rank - '0');

    return true;
}

string chessSquareName(int row, int col) {
    if (!chessInside(row, col))
        return "??";

    string square;
    square += (char)('a' + col);
    square += (char)('8' - row);
    return square;
}

void sendChessLine(
    ChessGame& game,
    const string& text
) {
    // Chess UI/status packets are intentionally separate from GAME
    // so the graphical client does not print Chess into chat.
    sendPacket(game.white, "CHESS_NOTICE", text);
    sendPacket(game.black, "CHESS_NOTICE", text);
    for (Socket spectator : game.spectators)
        sendPacket(spectator, "CHESS_NOTICE", text);
}

string encodeChessBoard(const ChessGame& game) {
    string encoded;
    encoded.reserve(64);

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            char piece = game.board[row][col];
            encoded += piece == ' ' ? '.' : piece;
        }
    }

    return encoded;
}

void sendChessStateTo(
    ChessGame& game,
    Socket socket
) {
    string yourColor =
        socket == game.white
        ? "WHITE"
        : (socket == game.black ? "BLACK" : "SPECTATOR");

    string data =
        encodeChessBoard(game) + "|" +
        getName(game.white) + "|" +
        getName(game.black) + "|" +
        getName(game.turn) + "|" +
        yourColor + "|" +
        to_string(game.spectators.size());

    for (Socket spectator : game.spectators)
        data += "|" + getName(spectator);

    sendPacket(socket, "CHESS_STATE", data);
}

void sendChessState(ChessGame& game) {
    sendChessStateTo(game, game.white);
    sendChessStateTo(game, game.black);
    for (Socket spectator : game.spectators)
        sendChessStateTo(game, spectator);
}

void sendChessEnd(
    ChessGame& game,
    const string& text
) {
    auto sendTo = [&](Socket socket) {
        string yourColor =
            socket == game.white
            ? "WHITE"
            : (socket == game.black ? "BLACK" : "SPECTATOR");

        // Repeat the final board in the end packet. This makes the final
        // position independent of packet timing and keeps every client,
        // including spectators, on the same completed position.
        sendPacket(
            socket,
            "CHESS_END",
            text + "|" +
            encodeChessBoard(game) + "|" +
            getName(game.white) + "|" +
            getName(game.black) + "|" +
            getName(game.turn) + "|" +
            yourColor
        );
    };

    sendTo(game.white);
    sendTo(game.black);
    for (Socket spectator : game.spectators)
        sendTo(spectator);
}

void readyChessPlayers(ChessGame& game) {
    sendReady(game.white);
    sendReady(game.black);
    for (Socket spectator : game.spectators)
        sendReady(spectator);
}

ChessGame makeChessGame(
    Socket white,
    Socket black
) {
    ChessGame game{};
    game.white = white;
    game.black = black;
    game.turn = white;
    game.enPassantRow = -1;
    game.enPassantCol = -1;

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            game.board[row][col] = ' ';
        }
    }

    const string blackBack = "rnbqkbnr";
    const string whiteBack = "RNBQKBNR";

    for (int col = 0; col < 8; col++) {
        game.board[0][col] = blackBack[col];
        game.board[1][col] = 'p';
        game.board[6][col] = 'P';
        game.board[7][col] = whiteBack[col];
    }

    return game;
}

bool findChessKing(
    const ChessGame& game,
    bool white,
    int& kingRow,
    int& kingCol
) {
    char king = white ? 'K' : 'k';

    for (int row = 0; row < 8; row++) {
        for (int col = 0; col < 8; col++) {
            if (game.board[row][col] == king) {
                kingRow = row;
                kingCol = col;
                return true;
            }
        }
    }

    return false;
}

bool chessSquareAttacked(
    const ChessGame& game,
    int row,
    int col,
    bool byWhite
) {
    // Pawns.
    int pawnSourceRow = row + (byWhite ? 1 : -1);
    char pawn = byWhite ? 'P' : 'p';

    for (int dc : {-1, 1}) {
        int sourceCol = col + dc;

        if (
            chessInside(pawnSourceRow, sourceCol) &&
            game.board[pawnSourceRow][sourceCol] == pawn
        ) {
            return true;
        }
    }

    // Knights.
    const int knightMoves[8][2] = {
        {-2,-1}, {-2, 1}, {-1,-2}, {-1, 2},
        { 1,-2}, { 1, 2}, { 2,-1}, { 2, 1}
    };

    char knight = byWhite ? 'N' : 'n';

    for (const auto& move : knightMoves) {
        int r = row + move[0];
        int c = col + move[1];

        if (
            chessInside(r, c) &&
            game.board[r][c] == knight
        ) {
            return true;
        }
    }

    // Kings.
    char king = byWhite ? 'K' : 'k';

    for (int dr = -1; dr <= 1; dr++) {
        for (int dc = -1; dc <= 1; dc++) {
            if (dr == 0 && dc == 0)
                continue;

            int r = row + dr;
            int c = col + dc;

            if (
                chessInside(r, c) &&
                game.board[r][c] == king
            ) {
                return true;
            }
        }
    }

    // Rooks / queens.
    const int straight[4][2] = {
        {-1,0}, {1,0}, {0,-1}, {0,1}
    };

    for (const auto& direction : straight) {
        int r = row + direction[0];
        int c = col + direction[1];

        while (chessInside(r, c)) {
            char piece = game.board[r][c];

            if (piece != ' ') {
                if (
                    (byWhite && chessWhitePiece(piece)) ||
                    (!byWhite && chessBlackPiece(piece))
                ) {
                    char type = (char)tolower((unsigned char)piece);
                    if (type == 'r' || type == 'q')
                        return true;
                }

                break;
            }

            r += direction[0];
            c += direction[1];
        }
    }

    // Bishops / queens.
    const int diagonal[4][2] = {
        {-1,-1}, {-1,1}, {1,-1}, {1,1}
    };

    for (const auto& direction : diagonal) {
        int r = row + direction[0];
        int c = col + direction[1];

        while (chessInside(r, c)) {
            char piece = game.board[r][c];

            if (piece != ' ') {
                if (
                    (byWhite && chessWhitePiece(piece)) ||
                    (!byWhite && chessBlackPiece(piece))
                ) {
                    char type = (char)tolower((unsigned char)piece);
                    if (type == 'b' || type == 'q')
                        return true;
                }

                break;
            }

            r += direction[0];
            c += direction[1];
        }
    }

    return false;
}

bool chessInCheck(
    const ChessGame& game,
    bool white
) {
    int kingRow = -1;
    int kingCol = -1;

    if (!findChessKing(game, white, kingRow, kingCol))
        return true;

    return chessSquareAttacked(
        game,
        kingRow,
        kingCol,
        !white
    );
}

bool chessPathClear(
    const ChessGame& game,
    int fromRow,
    int fromCol,
    int toRow,
    int toCol
) {
    int rowStep =
        (toRow > fromRow) ? 1 :
        (toRow < fromRow) ? -1 : 0;

    int colStep =
        (toCol > fromCol) ? 1 :
        (toCol < fromCol) ? -1 : 0;

    int row = fromRow + rowStep;
    int col = fromCol + colStep;

    while (row != toRow || col != toCol) {
        if (game.board[row][col] != ' ')
            return false;

        row += rowStep;
        col += colStep;
    }

    return true;
}

bool chessPseudoLegalMove(
    const ChessGame& game,
    int fromRow,
    int fromCol,
    int toRow,
    int toCol,
    bool white
) {
    if (
        !chessInside(fromRow, fromCol) ||
        !chessInside(toRow, toCol) ||
        (fromRow == toRow && fromCol == toCol)
    ) {
        return false;
    }

    char piece = game.board[fromRow][fromCol];
    char target = game.board[toRow][toCol];

    if (piece == ' ')
        return false;

    if (white && !chessWhitePiece(piece))
        return false;

    if (!white && !chessBlackPiece(piece))
        return false;

    if (chessSameColor(piece, target))
        return false;

    // Kings are never captured directly. Check/checkmate ends the game.
    if (target == 'K' || target == 'k')
        return false;

    int dr = toRow - fromRow;
    int dc = toCol - fromCol;

    switch ((char)tolower((unsigned char)piece)) {
        case 'p': {
            int direction = white ? -1 : 1;
            int startRow = white ? 6 : 1;

            if (dc == 0 && dr == direction && target == ' ')
                return true;

            if (
                dc == 0 &&
                dr == 2 * direction &&
                fromRow == startRow &&
                target == ' ' &&
                game.board[fromRow + direction][fromCol] == ' '
            ) {
                return true;
            }

            if (abs(dc) == 1 && dr == direction) {
                if (target != ' ')
                    return true;

                if (
                    toRow == game.enPassantRow &&
                    toCol == game.enPassantCol
                ) {
                    char adjacent = game.board[fromRow][toCol];
                    return adjacent == (white ? 'p' : 'P');
                }
            }

            return false;
        }

        case 'n':
            return
                (abs(dr) == 2 && abs(dc) == 1) ||
                (abs(dr) == 1 && abs(dc) == 2);

        case 'b':
            return
                abs(dr) == abs(dc) &&
                chessPathClear(
                    game,
                    fromRow,
                    fromCol,
                    toRow,
                    toCol
                );

        case 'r': {
            // Rooks must stay on exactly one rank/file. This explicit
            // branch is shared by both white ('R') and black ('r').
            if (dr != 0 && dc != 0)
                return false;

            return chessPathClear(
                game,
                fromRow,
                fromCol,
                toRow,
                toCol
            );
        }

        case 'q':
            return
                (
                    dr == 0 ||
                    dc == 0 ||
                    abs(dr) == abs(dc)
                ) &&
                chessPathClear(
                    game,
                    fromRow,
                    fromCol,
                    toRow,
                    toCol
                );

        case 'k': {
            if (abs(dr) <= 1 && abs(dc) <= 1)
                return true;

            // Castling.
            int homeRow = white ? 7 : 0;

            if (
                fromRow != homeRow ||
                fromCol != 4 ||
                toRow != homeRow ||
                abs(dc) != 2
            ) {
                return false;
            }

            bool kingMoved =
                white
                ? game.whiteKingMoved
                : game.blackKingMoved;

            if (kingMoved)
                return false;

            bool kingSide = toCol == 6;
            bool queenSide = toCol == 2;

            if (!kingSide && !queenSide)
                return false;

            if (kingSide) {
                bool rookMoved =
                    white
                    ? game.whiteHRookMoved
                    : game.blackHRookMoved;

                char rook = white ? 'R' : 'r';

                if (
                    rookMoved ||
                    game.board[homeRow][7] != rook ||
                    game.board[homeRow][5] != ' ' ||
                    game.board[homeRow][6] != ' '
                ) {
                    return false;
                }

                if (
                    chessInCheck(game, white) ||
                    chessSquareAttacked(game, homeRow, 5, !white) ||
                    chessSquareAttacked(game, homeRow, 6, !white)
                ) {
                    return false;
                }

                return true;
            }

            bool rookMoved =
                white
                ? game.whiteARookMoved
                : game.blackARookMoved;

            char rook = white ? 'R' : 'r';

            if (
                rookMoved ||
                game.board[homeRow][0] != rook ||
                game.board[homeRow][1] != ' ' ||
                game.board[homeRow][2] != ' ' ||
                game.board[homeRow][3] != ' '
            ) {
                return false;
            }

            if (
                chessInCheck(game, white) ||
                chessSquareAttacked(game, homeRow, 3, !white) ||
                chessSquareAttacked(game, homeRow, 2, !white)
            ) {
                return false;
            }

            return true;
        }
    }

    return false;
}

void applyChessMoveUnchecked(
    ChessGame& game,
    int fromRow,
    int fromCol,
    int toRow,
    int toCol,
    char promotion
) {
    char piece = game.board[fromRow][fromCol];
    char captured = game.board[toRow][toCol];
    bool white = chessWhitePiece(piece);
    char type = (char)tolower((unsigned char)piece);

    // If a rook is captured on its original square, castling on that side
    // can never happen later.
    if (captured == 'R' && toRow == 7 && toCol == 0)
        game.whiteARookMoved = true;
    if (captured == 'R' && toRow == 7 && toCol == 7)
        game.whiteHRookMoved = true;
    if (captured == 'r' && toRow == 0 && toCol == 0)
        game.blackARookMoved = true;
    if (captured == 'r' && toRow == 0 && toCol == 7)
        game.blackHRookMoved = true;

    // En passant capture.
    if (
        type == 'p' &&
        fromCol != toCol &&
        captured == ' ' &&
        toRow == game.enPassantRow &&
        toCol == game.enPassantCol
    ) {
        game.board[fromRow][toCol] = ' ';
    }

    game.board[fromRow][fromCol] = ' ';
    game.board[toRow][toCol] = piece;

    // Castling rook movement.
    if (type == 'k' && abs(toCol - fromCol) == 2) {
        if (toCol == 6) {
            game.board[toRow][5] = game.board[toRow][7];
            game.board[toRow][7] = ' ';
        }
        else {
            game.board[toRow][3] = game.board[toRow][0];
            game.board[toRow][0] = ' ';
        }
    }

    // Track castling rights.
    if (piece == 'K')
        game.whiteKingMoved = true;
    else if (piece == 'k')
        game.blackKingMoved = true;
    else if (piece == 'R' && fromRow == 7 && fromCol == 0)
        game.whiteARookMoved = true;
    else if (piece == 'R' && fromRow == 7 && fromCol == 7)
        game.whiteHRookMoved = true;
    else if (piece == 'r' && fromRow == 0 && fromCol == 0)
        game.blackARookMoved = true;
    else if (piece == 'r' && fromRow == 0 && fromCol == 7)
        game.blackHRookMoved = true;

    // Reset en-passant unless this was a two-square pawn move.
    game.enPassantRow = -1;
    game.enPassantCol = -1;

    if (type == 'p' && abs(toRow - fromRow) == 2) {
        game.enPassantRow = (fromRow + toRow) / 2;
        game.enPassantCol = fromCol;
    }

    // Pawn promotion. Default is queen; q/r/b/n are accepted.
    if (
        type == 'p' &&
        (toRow == 0 || toRow == 7)
    ) {
        char promoteTo = (char)tolower((unsigned char)promotion);

        if (
            promoteTo != 'q' &&
            promoteTo != 'r' &&
            promoteTo != 'b' &&
            promoteTo != 'n'
        ) {
            promoteTo = 'q';
        }

        game.board[toRow][toCol] =
            white
            ? (char)toupper((unsigned char)promoteTo)
            : promoteTo;
    }
}

bool legalChessMove(
    const ChessGame& game,
    int fromRow,
    int fromCol,
    int toRow,
    int toCol,
    bool white,
    char promotion = 'q'
) {
    if (
        !chessPseudoLegalMove(
            game,
            fromRow,
            fromCol,
            toRow,
            toCol,
            white
        )
    ) {
        return false;
    }

    ChessGame copy = game;

    applyChessMoveUnchecked(
        copy,
        fromRow,
        fromCol,
        toRow,
        toCol,
        promotion
    );

    return !chessInCheck(copy, white);
}

bool chessHasLegalMove(
    const ChessGame& game,
    bool white
) {
    for (int fromRow = 0; fromRow < 8; fromRow++) {
        for (int fromCol = 0; fromCol < 8; fromCol++) {
            char piece = game.board[fromRow][fromCol];

            if (
                piece == ' ' ||
                (white && !chessWhitePiece(piece)) ||
                (!white && !chessBlackPiece(piece))
            ) {
                continue;
            }

            for (int toRow = 0; toRow < 8; toRow++) {
                for (int toCol = 0; toCol < 8; toCol++) {
                    if (
                        legalChessMove(
                            game,
                            fromRow,
                            fromCol,
                            toRow,
                            toCol,
                            white,
                            'q'
                        )
                    ) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}


void sendChessLegalMoves(
    ChessGame& game,
    Socket socket,
    const string& fromText
) {
    int fromRow = -1;
    int fromCol = -1;

    if (!parseChessSquare(fromText, fromRow, fromCol)) {
        sendPacket(
            socket,
            "CHESS_ERROR",
            "Invalid Chess square."
        );
        return;
    }

    string normalizedFrom =
        chessSquareName(fromRow, fromCol);

    // Only the player whose turn it is should receive clickable
    // destinations.
    if (game.turn != socket) {
        sendPacket(
            socket,
            "CHESS_LEGAL",
            normalizedFrom + "|-"
        );
        return;
    }

    bool white = socket == game.white;
    char piece = game.board[fromRow][fromCol];

    if (
        piece == ' ' ||
        (white && !chessWhitePiece(piece)) ||
        (!white && !chessBlackPiece(piece))
    ) {
        sendPacket(
            socket,
            "CHESS_LEGAL",
            normalizedFrom + "|-"
        );
        return;
    }

    string destinations;

    for (int toRow = 0; toRow < 8; toRow++) {
        for (int toCol = 0; toCol < 8; toCol++) {
            if (
                legalChessMove(
                    game,
                    fromRow,
                    fromCol,
                    toRow,
                    toCol,
                    white,
                    'q'
                )
            ) {
                if (!destinations.empty())
                    destinations += ",";

                destinations +=
                    chessSquareName(
                        toRow,
                        toCol
                    );
            }
        }
    }

    if (destinations.empty())
        destinations = "-";

    sendPacket(
        socket,
        "CHESS_LEGAL",
        normalizedFrom +
        "|" +
        destinations
    );
}


void showChessBoard(
    ChessGame& game,
    bool showTurn = true
) {
    (void)showTurn;
    sendChessState(game);
}

void playChessMove(
    int gameIndex,
    Client& client,
    const string& fromText,
    const string& toText,
    const string& promotionText
) {
    if (
        gameIndex < 0 ||
        gameIndex >= (int)chessGames.size()
    ) {
        sendPacket(
            client.socket,
            "CHESS_ERROR",
            "You are not in a Chess game."
        );
        return;
    }

    ChessGame& game = chessGames[gameIndex];

    if (game.turn != client.socket) {
        sendPacket(
            client.socket,
            "CHESS_ERROR",
            "It is not your Chess turn."
        );
        return;
    }

    int fromRow, fromCol, toRow, toCol;

    if (
        !parseChessSquare(fromText, fromRow, fromCol) ||
        !parseChessSquare(toText, toRow, toCol)
    ) {
        sendPacket(
            client.socket,
            "CHESS_ERROR",
            "Choose a piece, then choose its destination."
        );
        return;
    }

    bool white = client.socket == game.white;
    char promotion = 'q';

    if (!promotionText.empty()) {
        promotion = (char)tolower(
            (unsigned char)promotionText[0]
        );

        if (
            promotion != 'q' &&
            promotion != 'r' &&
            promotion != 'b' &&
            promotion != 'n'
        ) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                "Promotion must be q, r, b, or n."
            );
            return;
        }
    }

    if (
        !legalChessMove(
            game,
            fromRow,
            fromCol,
            toRow,
            toCol,
            white,
            promotion
        )
    ) {
        sendPacket(
            client.socket,
            "CHESS_ERROR",
            "Illegal Chess move."
        );
        return;
    }

    applyChessMoveUnchecked(
        game,
        fromRow,
        fromCol,
        toRow,
        toCol,
        promotion
    );

    sendChessLine(
        game,
        client.name +
        " moves " +
        chessSquareName(fromRow, fromCol) +
        " to " +
        chessSquareName(toRow, toCol) +
        "."
    );

    game.turn =
        game.turn == game.white
        ? game.black
        : game.white;

    bool nextWhite = game.turn == game.white;
    bool nextInCheck = chessInCheck(game, nextWhite);
    bool nextHasMove = chessHasLegalMove(game, nextWhite);

    if (!nextHasMove) {
        showChessBoard(game, false);

        if (nextInCheck) {
            sendChessEnd(
                game,
                "CHECKMATE - " +
                client.name +
                " wins."
            );
        }
        else {
            sendChessEnd(
                game,
                "STALEMATE - draw."
            );
        }

        readyChessPlayers(game);

        chessGames.erase(
            chessGames.begin() + gameIndex
        );

        return;
    }

    showChessBoard(game);

    if (nextInCheck) {
        sendChessLine(
            game,
            "*** CHECK! ***"
        );
    }

    readyChessPlayers(game);
}


// ============================================================
// BLACKJACK HELPERS - 2 TO 6 PLAYER GRAPHICAL TABLE
// ============================================================

int blackjackPlayerIndex(
    const BlackjackGame& game,
    Socket socket
) {
    for (int i = 0; i < (int)game.players.size(); i++) {
        if (game.players[i].socket == socket)
            return i;
    }

    return -1;
}

BlackjackPlayer* blackjackPlayer(
    BlackjackGame& game,
    Socket socket
) {
    int index = blackjackPlayerIndex(game, socket);
    return index >= 0 ? &game.players[index] : nullptr;
}

const BlackjackPlayer* blackjackPlayer(
    const BlackjackGame& game,
    Socket socket
) {
    int index = blackjackPlayerIndex(game, socket);
    return index >= 0 ? &game.players[index] : nullptr;
}

void readyBlackjackPlayers(BlackjackGame& game) {
    for (const BlackjackPlayer& player : game.players)
        sendReady(player.socket);
}

void sendBlackjackPacket(
    BlackjackGame& game,
    const string& type,
    const string& data
) {
    for (const BlackjackPlayer& player : game.players)
        sendPacket(player.socket, type, data);
}

void sendBlackjackNotice(
    BlackjackGame& game,
    const string& text
) {
    sendBlackjackPacket(game, "BJ_NOTICE", text);
}

void sendBlackjackError(
    Socket socket,
    const string& text
) {
    sendPacket(socket, "BJ_ERROR", text);
}

vector<Card> makeBlackjackDeck() {
    vector<Card> deck;

    const char suits[] = {'S', 'H', 'D', 'C'};

    struct RankInfo {
        const char* rank;
        int value;
    };

    RankInfo ranks[] = {
        {"A", 11}, {"2", 2}, {"3", 3}, {"4", 4},
        {"5", 5}, {"6", 6}, {"7", 7}, {"8", 8},
        {"9", 9}, {"10", 10}, {"J", 10}, {"Q", 10}, {"K", 10}
    };

    // Six-deck shoe so a six-player table has plenty of cards.
    for (int shoeDeck = 0; shoeDeck < 6; shoeDeck++) {
        for (char suit : suits) {
            for (const RankInfo& rank : ranks) {
                Card card;
                card.rank = rank.rank;
                card.suit = suit;
                card.value = rank.value;
                deck.push_back(card);
            }
        }
    }

    shuffle(deck.begin(), deck.end(), rng);
    return deck;
}

string blackjackCardCode(const Card& card) {
    return card.rank + string(1, card.suit);
}

Card drawBlackjackCard(BlackjackGame& game) {
    if (game.deck.empty())
        game.deck = makeBlackjackDeck();

    Card card = game.deck.back();
    game.deck.pop_back();
    return card;
}

int blackjackHandValue(const vector<Card>& hand) {
    int total = 0;
    int aces = 0;

    for (const Card& card : hand) {
        total += card.value;
        if (card.rank == "A")
            aces++;
    }

    while (total > 21 && aces > 0) {
        total -= 10;
        aces--;
    }

    return total;
}

int blackjackHandValue(const BlackjackHand& hand) {
    return blackjackHandValue(hand.cards);
}

bool naturalBlackjack(const BlackjackHand& hand) {
    return
        !hand.fromSplit &&
        hand.cards.size() == 2 &&
        blackjackHandValue(hand) == 21;
}

bool dealerNaturalBlackjack(const BlackjackGame& game) {
    return
        game.dealerHand.size() == 2 &&
        blackjackHandValue(game.dealerHand) == 21;
}

bool blackjackHandDone(const BlackjackHand& hand) {
    return
        hand.stood ||
        hand.busted ||
        blackjackHandValue(hand) >= 21;
}

int blackjackReservedBet(const BlackjackPlayer& player) {
    int total = 0;

    for (const BlackjackHand& hand : player.hands)
        total += hand.bet;

    return total;
}

int blackjackAvailableChips(const BlackjackPlayer& player) {
    return max(0, player.chips - blackjackReservedBet(player));
}

string encodeBlackjackCards(const vector<Card>& cards) {
    if (cards.empty())
        return "-";

    string result;

    for (int i = 0; i < (int)cards.size(); i++) {
        if (i > 0)
            result += ",";

        result += blackjackCardCode(cards[i]);
    }

    return result;
}

string encodeBlackjackHand(const BlackjackHand& hand) {
    return
        to_string(hand.bet) + "~" +
        to_string(blackjackHandValue(hand)) + "~" +
        string(blackjackHandDone(hand) ? "1" : "0") + "~" +
        string(hand.busted ? "1" : "0") + "~" +
        string(hand.doubled ? "1" : "0") + "~" +
        string(hand.fromSplit ? "1" : "0") + "~" +
        encodeBlackjackCards(hand.cards);
}

string encodeBlackjackHands(const BlackjackPlayer& player) {
    if (player.hands.empty())
        return "-";

    string result;

    for (int i = 0; i < (int)player.hands.size(); i++) {
        if (i > 0)
            result += ";";

        result += encodeBlackjackHand(player.hands[i]);
    }

    return result;
}

string encodeBlackjackPlayer(const BlackjackPlayer& player) {
    return
        getName(player.socket) + "^" +
        to_string(player.chips) + "^" +
        string(player.betPlaced ? "1" : "0") + "^" +
        to_string(player.pendingBet) + "^" +
        encodeBlackjackHands(player);
}

string encodeDealerCards(const BlackjackGame& game) {
    if (game.dealerHand.empty())
        return "-";

    string result;

    for (int i = 0; i < (int)game.dealerHand.size(); i++) {
        if (i > 0)
            result += ",";

        if (!game.dealerRevealed && i == 1)
            result += "--";
        else
            result += blackjackCardCode(game.dealerHand[i]);
    }

    return result;
}

void sendBlackjackState(BlackjackGame& game) {
    string payload =
        game.phase + "|" +
        to_string(game.currentHand) + "|" +
        to_string(game.totalHands) + "|" +
        to_string(game.startingChips) + "|" +
        getName(game.host) + "|" +
        (game.turn == INVALID_SOCK ? string("") : getName(game.turn)) + "|" +
        to_string(game.turnHandIndex) + "|" +
        string(game.dealerRevealed ? "1" : "0") + "|" +
        encodeDealerCards(game) + "|" +
        string(game.awaitingNextHand ? "1" : "0") + "|" +
        game.status + "|" +
        to_string(game.players.size());

    for (const BlackjackPlayer& player : game.players)
        payload += "|" + encodeBlackjackPlayer(player);

    sendBlackjackPacket(game, "BJ_STATE", payload);
}

void sendBlackjackCardEvent(
    BlackjackGame& game,
    const string& target,
    int handIndex,
    int cardIndex,
    const string& cardCode
) {
    string payload =
        target + "|" +
        to_string(handIndex) + "|" +
        to_string(cardIndex) + "|" +
        cardCode;

    sendBlackjackPacket(game, "BJ_CARD", payload);
}

void resetBlackjackRound(BlackjackGame& game) {
    for (BlackjackPlayer& player : game.players) {
        player.pendingBet = 0;
        player.betPlaced = player.chips <= 0;
        player.hands.clear();
    }

    game.deck.clear();
    game.dealerHand.clear();
    game.handInProgress = false;
    game.dealerRevealed = false;
    game.awaitingNextHand = false;
    game.dealScheduled = false;
    game.turn = INVALID_SOCK;
    game.turnHandIndex = 0;
    game.phase = "BETTING";
}

bool allBlackjackBetsPlaced(const BlackjackGame& game) {
    bool hasActivePlayer = false;

    for (const BlackjackPlayer& player : game.players) {
        if (player.chips <= 0)
            continue;

        hasActivePlayer = true;

        if (!player.betPlaced || player.pendingBet <= 0)
            return false;
    }

    return hasActivePlayer;
}

bool findBlackjackTurnFrom(
    BlackjackGame& game,
    int startPlayer,
    int startHand
) {
    for (int p = startPlayer; p < (int)game.players.size(); p++) {
        BlackjackPlayer& player = game.players[p];
        int firstHand = p == startPlayer ? startHand : 0;

        for (int h = firstHand; h < (int)player.hands.size(); h++) {
            if (!blackjackHandDone(player.hands[h])) {
                game.turn = player.socket;
                game.turnHandIndex = h;
                return true;
            }
        }
    }

    return false;
}

int blackjackHandDelta(
    const BlackjackHand& hand,
    const vector<Card>& dealerHand
) {
    int bet = hand.bet;
    int playerValue = blackjackHandValue(hand);
    int dealerValue = blackjackHandValue(dealerHand);

    bool playerNatural = naturalBlackjack(hand);
    bool dealerNatural =
        dealerHand.size() == 2 &&
        dealerValue == 21;

    if (hand.busted || playerValue > 21)
        return -bet;

    if (dealerNatural && !playerNatural)
        return -bet;

    if (playerNatural && !dealerNatural)
        return (bet * 3) / 2;

    if (dealerValue > 21)
        return bet;

    if (playerValue > dealerValue)
        return bet;

    if (playerValue < dealerValue)
        return -bet;

    return 0;
}

string blackjackPayoutReason(
    const BlackjackHand& hand,
    const vector<Card>& dealerHand
) {
    int playerValue = blackjackHandValue(hand);
    int dealerValue = blackjackHandValue(dealerHand);
    bool dealerNatural = dealerHand.size() == 2 && dealerValue == 21;

    if (hand.busted || playerValue > 21)
        return "bust";

    if (naturalBlackjack(hand) && !dealerNatural)
        return "blackjack pays 3:2";

    if (dealerNatural && !naturalBlackjack(hand))
        return "dealer blackjack";

    if (dealerValue > 21)
        return "dealer bust";

    if (playerValue > dealerValue)
        return "win pays 1:1";

    if (playerValue < dealerValue)
        return "dealer wins";

    return "push";
}

void sendBlackjackPayout(
    BlackjackGame& game,
    const BlackjackPlayer& player,
    int handIndex,
    int delta,
    const string& reason
) {
    string payload =
        getName(player.socket) + "|" +
        to_string(handIndex) + "|" +
        to_string(delta) + "|" +
        reason;

    sendBlackjackPacket(game, "BJ_PAYOUT", payload);
}

void finishBlackjackMatch(
    int gameIndex,
    const string& forcedResult = ""
) {
    if (gameIndex < 0 || gameIndex >= (int)blackjackGames.size())
        return;

    BlackjackGame game = blackjackGames[gameIndex];

    string result = forcedResult;

    if (result.empty()) {
        int best = -1;
        vector<string> winners;

        for (const BlackjackPlayer& player : game.players) {
            if (player.chips > best) {
                best = player.chips;
                winners.clear();
                winners.push_back(getName(player.socket));
            }
            else if (player.chips == best) {
                winners.push_back(getName(player.socket));
            }
        }

        if (winners.size() == 1)
            result = winners[0] + " wins the Blackjack match with " + to_string(best) + " chips.";
        else
            result = "Blackjack match complete. Top stack: " + to_string(best) + " chips.";
    }

    for (const BlackjackPlayer& player : game.players) {
        sendPacket(player.socket, "BJ_END", result);
        sendReady(player.socket);
    }

    blackjackGames.erase(blackjackGames.begin() + gameIndex);
}

void resolveBlackjackHand(int gameIndex);

void advanceBlackjackTurn(int gameIndex) {
    if (gameIndex < 0 || gameIndex >= (int)blackjackGames.size())
        return;

    BlackjackGame& game = blackjackGames[gameIndex];

    int playerIndex = blackjackPlayerIndex(game, game.turn);

    if (playerIndex < 0) {
        if (findBlackjackTurnFrom(game, 0, 0)) {
            game.status = "Turn: " + getName(game.turn) + ".";
            sendBlackjackState(game);
            readyBlackjackPlayers(game);
            return;
        }

        resolveBlackjackHand(gameIndex);
        return;
    }

    if (findBlackjackTurnFrom(game, playerIndex, game.turnHandIndex + 1)) {
        game.status = "Turn: " + getName(game.turn) + ".";
        sendBlackjackState(game);
        readyBlackjackPlayers(game);
        return;
    }

    if (findBlackjackTurnFrom(game, playerIndex + 1, 0)) {
        game.status = "Turn: " + getName(game.turn) + ".";
        sendBlackjackState(game);
        readyBlackjackPlayers(game);
        return;
    }

    resolveBlackjackHand(gameIndex);
}

void startBlackjackHand(int gameIndex) {
    if (gameIndex < 0 || gameIndex >= (int)blackjackGames.size())
        return;

    BlackjackGame& game = blackjackGames[gameIndex];

    game.deck = makeBlackjackDeck();
    game.dealerHand.clear();
    game.dealerRevealed = false;
    game.handInProgress = true;
    game.awaitingNextHand = false;
    game.dealScheduled = false;
    game.phase = "PLAYING";
    game.turn = INVALID_SOCK;
    game.turnHandIndex = 0;

    sendBlackjackPacket(game, "BJ_NEW_HAND", to_string(game.currentHand));

    for (BlackjackPlayer& player : game.players) {
        player.hands.clear();

        if (player.chips <= 0 || player.pendingBet <= 0)
            continue;

        BlackjackHand hand;
        hand.bet = player.pendingBet;
        player.hands.push_back(hand);
    }

    // First card to every active player.
    for (BlackjackPlayer& player : game.players) {
        if (player.hands.empty())
            continue;

        Card card = drawBlackjackCard(game);
        player.hands[0].cards.push_back(card);

        sendBlackjackCardEvent(
            game,
            getName(player.socket),
            0,
            0,
            blackjackCardCode(card)
        );
    }

    // Dealer up card.
    Card dealerUp = drawBlackjackCard(game);
    game.dealerHand.push_back(dealerUp);
    sendBlackjackCardEvent(game, "DEALER", 0, 0, blackjackCardCode(dealerUp));

    // Second card to every active player.
    for (BlackjackPlayer& player : game.players) {
        if (player.hands.empty())
            continue;

        Card card = drawBlackjackCard(game);
        player.hands[0].cards.push_back(card);

        sendBlackjackCardEvent(
            game,
            getName(player.socket),
            0,
            1,
            blackjackCardCode(card)
        );

        if (naturalBlackjack(player.hands[0]))
            player.hands[0].stood = true;
    }

    // Dealer hole card. Clients receive only the card back until reveal.
    Card dealerHole = drawBlackjackCard(game);
    game.dealerHand.push_back(dealerHole);
    sendBlackjackCardEvent(game, "DEALER", 0, 1, "--");

    game.status = "Cards dealt.";
    sendBlackjackState(game);

    if (dealerNaturalBlackjack(game)) {
        resolveBlackjackHand(gameIndex);
        return;
    }

    if (findBlackjackTurnFrom(game, 0, 0)) {
        game.status = "Turn: " + getName(game.turn) + ".";
        sendBlackjackState(game);
        readyBlackjackPlayers(game);
        return;
    }

    resolveBlackjackHand(gameIndex);
}

void processBlackjackDealTimers() {
    auto now = chrono::steady_clock::now();

    for (int i = 0; i < (int)blackjackGames.size();) {
        if (
            blackjackGames[i].dealScheduled &&
            now >= blackjackGames[i].dealAt
        ) {
            blackjackGames[i].dealScheduled = false;

            size_t beforeSize =
                blackjackGames.size();

            startBlackjackHand(i);

            // startBlackjackHand() can immediately resolve/erase a
            // final hand (for example dealer blackjack). If that
            // happened, keep this index because the next game shifted
            // into it.
            if (blackjackGames.size() < beforeSize)
                continue;
        }

        i++;
    }
}

void showBlackjackBetting(BlackjackGame& game) {
    game.phase = "BETTING";
    game.status =
        "Hand " +
        to_string(game.currentHand) +
        " of " +
        to_string(game.totalHands) +
        ". Place your bets.";

    sendBlackjackState(game);
    readyBlackjackPlayers(game);
}

void resolveBlackjackHand(int gameIndex) {
    if (gameIndex < 0 || gameIndex >= (int)blackjackGames.size())
        return;

    BlackjackGame& game = blackjackGames[gameIndex];

    game.dealerRevealed = true;
    game.turn = INVALID_SOCK;
    game.turnHandIndex = 0;

    // Let the GUI replace the hole-card back with the real card.
    sendBlackjackState(game);

    bool anyoneAlive = false;

    for (const BlackjackPlayer& player : game.players) {
        for (const BlackjackHand& hand : player.hands) {
            if (!hand.busted && blackjackHandValue(hand) <= 21) {
                anyoneAlive = true;
                break;
            }
        }

        if (anyoneAlive)
            break;
    }

    if (anyoneAlive && !dealerNaturalBlackjack(game)) {
        while (blackjackHandValue(game.dealerHand) < 17) {
            Card card = drawBlackjackCard(game);
            game.dealerHand.push_back(card);

            sendBlackjackCardEvent(
                game,
                "DEALER",
                0,
                (int)game.dealerHand.size() - 1,
                blackjackCardCode(card)
            );
        }
    }

    for (BlackjackPlayer& player : game.players) {
        for (int h = 0; h < (int)player.hands.size(); h++) {
            BlackjackHand& hand = player.hands[h];
            int delta = blackjackHandDelta(hand, game.dealerHand);
            player.chips += delta;

            sendBlackjackPayout(
                game,
                player,
                h,
                delta,
                blackjackPayoutReason(hand, game.dealerHand)
            );
        }
    }

    game.handInProgress = false;
    game.phase = "RESULT";
    game.awaitingNextHand = game.currentHand < game.totalHands;

    if (game.awaitingNextHand) {
        game.status =
            "Hand complete. " +
            getName(game.host) +
            " can start the next hand.";

        sendBlackjackState(game);
        readyBlackjackPlayers(game);
        return;
    }

    game.status = "Final hand complete.";
    sendBlackjackState(game);
    finishBlackjackMatch(gameIndex);
}

bool canBlackjackDouble(
    const BlackjackGame& game,
    Socket playerSocket,
    int handIndex
) {
    const BlackjackPlayer* player = blackjackPlayer(game, playerSocket);

    if (!player || handIndex < 0 || handIndex >= (int)player->hands.size())
        return false;

    const BlackjackHand& hand = player->hands[handIndex];

    return
        !blackjackHandDone(hand) &&
        hand.cards.size() == 2 &&
        blackjackAvailableChips(*player) >= hand.bet;
}

bool canBlackjackSplit(
    const BlackjackGame& game,
    Socket playerSocket,
    int handIndex
) {
    const BlackjackPlayer* player = blackjackPlayer(game, playerSocket);

    if (!player || player->hands.size() >= 2)
        return false;

    if (handIndex < 0 || handIndex >= (int)player->hands.size())
        return false;

    const BlackjackHand& hand = player->hands[handIndex];

    return
        !blackjackHandDone(hand) &&
        hand.cards.size() == 2 &&
        hand.cards[0].rank == hand.cards[1].rank &&
        blackjackAvailableChips(*player) >= hand.bet;
}


// ============================================================
// ROULETTE HELPERS - 1 TO 6 PLAYER EUROPEAN TABLE
// ============================================================

int roulettePlayerIndex(
    RouletteGame& game,
    Socket socket
) {
    for (int i = 0; i < (int)game.players.size(); i++) {
        if (game.players[i].socket == socket)
            return i;
    }

    return -1;
}

RoulettePlayer* roulettePlayer(
    RouletteGame& game,
    Socket socket
) {
    int index = roulettePlayerIndex(game, socket);

    if (index < 0)
        return nullptr;

    return &game.players[index];
}

int rouletteBetTotal(
    const RoulettePlayer& player
) {
    int total = 0;

    for (const RouletteBet& bet : player.bets)
        total += bet.amount;

    return total;
}

bool rouletteIsRed(int number) {
    const int redNumbers[] = {
        1,3,5,7,9,12,14,16,18,
        19,21,23,25,27,30,32,34,36
    };

    for (int red : redNumbers) {
        if (number == red)
            return true;
    }

    return false;
}

bool rouletteBetWins(
    const RouletteBet& bet,
    int result
) {
    if (bet.type == "NUMBER")
        return result == bet.value;

    if (result == 0)
        return false;

    if (bet.type == "RED")
        return rouletteIsRed(result);

    if (bet.type == "BLACK")
        return !rouletteIsRed(result);

    if (bet.type == "ODD")
        return result % 2 == 1;

    if (bet.type == "EVEN")
        return result % 2 == 0;

    if (bet.type == "LOW")
        return result >= 1 && result <= 18;

    if (bet.type == "HIGH")
        return result >= 19 && result <= 36;

    if (bet.type == "DOZEN1")
        return result >= 1 && result <= 12;

    if (bet.type == "DOZEN2")
        return result >= 13 && result <= 24;

    if (bet.type == "DOZEN3")
        return result >= 25 && result <= 36;

    return false;
}

int rouletteProfitMultiplier(
    const RouletteBet& bet
) {
    if (bet.type == "NUMBER")
        return 35;

    if (
        bet.type == "DOZEN1" ||
        bet.type == "DOZEN2" ||
        bet.type == "DOZEN3"
    ) {
        return 2;
    }

    return 1;
}

bool validRouletteBetType(
    const string& type,
    int value
) {
    if (type == "NUMBER")
        return value >= 0 && value <= 36;

    return
        type == "RED" ||
        type == "BLACK" ||
        type == "ODD" ||
        type == "EVEN" ||
        type == "LOW" ||
        type == "HIGH" ||
        type == "DOZEN1" ||
        type == "DOZEN2" ||
        type == "DOZEN3";
}

void sendRouletteError(
    Socket socket,
    const string& text
) {
    sendPacket(
        socket,
        "RLT_ERROR",
        text
    );
}

void sendRouletteNotice(
    Socket socket,
    const string& text
) {
    sendPacket(
        socket,
        "RLT_NOTICE",
        text
    );
}

string encodeRoulettePlayer(
    const RoulettePlayer& player
) {
    return
        getName(player.socket) + "^" +
        to_string(player.chips) + "^" +
        (player.ready ? "1" : "0") + "^" +
        to_string(rouletteBetTotal(player));
}

string encodeRouletteBets(
    const RoulettePlayer& player
) {
    if (player.bets.empty())
        return "-";

    string result;

    for (int i = 0; i < (int)player.bets.size(); i++) {
        if (i > 0)
            result += ";";

        result +=
            player.bets[i].type + "~" +
            to_string(player.bets[i].value) + "~" +
            to_string(player.bets[i].amount);
    }

    return result;
}

string encodeRouletteTableBets(
    const RouletteGame& game
) {
    string result;

    for (const RoulettePlayer& player : game.players) {
        string playerName = getName(player.socket);

        for (const RouletteBet& bet : player.bets) {
            if (!result.empty())
                result += ";";

            result +=
                playerName + "~" +
                bet.type + "~" +
                to_string(bet.value) + "~" +
                to_string(bet.amount);
        }
    }

    return result.empty() ? "-" : result;
}

void sendRouletteState(
    RouletteGame& game
) {
    string state =
        game.phase + "|" +
        to_string(game.currentRound) + "|" +
        to_string(game.totalRounds) + "|" +
        to_string(game.startingChips) + "|" +
        getName(game.host) + "|" +
        to_string(game.lastResult) + "|" +
        game.status + "|" +
        to_string(game.players.size());

    for (const RoulettePlayer& player : game.players) {
        state += "|" +
            encodeRoulettePlayer(player);
    }

    for (const RoulettePlayer& player : game.players) {
        sendPacket(
            player.socket,
            "RLT_STATE",
            state
        );

        sendPacket(
            player.socket,
            "RLT_BETS",
            encodeRouletteBets(player)
        );

        sendPacket(
            player.socket,
            "RLT_TABLE_BETS",
            encodeRouletteTableBets(game)
        );
    }
}

void readyRoulettePlayers(
    RouletteGame& game
) {
    for (const RoulettePlayer& player : game.players)
        sendReady(player.socket);
}

void startRouletteBetting(
    RouletteGame& game
) {
    game.phase = "BETTING";
    game.lastResult = -1;
    game.status =
        "Place your bets, then lock them.";

    for (RoulettePlayer& player : game.players) {
        player.ready = false;
        player.bets.clear();
    }

    sendRouletteState(game);
    readyRoulettePlayers(game);
}

void resolveRouletteSpin(
    int gameIndex
) {
    if (
        gameIndex < 0 ||
        gameIndex >= (int)rouletteGames.size()
    ) {
        return;
    }

    RouletteGame& game =
        rouletteGames[gameIndex];

    uniform_int_distribution<int> distribution(
        0,
        36
    );

    int result =
        distribution(rng);

    game.lastResult = result;
    game.phase = "RESULT";
    game.status =
        "Winning number: " +
        to_string(result);

    // Tell clients the result first so the wheel can begin animating.
    for (const RoulettePlayer& player : game.players) {
        sendPacket(
            player.socket,
            "RLT_SPIN",
            to_string(result)
        );
    }

    for (RoulettePlayer& player : game.players) {
        int delta = 0;
        int wins = 0;

        for (const RouletteBet& bet : player.bets) {
            if (rouletteBetWins(bet, result)) {
                delta +=
                    bet.amount *
                    rouletteProfitMultiplier(bet);

                wins++;
            }
            else {
                delta -=
                    bet.amount;
            }
        }

        player.chips += delta;

        string summary;

        if (delta > 0) {
            summary =
                "won " +
                to_string(wins) +
                " bet" +
                (wins == 1 ? "" : "s");
        }
        else if (delta < 0) {
            summary = "round loss";
        }
        else {
            summary = "even round";
        }

        sendPacket(
            player.socket,
            "RLT_PAYOUT",
            getName(player.socket) + "|" +
            to_string(delta) + "|" +
            to_string(player.chips) + "|" +
            summary
        );

        player.ready = false;
    }

    if (game.currentRound >= game.totalRounds) {
        int topStack = -1;
        string winner;

        for (const RoulettePlayer& player : game.players) {
            if (player.chips > topStack) {
                topStack = player.chips;
                winner = getName(player.socket);
            }
        }

        string finalSummary =
            "Roulette match complete. Top stack: " +
            winner +
            " with " +
            to_string(topStack) +
            " chips.";

        // IMPORTANT:
        // Keep the visual state as RESULT. If we send ENDED here,
        // clients receive RLT_SPIN and ENDED in the same network batch
        // and the end panel replaces the wheel before the first
        // animation frame is drawn.
        game.phase = "RESULT";
        game.status =
            "Final spin - winning number: " +
            to_string(result);

        sendRouletteState(game);

        vector<Socket> sockets;

        for (const RoulettePlayer& player : game.players)
            sockets.push_back(player.socket);

        // RLT_FINAL tells the GUI that this is the last round, but the
        // GUI waits for the wheel animation + a short result hold before
        // showing the match-complete screen.
        for (Socket socket : sockets) {
            sendPacket(
                socket,
                "RLT_FINAL",
                finalSummary
            );

            sendReady(socket);
        }

        // The authoritative match is finished server-side. Clients keep
        // only enough local state to finish the visual animation.
        rouletteGames.erase(
            rouletteGames.begin() +
            gameIndex
        );

        return;
    }

    sendRouletteState(game);
    readyRoulettePlayers(game);
}


// ============================================================
// POKER - 2 TO 6 PLAYER TEXAS HOLD'EM
// ============================================================

string pokerStageName(PokerStage stage) {
    switch (stage) {
        case PokerStage::PREFLOP: return "PREFLOP";
        case PokerStage::FLOP: return "FLOP";
        case PokerStage::TURN: return "TURN";
        case PokerStage::RIVER: return "RIVER";
        case PokerStage::SHOWDOWN: return "SHOWDOWN";
    }

    return "UNKNOWN";
}

int pokerPlayerIndex(const PokerGame& game, Socket socket) {
    for (int i = 0; i < (int)game.players.size(); i++)
        if (game.players[i].socket == socket)
            return i;

    return -1;
}

PokerGame::Player* pokerPlayer(PokerGame& game, Socket socket) {
    int index = pokerPlayerIndex(game, socket);
    return index < 0 ? nullptr : &game.players[index];
}

const PokerGame::Player* pokerPlayer(const PokerGame& game, Socket socket) {
    int index = pokerPlayerIndex(game, socket);
    return index < 0 ? nullptr : &game.players[index];
}

Socket nextPokerPlayer(
    const PokerGame& game,
    Socket after,
    bool requireChips,
    bool requireAction
) {
    if (game.players.empty())
        return INVALID_SOCK;

    int start = pokerPlayerIndex(game, after);
    if (start < 0)
        start = (int)game.players.size() - 1;

    for (int offset = 1; offset <= (int)game.players.size(); offset++) {
        const PokerGame::Player& player =
            game.players[(start + offset) % game.players.size()];

        if (player.left)
            continue;
        if (requireChips && player.chips <= 0)
            continue;
        if (requireAction && (player.folded || player.chips <= 0))
            continue;
        return player.socket;
    }

    return INVALID_SOCK;
}

int activePokerPlayers(const PokerGame& game) {
    int count = 0;
    for (const PokerGame::Player& player : game.players)
        if (!player.left && !player.folded)
            count++;
    return count;
}

int seatedPokerPlayers(const PokerGame& game) {
    int count = 0;
    for (const PokerGame::Player& player : game.players)
        if (!player.left)
            count++;
    return count;
}

int fundedPokerPlayers(const PokerGame& game) {
    int count = 0;
    for (const PokerGame::Player& player : game.players)
        if (!player.left && player.chips > 0)
            count++;
    return count;
}

int pokerMaximumRaiseTo(const PokerGame& game, Socket socket) {
    const PokerGame::Player* actor = pokerPlayer(game, socket);
    if (!actor)
        return game.currentBet;

    int actorMaximum = actor->roundBet + actor->chips;
    int opponentMaximum = 0;

    for (const PokerGame::Player& player : game.players) {
        if (player.left || player.socket == socket || player.folded)
            continue;
        opponentMaximum = max(
            opponentMaximum,
            player.roundBet + player.chips
        );
    }

    return min(actorMaximum, opponentMaximum);
}

string pokerCardCode(const Card& card) {
    return card.rank + string(1, card.suit);
}

vector<Card> makePokerDeck() {
    vector<Card> deck;

    const vector<pair<string, int>> ranks = {
        {"2", 2}, {"3", 3}, {"4", 4}, {"5", 5},
        {"6", 6}, {"7", 7}, {"8", 8}, {"9", 9},
        {"10", 10}, {"J", 11}, {"Q", 12}, {"K", 13}, {"A", 14}
    };

    const char suits[] = {'S', 'H', 'D', 'C'};

    for (char suit : suits) {
        for (const auto& rank : ranks) {
            Card card;
            card.rank = rank.first;
            card.suit = suit;
            card.value = rank.second;
            deck.push_back(card);
        }
    }

    shuffle(deck.begin(), deck.end(), rng);
    return deck;
}

Card drawPokerCard(PokerGame& game) {
    Card card = game.deck.back();
    game.deck.pop_back();
    return card;
}

void sendPokerLobbyState(
    PokerGame& game
) {
    string data =
        getName(game.host) + "|" +
        to_string(game.startingChips) + "|" +
        to_string(game.smallBlind) + "|" +
        to_string(game.bigBlind) + "|" +
        game.status + "|" +
        to_string(seatedPokerPlayers(game));

    for (const PokerGame::Player& player : game.players)
        if (!player.left)
            data += "|" + player.name;

    for (const PokerGame::Player& player : game.players) {
        if (player.left)
            continue;
        sendPacket(player.socket, "POKER_LOBBY", data);
        sendReady(player.socket);
    }
}

void sendPokerStateTo(
    PokerGame& game,
    Socket player
) {
    string state =
        pokerStageName(game.stage) + "|" +
        to_string(game.pot) + "|" +
        to_string(game.currentBet) + "|" +
        (game.turn == INVALID_SOCK ? string("") : getName(game.turn)) + "|" +
        (game.dealer == INVALID_SOCK ? string("") : getName(game.dealer)) + "|" +
        (game.smallBlindPlayer == INVALID_SOCK ? string("") : getName(game.smallBlindPlayer)) + "|" +
        (game.bigBlindPlayer == INVALID_SOCK ? string("") : getName(game.bigBlindPlayer)) + "|" +
        to_string(game.smallBlind) + "|" +
        to_string(game.bigBlind) + "|" +
        (game.handActive ? "1" : "0") + "|" +
        to_string(game.handNumber) + "|" +
        to_string(game.lastRaiseSize) + "|" +
        to_string(pokerMaximumRaiseTo(game, player)) + "|" +
        to_string(seatedPokerPlayers(game));

    for (const PokerGame::Player& seat : game.players) {
        if (seat.left)
            continue;
        state +=
            "|" + seat.name +
            "|" + to_string(seat.chips) +
            "|" + to_string(seat.roundBet) +
            "|" + (seat.folded ? "1" : "0") +
            "|" + (seat.chips == 0 ? "1" : "0");
    }

    sendPacket(player, "POKER_HOST", getName(game.host));
    sendPacket(player, "POKER_STATE", state);

    const PokerGame::Player* seat = pokerPlayer(game, player);

    string holeData = "--|--";

    if (seat && seat->hole.size() >= 2) {
        holeData =
            pokerCardCode(seat->hole[0]) + "|" +
            pokerCardCode(seat->hole[1]);
    }

    sendPacket(player, "POKER_HOLE", holeData);

    string boardData;

    for (int i = 0; i < 5; i++) {
        if (i > 0)
            boardData += "|";

        boardData +=
            i < (int)game.community.size()
            ? pokerCardCode(game.community[i])
            : "--";
    }

    sendPacket(player, "POKER_BOARD", boardData);
}

void sendPokerState(PokerGame& game) {
    for (const PokerGame::Player& player : game.players) {
        if (player.left)
            continue;
        sendPokerStateTo(game, player.socket);
        sendReady(player.socket);
    }
}

void sendPokerNotice(PokerGame& game, const string& text) {
    for (const PokerGame::Player& player : game.players)
        if (!player.left)
            sendPacket(player.socket, "POKER_NOTICE", text);
}

void sendPokerResult(PokerGame& game, const string& text) {
    for (const PokerGame::Player& player : game.players)
        if (!player.left)
            sendPacket(player.socket, "POKER_RESULT", text);
}

Socket previousSeatedPokerPlayer(
    const PokerGame& game,
    Socket before
) {
    if (game.players.empty())
        return INVALID_SOCK;

    int start = pokerPlayerIndex(game, before);
    if (start < 0)
        start = 0;

    for (int offset = 1; offset <= (int)game.players.size(); offset++) {
        int index =
            (start - offset + (int)game.players.size()) %
            (int)game.players.size();

        if (!game.players[index].left)
            return game.players[index].socket;
    }

    return INVALID_SOCK;
}

void purgeDepartedPokerPlayers(PokerGame& game) {
    const PokerGame::Player* dealer = pokerPlayer(game, game.dealer);
    if (dealer && dealer->left)
        game.dealer = previousSeatedPokerPlayer(game, game.dealer);

    for (const PokerGame::Player& player : game.players) {
        if (!player.left)
            continue;

        PokerGame::Standing standing;
        standing.name = player.name;
        standing.chips = player.chips;
        game.departedStandings.push_back(standing);
    }

    game.players.erase(
        remove_if(
            game.players.begin(),
            game.players.end(),
            [](const PokerGame::Player& player) {
                return player.left;
            }
        ),
        game.players.end()
    );

    if (pokerPlayerIndex(game, game.host) < 0)
        game.host = game.players.empty()
            ? INVALID_SOCK
            : game.players.front().socket;
}

string pokerLeaderboardData(const PokerGame& game) {
    vector<PokerGame::Standing> ranked;
    vector<PokerGame::Standing> leavers;

    for (const PokerGame::Player& player : game.players) {
        if (player.left)
            continue;

        PokerGame::Standing standing;
        standing.name = player.name;
        standing.chips = player.chips;
        standing.left = false;
        ranked.push_back(standing);
    }

    sort(
        ranked.begin(),
        ranked.end(),
        [](const PokerGame::Standing& left, const PokerGame::Standing& right) {
            if (left.chips != right.chips)
                return left.chips > right.chips;
            return left.name < right.name;
        }
    );

    // A player who returned to the table is represented by the current seat,
    // not by an older departure record. Repeated departures also collapse to
    // one entry, with the most recent chip count retained.
    for (const PokerGame::Standing& departed : game.departedStandings) {
        bool currentlySeated = any_of(
            ranked.begin(),
            ranked.end(),
            [&](const PokerGame::Standing& standing) {
                return lowerCopy(standing.name) == lowerCopy(departed.name);
            }
        );
        if (currentlySeated)
            continue;

        auto existing = find_if(
            leavers.begin(),
            leavers.end(),
            [&](const PokerGame::Standing& standing) {
                return lowerCopy(standing.name) == lowerCopy(departed.name);
            }
        );

        if (existing == leavers.end()) {
            PokerGame::Standing standing = departed;
            standing.left = true;
            leavers.push_back(standing);
        }
        else {
            existing->chips = departed.chips;
        }
    }

    sort(
        leavers.begin(),
        leavers.end(),
        [](const PokerGame::Standing& left, const PokerGame::Standing& right) {
            return left.name < right.name;
        }
    );

    vector<PokerGame::Standing> standings = ranked;
    standings.insert(standings.end(), leavers.begin(), leavers.end());

    string data =
        to_string(game.startingChips) + "|" +
        to_string(standings.size());

    for (const PokerGame::Standing& standing : standings) {
        data +=
            "|" + standing.name +
            "|" + to_string(standing.chips) +
            "|" + (standing.left ? "1" : "0");
    }

    return data;
}

void endPokerMatch(int gameIndex, const string& result) {
    if (gameIndex < 0 || gameIndex >= (int)pokerGames.size())
        return;

    PokerGame& game = pokerGames[gameIndex];
    purgeDepartedPokerPlayers(game);
    string leaderboard = pokerLeaderboardData(game);

    for (const PokerGame::Player& player : game.players) {
        sendPacket(player.socket, "POKER_LEADERBOARD", leaderboard);
        sendPacket(player.socket, "POKER_END", result);
        sendReady(player.socket);
    }

    pokerGames.erase(pokerGames.begin() + gameIndex);
}

void sendPokerReveal(PokerGame& game) {
    for (const PokerGame::Player& revealed : game.players) {
        if (revealed.left || revealed.folded || revealed.hole.size() < 2)
            continue;

        string data =
            revealed.name + "|" +
            pokerCardCode(revealed.hole[0]) + "|" +
            pokerCardCode(revealed.hole[1]);

        for (const PokerGame::Player& viewer : game.players)
            if (!viewer.left)
                sendPacket(viewer.socket, "POKER_REVEAL", data);
    }
}

uint64_t packPokerScore(
    int category,
    const vector<int>& kickers
) {
    uint64_t score = ((uint64_t)category) << 24;
    const int shifts[5] = {20, 16, 12, 8, 4};

    for (
        int i = 0;
        i < (int)kickers.size() && i < 5;
        i++
    ) {
        score |= ((uint64_t)kickers[i]) << shifts[i];
    }

    return score;
}

uint64_t evaluatePokerFive(const array<Card, 5>& cards) {
    int counts[15] = {};
    vector<int> ranks;

    bool flush = true;
    char firstSuit = cards[0].suit;

    for (const Card& card : cards) {
        counts[card.value]++;
        ranks.push_back(card.value);

        if (card.suit != firstSuit)
            flush = false;
    }

    sort(ranks.begin(), ranks.end(), greater<int>());

    vector<int> uniqueRanks = ranks;
    uniqueRanks.erase(
        unique(uniqueRanks.begin(), uniqueRanks.end()),
        uniqueRanks.end()
    );

    int straightHigh = 0;

    if (uniqueRanks.size() == 5) {
        if (uniqueRanks[0] - uniqueRanks[4] == 4) {
            straightHigh = uniqueRanks[0];
        }
        else if (
            uniqueRanks[0] == 14 &&
            uniqueRanks[1] == 5 &&
            uniqueRanks[2] == 4 &&
            uniqueRanks[3] == 3 &&
            uniqueRanks[4] == 2
        ) {
            straightHigh = 5;
        }
    }

    vector<int> quads;
    vector<int> trips;
    vector<int> pairs;
    vector<int> singles;

    for (int rank = 14; rank >= 2; rank--) {
        if (counts[rank] == 4)
            quads.push_back(rank);
        else if (counts[rank] == 3)
            trips.push_back(rank);
        else if (counts[rank] == 2)
            pairs.push_back(rank);
        else if (counts[rank] == 1)
            singles.push_back(rank);
    }

    if (flush && straightHigh)
        return packPokerScore(8, {straightHigh});

    if (!quads.empty())
        return packPokerScore(7, {quads[0], singles[0]});

    if (!trips.empty() && !pairs.empty())
        return packPokerScore(6, {trips[0], pairs[0]});

    if (flush)
        return packPokerScore(5, ranks);

    if (straightHigh)
        return packPokerScore(4, {straightHigh});

    if (!trips.empty()) {
        vector<int> values = {trips[0]};
        values.insert(values.end(), singles.begin(), singles.end());
        return packPokerScore(3, values);
    }

    if (pairs.size() >= 2) {
        return packPokerScore(
            2,
            {pairs[0], pairs[1], singles[0]}
        );
    }

    if (pairs.size() == 1) {
        vector<int> values = {pairs[0]};
        values.insert(values.end(), singles.begin(), singles.end());
        return packPokerScore(1, values);
    }

    return packPokerScore(0, ranks);
}

uint64_t evaluatePokerBest(
    const vector<Card>& hole,
    const vector<Card>& community
) {
    vector<Card> all = hole;
    all.insert(all.end(), community.begin(), community.end());

    if (all.size() < 5)
        return 0;

    uint64_t best = 0;
    int n = (int)all.size();

    for (int a = 0; a < n - 4; a++)
    for (int b = a + 1; b < n - 3; b++)
    for (int c = b + 1; c < n - 2; c++)
    for (int d = c + 1; d < n - 1; d++)
    for (int e = d + 1; e < n; e++) {
        array<Card, 5> five = {
            all[a], all[b], all[c], all[d], all[e]
        };

        best = max(best, evaluatePokerFive(five));
    }

    return best;
}

string pokerHandName(uint64_t score) {
    int category = (int)(score >> 24);

    switch (category) {
        case 8: return "Straight Flush";
        case 7: return "Four of a Kind";
        case 6: return "Full House";
        case 5: return "Flush";
        case 4: return "Straight";
        case 3: return "Three of a Kind";
        case 2: return "Two Pair";
        case 1: return "Pair";
        default: return "High Card";
    }
}

void normalizePokerUncalledBet(PokerGame& game) {
    int highest = 0;
    int secondHighest = 0;
    PokerGame::Player* highestPlayer = nullptr;
    bool tiedForHighest = false;

    for (PokerGame::Player& player : game.players) {
        if (player.roundBet > highest) {
            secondHighest = highest;
            highest = player.roundBet;
            highestPlayer = &player;
            tiedForHighest = false;
        }
        else if (player.roundBet == highest) {
            tiedForHighest = true;
        }
        else {
            secondHighest = max(secondHighest, player.roundBet);
        }
    }

    if (highestPlayer && !tiedForHighest && highest > secondHighest) {
        int refund = highest - secondHighest;
        highestPlayer->roundBet -= refund;
        highestPlayer->handContribution -= refund;
        highestPlayer->chips += refund;
        game.pot -= refund;
        highest = secondHighest;
    }

    game.currentBet = highest;
}

void finishPokerHand(PokerGame& game, const string& result) {
    game.pot = 0;
    game.handActive = false;
    game.turn = INVALID_SOCK;
    game.stage = PokerStage::SHOWDOWN;
    game.phase = "RESULT";
    game.status = result;
    purgeDepartedPokerPlayers(game);
    sendPokerState(game);
    sendPokerResult(game, result);
}

void awardPokerFoldWin(PokerGame& game) {
    PokerGame::Player* winner = nullptr;
    for (PokerGame::Player& player : game.players) {
        if (!player.left && !player.folded) {
            winner = &player;
            break;
        }
    }

    if (!winner)
        return;

    int won = game.pot;
    winner->chips += won;
    finishPokerHand(
        game,
        winner->name + " wins " + to_string(won) +
        " chips; all other players folded."
    );
}

void pokerShowdown(PokerGame& game) {
    while (game.community.size() < 5)
        game.community.push_back(drawPokerCard(game));

    normalizePokerUncalledBet(game);
    sendPokerReveal(game);

    vector<int> levels;
    for (const PokerGame::Player& player : game.players)
        if (player.handContribution > 0)
            levels.push_back(player.handContribution);

    sort(levels.begin(), levels.end());
    levels.erase(unique(levels.begin(), levels.end()), levels.end());

    int previous = 0;
    vector<string> winnersSummary;

    for (int level : levels) {
        int contributors = 0;
        vector<int> eligible;

        for (int i = 0; i < (int)game.players.size(); i++) {
            const PokerGame::Player& player = game.players[i];
            if (player.handContribution >= level)
                contributors++;
            if (!player.left && !player.folded && player.handContribution >= level)
                eligible.push_back(i);
        }

        int sidePot = (level - previous) * contributors;
        previous = level;
        if (sidePot <= 0 || eligible.empty())
            continue;

        uint64_t best = 0;
        vector<int> winners;
        for (int index : eligible) {
            uint64_t score = evaluatePokerBest(
                game.players[index].hole,
                game.community
            );
            if (winners.empty() || score > best) {
                best = score;
                winners = {index};
            }
            else if (score == best) {
                winners.push_back(index);
            }
        }

        int share = sidePot / (int)winners.size();
        int odd = sidePot % (int)winners.size();
        for (int index : winners)
            game.players[index].chips += share;

        int dealerIndex = pokerPlayerIndex(game, game.dealer);
        for (int offset = 1; odd > 0 && offset <= (int)game.players.size(); offset++) {
            int index = (dealerIndex + offset) % game.players.size();
            if (find(winners.begin(), winners.end(), index) != winners.end()) {
                game.players[index].chips++;
                odd--;
            }
        }

        string names;
        for (int index : winners) {
            if (!names.empty())
                names += winners.size() == 2 ? " and " : ", ";
            names += game.players[index].name;
        }

        winnersSummary.push_back(
            names + " win" + (winners.size() == 1 ? "s " : " ") +
            to_string(sidePot) + " with " + pokerHandName(best)
        );
    }

    string result;
    for (int i = 0; i < (int)winnersSummary.size(); i++) {
        if (i > 0)
            result += " | ";
        result += winnersSummary[i];
    }
    if (result.empty())
        result = "Poker hand complete.";

    finishPokerHand(game, result + ".");
}

void runOutPokerBoard(PokerGame& game) {
    while (game.community.size() < 5)
        game.community.push_back(drawPokerCard(game));
    pokerShowdown(game);
}

int pokerPlayersAbleToAct(const PokerGame& game) {
    int count = 0;
    for (const PokerGame::Player& player : game.players)
        if (!player.left && !player.folded && player.chips > 0)
            count++;
    return count;
}

Socket nextPokerActor(const PokerGame& game, Socket after) {
    if (game.players.empty())
        return INVALID_SOCK;

    int start = pokerPlayerIndex(game, after);
    for (int offset = 1; offset <= (int)game.players.size(); offset++) {
        const PokerGame::Player& player =
            game.players[(start + offset) % game.players.size()];
        if (
            !player.left &&
            !player.folded &&
            player.chips > 0 &&
            (!player.acted || player.roundBet < game.currentBet)
        ) {
            return player.socket;
        }
    }
    return INVALID_SOCK;
}

bool pokerBettingRoundComplete(const PokerGame& game) {
    for (const PokerGame::Player& player : game.players) {
        if (
            !player.left &&
            !player.folded &&
            player.chips > 0 &&
            (!player.acted || player.roundBet != game.currentBet)
        ) {
            return false;
        }
    }
    return true;
}

void startPokerHand(PokerGame& game) {
    game.phase = "PLAYING";
    game.handNumber++;
    game.stage = PokerStage::PREFLOP;
    game.handActive = true;
    game.turn = INVALID_SOCK;
    game.deck = makePokerDeck();
    game.community.clear();
    game.pot = 0;
    game.currentBet = 0;
    game.lastRaiseSize = game.bigBlind;

    for (PokerGame::Player& player : game.players) {
        if (player.left)
            continue;
        player.roundBet = 0;
        player.handContribution = 0;
        player.acted = player.chips == 0;
        player.folded = player.chips == 0;
        player.hole.clear();
    }

    vector<Socket> funded;
    for (const PokerGame::Player& player : game.players)
        if (!player.left && player.chips > 0)
            funded.push_back(player.socket);

    if (funded.size() < 2) {
        game.handActive = false;
        return;
    }

    if (pokerPlayerIndex(game, game.dealer) < 0 ||
        !pokerPlayer(game, game.dealer) ||
        pokerPlayer(game, game.dealer)->left ||
        pokerPlayer(game, game.dealer)->chips <= 0) {
        game.dealer = funded.front();
    }

    if (funded.size() == 2) {
        game.smallBlindPlayer = game.dealer;
        game.bigBlindPlayer = nextPokerPlayer(game, game.dealer, true, false);
    }
    else {
        game.smallBlindPlayer = nextPokerPlayer(game, game.dealer, true, false);
        game.bigBlindPlayer = nextPokerPlayer(game, game.smallBlindPlayer, true, false);
    }

    Socket dealFrom = game.dealer;
    for (int card = 0; card < 2; card++) {
        Socket seat = dealFrom;
        for (int count = 0; count < (int)funded.size(); count++) {
            seat = nextPokerPlayer(game, seat, true, false);
            pokerPlayer(game, seat)->hole.push_back(drawPokerCard(game));
        }
    }

    auto postBlind = [&](Socket socket, int blind) {
        PokerGame::Player* player = pokerPlayer(game, socket);
        int amount = min(blind, player->chips);
        player->chips -= amount;
        player->roundBet += amount;
        player->handContribution += amount;
        game.pot += amount;
    };

    postBlind(game.smallBlindPlayer, game.smallBlind);
    postBlind(game.bigBlindPlayer, game.bigBlind);

    for (PokerGame::Player& player : game.players)
        if (!player.left)
            player.acted = player.folded || player.chips == 0;

    for (const PokerGame::Player& player : game.players)
        if (!player.left)
            game.currentBet = max(game.currentBet, player.roundBet);

    if (pokerPlayersAbleToAct(game) == 0) {
        runOutPokerBoard(game);
        return;
    }

    game.turn = nextPokerActor(game, game.bigBlindPlayer);
    if (game.turn == INVALID_SOCK) {
        runOutPokerBoard(game);
        return;
    }

    sendPokerState(game);
    sendPokerNotice(
        game,
        "Hand " + to_string(game.handNumber) + " - cards dealt."
    );
}

void advancePokerStreet(PokerGame& game) {
    for (PokerGame::Player& player : game.players) {
        if (player.left)
            continue;
        player.roundBet = 0;
        player.acted = player.folded || player.chips == 0;
    }
    game.currentBet = 0;
    game.lastRaiseSize = game.bigBlind;

    if (game.stage == PokerStage::PREFLOP) {
        game.stage = PokerStage::FLOP;
        game.community.push_back(drawPokerCard(game));
        game.community.push_back(drawPokerCard(game));
        game.community.push_back(drawPokerCard(game));
    }
    else if (game.stage == PokerStage::FLOP) {
        game.stage = PokerStage::TURN;
        game.community.push_back(drawPokerCard(game));
    }
    else if (game.stage == PokerStage::TURN) {
        game.stage = PokerStage::RIVER;
        game.community.push_back(drawPokerCard(game));
    }
    else if (game.stage == PokerStage::RIVER) {
        pokerShowdown(game);
        return;
    }

    if (pokerPlayersAbleToAct(game) <= 1) {
        runOutPokerBoard(game);
        return;
    }

    game.turn = nextPokerActor(game, game.dealer);
    if (game.turn == INVALID_SOCK) {
        runOutPokerBoard(game);
        return;
    }
    sendPokerState(game);
}

void finishPokerAction(PokerGame& game, Socket actor) {
    if (activePokerPlayers(game) == 1) {
        awardPokerFoldWin(game);
        return;
    }

    if (pokerBettingRoundComplete(game)) {
        normalizePokerUncalledBet(game);
        if (pokerPlayersAbleToAct(game) <= 1)
            runOutPokerBoard(game);
        else
            advancePokerStreet(game);
        return;
    }

    game.turn = nextPokerActor(game, actor);
    if (game.turn == INVALID_SOCK) {
        normalizePokerUncalledBet(game);
        runOutPokerBoard(game);
        return;
    }
    sendPokerState(game);
}

void removePokerPlayerFromMatch(
    int gameIndex,
    Socket socket,
    const string& name,
    bool notifyDepartingPlayer
) {
    if (gameIndex < 0 || gameIndex >= (int)pokerGames.size())
        return;

    PokerGame& game = pokerGames[gameIndex];
    PokerGame::Player* player = pokerPlayer(game, socket);
    if (!player || player->left)
        return;

    bool wasTurn = game.handActive && game.turn == socket;

    // Keep the folded record internally until the current hand settles so
    // chips already committed by the departing player remain in every pot.
    player->left = true;
    player->folded = true;
    player->acted = true;

    Socket previous = previousSeatedPokerPlayer(game, socket);

    if (notifyDepartingPlayer) {
        sendPacket(
            socket,
            "POKER_END",
            "You left the Poker table."
        );
        sendReady(socket);
    }

    sendPokerNotice(game, name + " left the Poker table.");

    if (!game.handActive)
        purgeDepartedPokerPlayers(game);

    if (seatedPokerPlayers(game) < 2) {
        if (game.handActive && activePokerPlayers(game) == 1)
            awardPokerFoldWin(game);

        string winner;
        for (const PokerGame::Player& remaining : game.players) {
            if (!remaining.left) {
                winner = remaining.name;
                break;
            }
        }

        string result = winner.empty()
            ? "Poker match ended."
            : winner + " wins the Poker match.";

        endPokerMatch(gameIndex, result);
        return;
    }

    if (!game.handActive) {
        game.status =
            name +
            " left between hands. " +
            to_string(seatedPokerPlayers(game)) +
            " players remain.";
        sendPokerState(game);
        sendPokerNotice(game, game.status);
        return;
    }

    if (activePokerPlayers(game) == 1) {
        awardPokerFoldWin(game);
        return;
    }

    if (wasTurn) {
        finishPokerAction(game, previous);
        return;
    }

    if (pokerBettingRoundComplete(game)) {
        normalizePokerUncalledBet(game);
        if (pokerPlayersAbleToAct(game) <= 1)
            runOutPokerBoard(game);
        else
            advancePokerStreet(game);
        return;
    }

    sendPokerState(game);
}
// JENG ARENA LOBBY HELPERS
// ============================================================

bool arenaTeamMode(const string& mode) {
    return mode == "TEAM_2V2" || mode == "TEAM_3V3";
}

bool validArenaMode(const string& mode) {
    return
        mode == "SCORE_FFA" ||
        mode == "TIME_FFA" ||
        mode == "DUEL" ||
        mode == "TEAM_2V2" ||
        mode == "TEAM_3V3";
}


bool validArenaMap(const string& map) {
    return
        map == "REACTOR_YARD" ||
        map == "ALIEN_OUTPOST";
}

float arenaHalf(const ArenaGame& game) {
    return
        game.map == "ALIEN_OUTPOST"
        ? 30.0f
        : ARENA_HALF;
}

float arenaProjectileBoundary(const ArenaGame& game) {
    return arenaHalf(game) + 0.5f;
}

float arenaSafeSpawnDistance(const ArenaGame& game) {
    return
        game.map == "ALIEN_OUTPOST"
        ? 8.0f
        : 7.0f;
}

string arenaStateModeToken(const ArenaGame& game) {
    return
        game.mode +
        "@" +
        game.map;
}

int normalizeArenaPlayerCount(const string& mode, int requested) {
    if (mode == "DUEL")
        return 2;
    if (mode == "TEAM_2V2")
        return 4;
    if (mode == "TEAM_3V3")
        return 6;

    return max(2, min(requested, MAX_ARENA_PLAYERS));
}

int arenaPlayerIndex(const ArenaGame& game, Socket socket) {
    for (int i = 0; i < (int)game.players.size(); i++) {
        if (game.players[i].socket == socket)
            return i;
    }

    return -1;
}

bool arenaColorUsed(
    const ArenaGame& game,
    int colorIndex,
    Socket ignoreSocket = INVALID_SOCK
) {
    for (const ArenaPlayer& player : game.players) {
        if (
            player.socket != ignoreSocket &&
            player.colorIndex == colorIndex
        ) {
            return true;
        }
    }

    return false;
}

int firstAvailableArenaColor(const ArenaGame& game) {
    for (int color = 0; color < ARENA_COLOR_COUNT; color++) {
        if (!arenaColorUsed(game, color))
            return color;
    }

    return 0;
}

int chooseArenaTeam(const ArenaGame& game) {
    int red = 0;
    int blue = 0;

    for (const ArenaPlayer& player : game.players) {
        if (player.team == 0)
            red++;
        else if (player.team == 1)
            blue++;
    }

    return red <= blue ? 0 : 1;
}

void readyArenaPlayers(ArenaGame& game) {
    for (const ArenaPlayer& player : game.players)
        sendReady(player.socket);
}

void sendArenaPacket(
    ArenaGame& game,
    const string& type,
    const string& data
) {
    for (const ArenaPlayer& player : game.players)
        sendPacket(player.socket, type, data);
}

void sendArenaError(Socket socket, const string& text) {
    sendPacket(socket, "ARENA_ERROR", text);
}

string encodeArenaPlayer(const ArenaPlayer& player) {
    return
        getName(player.socket) + "^" +
        string(player.ready ? "1" : "0") + "^" +
        to_string(player.colorIndex) + "^" +
        to_string(player.team);
}

void sendArenaState(ArenaGame& game) {
    string payload =
        game.phase + "|" +
        arenaStateModeToken(game) + "|" +
        to_string(game.maxPlayers) + "|" +
        to_string(game.scoreLimit) + "|" +
        to_string(game.timeLimitSeconds) + "|" +
        getName(game.host) + "|" +
        game.status + "|" +
        to_string(game.players.size());

    for (const ArenaPlayer& player : game.players)
        payload += "|" + encodeArenaPlayer(player);

    sendArenaPacket(game, "ARENA_STATE", payload);
}

bool allArenaPlayersReady(const ArenaGame& game) {
    if ((int)game.players.size() != game.maxPlayers)
        return false;

    for (const ArenaPlayer& player : game.players) {
        if (!player.ready)
            return false;
    }

    return true;
}

bool parseArenaInt(const string& text, int& value) {
    try {
        size_t consumed = 0;
        int parsed = stoi(text, &consumed);

        if (consumed != text.size())
            return false;

        value = parsed;
        return true;
    }
    catch (...) {
        return false;
    }
}

bool parseArenaFloat(const string& text, float& value) {
    try {
        size_t consumed = 0;
        float parsed = stof(text, &consumed);

        if (consumed != text.size() || !isfinite(parsed))
            return false;

        value = parsed;
        return true;
    }
    catch (...) {
        return false;
    }
}

struct ArenaObstacle2D {
    float minX;
    float minZ;
    float maxX;
    float maxZ;
};

bool arenaCircleHitsBox(
    float x,
    float z,
    const ArenaObstacle2D& box
) {
    float closestX = max(box.minX, min(x, box.maxX));
    float closestZ = max(box.minZ, min(z, box.maxZ));

    float dx = x - closestX;
    float dz = z - closestZ;

    return
        dx * dx + dz * dz <
        ARENA_TANK_RADIUS * ARENA_TANK_RADIUS;
}

bool arenaPositionBlocked(
    const ArenaGame& game,
    float x,
    float z
) {
    const float half =
        arenaHalf(game);

    if (
        x < -half + ARENA_TANK_RADIUS ||
        x >  half - ARENA_TANK_RADIUS ||
        z < -half + ARENA_TANK_RADIUS ||
        z >  half - ARENA_TANK_RADIUS
    ) {
        return true;
    }

    if (game.map == "ALIEN_OUTPOST") {
        static const ArenaObstacle2D alienObstacles[] = {
            {-4.2f,  -4.2f,   4.2f,   4.2f},
            {-25.0f, -3.0f, -19.0f,   3.0f},
            { 19.0f, -3.0f,  25.0f,   3.0f},
            {-3.0f, -25.0f,   3.0f, -19.0f},
            {-3.0f,  19.0f,   3.0f,  25.0f},
            {-20.5f,-20.5f, -14.0f, -14.0f},
            { 14.0f,-20.5f,  20.5f, -14.0f},
            {-20.5f, 14.0f, -14.0f,  20.5f},
            { 14.0f, 14.0f,  20.5f,  20.5f},
            {-12.0f, -8.5f,  -8.0f,  -4.0f},
            {  8.0f,  4.0f,  12.0f,   8.5f},
            {-12.0f,  4.0f,  -8.0f,   8.5f},
            {  8.0f, -8.5f,  12.0f,  -4.0f}
        };

        for (const ArenaObstacle2D& obstacle : alienObstacles) {
            if (arenaCircleHitsBox(x, z, obstacle))
                return true;
        }

        return false;
    }

    static const ArenaObstacle2D reactorObstacles[] = {
        {-4.5f,  -4.5f,   4.5f,   4.5f},
        {-20.0f, -2.2f, -15.0f,   2.2f},
        { 15.0f, -2.2f,  20.0f,   2.2f},
        {-2.2f, -20.0f,   2.2f, -15.0f},
        {-2.2f,  15.0f,   2.2f,  20.0f},
        {-17.0f,-17.0f, -12.0f, -12.0f},
        { 12.0f,-17.0f,  17.0f, -12.0f},
        {-17.0f, 12.0f, -12.0f,  17.0f},
        { 12.0f, 12.0f,  17.0f,  17.0f}
    };

    for (const ArenaObstacle2D& obstacle : reactorObstacles) {
        if (arenaCircleHitsBox(x, z, obstacle))
            return true;
    }

    return false;
}

bool arenaPositionBlockedByPlayer(
    const ArenaGame& game,
    Socket movingSocket,
    float x,
    float z
) {
    const float minimumDistance =
        ARENA_TANK_RADIUS * 2.0f;

    const float minimumDistanceSquared =
        minimumDistance * minimumDistance;

    for (const ArenaPlayer& other : game.players) {
        if (
            other.socket == movingSocket ||
            !other.alive
        ) {
            continue;
        }

        float dx = x - other.x;
        float dz = z - other.z;

        if (
            dx * dx + dz * dz <
            minimumDistanceSquared
        ) {
            return true;
        }
    }

    return false;
}

float arenaNormalizeYaw(float yaw) {
    while (yaw > 180.0f)
        yaw -= 360.0f;

    while (yaw <= -180.0f)
        yaw += 360.0f;

    return yaw;
}

float arenaDirectionYaw(float x, float z) {
    return
        atan2(x, z) *
        (180.0f / ARENA_PI);
}

static const float ARENA_FFA_SPAWNS[8][2] = {
    {-21.0f,  21.0f}, { 0.0f,  22.0f},
    { 21.0f,  21.0f}, {22.0f,   0.0f},
    { 21.0f, -21.0f}, { 0.0f, -22.0f},
    {-21.0f, -21.0f}, {-22.0f,  0.0f}
};

static const float ARENA_RED_SPAWNS[4][2] = {
    {-21.0f, 16.0f}, {-22.0f, 5.0f},
    {-22.0f, -5.0f}, {-21.0f, -16.0f}
};

static const float ARENA_BLUE_SPAWNS[4][2] = {
    {21.0f, -16.0f}, {22.0f, -5.0f},
    {22.0f, 5.0f}, {21.0f, 16.0f}
};


static const float ALIEN_FFA_SPAWNS[8][2] = {
    {-26.0f,  26.0f}, { 0.0f,  27.0f},
    { 26.0f,  26.0f}, {27.0f,   0.0f},
    { 26.0f, -26.0f}, { 0.0f, -27.0f},
    {-26.0f, -26.0f}, {-27.0f,  0.0f}
};

static const float ALIEN_RED_SPAWNS[4][2] = {
    {-26.0f, 20.0f}, {-27.0f, 7.0f},
    {-27.0f, -7.0f}, {-26.0f, -20.0f}
};

static const float ALIEN_BLUE_SPAWNS[4][2] = {
    {26.0f, -20.0f}, {27.0f, -7.0f},
    {27.0f, 7.0f}, {26.0f, 20.0f}
};

bool arenaSpawnIsClear(
    const ArenaGame& game,
    Socket respawningSocket,
    float x,
    float z,
    float minimumDistance
) {
    float minimumDistanceSquared =
        minimumDistance * minimumDistance;

    for (const ArenaPlayer& other : game.players) {
        if (other.socket == respawningSocket || !other.alive)
            continue;

        float dx = x - other.x;
        float dz = z - other.z;

        if (dx * dx + dz * dz < minimumDistanceSquared)
            return false;
    }

    return true;
}

float arenaNearestLivingPlayerDistanceSquared(
    const ArenaGame& game,
    Socket respawningSocket,
    float x,
    float z
) {
    float best = 1000000.0f;
    bool found = false;

    for (const ArenaPlayer& other : game.players) {
        if (other.socket == respawningSocket || !other.alive)
            continue;

        float dx = x - other.x;
        float dz = z - other.z;
        float distanceSquared = dx * dx + dz * dz;

        if (distanceSquared < best)
            best = distanceSquared;

        found = true;
    }

    return found ? best : 1000000.0f;
}

void chooseArenaSpawn(
    ArenaGame& game,
    ArenaPlayer& player
) {
    vector<array<float, 2>> candidates;

    const bool alien =
        game.map == "ALIEN_OUTPOST";

    if (arenaTeamMode(game.mode)) {
        const float (*spawns)[2] = nullptr;

        if (alien) {
            spawns =
                player.team == 0
                ? ALIEN_RED_SPAWNS
                : ALIEN_BLUE_SPAWNS;
        }
        else {
            spawns =
                player.team == 0
                ? ARENA_RED_SPAWNS
                : ARENA_BLUE_SPAWNS;
        }

        for (int i = 0; i < 4; i++) {
            candidates.push_back({
                spawns[i][0],
                spawns[i][1]
            });
        }
    }
    else {
        const float (*spawns)[2] =
            alien
            ? ALIEN_FFA_SPAWNS
            : ARENA_FFA_SPAWNS;

        for (int i = 0; i < 8; i++) {
            candidates.push_back({
                spawns[i][0],
                spawns[i][1]
            });
        }
    }

    shuffle(
        candidates.begin(),
        candidates.end(),
        rng
    );

    const float safeSpawnDistance =
        arenaSafeSpawnDistance(game);

    for (const auto& spawn : candidates) {
        if (
            arenaSpawnIsClear(
                game,
                player.socket,
                spawn[0],
                spawn[1],
                safeSpawnDistance
            ) &&
            !arenaPositionBlocked(
                game,
                spawn[0],
                spawn[1]
            )
        ) {
            player.spawnX = spawn[0];
            player.spawnZ = spawn[1];
            return;
        }
    }

    float bestDistanceSquared = -1.0f;

    for (const auto& spawn : candidates) {
        if (
            arenaPositionBlocked(
                game,
                spawn[0],
                spawn[1]
            )
        ) {
            continue;
        }

        float distanceSquared =
            arenaNearestLivingPlayerDistanceSquared(
                game,
                player.socket,
                spawn[0],
                spawn[1]
            );

        if (distanceSquared > bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            player.spawnX = spawn[0];
            player.spawnZ = spawn[1];
        }
    }
}

void respawnArenaPlayer(
    ArenaGame& game,
    ArenaPlayer& player
) {
    chooseArenaSpawn(game, player);

    player.x = player.spawnX;
    player.z = player.spawnZ;

    if (arenaTeamMode(game.mode)) {
        player.bodyYaw =
            player.team == 0 ? 90.0f : -90.0f;
    }
    else {
        player.bodyYaw =
            arenaDirectionYaw(-player.x, -player.z);
    }

    player.aimYaw = player.bodyYaw;
    player.health = ARENA_MAX_HEALTH;
    player.alive = true;
    player.respawnTimer = 0.0f;
}

bool arenaPlayersAreEnemies(
    const ArenaGame& game,
    const ArenaPlayer& a,
    const ArenaPlayer& b
) {
    if (!arenaTeamMode(game.mode))
        return a.socket != b.socket;

    return a.team != b.team;
}

int arenaTeamScore(
    const ArenaGame& game,
    int team
) {
    int score = 0;

    for (const ArenaPlayer& player : game.players) {
        if (player.team == team)
            score += player.kills;
    }

    return score;
}

void initializeArenaWorld(ArenaGame& game) {
    auto now = chrono::steady_clock::now();

    for (ArenaPlayer& player : game.players)
        player.alive = false;

    for (int i = 0; i < (int)game.players.size(); i++) {
        ArenaPlayer& player = game.players[i];

        player.lastInputSequence = -1;
        player.inputClockReady = true;
        player.lastInputAt = now;
        player.fireClockReady = false;
        player.lastFireAt = now;
        player.mineClockReady = false;
        player.lastMineAt = now;

        player.kills = 0;
        player.deaths = 0;
        player.damageDealt = 0;
        player.damageTaken = 0;
        respawnArenaPlayer(game, player);
    }

    game.projectiles.clear();
    game.mines.clear();
    game.timeRemainingSeconds = (float)game.timeLimitSeconds;
    game.worldSequence = 0;
    game.broadcastClockReady = false;
    game.simClockReady = false;
}

string encodeArenaWorldPlayer(const ArenaPlayer& player) {
    return
        getName(player.socket) + "^" +
        to_string(player.x) + "^" +
        to_string(player.z) + "^" +
        to_string(player.bodyYaw) + "^" +
        to_string(player.aimYaw) + "^" +
        to_string(player.colorIndex) + "^" +
        to_string(player.team) + "^" +
        to_string(player.health) + "^" +
        string(player.alive ? "1" : "0") + "^" +
        to_string(player.kills) + "^" +
        to_string(player.deaths) + "^" +
        to_string(player.damageDealt) + "^" +
        to_string(player.damageTaken) + "^" +
        to_string(player.respawnTimer);
}

string encodeArenaProjectile(const ArenaProjectile& projectile) {
    return
        to_string(projectile.x) + "^" +
        to_string(projectile.y) + "^" +
        to_string(projectile.z);
}

string encodeArenaMine(const ArenaMine& mine) {
    return
        to_string(mine.x) + "^" +
        to_string(mine.z) + "^" +
        to_string(mine.colorIndex) + "^" +
        to_string(mine.team);
}

void sendArenaWorld(ArenaGame& game) {
    game.worldSequence++;

    string payload =
        to_string(game.worldSequence) + "|" +
        to_string(max(0.0f, game.timeRemainingSeconds)) + "|" +
        to_string(game.players.size()) + "|" +
        to_string(game.projectiles.size()) + "|" +
        to_string(game.mines.size());

    for (const ArenaPlayer& player : game.players)
        payload += "|" + encodeArenaWorldPlayer(player);

    for (const ArenaProjectile& projectile : game.projectiles)
        payload += "|" + encodeArenaProjectile(projectile);

    for (const ArenaMine& mine : game.mines)
        payload += "|" + encodeArenaMine(mine);

    sendArenaPacket(game, "ARENA_WORLD", payload);

    game.lastBroadcastAt = chrono::steady_clock::now();
    game.broadcastClockReady = true;
}

void maybeSendArenaWorld(ArenaGame& game) {
    auto now = chrono::steady_clock::now();

    if (!game.broadcastClockReady) {
        sendArenaWorld(game);
        return;
    }

    float elapsed =
        chrono::duration<float>(
            now - game.lastBroadcastAt
        ).count();

    if (elapsed >= 1.0f / 30.0f)
        sendArenaWorld(game);
}

void applyArenaInput(
    ArenaGame& game,
    ArenaPlayer& player,
    int sequence,
    float moveX,
    float moveZ,
    float aimYaw
) {
    if (sequence <= player.lastInputSequence)
        return;

    player.lastInputSequence = sequence;

    auto now = chrono::steady_clock::now();
    float dt = 1.0f / 30.0f;

    if (player.inputClockReady) {
        dt = chrono::duration<float>(
            now - player.lastInputAt
        ).count();
    }

    player.lastInputAt = now;
    player.inputClockReady = true;

    dt = max(0.0f, min(dt, 0.075f));

    player.aimYaw = arenaNormalizeYaw(aimYaw);

    if (!player.alive)
        return;

    float movementLength =
        sqrt(moveX * moveX + moveZ * moveZ);

    if (movementLength > 1.0f) {
        moveX /= movementLength;
        moveZ /= movementLength;
        movementLength = 1.0f;
    }

    if (movementLength > 0.001f) {
        player.bodyYaw =
            arenaDirectionYaw(
                moveX,
                moveZ
            );

        float nextX =
            player.x +
            moveX * ARENA_PLAYER_SPEED * dt;

        if (
            !arenaPositionBlocked(game, nextX, player.z) &&
            !arenaPositionBlockedByPlayer(
                game,
                player.socket,
                nextX,
                player.z
            )
        ) {
            player.x = nextX;
        }

        float nextZ =
            player.z +
            moveZ * ARENA_PLAYER_SPEED * dt;

        if (
            !arenaPositionBlocked(game, player.x, nextZ) &&
            !arenaPositionBlockedByPlayer(
                game,
                player.socket,
                player.x,
                nextZ
            )
        ) {
            player.z = nextZ;
        }
    }
}

void fireArenaProjectile(
    ArenaGame& game,
    ArenaPlayer& player
) {
    if (!player.alive)
        return;

    auto now = chrono::steady_clock::now();

    if (player.fireClockReady) {
        float elapsed = chrono::duration<float>(
            now - player.lastFireAt
        ).count();

        if (elapsed < ARENA_FIRE_COOLDOWN)
            return;
    }

    player.lastFireAt = now;
    player.fireClockReady = true;

    float radians = player.aimYaw * (ARENA_PI / 180.0f);
    float dx = sin(radians);
    float dz = cos(radians);

    ArenaProjectile projectile;
    projectile.x = player.x + dx * 1.55f;
    projectile.y = 1.10f;
    projectile.z = player.z + dz * 1.55f;
    projectile.vx = dx * ARENA_BULLET_SPEED;
    projectile.vz = dz * ARENA_BULLET_SPEED;
    projectile.owner = player.socket;
    projectile.active = true;

    game.projectiles.push_back(projectile);
}

void placeArenaMine(
    ArenaGame& game,
    ArenaPlayer& player
) {
    if (!player.alive)
        return;

    auto now = chrono::steady_clock::now();

    if (player.mineClockReady) {
        float elapsed = chrono::duration<float>(
            now - player.lastMineAt
        ).count();

        if (elapsed < ARENA_MINE_COOLDOWN)
            return;
    }

    player.lastMineAt = now;
    player.mineClockReady = true;

    int activeOwned = 0;
    int oldestOwned = -1;

    for (int i = 0; i < (int)game.mines.size(); i++) {
        if (
            game.mines[i].active &&
            game.mines[i].owner == player.socket
        ) {
            activeOwned++;

            if (oldestOwned < 0)
                oldestOwned = i;
        }
    }

    if (
        activeOwned >= ARENA_MAX_MINES_PER_PLAYER &&
        oldestOwned >= 0
    ) {
        game.mines.erase(
            game.mines.begin() + oldestOwned
        );
    }

    ArenaMine mine;
    mine.x = player.x;
    mine.z = player.z;
    mine.owner = player.socket;
    mine.colorIndex = player.colorIndex;
    mine.team = player.team;
    mine.active = true;

    game.mines.push_back(mine);
}

bool arenaProjectileHitsObstacle(
    const ArenaGame& game,
    const ArenaProjectile& projectile
) {
    auto Hits =
        [&](const ArenaObstacle2D& obstacle)
        {
            return
                projectile.x + ARENA_BULLET_RADIUS > obstacle.minX &&
                projectile.x - ARENA_BULLET_RADIUS < obstacle.maxX &&
                projectile.z + ARENA_BULLET_RADIUS > obstacle.minZ &&
                projectile.z - ARENA_BULLET_RADIUS < obstacle.maxZ;
        };

    if (game.map == "ALIEN_OUTPOST") {
        static const ArenaObstacle2D alienObstacles[] = {
            {-4.2f,  -4.2f,   4.2f,   4.2f},
            {-25.0f, -3.0f, -19.0f,   3.0f},
            { 19.0f, -3.0f,  25.0f,   3.0f},
            {-3.0f, -25.0f,   3.0f, -19.0f},
            {-3.0f,  19.0f,   3.0f,  25.0f},
            {-20.5f,-20.5f, -14.0f, -14.0f},
            { 14.0f,-20.5f,  20.5f, -14.0f},
            {-20.5f, 14.0f, -14.0f,  20.5f},
            { 14.0f, 14.0f,  20.5f,  20.5f},
            {-12.0f, -8.5f,  -8.0f,  -4.0f},
            {  8.0f,  4.0f,  12.0f,   8.5f},
            {-12.0f,  4.0f,  -8.0f,   8.5f},
            {  8.0f, -8.5f,  12.0f,  -4.0f}
        };

        for (const ArenaObstacle2D& obstacle : alienObstacles) {
            if (Hits(obstacle))
                return true;
        }

        return false;
    }

    static const ArenaObstacle2D reactorObstacles[] = {
        {-4.5f,  -4.5f,   4.5f,   4.5f},
        {-20.0f, -2.2f, -15.0f,   2.2f},
        { 15.0f, -2.2f,  20.0f,   2.2f},
        {-2.2f, -20.0f,   2.2f, -15.0f},
        {-2.2f,  15.0f,   2.2f,  20.0f},
        {-17.0f,-17.0f, -12.0f, -12.0f},
        { 12.0f,-17.0f,  17.0f, -12.0f},
        {-17.0f, 12.0f, -12.0f,  17.0f},
        { 12.0f, 12.0f,  17.0f,  17.0f}
    };

    for (const ArenaObstacle2D& obstacle : reactorObstacles) {
        if (Hits(obstacle))
            return true;
    }

    return false;
}

void damageArenaPlayer(
    ArenaGame& game,
    int attackerIndex,
    int victimIndex,
    int damage = ARENA_SHOT_DAMAGE
) {
    if (
        victimIndex < 0 ||
        victimIndex >= (int)game.players.size()
    ) {
        return;
    }

    ArenaPlayer& target = game.players[victimIndex];

    if (!target.alive)
        return;

    int appliedDamage = min(damage, target.health);
    target.health -= appliedDamage;
    target.damageTaken += appliedDamage;

    if (
        attackerIndex >= 0 &&
        attackerIndex < (int)game.players.size()
    ) {
        game.players[attackerIndex].damageDealt += appliedDamage;
    }

    if (target.health > 0)
        return;

    target.health = 0;
    target.alive = false;
    target.respawnTimer = ARENA_RESPAWN_TIME;
    target.deaths++;

    if (
        attackerIndex >= 0 &&
        attackerIndex < (int)game.players.size() &&
        attackerIndex != victimIndex
    ) {
        game.players[attackerIndex].kills++;
    }
}

void updateArenaProjectiles(
    ArenaGame& game,
    float dt
) {
    for (ArenaProjectile& projectile : game.projectiles) {
        if (!projectile.active)
            continue;

        projectile.x += projectile.vx * dt;
        projectile.z += projectile.vz * dt;

        const float projectileBoundary =
            arenaProjectileBoundary(game);

        if (
            fabs(projectile.x) > projectileBoundary ||
            fabs(projectile.z) > projectileBoundary
        ) {
            projectile.active = false;
            continue;
        }

        if (
            arenaProjectileHitsObstacle(
                game,
                projectile
            )
        ) {
            projectile.active = false;
            continue;
        }

        int attackerIndex = -1;

        for (int i = 0; i < (int)game.players.size(); i++) {
            if (game.players[i].socket == projectile.owner) {
                attackerIndex = i;
                break;
            }
        }

        for (int i = 0; i < (int)game.players.size(); i++) {
            ArenaPlayer& target = game.players[i];

            if (!target.alive || target.socket == projectile.owner)
                continue;

            if (
                attackerIndex >= 0 &&
                !arenaPlayersAreEnemies(
                    game,
                    game.players[attackerIndex],
                    target
                )
            ) {
                continue;
            }

            float dx = projectile.x - target.x;
            float dz = projectile.z - target.z;
            float radius = ARENA_TANK_RADIUS + ARENA_BULLET_RADIUS;

            if (dx * dx + dz * dz <= radius * radius) {
                damageArenaPlayer(
                    game,
                    attackerIndex,
                    i
                );

                projectile.active = false;
                break;
            }
        }
    }

    game.projectiles.erase(
        remove_if(
            game.projectiles.begin(),
            game.projectiles.end(),
            [](const ArenaProjectile& projectile) {
                return !projectile.active;
            }
        ),
        game.projectiles.end()
    );
}

void updateArenaMines(ArenaGame& game) {
    for (ArenaMine& mine : game.mines) {
        if (!mine.active)
            continue;

        int ownerIndex = -1;

        for (int i = 0; i < (int)game.players.size(); i++) {
            if (game.players[i].socket == mine.owner) {
                ownerIndex = i;
                break;
            }
        }

        if (ownerIndex < 0) {
            mine.active = false;
            continue;
        }

        for (int i = 0; i < (int)game.players.size(); i++) {
            ArenaPlayer& target = game.players[i];

            if (!target.alive || target.socket == mine.owner)
                continue;

            if (
                !arenaPlayersAreEnemies(
                    game,
                    game.players[ownerIndex],
                    target
                )
            ) {
                continue;
            }

            float dx = mine.x - target.x;
            float dz = mine.z - target.z;
            float radius =
                ARENA_TANK_RADIUS +
                ARENA_MINE_RADIUS;

            if (dx * dx + dz * dz <= radius * radius) {
                damageArenaPlayer(
                    game,
                    ownerIndex,
                    i,
                    ARENA_MINE_DAMAGE
                );

                mine.active = false;
                break;
            }
        }
    }

    game.mines.erase(
        remove_if(
            game.mines.begin(),
            game.mines.end(),
            [](const ArenaMine& mine) {
                return !mine.active;
            }
        ),
        game.mines.end()
    );
}

void updateArenaRespawns(
    ArenaGame& game,
    float dt
) {
    for (ArenaPlayer& player : game.players) {
        if (player.alive)
            continue;

        player.respawnTimer -= dt;

        if (player.respawnTimer <= 0.0f)
            respawnArenaPlayer(game, player);
    }
}

bool evaluateArenaMatchEnd(
    const ArenaGame& game,
    string& endText
) {
    if (game.mode == "TIME_FFA") {
        if (game.timeRemainingSeconds > 0.0f)
            return false;

        int bestKills = -1;
        int bestPlayer = -1;
        bool tie = false;

        for (int i = 0; i < (int)game.players.size(); i++) {
            const ArenaPlayer& player = game.players[i];

            if (player.kills > bestKills) {
                bestKills = player.kills;
                bestPlayer = i;
                tie = false;
            }
            else if (player.kills == bestKills) {
                tie = true;
            }
        }

        if (tie || bestPlayer < 0) {
            endText = "MATCH OVER - DRAW";
        }
        else {
            endText =
                "MATCH OVER - " +
                getName(game.players[bestPlayer].socket) +
                " wins the timed FFA.";
        }

        return true;
    }

    if (arenaTeamMode(game.mode)) {
        int redScore = arenaTeamScore(game, 0);
        int blueScore = arenaTeamScore(game, 1);

        if (redScore >= game.scoreLimit) {
            endText =
                "MATCH OVER - RED TEAM WINS " +
                to_string(redScore) +
                "-" +
                to_string(blueScore);
            return true;
        }

        if (blueScore >= game.scoreLimit) {
            endText =
                "MATCH OVER - BLUE TEAM WINS " +
                to_string(blueScore) +
                "-" +
                to_string(redScore);
            return true;
        }

        return false;
    }

    for (const ArenaPlayer& player : game.players) {
        if (player.kills >= game.scoreLimit) {
            endText =
                "MATCH OVER - " +
                getName(player.socket) +
                " wins.";
            return true;
        }
    }

    return false;
}

void processArenaRealtimeTick() {
    auto now = chrono::steady_clock::now();

    for (int i = 0; i < (int)arenaGames.size();) {
        ArenaGame& game = arenaGames[i];

        if (game.phase != "PLAYING") {
            i++;
            continue;
        }

        float dt = 1.0f / 60.0f;

        if (game.simClockReady) {
            dt = chrono::duration<float>(
                now - game.lastSimAt
            ).count();
        }

        game.lastSimAt = now;
        game.simClockReady = true;

        dt = max(0.0f, min(dt, 0.05f));

        if (game.mode == "TIME_FFA") {
            game.timeRemainingSeconds = max(
                0.0f,
                game.timeRemainingSeconds - dt
            );
        }

        updateArenaProjectiles(game, dt);
        updateArenaMines(game);
        updateArenaRespawns(game, dt);

        string endText;
        if (evaluateArenaMatchEnd(game, endText)) {
            // Preserve the Arena party after a normal match ends. Publish one
            // final authoritative snapshot so every client has the exact
            // scoreboard, then move the same game object into POSTGAME.
            game.projectiles.clear();
            sendArenaWorld(game);

            game.phase = "POSTGAME";
            game.status = endText;

            for (ArenaPlayer& player : game.players)
                player.ready = false;

            sendArenaState(game);
            readyArenaPlayers(game);
            i++;
            continue;
        }

        maybeSendArenaWorld(game);
        i++;
    }
}


// ============================================================
// COMMAND HANDLER
// ============================================================

void handleCommand(Client& client, const string& line) {
    if (line.empty())
        return;

    string workingLine = line;

    // Graphical Blackjack actions are translated into the same
    // server-side handlers used by the legacy slash commands.
    if (line.rfind("BJ_BET|", 0) == 0)
        workingLine = "/bet " + line.substr(7);
    else if (line == "BJ_HIT")
        workingLine = "/hit";
    else if (line == "BJ_STAND")
        workingLine = "/stand";
    else if (line == "BJ_DOUBLE")
        workingLine = "/double";
    else if (line == "BJ_SPLIT")
        workingLine = "/split";
    else if (line == "BJ_NEXT")
        workingLine = "/bjnext";
    else if (line == "BJ_START")
        workingLine = "/bjstart";
    else if (line == "BJ_RESIGN")
        workingLine = "/resign";

    // --------------------------------------------------------
    // Graphical Roulette packets
    // --------------------------------------------------------

    if (line.rfind("RLT_CREATE|", 0) == 0) {
        vector<string> fields;
        string part;
        stringstream stream(line.substr(11));

        while (getline(stream, part, '|'))
            fields.push_back(part);

        if (fields.size() < 2) {
            sendRouletteError(
                client.socket,
                "Choose starting chips and number of rounds."
            );
            return;
        }

        int startingChips = 0;
        int rounds = 0;

        try {
            size_t chipsLength = 0;
            size_t roundsLength = 0;
            startingChips = stoi(fields[0], &chipsLength);
            rounds = stoi(fields[1], &roundsLength);

            if (
                chipsLength != fields[0].size() ||
                roundsLength != fields[1].size()
            ) {
                throw invalid_argument("trailing characters");
            }
        }
        catch (...) {
            sendRouletteError(
                client.socket,
                "Starting chips and rounds must be whole numbers."
            );
            return;
        }

        if (
            startingChips <= 0 ||
            startingChips > MAX_STARTING_CHIPS
        ) {
            sendRouletteError(
                client.socket,
                "Starting chips must be between 1 and " +
                to_string(MAX_STARTING_CHIPS) +
                "."
            );
            return;
        }

        if (
            rounds < 1 ||
            rounds > MAX_ROULETTE_ROUNDS
        ) {
            sendRouletteError(
                client.socket,
                "Rounds must be between 1 and " +
                to_string(MAX_ROULETTE_ROUNDS) +
                "."
            );
            return;
        }

        if (isPlayerBusy(client.socket)) {
            sendRouletteError(
                client.socket,
                "You are already in a game."
            );
            return;
        }

        RouletteGame game;
        game.host = client.socket;
        game.startingChips = startingChips;
        game.totalRounds = rounds;
        game.currentRound = 1;
        game.phase = "LOBBY";
        game.status = "Invite players to a Roulette table.";

        RoulettePlayer host;
        host.socket = client.socket;
        host.chips = startingChips;
        game.players.push_back(host);

        rouletteGames.push_back(game);

        sendRouletteState(
            rouletteGames.back()
        );

        readyRoulettePlayers(
            rouletteGames.back()
        );

        return;
    }

    if (line.rfind("RLT_INVITE|", 0) == 0) {
        string targetName =
            line.substr(11);

        int gameIndex =
            findRouletteGame(
                client.socket
            );

        if (gameIndex == -1) {
            sendRouletteError(
                client.socket,
                "Create a Roulette table first."
            );
            return;
        }

        RouletteGame& game =
            rouletteGames[gameIndex];

        if (
            game.host != client.socket ||
            game.phase != "LOBBY"
        ) {
            sendRouletteError(
                client.socket,
                "Only the host can invite players before the table starts."
            );
            return;
        }

        if (
            (int)game.players.size() >=
            MAX_ROULETTE_PLAYERS
        ) {
            sendRouletteError(
                client.socket,
                "This Roulette table is full."
            );
            return;
        }

        Client* target =
            getClientByName(
                targetName
            );

        if (!target) {
            sendRouletteError(
                client.socket,
                "User not found."
            );
            return;
        }

        if (target->socket == client.socket) {
            sendRouletteError(
                client.socket,
                "You cannot invite yourself."
            );
            return;
        }

        if (isPlayerBusy(target->socket)) {
            sendRouletteError(
                client.socket,
                target->name +
                " is already playing."
            );
            return;
        }

        if (
            target->pendingChallenge !=
            INVALID_SOCK
        ) {
            sendRouletteError(
                client.socket,
                target->name +
                " already has a pending invite."
            );
            return;
        }

        target->pendingChallenge =
            client.socket;

        target->pendingGame =
            "roulette";

        target->pendingChips =
            game.startingChips;

        target->pendingHands =
            game.totalRounds;

        sendPacket(
            target->socket,
            "RLT_CHALLENGE",
            client.name + "|" +
            to_string(game.startingChips) + "|" +
            to_string(game.totalRounds) + "|" +
            to_string(game.players.size())
        );

        game.status =
            "Invitation sent to " +
            target->name +
            ".";

        sendRouletteState(game);
        readyRoulettePlayers(game);
        sendReady(target->socket);
        return;
    }

    if (line == "RLT_START") {
        int gameIndex =
            findRouletteGame(
                client.socket
            );

        if (gameIndex == -1) {
            sendRouletteError(
                client.socket,
                "You are not at a Roulette table."
            );
            return;
        }

        RouletteGame& game =
            rouletteGames[gameIndex];

        if (
            game.host != client.socket
        ) {
            sendRouletteError(
                client.socket,
                "Only the host can start the table."
            );
            return;
        }

        if (game.phase != "LOBBY") {
            sendRouletteError(
                client.socket,
                "The Roulette table has already started."
            );
            return;
        }

        startRouletteBetting(game);
        return;
    }

    if (line.rfind("RLT_BET|", 0) == 0) {
        vector<string> fields;
        string part;
        stringstream stream(
            line.substr(8)
        );

        while (getline(stream, part, '|'))
            fields.push_back(part);

        if (fields.size() < 3) {
            sendRouletteError(
                client.socket,
                "Invalid Roulette bet."
            );
            return;
        }

        string type =
            fields[0];

        int value =
            stoi(fields[1]);

        int amount =
            stoi(fields[2]);

        int gameIndex =
            findRouletteGame(
                client.socket
            );

        if (gameIndex == -1) {
            sendRouletteError(
                client.socket,
                "You are not at a Roulette table."
            );
            return;
        }

        RouletteGame& game =
            rouletteGames[gameIndex];

        RoulettePlayer* player =
            roulettePlayer(
                game,
                client.socket
            );

        if (
            !player ||
            game.phase != "BETTING"
        ) {
            sendRouletteError(
                client.socket,
                "Roulette is not accepting bets right now."
            );
            return;
        }

        if (player->ready) {
            sendRouletteError(
                client.socket,
                "Your bets are already locked."
            );
            return;
        }

        if (
            amount <= 0 ||
            !validRouletteBetType(
                type,
                value
            )
        ) {
            sendRouletteError(
                client.socket,
                "Invalid Roulette bet."
            );
            return;
        }

        int available =
            player->chips -
            rouletteBetTotal(*player);

        if (amount > available) {
            sendRouletteError(
                client.socket,
                "You do not have enough chips for that bet."
            );
            return;
        }

        bool merged = false;

        for (RouletteBet& bet : player->bets) {
            if (
                bet.type == type &&
                bet.value == value
            ) {
                bet.amount += amount;
                merged = true;
                break;
            }
        }

        if (!merged) {
            RouletteBet bet;
            bet.type = type;
            bet.value = value;
            bet.amount = amount;
            player->bets.push_back(bet);
        }

        sendRouletteState(game);
        readyRoulettePlayers(game);
        return;
    }

    if (line == "RLT_CLEAR") {
        int gameIndex =
            findRouletteGame(
                client.socket
            );

        if (gameIndex == -1)
            return;

        RouletteGame& game =
            rouletteGames[gameIndex];

        RoulettePlayer* player =
            roulettePlayer(
                game,
                client.socket
            );

        if (
            !player ||
            game.phase != "BETTING" ||
            player->ready
        ) {
            return;
        }

        player->bets.clear();

        sendRouletteState(game);
        readyRoulettePlayers(game);
        return;
    }

    if (line == "RLT_READY") {
        int gameIndex =
            findRouletteGame(
                client.socket
            );

        if (gameIndex == -1) {
            sendRouletteError(
                client.socket,
                "You are not at a Roulette table."
            );
            return;
        }

        RouletteGame& game =
            rouletteGames[gameIndex];

        RoulettePlayer* player =
            roulettePlayer(
                game,
                client.socket
            );

        if (
            !player ||
            game.phase != "BETTING"
        ) {
            return;
        }

        if (player->bets.empty()) {
            sendRouletteError(
                client.socket,
                "Place at least one bet before locking."
            );
            return;
        }

        player->ready = true;

        bool allReady =
            !game.players.empty();

        for (const RoulettePlayer& p : game.players) {
            if (!p.ready) {
                allReady = false;
                break;
            }
        }

        if (allReady) {
            resolveRouletteSpin(
                gameIndex
            );
            return;
        }

        game.status =
            client.name +
            " locked their bets. Waiting for the other players.";

        sendRouletteState(game);
        readyRoulettePlayers(game);
        return;
    }

    if (line == "RLT_NEXT") {
        int gameIndex =
            findRouletteGame(
                client.socket
            );

        if (gameIndex == -1)
            return;

        RouletteGame& game =
            rouletteGames[gameIndex];

        if (
            game.host != client.socket ||
            game.phase != "RESULT"
        ) {
            return;
        }

        game.currentRound++;
        startRouletteBetting(game);
        return;
    }

    if (line == "RLT_LEAVE") {
        int gameIndex =
            findRouletteGame(
                client.socket
            );

        if (gameIndex == -1)
            return;

        RouletteGame& game =
            rouletteGames[gameIndex];

        if (game.host == client.socket) {
            vector<Socket> sockets;

            for (const RoulettePlayer& player : game.players)
                sockets.push_back(player.socket);

            string text =
                getName(client.socket) +
                " closed the Roulette table.";

            for (Socket socket : sockets) {
                sendPacket(
                    socket,
                    "RLT_END",
                    text
                );

                sendReady(socket);
            }

            rouletteGames.erase(
                rouletteGames.begin() +
                gameIndex
            );

            return;
        }

        int playerIndex =
            roulettePlayerIndex(
                game,
                client.socket
            );

        if (playerIndex >= 0) {
            game.players.erase(
                game.players.begin() +
                playerIndex
            );
        }

        sendPacket(
            client.socket,
            "RLT_END",
            "You left the Roulette table."
        );

        game.status =
            client.name +
            " left the Roulette table.";

        sendRouletteState(game);
        readyRoulettePlayers(game);
        sendReady(client.socket);
        return;
    }

    // --------------------------------------------------------
    // Invite another online user to watch an active Chess game.
    // Either player may invite; spectators may not invite others.
    // --------------------------------------------------------
    if (line.rfind("CHESS_SPECTATE_INVITE|", 0) == 0) {
        string targetName = line.substr(22);
        int chessIndex = findChessGame(client.socket);

        if (chessIndex == -1 ||
            !chessIsPlayer(chessGames[chessIndex], client.socket)) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                "Only an active Chess player can invite spectators."
            );
            return;
        }

        ChessGame& game = chessGames[chessIndex];

        if ((int)game.spectators.size() >= 8) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                "This Chess game already has eight spectators."
            );
            return;
        }

        Client* target = getClientByName(targetName);

        if (!target || target->socket == client.socket) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                target ? "You cannot invite yourself." : "User not found."
            );
            return;
        }

        if (isPlayerBusy(target->socket)) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                target->name + " is already in a game."
            );
            return;
        }

        if (target->pendingChallenge != INVALID_SOCK) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                target->name + " already has a pending invitation."
            );
            return;
        }

        target->pendingChallenge = client.socket;
        target->pendingGame = "chess_spectate";
        target->pendingChips = 0;
        target->pendingHands = 0;

        sendPacket(
            target->socket,
            "CHESS_SPECTATE_CHALLENGE",
            client.name + "|" +
            getName(game.white) + "|" +
            getName(game.black)
        );
        sendPacket(
            client.socket,
            "CHESS_NOTICE",
            "Spectator invitation sent to " + target->name + "."
        );
        sendReady(target->socket);
        return;
    }

    if (line == "CHESS_SPECTATE_LEAVE") {
        int chessIndex = findChessGame(client.socket);

        if (chessIndex == -1 ||
            !chessIsSpectator(chessGames[chessIndex], client.socket)) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                "You are not spectating a Chess game."
            );
            return;
        }

        ChessGame& game = chessGames[chessIndex];
        game.spectators.erase(
            remove(
                game.spectators.begin(),
                game.spectators.end(),
                client.socket
            ),
            game.spectators.end()
        );

        sendPacket(
            client.socket,
            "CHESS_END",
            "You stopped spectating the Chess game."
        );
        sendReady(client.socket);
        sendChessState(game);
        readyChessPlayers(game);
        return;
    }

    // --------------------------------------------------------
    // Graphical Chess legal-move request
    // CHESS_LEGAL|e2
    // --------------------------------------------------------
    if (line.rfind("CHESS_LEGAL|", 0) == 0) {
        string from =
            line.substr(12);

        int chessIndex =
            findChessGame(
                client.socket
            );

        if (chessIndex == -1) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                "You are not in a Chess game."
            );
            return;
        }

        if (!chessIsPlayer(chessGames[chessIndex], client.socket)) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                "Spectators cannot select or move Chess pieces."
            );
            return;
        }

        sendChessLegalMoves(
            chessGames[chessIndex],
            client.socket,
            from
        );

        return;
    }

    // --------------------------------------------------------
    // Graphical Chess move packet
    // CHESS_MOVE|e2|e4
    // CHESS_MOVE|e7|e8|q
    // --------------------------------------------------------
    if (line.rfind("CHESS_MOVE|", 0) == 0) {
        string payload = line.substr(11);
        vector<string> fields;
        string part;
        stringstream moveStream(payload);

        while (getline(moveStream, part, '|'))
            fields.push_back(part);

        if (fields.size() < 2) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                "Invalid Chess move packet."
            );
            return;
        }

        int chessIndex = findChessGame(client.socket);

        if (chessIndex == -1) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                "You are not in a Chess game."
            );
            return;
        }

        if (!chessIsPlayer(chessGames[chessIndex], client.socket)) {
            sendPacket(
                client.socket,
                "CHESS_ERROR",
                "Spectators cannot move Chess pieces."
            );
            return;
        }

        string promotion =
            fields.size() >= 3
            ? fields[2]
            : "";

        playChessMove(
            chessIndex,
            client,
            fields[0],
            fields[1],
            promotion
        );

        return;
    }


    // --------------------------------------------------------
    // Graphical JENG Arena lobby packets
    // --------------------------------------------------------

    if (line.rfind("ARENA_CREATE|", 0) == 0) {
        vector<string> fields;
        string part;
        stringstream stream(line.substr(13));

        while (getline(stream, part, '|'))
            fields.push_back(part);

        if (fields.size() < 5) {
            sendArenaError(
                client.socket,
                "Invalid Arena lobby settings."
            );
            return;
        }

        string mode = fields[0];
        string map =
            fields.size() >= 6
            ? fields[5]
            : "REACTOR_YARD";

        int requestedPlayers = 0;
        int scoreLimit = 0;
        int timeLimit = 0;
        int colorIndex = 0;

        if (
            !validArenaMode(mode) ||
            !validArenaMap(map) ||
            !parseArenaInt(fields[1], requestedPlayers) ||
            !parseArenaInt(fields[2], scoreLimit) ||
            !parseArenaInt(fields[3], timeLimit) ||
            !parseArenaInt(fields[4], colorIndex)
        ) {
            sendArenaError(
                client.socket,
                "Invalid Arena lobby settings."
            );
            return;
        }

        if (isPlayerBusy(client.socket)) {
            sendArenaError(
                client.socket,
                "You are already in a game."
            );
            return;
        }

        int maxPlayers =
            normalizeArenaPlayerCount(
                mode,
                requestedPlayers
            );

        if (scoreLimit < 1 || scoreLimit > 50) {
            sendArenaError(
                client.socket,
                "Arena score limit must be between 1 and 50."
            );
            return;
        }

        if (timeLimit < 60 || timeLimit > 600) {
            sendArenaError(
                client.socket,
                "Arena time limit must be between 60 and 600 seconds."
            );
            return;
        }

        if (
            !arenaTeamMode(mode) &&
            (colorIndex < 0 || colorIndex >= ARENA_COLOR_COUNT)
        ) {
            sendArenaError(
                client.socket,
                "Invalid Arena tank color."
            );
            return;
        }

        ArenaGame game;
        game.host = client.socket;
        game.mode = mode;
        game.map = map;
        game.maxPlayers = maxPlayers;
        game.scoreLimit = scoreLimit;
        game.timeLimitSeconds = timeLimit;
        game.phase = "LOBBY";
        game.status =
            string("Arena lobby created on ") +
            (
                game.map == "ALIEN_OUTPOST"
                ? "Alien Outpost"
                : "Reactor Yard"
            ) +
            ". Invite players.";

        ArenaPlayer host;
        host.socket = client.socket;
        host.ready = false;

        if (arenaTeamMode(mode)) {
            host.team = 0;
            host.colorIndex = -1;
        }
        else {
            host.team = -1;
            host.colorIndex = colorIndex;
        }

        game.players.push_back(host);
        arenaGames.push_back(game);

        sendArenaState(arenaGames.back());
        readyArenaPlayers(arenaGames.back());
        return;
    }

    if (line.rfind("ARENA_INVITE|", 0) == 0) {
        string targetName = line.substr(13);

        int gameIndex =
            findArenaGame(client.socket);

        if (gameIndex == -1) {
            sendArenaError(
                client.socket,
                "Create an Arena lobby first."
            );
            return;
        }

        ArenaGame& game =
            arenaGames[gameIndex];

        if (
            game.host != client.socket ||
            game.phase != "LOBBY"
        ) {
            sendArenaError(
                client.socket,
                "Only the Arena host can invite players before the match starts."
            );
            return;
        }

        if ((int)game.players.size() >= game.maxPlayers) {
            sendArenaError(
                client.socket,
                "This Arena lobby is full."
            );
            return;
        }

        Client* target =
            getClientByName(targetName);

        if (!target) {
            sendArenaError(
                client.socket,
                "User not found."
            );
            return;
        }

        if (target->socket == client.socket) {
            sendArenaError(
                client.socket,
                "You cannot invite yourself."
            );
            return;
        }

        if (isPlayerBusy(target->socket)) {
            sendArenaError(
                client.socket,
                target->name + " is already playing."
            );
            return;
        }

        if (
            target->pendingChallenge !=
            INVALID_SOCK
        ) {
            sendArenaError(
                client.socket,
                target->name +
                " already has a pending invite."
            );
            return;
        }

        target->pendingChallenge =
            client.socket;

        target->pendingGame =
            "arena";

        sendPacket(
            target->socket,
            "ARENA_CHALLENGE",
            client.name + "|" +
            game.mode + "|" +
            to_string(game.maxPlayers) + "|" +
            to_string(game.scoreLimit) + "|" +
            to_string(game.timeLimitSeconds) + "|" +
            to_string(game.players.size())
        );

        game.status =
            "Invitation sent to " +
            target->name +
            ".";

        sendArenaState(game);
        readyArenaPlayers(game);
        sendReady(target->socket);
        return;
    }

    if (line == "ARENA_READY") {
        int gameIndex =
            findArenaGame(client.socket);

        if (gameIndex == -1) {
            sendArenaError(
                client.socket,
                "You are not in an Arena lobby."
            );
            return;
        }

        ArenaGame& game =
            arenaGames[gameIndex];

        if (game.phase != "LOBBY") {
            sendArenaError(
                client.socket,
                "This Arena lobby is already locked."
            );
            return;
        }

        int playerIndex =
            arenaPlayerIndex(
                game,
                client.socket
            );

        if (playerIndex < 0)
            return;

        game.players[playerIndex].ready =
            !game.players[playerIndex].ready;

        game.status =
            client.name +
            (
                game.players[playerIndex].ready
                ? " is ready."
                : " is not ready."
            );

        sendArenaState(game);
        readyArenaPlayers(game);
        return;
    }

    if (line.rfind("ARENA_COLOR|", 0) == 0) {
        int gameIndex =
            findArenaGame(client.socket);

        if (gameIndex == -1) {
            sendArenaError(
                client.socket,
                "You are not in an Arena lobby."
            );
            return;
        }

        ArenaGame& game =
            arenaGames[gameIndex];

        if (
            game.phase != "LOBBY" ||
            arenaTeamMode(game.mode)
        ) {
            sendArenaError(
                client.socket,
                "Tank colors cannot be changed for this Arena lobby."
            );
            return;
        }

        int colorIndex = -1;

        if (
            !parseArenaInt(
                line.substr(12),
                colorIndex
            ) ||
            colorIndex < 0 ||
            colorIndex >= ARENA_COLOR_COUNT
        ) {
            sendArenaError(
                client.socket,
                "Invalid Arena tank color."
            );
            return;
        }

        if (
            arenaColorUsed(
                game,
                colorIndex,
                client.socket
            )
        ) {
            sendArenaError(
                client.socket,
                "That Arena color is already taken."
            );
            return;
        }

        int playerIndex =
            arenaPlayerIndex(
                game,
                client.socket
            );

        if (playerIndex < 0)
            return;

        game.players[playerIndex].colorIndex =
            colorIndex;

        game.players[playerIndex].ready = false;

        game.status =
            client.name +
            " changed tank color and must ready again.";

        sendArenaState(game);
        readyArenaPlayers(game);
        return;
    }

    if (line == "ARENA_START") {
        int gameIndex =
            findArenaGame(client.socket);

        if (gameIndex == -1) {
            sendArenaError(
                client.socket,
                "You are not in an Arena lobby."
            );
            return;
        }

        ArenaGame& game =
            arenaGames[gameIndex];

        if (game.host != client.socket) {
            sendArenaError(
                client.socket,
                "Only the Arena host can start the match."
            );
            return;
        }

        if (game.phase != "LOBBY") {
            sendArenaError(
                client.socket,
                "This Arena lobby is already locked."
            );
            return;
        }

        if ((int)game.players.size() != game.maxPlayers) {
            sendArenaError(
                client.socket,
                "The Arena lobby must be full before starting."
            );
            return;
        }

        if (!allArenaPlayersReady(game)) {
            sendArenaError(
                client.socket,
                "Every Arena player must be ready before starting."
            );
            return;
        }

        game.phase = "PLAYING";
        game.status =
            string("Online combat active - ") +
            (
                game.map == "ALIEN_OUTPOST"
                ? "Alien Outpost."
                : "Reactor Yard."
            );

        initializeArenaWorld(game);

        sendArenaPacket(
            game,
            "ARENA_START",
            game.mode + "|" +
            to_string(game.scoreLimit) + "|" +
            to_string(game.timeLimitSeconds)
        );

        sendArenaState(game);
        sendArenaWorld(game);
        readyArenaPlayers(game);
        return;
    }

    if (line.rfind("ARENA_INPUT|", 0) == 0) {
        int gameIndex =
            findArenaGame(client.socket);

        if (gameIndex == -1) {
            sendArenaError(
                client.socket,
                "You are not in an Arena match."
            );
            return;
        }

        ArenaGame& game =
            arenaGames[gameIndex];

        if (game.phase != "PLAYING") {
            sendArenaError(
                client.socket,
                "Arena movement is only available during an active match."
            );
            return;
        }

        vector<string> fields;
        string part;
        stringstream stream(
            line.substr(12)
        );

        while (getline(stream, part, '|'))
            fields.push_back(part);

        if (fields.size() < 4)
            return;

        int sequence = -1;
        float moveX = 0.0f;
        float moveZ = 0.0f;
        float aimYaw = 180.0f;
        int fireRequested = 0;

        if (
            !parseArenaInt(fields[0], sequence) ||
            !parseArenaFloat(fields[1], moveX) ||
            !parseArenaFloat(fields[2], moveZ) ||
            !parseArenaFloat(fields[3], aimYaw)
        ) {
            return;
        }

        if (fields.size() >= 5)
            parseArenaInt(fields[4], fireRequested);

        // Reject obviously malformed movement vectors instead of trusting
        // arbitrary client-provided values. Normal vectors are normalized
        // again inside applyArenaInput().
        if (
            fabs(moveX) > 2.0f ||
            fabs(moveZ) > 2.0f ||
            fabs(aimYaw) > 100000.0f
        ) {
            return;
        }

        int playerIndex =
            arenaPlayerIndex(
                game,
                client.socket
            );

        if (playerIndex < 0)
            return;

        applyArenaInput(
            game,
            game.players[playerIndex],
            sequence,
            moveX,
            moveZ,
            aimYaw
        );

        if (fireRequested != 0) {
            fireArenaProjectile(
                game,
                game.players[playerIndex]
            );
        }

        maybeSendArenaWorld(game);
        return;
    }

    if (line == "ARENA_MINE") {
        int gameIndex =
            findArenaGame(client.socket);

        if (gameIndex == -1) {
            sendArenaError(
                client.socket,
                "You are not in an Arena match."
            );
            return;
        }

        ArenaGame& game =
            arenaGames[gameIndex];

        if (game.phase != "PLAYING") {
            sendArenaError(
                client.socket,
                "Mines are only available during an active Arena match."
            );
            return;
        }

        int playerIndex =
            arenaPlayerIndex(
                game,
                client.socket
            );

        if (playerIndex < 0)
            return;

        placeArenaMine(
            game,
            game.players[playerIndex]
        );

        maybeSendArenaWorld(game);
        return;
    }

    if (line == "ARENA_PLAY_AGAIN") {
        int gameIndex =
            findArenaGame(client.socket);

        if (gameIndex == -1) {
            sendArenaError(
                client.socket,
                "You are not in an Arena post-game party."
            );
            return;
        }

        ArenaGame& game =
            arenaGames[gameIndex];

        if (game.phase != "POSTGAME") {
            sendArenaError(
                client.socket,
                "Play Again is only available after the Arena match ends."
            );
            return;
        }

        // Returning to the lobby is a party-wide action. Nobody is auto-ready;
        // the same players stay together and can change color / ready up again.
        game.phase = "LOBBY";
        game.status =
            "Party returned to the Arena lobby. Ready up for the next match.";
        game.projectiles.clear();
        game.mines.clear();
        game.broadcastClockReady = false;
        game.simClockReady = false;

        for (ArenaPlayer& player : game.players)
            player.ready = false;

        sendArenaState(game);
        readyArenaPlayers(game);
        return;
    }

    if (line == "ARENA_LEAVE") {
        int gameIndex =
            findArenaGame(client.socket);

        if (gameIndex == -1) {
            sendPacket(
                client.socket,
                "ARENA_END",
                "Left JENG Arena."
            );
            return;
        }

        ArenaGame& game =
            arenaGames[gameIndex];

        if (game.phase == "POSTGAME") {
            int playerIndex =
                arenaPlayerIndex(
                    game,
                    client.socket
                );

            bool hostLeft =
                game.host == client.socket;

            if (playerIndex >= 0) {
                game.players.erase(
                    game.players.begin() +
                    playerIndex
                );
            }

            sendPacket(
                client.socket,
                "ARENA_END",
                "Left the Arena party."
            );
            sendReady(client.socket);

            if (game.players.empty()) {
                arenaGames.erase(
                    arenaGames.begin() +
                    gameIndex
                );
                return;
            }

            if (hostLeft)
                game.host = game.players.front().socket;

            game.status =
                client.name +
                " left the Arena party.";

            if (hostLeft) {
                game.status +=
                    " " +
                    getName(game.host) +
                    " is now host.";
            }

            sendArenaState(game);
            readyArenaPlayers(game);
            return;
        }

        if (game.phase == "PLAYING") {
            ArenaGame copy = game;

            for (const ArenaPlayer& player : copy.players) {
                sendPacket(
                    player.socket,
                    "ARENA_END",
                    client.name +
                    " left the online Arena match."
                );
                sendReady(player.socket);
            }

            arenaGames.erase(
                arenaGames.begin() +
                gameIndex
            );

            return;
        }

        if (game.host == client.socket) {
            ArenaGame copy = game;

            for (const ArenaPlayer& player : copy.players) {
                sendPacket(
                    player.socket,
                    "ARENA_END",
                    "Arena lobby closed because the host left."
                );
                sendReady(player.socket);
            }

            arenaGames.erase(
                arenaGames.begin() +
                gameIndex
            );

            return;
        }

        int playerIndex =
            arenaPlayerIndex(
                game,
                client.socket
            );

        if (playerIndex >= 0) {
            game.players.erase(
                game.players.begin() +
                playerIndex
            );
        }

        game.status =
            client.name +
            " left the Arena lobby.";

        sendPacket(
            client.socket,
            "ARENA_END",
            "Left JENG Arena."
        );

        sendArenaState(game);
        readyArenaPlayers(game);
        return;
    }


    // Return a structured online-player roster for the Players and Invite menus.
    // This must be handled before normal chat so USERS_REQUEST is not broadcast.
    if (workingLine == "USERS_REQUEST") {
        string roster;

        for (const Client& user : clients) {
            if (user.name.empty())
                continue;

            if (!roster.empty())
                roster += "|";

            roster += user.name;
        }

        sendPacket(client.socket, "USERS_LIST", roster);
        return;
    }


    // --------------------------------------------------------
    // NORMAL CHAT
    // --------------------------------------------------------

    if (workingLine[0] != '/') {
        broadcastChat(
            client.name,
            workingLine
        );

        return;
    }

    stringstream ss(workingLine);
    string command;
    ss >> command;

    command = lowerCopy(command);


    // --------------------------------------------------------
    // /users
    // --------------------------------------------------------

    if (command == "/users") {
        string result = "Online users: ";
        bool first = true;

        for (Client& c : clients) {
            if (c.name.empty())
                continue;

            if (!first)
                result += ", ";

            result += c.name;
            first = false;
        }

        sendPacket(
            client.socket,
            "SYS",
            result
        );
    }


    // --------------------------------------------------------
    // /ttt <username>
    // --------------------------------------------------------

    else if (command == "/ttt") {
        string targetName;
        ss >> targetName;

        if (targetName.empty()) {
            sendPacket(
                client.socket,
                "ERR",
                "Usage: /ttt <username>"
            );
            return;
        }

        if (isPlayerBusy(client.socket)) {
            sendPacket(
                client.socket,
                "ERR",
                "You are already in a game."
            );
            return;
        }

        Client* target = getClientByName(targetName);

        if (!target) {
            sendPacket(
                client.socket,
                "ERR",
                "User not found."
            );
            return;
        }

        if (target->socket == client.socket) {
            sendPacket(
                client.socket,
                "ERR",
                "You cannot challenge yourself."
            );
            return;
        }

        if (isPlayerBusy(target->socket)) {
            sendPacket(
                client.socket,
                "ERR",
                target->name +
                " is already playing."
            );
            return;
        }

        if (target->pendingChallenge != INVALID_SOCK) {
            sendPacket(
                client.socket,
                "ERR",
                target->name +
                " already has a pending challenge."
            );
            return;
        }

        target->pendingChallenge = client.socket;
        target->pendingGame = "ttt";
        target->pendingChips = 0;
        target->pendingHands = 0;

        sendPacket(
            client.socket,
            "GAME",
            "Tic-Tac-Toe challenge sent to " +
            target->name +
            "."
        );

        sendPacket(
            target->socket,
            "GAME",
            "*** " +
            client.name +
            " challenged you to Tic-Tac-Toe! ***"
        );

        sendPacket(
            target->socket,
            "GAME",
            "Type /accept or /decline"
        );

        sendReady(target->socket);
    }


    // --------------------------------------------------------
    // /chess <username>
    // --------------------------------------------------------

    else if (command == "/chess") {
        string targetName;
        ss >> targetName;

        if (targetName.empty()) {
            sendPacket(
                client.socket,
                "ERR",
                "Usage: /chess <username>"
            );
            return;
        }

        if (isPlayerBusy(client.socket)) {
            sendPacket(
                client.socket,
                "ERR",
                "You are already in a game."
            );
            return;
        }

        Client* target = getClientByName(targetName);

        if (!target) {
            sendPacket(
                client.socket,
                "ERR",
                "User not found."
            );
            return;
        }

        if (target->socket == client.socket) {
            sendPacket(
                client.socket,
                "ERR",
                "You cannot challenge yourself."
            );
            return;
        }

        if (isPlayerBusy(target->socket)) {
            sendPacket(
                client.socket,
                "ERR",
                target->name + " is already playing."
            );
            return;
        }

        if (target->pendingChallenge != INVALID_SOCK) {
            sendPacket(
                client.socket,
                "ERR",
                target->name +
                " already has a pending challenge."
            );
            return;
        }

        target->pendingChallenge = client.socket;
        target->pendingGame = "chess";
        target->pendingChips = 0;
        target->pendingHands = 0;

        sendPacket(
            client.socket,
            "CHESS_NOTICE",
            "Challenge sent to " +
            target->name +
            ". You will play White."
        );

        sendPacket(
            target->socket,
            "GAME",
            "*** " +
            client.name +
            " challenged you to Chess! ***"
        );

        sendPacket(
            target->socket,
            "CHESS_NOTICE",
            client.name +
            " will play White; you will play Black."
        );

        sendPacket(
            target->socket,
            "GAME",
            "Type /accept or /decline"
        );

        sendReady(target->socket);
    }


    // --------------------------------------------------------
    // /blackjackcreate <starting_chips> <hands>
    // Create the table first, then invite players from the lobby.
    // --------------------------------------------------------

    else if (
        command == "/blackjackcreate" ||
        command == "/bjcreate"
    ) {
        int startingChips = 0;
        int hands = 0;

        if (!(ss >> startingChips >> hands)) {
            sendBlackjackError(
                client.socket,
                "Enter starting chips and number of hands."
            );
            return;
        }

        if (
            startingChips <= 0 ||
            startingChips > MAX_STARTING_CHIPS
        ) {
            sendBlackjackError(
                client.socket,
                "Starting chips must be between 1 and " +
                to_string(MAX_STARTING_CHIPS) +
                "."
            );
            return;
        }

        if (
            hands < 1 ||
            hands > MAX_BLACKJACK_HANDS
        ) {
            sendBlackjackError(
                client.socket,
                "Hands must be between 1 and " +
                to_string(MAX_BLACKJACK_HANDS) +
                "."
            );
            return;
        }

        if (isPlayerBusy(client.socket)) {
            sendBlackjackError(
                client.socket,
                "You are already in a game."
            );
            return;
        }

        BlackjackGame game;
        game.host = client.socket;
        game.startingChips = startingChips;
        game.totalHands = hands;
        game.currentHand = 1;
        game.phase = "LOBBY";
        game.status =
            "Table created. Invite players, then start the match.";

        BlackjackPlayer hostPlayer;
        hostPlayer.socket = client.socket;
        hostPlayer.chips = startingChips;
        game.players.push_back(hostPlayer);

        blackjackGames.push_back(game);

        sendBlackjackState(
            blackjackGames.back()
        );

        readyBlackjackPlayers(
            blackjackGames.back()
        );

        return;
    }


    // --------------------------------------------------------
    // /blackjack
    // Normal GUI flow: create with /blackjackcreate, then invite with
    // /blackjack <username>. The old 3-argument form is still accepted
    // for backward compatibility.
    // --------------------------------------------------------

    else if (
        command == "/blackjack" ||
        command == "/bj"
    ) {
        string targetName;
        int startingChips = 0;
        int hands = 0;

        int existingIndex = findBlackjackGame(client.socket);
        BlackjackGame* existingGame =
            existingIndex >= 0
            ? &blackjackGames[existingIndex]
            : nullptr;

        if (existingGame) {
            if (
                existingGame->host != client.socket ||
                existingGame->phase != "LOBBY"
            ) {
                sendBlackjackError(
                    client.socket,
                    "You can only invite players while hosting a Blackjack lobby."
                );
                return;
            }

            if (!(ss >> targetName)) {
                sendBlackjackError(
                    client.socket,
                    "Choose a player to invite."
                );
                return;
            }

            startingChips = existingGame->startingChips;
            hands = existingGame->totalHands;
        }
        else {
            if (!(ss >> targetName >> startingChips >> hands)) {
                sendBlackjackError(
                    client.socket,
                    "Enter a player, starting chips, and number of hands."
                );
                return;
            }

            if (
                startingChips <= 0 ||
                startingChips > MAX_STARTING_CHIPS
            ) {
                sendBlackjackError(
                    client.socket,
                    "Starting chips must be between 1 and " +
                    to_string(MAX_STARTING_CHIPS) +
                    "."
                );
                return;
            }

            if (
                hands < 1 ||
                hands > MAX_BLACKJACK_HANDS
            ) {
                sendBlackjackError(
                    client.socket,
                    "Hands must be between 1 and " +
                    to_string(MAX_BLACKJACK_HANDS) +
                    "."
                );
                return;
            }

            if (isPlayerBusy(client.socket)) {
                sendBlackjackError(
                    client.socket,
                    "You are already in a game."
                );
                return;
            }
        }

        Client* target = getClientByName(targetName);

        if (!target) {
            sendBlackjackError(
                client.socket,
                "User not found."
            );
            return;
        }

        if (target->socket == client.socket) {
            sendBlackjackError(
                client.socket,
                "You cannot invite yourself."
            );
            return;
        }

        if (isPlayerBusy(target->socket)) {
            sendBlackjackError(
                client.socket,
                target->name + " is already playing."
            );
            return;
        }

        if (target->pendingChallenge != INVALID_SOCK) {
            sendBlackjackError(
                client.socket,
                target->name + " already has a pending invite."
            );
            return;
        }

        if (!existingGame) {
            BlackjackGame game;
            game.host = client.socket;
            game.startingChips = startingChips;
            game.totalHands = hands;
            game.currentHand = 1;
            game.phase = "LOBBY";
            game.status = "Invite players to a Blackjack match.";

            BlackjackPlayer hostPlayer;
            hostPlayer.socket = client.socket;
            hostPlayer.chips = startingChips;
            game.players.push_back(hostPlayer);

            blackjackGames.push_back(game);
            existingIndex = (int)blackjackGames.size() - 1;
            existingGame = &blackjackGames[existingIndex];
        }

        BlackjackGame& game = *existingGame;

        if ((int)game.players.size() >= 6) {
            sendBlackjackError(
                client.socket,
                "This Blackjack table already has 6 players."
            );
            return;
        }

        target->pendingChallenge = client.socket;
        target->pendingGame = "blackjack";
        target->pendingChips = game.startingChips;
        target->pendingHands = game.totalHands;

        sendPacket(
            target->socket,
            "BJ_CHALLENGE",
            client.name + "|" +
            to_string(game.startingChips) + "|" +
            to_string(game.totalHands) + "|" +
            to_string(game.players.size())
        );

        game.status =
            "Invitation sent to " +
            target->name +
            ".";

        sendBlackjackState(game);
        sendReady(target->socket);
        readyBlackjackPlayers(game);
    }



    // --------------------------------------------------------
    // /roulettecreate <starting_chips> <rounds>
    // --------------------------------------------------------

    else if (command == "/roulettecreate") {
        int startingChips = 0;
        int rounds = 0;

        if (!(ss >> startingChips >> rounds)) {
            sendRouletteError(
                client.socket,
                "Enter starting chips and number of rounds."
            );
            return;
        }

        handleCommand(
            client,
            "RLT_CREATE|" +
            to_string(startingChips) +
            "|" +
            to_string(rounds)
        );

        return;
    }


    // --------------------------------------------------------
    // /roulette <username>
    // --------------------------------------------------------

    else if (command == "/roulette") {
        string targetName;
        ss >> targetName;

        if (targetName.empty()) {
            sendRouletteError(
                client.socket,
                "Choose a player to invite."
            );
            return;
        }

        handleCommand(
            client,
            "RLT_INVITE|" +
            targetName
        );

        return;
    }


    // --------------------------------------------------------
    // /pokercreate <starting_chips> <small_blind>
    // Create the table first, then invite up to five players from the lobby.
    // --------------------------------------------------------

    else if (command == "/pokercreate") {
        int startingChips = 0;
        int smallBlind = 0;

        if (!(ss >> startingChips >> smallBlind)) {
            sendPacket(
                client.socket,
                "ERR",
                "Enter starting chips and the small blind."
            );
            return;
        }

        int bigBlind =
            smallBlind * 2;

        if (
            smallBlind <= 0 ||
            startingChips < bigBlind ||
            startingChips > MAX_POKER_CHIPS
        ) {
            sendPacket(
                client.socket,
                "ERR",
                "Poker requires positive blinds and starting chips at least equal to the big blind."
            );
            return;
        }

        if (isPlayerBusy(client.socket)) {
            sendPacket(
                client.socket,
                "ERR",
                "You are already in a game."
            );
            return;
        }

        PokerGame game;
        game.host = client.socket;
        game.startingChips = startingChips;
        game.smallBlind = smallBlind;
        game.bigBlind = bigBlind;
        game.dealer = client.socket;
        game.phase = "LOBBY";
        game.status =
            "Table created. Invite up to five players to join.";

        PokerGame::Player hostPlayer;
        hostPlayer.socket = client.socket;
        hostPlayer.name = client.name;
        hostPlayer.chips = startingChips;
        game.players.push_back(hostPlayer);

        pokerGames.push_back(game);

        sendPokerLobbyState(
            pokerGames.back()
        );

        return;
    }


    // --------------------------------------------------------
    // /poker <username>
    // Invite another player to an already-created Poker table.
    // --------------------------------------------------------

    else if (command == "/poker") {
        string targetName;
        ss >> targetName;

        if (targetName.empty()) {
            sendPacket(
                client.socket,
                "ERR",
                "Choose a player to invite."
            );
            return;
        }

        int gameIndex =
            findPokerGame(
                client.socket
            );

        if (gameIndex == -1) {
            sendPacket(
                client.socket,
                "ERR",
                "Create a Poker table first."
            );
            return;
        }

        PokerGame& game =
            pokerGames[gameIndex];

        bool betweenHands =
            game.phase == "RESULT" &&
            !game.handActive;

        if (
            game.host != client.socket ||
            (game.phase != "LOBBY" && !betweenHands)
        ) {
            sendPacket(
                client.socket,
                "ERR",
                "Only the host can invite players before the match or between hands."
            );
            return;
        }

        if (seatedPokerPlayers(game) >= 6) {
            sendPacket(
                client.socket,
                "ERR",
                "This Poker table already has six players."
            );
            return;
        }

        Client* target =
            getClientByName(
                targetName
            );

        if (!target) {
            sendPacket(
                client.socket,
                "ERR",
                "User not found."
            );
            return;
        }

        if (target->socket == client.socket) {
            sendPacket(
                client.socket,
                "ERR",
                "You cannot invite yourself."
            );
            return;
        }

        if (isPlayerBusy(target->socket)) {
            sendPacket(
                client.socket,
                "ERR",
                target->name +
                " is already playing."
            );
            return;
        }

        if (
            target->pendingChallenge !=
            INVALID_SOCK
        ) {
            sendPacket(
                client.socket,
                "ERR",
                target->name +
                " already has a pending invite."
            );
            return;
        }

        target->pendingChallenge =
            client.socket;

        target->pendingGame =
            "poker";

        target->pendingChips =
            game.startingChips;

        target->pendingHands =
            game.smallBlind;

        sendPacket(
            target->socket,
            "POKER_CHALLENGE",
            client.name + "|" +
            to_string(game.startingChips) + "|" +
            to_string(game.smallBlind) + "|" +
            to_string(game.bigBlind)
        );

        game.status =
            "Invitation sent to " +
            target->name +
            ".";

        if (game.phase == "LOBBY") {
            sendPokerLobbyState(game);
        }
        else {
            sendPokerState(game);
            sendPokerNotice(game, game.status);
        }
        sendReady(target->socket);
        return;
    }


    // --------------------------------------------------------
    // /pokerstart
    // --------------------------------------------------------

    else if (command == "/pokerstart") {
        int gameIndex =
            findPokerGame(
                client.socket
            );

        if (gameIndex == -1) {
            sendPacket(
                client.socket,
                "ERR",
                "You are not at a Poker table."
            );
            return;
        }

        PokerGame& game =
            pokerGames[gameIndex];

        if (game.host != client.socket) {
            sendPacket(
                client.socket,
                "ERR",
                "Only the host can start the Poker match."
            );
            return;
        }

        if (game.phase != "LOBBY") {
            sendPacket(
                client.socket,
                "ERR",
                "The Poker match has already started."
            );
            return;
        }

        if (game.players.size() < 2) {
            sendPacket(
                client.socket,
                "ERR",
                "Invite at least one player before starting the Poker match."
            );
            return;
        }

        game.status = "Poker match started.";
        game.dealer = game.host;
        startPokerHand(game);
        return;
    }


    // --------------------------------------------------------
    // /accept
    // --------------------------------------------------------

    else if (command == "/accept") {
        if (client.pendingChallenge == INVALID_SOCK) {
            sendPacket(
                client.socket,
                "ERR",
                "You have no pending challenge."
            );
            return;
        }

        Client* challenger = getClient(
            client.pendingChallenge
        );

        if (!challenger) {
            clearPendingChallenge(client);

            sendPacket(
                client.socket,
                "ERR",
                "That player disconnected."
            );
            return;
        }

        string gameType = client.pendingGame;

        Socket challengerSocket = challenger->socket;
        Socket accepterSocket = client.socket;

        if (gameType == "chess_spectate") {
            if (isPlayerBusy(accepterSocket)) {
                clearPendingChallenge(client);
                sendPacket(
                    accepterSocket,
                    "CHESS_ERROR",
                    "You are already in a game."
                );
                return;
            }

            int chessIndex = findChessGame(challengerSocket);

            if (chessIndex == -1 ||
                !chessIsPlayer(chessGames[chessIndex], challengerSocket)) {
                clearPendingChallenge(client);
                sendPacket(
                    accepterSocket,
                    "CHESS_ERROR",
                    "That Chess game is no longer available."
                );
                return;
            }

            ChessGame& game = chessGames[chessIndex];

            if ((int)game.spectators.size() >= 8) {
                clearPendingChallenge(client);
                sendPacket(
                    accepterSocket,
                    "CHESS_ERROR",
                    "That Chess game already has eight spectators."
                );
                return;
            }

            game.spectators.push_back(accepterSocket);
            clearPendingChallenge(client);

            sendChessState(game);
            sendChessLine(
                game,
                client.name + " joined as a spectator."
            );
            readyChessPlayers(game);
            return;
        }

        // Blackjack invitations join an existing host lobby. The host is
        // intentionally already considered busy because they own that table.
        if (gameType == "blackjack") {
            if (isPlayerBusy(accepterSocket)) {
                clearPendingChallenge(client);
                sendBlackjackError(
                    accepterSocket,
                    "You are already in a game."
                );
                return;
            }

            int blackjackIndex = findBlackjackGame(challengerSocket);

            if (blackjackIndex == -1) {
                clearPendingChallenge(client);
                sendBlackjackError(
                    accepterSocket,
                    "That Blackjack lobby no longer exists."
                );
                return;
            }

            BlackjackGame& game = blackjackGames[blackjackIndex];

            if (
                game.host != challengerSocket ||
                game.phase != "LOBBY"
            ) {
                clearPendingChallenge(client);
                sendBlackjackError(
                    accepterSocket,
                    "That Blackjack lobby has already started."
                );
                return;
            }

            if ((int)game.players.size() >= 6) {
                clearPendingChallenge(client);
                sendBlackjackError(
                    accepterSocket,
                    "That Blackjack table is full."
                );
                return;
            }

            BlackjackPlayer player;
            player.socket = accepterSocket;
            player.chips = game.startingChips;
            game.players.push_back(player);

            clearPendingChallenge(client);

            sendPacket(
                accepterSocket,
                "BJ_NOTICE",
                "Joined " +
                getName(game.host) +
                "'s Blackjack lobby."
            );

            game.status =
                client.name +
                " joined the Blackjack lobby. " +
                to_string(game.players.size()) +
                "/6 players.";

            sendBlackjackState(game);
            readyBlackjackPlayers(game);
            return;
        }


        // Roulette invitations join an existing host lobby.
        if (gameType == "roulette") {
            if (isPlayerBusy(accepterSocket)) {
                clearPendingChallenge(client);
                sendRouletteError(
                    accepterSocket,
                    "You are already in a game."
                );
                return;
            }

            int rouletteIndex =
                findRouletteGame(
                    challengerSocket
                );

            if (rouletteIndex == -1) {
                clearPendingChallenge(client);
                sendRouletteError(
                    accepterSocket,
                    "That Roulette lobby no longer exists."
                );
                return;
            }

            RouletteGame& game =
                rouletteGames[rouletteIndex];

            if (
                game.host != challengerSocket ||
                game.phase != "LOBBY"
            ) {
                clearPendingChallenge(client);
                sendRouletteError(
                    accepterSocket,
                    "That Roulette table has already started."
                );
                return;
            }

            if (
                (int)game.players.size() >=
                MAX_ROULETTE_PLAYERS
            ) {
                clearPendingChallenge(client);
                sendRouletteError(
                    accepterSocket,
                    "That Roulette table is full."
                );
                return;
            }

            RoulettePlayer player;
            player.socket = accepterSocket;
            player.chips = game.startingChips;
            game.players.push_back(player);

            clearPendingChallenge(client);

            sendRouletteNotice(
                accepterSocket,
                "Joined " +
                getName(game.host) +
                "'s Roulette lobby."
            );

            game.status =
                client.name +
                " joined the Roulette lobby. " +
                to_string(game.players.size()) +
                "/6 players.";

            sendRouletteState(game);
            readyRoulettePlayers(game);
            return;
        }

        // Poker invitations join the host's existing table lobby.
        if (gameType == "poker") {
            if (isPlayerBusy(accepterSocket)) {
                clearPendingChallenge(client);

                sendPacket(
                    accepterSocket,
                    "ERR",
                    "You are already in a game."
                );
                return;
            }

            int pokerIndex =
                findPokerGame(
                    challengerSocket
                );

            if (pokerIndex == -1) {
                clearPendingChallenge(client);

                sendPacket(
                    accepterSocket,
                    "ERR",
                    "That Poker lobby no longer exists."
                );
                return;
            }

            PokerGame& game =
                pokerGames[pokerIndex];

            bool betweenHands =
                game.phase == "RESULT" &&
                !game.handActive;

            if (
                game.host != challengerSocket ||
                (game.phase != "LOBBY" && !betweenHands)
            ) {
                clearPendingChallenge(client);

                sendPacket(
                    accepterSocket,
                    "ERR",
                    "That Poker table is not accepting players right now."
                );
                return;
            }

            if (seatedPokerPlayers(game) >= 6) {
                clearPendingChallenge(client);

                sendPacket(
                    accepterSocket,
                    "ERR",
                    "That Poker table is already full."
                );
                return;
            }

            PokerGame::Player player;
            player.socket = accepterSocket;
            player.name = client.name;
            player.chips = game.startingChips;
            game.players.push_back(player);

            clearPendingChallenge(client);

            game.status =
                client.name +
                " joined the Poker table. " +
                to_string(seatedPokerPlayers(game)) +
                "/6 players.";

            if (game.phase == "LOBBY") {
                sendPokerLobbyState(game);
            }
            else {
                sendPokerState(game);
                sendPokerNotice(
                    game,
                    client.name +
                    " joined between hands with " +
                    to_string(game.startingChips) +
                    " chips."
                );
            }
            return;
        }


        // JENG Arena invitations join the host's existing lobby.
        if (gameType == "arena") {
            if (isPlayerBusy(accepterSocket)) {
                clearPendingChallenge(client);

                sendArenaError(
                    accepterSocket,
                    "You are already in a game."
                );
                return;
            }

            int arenaIndex =
                findArenaGame(
                    challengerSocket
                );

            if (arenaIndex == -1) {
                clearPendingChallenge(client);

                sendArenaError(
                    accepterSocket,
                    "That Arena lobby no longer exists."
                );
                return;
            }

            ArenaGame& game =
                arenaGames[arenaIndex];

            if (
                game.host != challengerSocket ||
                game.phase != "LOBBY"
            ) {
                clearPendingChallenge(client);

                sendArenaError(
                    accepterSocket,
                    "That Arena lobby has already started."
                );
                return;
            }

            if (
                (int)game.players.size() >=
                game.maxPlayers
            ) {
                clearPendingChallenge(client);

                sendArenaError(
                    accepterSocket,
                    "That Arena lobby is full."
                );
                return;
            }

            ArenaPlayer player;
            player.socket = accepterSocket;
            player.ready = false;

            if (arenaTeamMode(game.mode)) {
                player.team =
                    chooseArenaTeam(game);
                player.colorIndex = -1;
            }
            else {
                player.team = -1;
                player.colorIndex =
                    firstAvailableArenaColor(game);
            }

            game.players.push_back(player);

            clearPendingChallenge(client);

            game.status =
                client.name +
                " joined the Arena lobby. " +
                to_string(game.players.size()) +
                "/" +
                to_string(game.maxPlayers) +
                " players.";

            sendArenaState(game);
            readyArenaPlayers(game);
            return;
        }

        if (
            isPlayerBusy(accepterSocket) ||
            isPlayerBusy(challengerSocket)
        ) {
            clearPendingChallenge(client);

            sendPacket(
                accepterSocket,
                "ERR",
                "One of the players is already in a game."
            );
            return;
        }

        clearPendingChallenge(client);

        if (gameType == "ttt") {
            TicTacToeGame game;
            game.playerX = challengerSocket;
            game.playerO = accepterSocket;
            game.turn = challengerSocket;

            ticTacToeGames.push_back(game);

            sendPacket(
                challengerSocket,
                "GAME",
                client.name +
                " accepted your Tic-Tac-Toe challenge!"
            );

            sendPacket(
                accepterSocket,
                "GAME",
                "Challenge accepted!"
            );

            showTicTacToeBoard(
                ticTacToeGames.back()
            );

            sendReady(challengerSocket);
            sendReady(accepterSocket);
        }
        else if (gameType == "chess") {
            ChessGame game = makeChessGame(
                challengerSocket,
                accepterSocket
            );

            chessGames.push_back(game);

            ChessGame& created = chessGames.back();

            sendChessLine(
                created,
                "*** Chess challenge accepted! ***"
            );

            sendChessLine(
                created,
                getName(challengerSocket) +
                " is White. " +
                getName(accepterSocket) +
                " is Black."
            );

            showChessBoard(created);
            readyChessPlayers(created);
        }
        else {
            sendPacket(
                accepterSocket,
                "ERR",
                "The pending challenge type was invalid."
            );
        }
    }


    // --------------------------------------------------------
    // /decline
    // --------------------------------------------------------

    else if (command == "/decline") {
        if (client.pendingChallenge == INVALID_SOCK) {
            sendPacket(
                client.socket,
                "ERR",
                "You have no pending challenge."
            );
            return;
        }

        Client* challenger = getClient(
            client.pendingChallenge
        );

        string gameName;

        if (client.pendingGame == "blackjack")
            gameName = "Blackjack";
        else if (
            client.pendingGame == "chess" ||
            client.pendingGame == "chess_spectate"
        )
            gameName = "Chess";
        else if (client.pendingGame == "poker")
            gameName = "Poker";
        else if (client.pendingGame == "roulette")
            gameName = "Roulette";
        else if (client.pendingGame == "arena")
            gameName = "JENG Arena";
        else
            gameName = "Tic-Tac-Toe";

        if (challenger) {
            if (client.pendingGame == "chess_spectate") {
                sendPacket(
                    challenger->socket,
                    "CHESS_NOTICE",
                    client.name +
                    " declined your Chess spectator invitation."
                );
            }
            else if (client.pendingGame == "blackjack") {
                sendPacket(
                    challenger->socket,
                    "BJ_NOTICE",
                    client.name +
                    " declined your Blackjack invitation."
                );
            }
            else if (client.pendingGame == "roulette") {
                sendPacket(
                    challenger->socket,
                    "RLT_NOTICE",
                    client.name +
                    " declined your Roulette invitation."
                );
            }
            else if (client.pendingGame == "poker") {
                sendPacket(
                    challenger->socket,
                    "POKER_NOTICE",
                    client.name +
                    " declined your Poker invitation."
                );
            }
            else if (client.pendingGame == "arena") {
                sendPacket(
                    challenger->socket,
                    "ARENA_NOTICE",
                    client.name +
                    " declined your JENG Arena invitation."
                );
            }
            else {
                sendPacket(
                    challenger->socket,
                    "GAME",
                    client.name +
                    " declined your " +
                    gameName +
                    " challenge."
                );
            }

            sendReady(challenger->socket);
        }

        bool wasBlackjack = client.pendingGame == "blackjack";
        bool wasChessSpectate = client.pendingGame == "chess_spectate";
        bool wasRoulette = client.pendingGame == "roulette";
        bool wasPoker = client.pendingGame == "poker";
        bool wasArena = client.pendingGame == "arena";
        clearPendingChallenge(client);

        sendPacket(
            client.socket,
            wasChessSpectate
                ? "CHESS_NOTICE"
                : (wasBlackjack
                ? "BJ_NOTICE"
                : (
                    wasRoulette
                    ? "RLT_NOTICE"
                    : (
                        wasPoker
                        ? "POKER_NOTICE"
                        : (wasArena ? "ARENA_NOTICE" : "GAME")
                      )
                  )),
            wasChessSpectate
                ? "Chess spectator invitation declined."
                : (wasBlackjack
                ? "Blackjack invitation declined."
                : (
                    wasRoulette
                    ? "Roulette invitation declined."
                    : (
                        wasPoker
                        ? "Poker invitation declined."
                        : (
                            wasArena
                            ? "JENG Arena invitation declined."
                            : "Challenge declined."
                          )
                      )
                  ))
        );
    }


    // --------------------------------------------------------
    // /move
    // Tic-Tac-Toe: /move <1-9>
    // Chess:       /move <from> <to> [promotion]
    // --------------------------------------------------------

    else if (command == "/move") {
        int chessIndex = findChessGame(
            client.socket
        );

        if (chessIndex != -1) {
            string fromSquare;
            string toSquare;
            string promotion;

            ss >> fromSquare >> toSquare >> promotion;

            if (fromSquare.empty() || toSquare.empty()) {
                sendPacket(
                    client.socket,
                    "ERR",
                    "Usage: /move <from> <to>  Example: /move e2 e4"
                );
                return;
            }

            playChessMove(
                chessIndex,
                client,
                fromSquare,
                toSquare,
                promotion
            );

            return;
        }

        int position;

        if (!(ss >> position)) {
            sendPacket(
                client.socket,
                "ERR",
                "Tic-Tac-Toe: /move <1-9> | Chess: /move e2 e4"
            );
            return;
        }

        int gameIndex = findTicTacToeGame(
            client.socket
        );

        if (gameIndex == -1) {
            sendPacket(
                client.socket,
                "ERR",
                "You are not in a Tic-Tac-Toe or Chess game."
            );
            return;
        }

        TicTacToeGame& game =
            ticTacToeGames[gameIndex];

        if (game.turn != client.socket) {
            sendPacket(
                client.socket,
                "ERR",
                "It is not your turn."
            );
            return;
        }

        if (position < 1 || position > 9) {
            sendPacket(
                client.socket,
                "ERR",
                "Position must be 1 through 9."
            );
            return;
        }

        int index = position - 1;

        if (game.board[index] != ' ') {
            sendPacket(
                client.socket,
                "ERR",
                "That square is already taken."
            );
            return;
        }

        char symbol =
            client.socket == game.playerX
            ? 'X'
            : 'O';

        game.board[index] = symbol;

        if (ticTacToeWinner(game, symbol)) {
            Socket playerX = game.playerX;
            Socket playerO = game.playerO;

            showTicTacToeBoard(
                game,
                false
            );

            sendTicTacToeLine(
                game,
                "*** " +
                client.name +
                " WINS! ***"
            );

            sendReady(playerX);
            sendReady(playerO);

            ticTacToeGames.erase(
                ticTacToeGames.begin() + gameIndex
            );

            return;
        }

        if (ticTacToeBoardFull(game)) {
            Socket playerX = game.playerX;
            Socket playerO = game.playerO;

            showTicTacToeBoard(
                game,
                false
            );

            sendTicTacToeLine(
                game,
                "*** DRAW! ***"
            );

            sendReady(playerX);
            sendReady(playerO);

            ticTacToeGames.erase(
                ticTacToeGames.begin() + gameIndex
            );

            return;
        }

        game.turn =
            game.turn == game.playerX
            ? game.playerO
            : game.playerX;

        showTicTacToeBoard(game);
        sendReady(game.playerX);
        sendReady(game.playerO);
    }

    // --------------------------------------------------------
    // /board
    // --------------------------------------------------------

    else if (command == "/board") {
        int chessIndex = findChessGame(
            client.socket
        );

        if (chessIndex != -1) {
            ChessGame& game = chessGames[chessIndex];
            showChessBoard(game);
            readyChessPlayers(game);
            return;
        }

        int gameIndex = findTicTacToeGame(
            client.socket
        );

        if (gameIndex == -1) {
            sendPacket(
                client.socket,
                "ERR",
                "You are not currently playing Tic-Tac-Toe or Chess."
            );
            return;
        }

        TicTacToeGame& game = ticTacToeGames[gameIndex];
        showTicTacToeBoard(game);
        sendReady(game.playerX);
        sendReady(game.playerO);
    }

    // --------------------------------------------------------
    // /bjstart - host starts a 2-6 player lobby
    // --------------------------------------------------------

    else if (command == "/bjstart") {
        int gameIndex = findBlackjackGame(client.socket);

        if (gameIndex == -1) {
            sendBlackjackError(client.socket, "You are not in a Blackjack lobby.");
            return;
        }

        BlackjackGame& game = blackjackGames[gameIndex];

        if (game.host != client.socket) {
            sendBlackjackError(client.socket, "Only the host can start the Blackjack match.");
            return;
        }

        if (game.phase != "LOBBY") {
            sendBlackjackError(client.socket, "This Blackjack match has already started.");
            return;
        }

        if (game.players.size() < 2) {
            sendBlackjackError(client.socket, "Invite at least one other player before starting.");
            return;
        }

        resetBlackjackRound(game);
        game.currentHand = 1;
        game.status =
            "Blackjack match started with " +
            to_string(game.players.size()) +
            " players. Place your bets.";

        showBlackjackBetting(game);
    }


    // --------------------------------------------------------
    // /bet <amount>
    // --------------------------------------------------------

    else if (command == "/bet") {
        int amount = 0;

        if (!(ss >> amount)) {
            sendBlackjackError(client.socket, "Enter a valid bet amount.");
            return;
        }

        int gameIndex = findBlackjackGame(client.socket);

        if (gameIndex == -1) {
            sendBlackjackError(client.socket, "You are not in a Blackjack game.");
            return;
        }

        BlackjackGame& game = blackjackGames[gameIndex];
        BlackjackPlayer* player = blackjackPlayer(game, client.socket);

        if (!player) {
            sendBlackjackError(client.socket, "Blackjack player state was not found.");
            return;
        }

        if (game.phase != "BETTING" || game.handInProgress) {
            sendBlackjackError(client.socket, "Betting is closed for this hand.");
            return;
        }

        if (player->betPlaced) {
            sendBlackjackError(client.socket, "You already placed a bet for this hand.");
            return;
        }

        if (player->chips <= 0) {
            sendBlackjackError(client.socket, "You are out of chips.");
            return;
        }

        if (amount <= 0) {
            sendBlackjackError(client.socket, "Your bet must be greater than 0.");
            return;
        }

        if (amount > player->chips) {
            sendBlackjackError(
                client.socket,
                "You only have " + to_string(player->chips) + " chips."
            );
            return;
        }

        player->pendingBet = amount;
        player->betPlaced = true;

        game.status =
            client.name +
            " placed a " +
            to_string(amount) +
            " chip bet.";

        sendBlackjackState(game);
        readyBlackjackPlayers(game);

        if (allBlackjackBetsPlaced(game)) {
            // Do NOT deal immediately. That made the final player's bet
            // visually disappear because the card state arrived in the
            // same network burst. Hold the completed betting table for
            // one second, then deal without blocking chat/network I/O.
            game.dealScheduled = true;
            game.dealAt =
                chrono::steady_clock::now() +
                chrono::milliseconds(1000);

            game.status =
                client.name +
                " placed the final bet. Dealing in 1 second...";

            sendBlackjackState(game);
            readyBlackjackPlayers(game);
        }
        else {
            game.status =
                "Waiting for all active players to place their bets.";

            sendBlackjackState(game);
            readyBlackjackPlayers(game);
        }
    }


    // --------------------------------------------------------
    // /hit
    // --------------------------------------------------------

    else if (command == "/hit") {
        int gameIndex = findBlackjackGame(client.socket);

        if (gameIndex == -1) {
            sendBlackjackError(client.socket, "You are not in a Blackjack game.");
            return;
        }

        BlackjackGame& game = blackjackGames[gameIndex];
        BlackjackPlayer* player = blackjackPlayer(game, client.socket);

        if (
            !player ||
            game.phase != "PLAYING" ||
            !game.handInProgress ||
            game.turn != client.socket
        ) {
            sendBlackjackError(client.socket, "It is not your Blackjack turn.");
            return;
        }

        if (
            game.turnHandIndex < 0 ||
            game.turnHandIndex >= (int)player->hands.size()
        ) {
            sendBlackjackError(client.socket, "No active Blackjack hand.");
            return;
        }

        BlackjackHand& hand = player->hands[game.turnHandIndex];

        if (blackjackHandDone(hand)) {
            sendBlackjackError(client.socket, "That hand is already complete.");
            return;
        }

        Card card = drawBlackjackCard(game);
        hand.cards.push_back(card);

        sendBlackjackCardEvent(
            game,
            client.name,
            game.turnHandIndex,
            (int)hand.cards.size() - 1,
            blackjackCardCode(card)
        );

        int value = blackjackHandValue(hand);

        if (value > 21) {
            hand.busted = true;
            hand.stood = true;
            game.status =
                client.name +
                " busted hand " +
                to_string(game.turnHandIndex + 1) +
                " with " +
                to_string(value) +
                ".";

            sendBlackjackState(game);
            advanceBlackjackTurn(gameIndex);
            return;
        }

        if (value == 21) {
            hand.stood = true;
            game.status =
                client.name +
                " has 21 on hand " +
                to_string(game.turnHandIndex + 1) +
                ".";

            sendBlackjackState(game);
            advanceBlackjackTurn(gameIndex);
            return;
        }

        game.status = client.name + " draws " + blackjackCardCode(card) + ".";
        sendBlackjackState(game);
        readyBlackjackPlayers(game);
    }


    // --------------------------------------------------------
    // /stand
    // --------------------------------------------------------

    else if (command == "/stand") {
        int gameIndex = findBlackjackGame(client.socket);

        if (gameIndex == -1) {
            sendBlackjackError(client.socket, "You are not in a Blackjack game.");
            return;
        }

        BlackjackGame& game = blackjackGames[gameIndex];
        BlackjackPlayer* player = blackjackPlayer(game, client.socket);

        if (
            !player ||
            game.phase != "PLAYING" ||
            !game.handInProgress ||
            game.turn != client.socket
        ) {
            sendBlackjackError(client.socket, "It is not your Blackjack turn.");
            return;
        }

        if (
            game.turnHandIndex < 0 ||
            game.turnHandIndex >= (int)player->hands.size()
        ) {
            sendBlackjackError(client.socket, "No active Blackjack hand.");
            return;
        }

        BlackjackHand& hand = player->hands[game.turnHandIndex];
        hand.stood = true;

        game.status =
            client.name +
            " stands on " +
            to_string(blackjackHandValue(hand)) +
            ".";

        sendBlackjackState(game);
        advanceBlackjackTurn(gameIndex);
    }


    // --------------------------------------------------------
    // /double
    // --------------------------------------------------------

    else if (command == "/double") {
        int gameIndex = findBlackjackGame(client.socket);

        if (gameIndex == -1) {
            sendBlackjackError(client.socket, "You are not in a Blackjack game.");
            return;
        }

        BlackjackGame& game = blackjackGames[gameIndex];

        if (
            game.phase != "PLAYING" ||
            game.turn != client.socket
        ) {
            sendBlackjackError(client.socket, "It is not your Blackjack turn.");
            return;
        }

        int handIndex = game.turnHandIndex;

        if (!canBlackjackDouble(game, client.socket, handIndex)) {
            sendBlackjackError(
                client.socket,
                "Double is only available on a two-card hand when you have enough chips."
            );
            return;
        }

        BlackjackPlayer* player = blackjackPlayer(game, client.socket);
        BlackjackHand& hand = player->hands[handIndex];
        hand.bet *= 2;
        hand.doubled = true;

        Card card = drawBlackjackCard(game);
        hand.cards.push_back(card);

        sendBlackjackCardEvent(
            game,
            client.name,
            handIndex,
            (int)hand.cards.size() - 1,
            blackjackCardCode(card)
        );

        int value = blackjackHandValue(hand);

        if (value > 21)
            hand.busted = true;

        hand.stood = true;

        game.status =
            client.name +
            " doubles hand " +
            to_string(handIndex + 1) +
            " to " +
            to_string(hand.bet) +
            " chips and receives one card.";

        sendBlackjackState(game);
        advanceBlackjackTurn(gameIndex);
    }


    // --------------------------------------------------------
    // /split
    // --------------------------------------------------------

    else if (command == "/split") {
        int gameIndex = findBlackjackGame(client.socket);

        if (gameIndex == -1) {
            sendBlackjackError(client.socket, "You are not in a Blackjack game.");
            return;
        }

        BlackjackGame& game = blackjackGames[gameIndex];

        if (
            game.phase != "PLAYING" ||
            game.turn != client.socket
        ) {
            sendBlackjackError(client.socket, "It is not your Blackjack turn.");
            return;
        }

        int handIndex = game.turnHandIndex;

        if (!canBlackjackSplit(game, client.socket, handIndex)) {
            sendBlackjackError(
                client.socket,
                "Split requires a matching two-card pair and enough chips for the second bet."
            );
            return;
        }

        BlackjackPlayer* player = blackjackPlayer(game, client.socket);
        BlackjackHand original = player->hands[handIndex];

        BlackjackHand first;
        first.bet = original.bet;
        first.fromSplit = true;
        first.cards.push_back(original.cards[0]);

        BlackjackHand second;
        second.bet = original.bet;
        second.fromSplit = true;
        second.cards.push_back(original.cards[1]);

        player->hands.clear();
        player->hands.push_back(first);
        player->hands.push_back(second);
        game.turnHandIndex = 0;

        sendBlackjackPacket(game, "BJ_SPLIT_RESET", client.name);

        game.status = client.name + " splits the pair.";
        sendBlackjackState(game);

        for (int i = 0; i < 2; i++) {
            Card card = drawBlackjackCard(game);
            player->hands[i].cards.push_back(card);

            sendBlackjackCardEvent(
                game,
                client.name,
                i,
                1,
                blackjackCardCode(card)
            );

            if (blackjackHandValue(player->hands[i]) >= 21)
                player->hands[i].stood = true;
        }

        game.status = client.name + " now has two Blackjack hands.";
        sendBlackjackState(game);

        if (blackjackHandDone(player->hands[0])) {
            advanceBlackjackTurn(gameIndex);
            return;
        }

        readyBlackjackPlayers(game);
    }


    // --------------------------------------------------------
    // /bjnext
    // --------------------------------------------------------

    else if (command == "/bjnext") {
        int gameIndex = findBlackjackGame(client.socket);

        if (gameIndex == -1) {
            sendBlackjackError(client.socket, "You are not in a Blackjack game.");
            return;
        }

        BlackjackGame& game = blackjackGames[gameIndex];

        if (game.host != client.socket) {
            sendBlackjackError(client.socket, "Only the host can start the next hand.");
            return;
        }

        if (!game.awaitingNextHand || game.phase != "RESULT") {
            sendBlackjackError(client.socket, "The next hand is not ready yet.");
            return;
        }

        game.currentHand++;
        resetBlackjackRound(game);
        showBlackjackBetting(game);
    }


    // --------------------------------------------------------
    // /bjstatus
    // --------------------------------------------------------

    else if (
        command == "/bjstatus" ||
        command == "/blackjackstatus"
    ) {
        int gameIndex = findBlackjackGame(client.socket);

        if (gameIndex == -1) {
            sendBlackjackError(client.socket, "You are not in a Blackjack game.");
            return;
        }

        BlackjackGame& game = blackjackGames[gameIndex];
        sendBlackjackState(game);
        readyBlackjackPlayers(game);
    }


    // --------------------------------------------------------
    // POKER ACTIONS
    // --------------------------------------------------------

    else if (command == "/pokercheck") {
        int gameIndex = findPokerGame(client.socket);

        if (gameIndex == -1) {
            sendPacket(client.socket, "ERR", "You are not in a Poker game.");
            return;
        }

        PokerGame& game = pokerGames[gameIndex];

        if (!game.handActive || game.turn != client.socket) {
            sendPacket(client.socket, "ERR", "It is not your Poker turn.");
            return;
        }

        PokerGame::Player* player = pokerPlayer(game, client.socket);

        if (!player || player->roundBet != game.currentBet) {
            sendPacket(client.socket, "ERR", "You cannot check while facing a bet.");
            return;
        }

        player->acted = true;
        sendPokerNotice(game, client.name + " checks.");
        finishPokerAction(game, client.socket);
    }

    else if (command == "/pokercall") {
        int gameIndex = findPokerGame(client.socket);

        if (gameIndex == -1) {
            sendPacket(client.socket, "ERR", "You are not in a Poker game.");
            return;
        }

        PokerGame& game = pokerGames[gameIndex];

        if (!game.handActive || game.turn != client.socket) {
            sendPacket(client.socket, "ERR", "It is not your Poker turn.");
            return;
        }

        PokerGame::Player* player = pokerPlayer(game, client.socket);
        if (!player)
            return;

        int amount = game.currentBet - player->roundBet;

        if (amount <= 0) {
            sendPacket(client.socket, "ERR", "There is nothing to call.");
            return;
        }

        int paid = min(
            amount,
            player->chips
        );

        player->chips -= paid;
        player->roundBet += paid;
        player->handContribution += paid;
        game.pot += paid;
        player->acted = true;

        sendPokerNotice(
            game,
            client.name +
            " calls " +
            to_string(paid) +
            "."
        );

        finishPokerAction(game, client.socket);
    }

    else if (command == "/pokerraise") {
        int gameIndex = findPokerGame(client.socket);
        int target = 0;

        if (gameIndex == -1) {
            sendPacket(client.socket, "ERR", "You are not in a Poker game.");
            return;
        }

        if (!(ss >> target)) {
            sendPacket(client.socket, "ERR", "Usage: /pokerraise <total_bet>");
            return;
        }

        PokerGame& game = pokerGames[gameIndex];

        if (!game.handActive || game.turn != client.socket) {
            sendPacket(client.socket, "ERR", "It is not your Poker turn.");
            return;
        }

        PokerGame::Player* player = pokerPlayer(game, client.socket);
        if (!player)
            return;

        int maximum = pokerMaximumRaiseTo(game, client.socket);

        if (maximum <= game.currentBet) {
            sendPacket(client.socket, "ERR", "No further raise is possible.");
            return;
        }

        if (target > maximum || target <= game.currentBet) {
            sendPacket(
                client.socket,
                "ERR",
                "Raise target must be above the current bet and no more than " +
                to_string(maximum) +
                "."
            );
            return;
        }

        int minimum =
            game.currentBet +
            max(game.lastRaiseSize, game.bigBlind);

        bool allInRaise = target == maximum;

        if (target < minimum && !allInRaise) {
            sendPacket(
                client.socket,
                "ERR",
                "Minimum raise-to amount is " +
                to_string(minimum) +
                "."
            );
            return;
        }

        int oldCurrentBet = game.currentBet;
        int payment = target - player->roundBet;

        player->chips -= payment;
        player->roundBet = target;
        player->handContribution += payment;
        game.pot += payment;
        game.currentBet = target;

        int raiseSize = target - oldCurrentBet;

        if (raiseSize >= game.lastRaiseSize)
            game.lastRaiseSize = raiseSize;

        for (PokerGame::Player& other : game.players) {
            if (
                !other.left &&
                other.socket != client.socket &&
                !other.folded &&
                other.chips > 0
            )
                other.acted = false;
        }
        player->acted = true;

        sendPokerNotice(
            game,
            client.name +
            " raises to " +
            to_string(target) +
            "."
        );

        finishPokerAction(game, client.socket);
    }

    else if (command == "/pokerfold") {
        int gameIndex = findPokerGame(client.socket);

        if (gameIndex == -1) {
            sendPacket(client.socket, "ERR", "You are not in a Poker game.");
            return;
        }

        PokerGame& game = pokerGames[gameIndex];

        if (!game.handActive || game.turn != client.socket) {
            sendPacket(client.socket, "ERR", "It is not your Poker turn.");
            return;
        }

        PokerGame::Player* player = pokerPlayer(game, client.socket);
        if (!player)
            return;

        player->folded = true;
        player->acted = true;
        sendPokerNotice(game, client.name + " folds.");
        finishPokerAction(game, client.socket);
    }

    else if (command == "/pokernext") {
        int gameIndex = findPokerGame(client.socket);

        if (gameIndex == -1) {
            sendPacket(client.socket, "ERR", "You are not in a Poker game.");
            return;
        }

        PokerGame& game = pokerGames[gameIndex];

        if (game.handActive) {
            sendPacket(client.socket, "ERR", "The current Poker hand is still active.");
            return;
        }

        if (fundedPokerPlayers(game) < 2) {
            Socket winner = INVALID_SOCK;
            for (const PokerGame::Player& player : game.players)
                if (!player.left && player.chips > 0)
                    winner = player.socket;

            string result =
                (winner == INVALID_SOCK ? string("Poker match ended") : getName(winner) + " wins the Poker match") +
                "!";

            endPokerMatch(gameIndex, result);
            return;
        }

        game.dealer = nextPokerPlayer(game, game.dealer, true, false);

        startPokerHand(game);
    }

    else if (command == "/pokerend") {
        int gameIndex = findPokerGame(client.socket);

        if (gameIndex == -1) {
            sendPacket(client.socket, "ERR", "You are not in a Poker game.");
            return;
        }

        PokerGame& game = pokerGames[gameIndex];

        if (game.host != client.socket) {
            sendPacket(client.socket, "ERR", "Only the host can end the Poker match.");
            return;
        }

        if (game.handActive || game.phase != "RESULT") {
            sendPacket(client.socket, "ERR", "The Poker match can only be ended between hands.");
            return;
        }

        endPokerMatch(
            gameIndex,
            client.name + " ended the Poker match."
        );
    }


    // --------------------------------------------------------
    // /resign
    // --------------------------------------------------------

    else if (command == "/resign") {
        int ticTacToeIndex = findTicTacToeGame(
            client.socket
        );

        if (ticTacToeIndex != -1) {
            TicTacToeGame& game =
                ticTacToeGames[ticTacToeIndex];

            Socket opponent =
                game.playerX == client.socket
                ? game.playerO
                : game.playerX;

            Socket playerX = game.playerX;
            Socket playerO = game.playerO;

            sendTicTacToeLine(
                game,
                client.name +
                " resigned."
            );

            sendTicTacToeLine(
                game,
                getName(opponent) +
                " wins!"
            );

            sendReady(playerX);
            sendReady(playerO);

            ticTacToeGames.erase(
                ticTacToeGames.begin() + ticTacToeIndex
            );

            return;
        }

        int chessIndex = findChessGame(
            client.socket
        );

        if (chessIndex != -1) {
            ChessGame& game = chessGames[chessIndex];

            if (!chessIsPlayer(game, client.socket)) {
                sendPacket(
                    client.socket,
                    "CHESS_ERROR",
                    "Spectators cannot resign from a Chess game."
                );
                return;
            }

            Socket opponent =
                game.white == client.socket
                ? game.black
                : game.white;

            sendChessLine(
                game,
                client.name +
                " resigned from Chess."
            );

            sendChessEnd(
                game,
                getName(opponent) +
                " wins by resignation."
            );

            readyChessPlayers(game);

            chessGames.erase(
                chessGames.begin() + chessIndex
            );

            return;
        }

        int blackjackIndex = findBlackjackGame(
            client.socket
        );

        if (blackjackIndex != -1) {
            BlackjackGame& game =
                blackjackGames[blackjackIndex];

            if (game.host == client.socket) {
                finishBlackjackMatch(
                    blackjackIndex,
                    client.name +
                    " closed the Blackjack table."
                );
                return;
            }

            bool wasTurn = game.turn == client.socket;
            int playerIndex = blackjackPlayerIndex(game, client.socket);

            sendPacket(
                client.socket,
                "BJ_END",
                "You left the Blackjack table."
            );

            if (playerIndex >= 0) {
                game.players.erase(
                    game.players.begin() + playerIndex
                );
            }

            game.status =
                client.name +
                " left the Blackjack table.";

            if (game.players.empty()) {
                blackjackGames.erase(
                    blackjackGames.begin() + blackjackIndex
                );
                return;
            }

            if (wasTurn && game.phase == "PLAYING") {
                game.turn = INVALID_SOCK;
                game.turnHandIndex = 0;
                advanceBlackjackTurn(blackjackIndex);
                return;
            }

            if (
                game.phase == "BETTING" &&
                allBlackjackBetsPlaced(game)
            ) {
                startBlackjackHand(blackjackIndex);
                return;
            }

            sendBlackjackState(game);
            readyBlackjackPlayers(game);
            return;
        }

        int pokerIndex = findPokerGame(
            client.socket
        );

        if (pokerIndex != -1) {
            PokerGame& game =
                pokerGames[pokerIndex];

            // Lobby close/leave.
            if (game.phase == "LOBBY") {
                if (game.host == client.socket) {
                    for (const PokerGame::Player& player : game.players) {
                        if (player.socket == client.socket)
                            continue;
                        sendPacket(
                            player.socket,
                            "POKER_END",
                            client.name +
                            " closed the Poker table."
                        );
                        sendReady(player.socket);
                    }

                    sendPacket(
                        client.socket,
                        "POKER_END",
                        "Poker table closed."
                    );
                    sendReady(client.socket);

                    pokerGames.erase(
                        pokerGames.begin() +
                        pokerIndex
                    );
                    return;
                }

                // A guest can leave without closing the host's lobby.
                int playerIndex = pokerPlayerIndex(game, client.socket);
                if (playerIndex >= 0)
                    game.players.erase(game.players.begin() + playerIndex);
                game.status =
                    client.name +
                    " left the Poker table.";

                sendPacket(
                    client.socket,
                    "POKER_END",
                    "You left the Poker table."
                );
                sendReady(client.socket);

                sendPokerLobbyState(game);
                return;
            }

            removePokerPlayerFromMatch(
                pokerIndex,
                client.socket,
                client.name,
                true
            );
            return;
        }

        sendPacket(
            client.socket,
            "ERR",
            "You are not currently in a game."
        );
    }


    // --------------------------------------------------------
    // /help
    // --------------------------------------------------------

    else if (command == "/help") {
        sendPacket(client.socket, "SYS", "========== JENG CHAT COMMANDS ==========");
        sendPacket(client.socket, "SYS", "CHAT");
        sendPacket(client.socket, "SYS", "/users");
        sendPacket(client.socket, "SYS", "/quit");
        sendPacket(client.socket, "SYS", "");
        sendPacket(client.socket, "SYS", "TIC-TAC-TOE");
        sendPacket(client.socket, "SYS", "/ttt <username>");
        sendPacket(client.socket, "SYS", "/move <1-9>");
        sendPacket(client.socket, "SYS", "/board");
        sendPacket(client.socket, "SYS", "/resign");
        sendPacket(client.socket, "SYS", "");
        sendPacket(client.socket, "SYS", "CHESS");
        sendPacket(client.socket, "SYS", "/chess <username>");
        sendPacket(client.socket, "SYS", "/board");
        sendPacket(client.socket, "SYS", "/resign");
        sendPacket(client.socket, "SYS", "");
        sendPacket(client.socket, "SYS", "BLACKJACK");
        sendPacket(client.socket, "SYS", "/blackjack <username> <starting_chips> <hands>");
        sendPacket(client.socket, "SYS", "/bet <amount>");
        sendPacket(client.socket, "SYS", "/hit");
        sendPacket(client.socket, "SYS", "/stand");
        sendPacket(client.socket, "SYS", "/double");
        sendPacket(client.socket, "SYS", "/split");
        sendPacket(client.socket, "SYS", "/bjnext");
        sendPacket(client.socket, "SYS", "/bjstatus");
        sendPacket(client.socket, "SYS", "/resign");
        sendPacket(client.socket, "SYS", "");
        sendPacket(client.socket, "SYS", "POKER");
        sendPacket(client.socket, "SYS", "/pokercreate <starting_chips> <small_blind>");
        sendPacket(client.socket, "SYS", "/poker <username>");
        sendPacket(client.socket, "SYS", "/pokerstart");
        sendPacket(client.socket, "SYS", "/pokercheck");
        sendPacket(client.socket, "SYS", "/pokercall");
        sendPacket(client.socket, "SYS", "/pokerraise <total_bet>");
        sendPacket(client.socket, "SYS", "/pokerfold");
        sendPacket(client.socket, "SYS", "/pokernext");
        sendPacket(client.socket, "SYS", "/pokerend");
        sendPacket(client.socket, "SYS", "/resign");
        sendPacket(client.socket, "SYS", "");
        sendPacket(client.socket, "SYS", "CHALLENGES");
        sendPacket(client.socket, "SYS", "/accept");
        sendPacket(client.socket, "SYS", "/decline");
        sendPacket(client.socket, "SYS", "========================================");
    }

    else {
        sendPacket(
            client.socket,
            "ERR",
            "Unknown command. Type /help"
        );
    }
}


// ============================================================
// DISCONNECT
// ============================================================

void disconnectClient(int index) {
    Socket socket = clients[index].socket;
    string name = clients[index].name;

    // If this user was the target of a pending challenge,
    // tell the challenger that it is gone.
    if (clients[index].pendingChallenge != INVALID_SOCK) {
        Client* challenger = getClient(
            clients[index].pendingChallenge
        );

        if (challenger) {
            string pendingGame =
                clients[index].pendingGame;

            string packetType =
                pendingGame == "blackjack"
                ? "BJ_NOTICE"
                : (
                    pendingGame == "roulette"
                    ? "RLT_NOTICE"
                    : (
                        pendingGame == "poker"
                        ? "POKER_NOTICE"
                        : (
                            pendingGame == "arena"
                            ? "ARENA_NOTICE"
                            : "GAME"
                          )
                      )
                  );

            sendPacket(
                challenger->socket,
                packetType,
                (
                    pendingGame == "arena"
                    ? "Arena invitation cancelled because "
                    : "Challenge cancelled because "
                ) +
                name +
                " disconnected."
            );

            sendReady(challenger->socket);
        }
    }

    int ticTacToeIndex = findTicTacToeGame(socket);

    if (ticTacToeIndex != -1) {
        TicTacToeGame game =
            ticTacToeGames[ticTacToeIndex];

        Socket opponent =
            game.playerX == socket
            ? game.playerO
            : game.playerX;

        sendPacket(
            opponent,
            "GAME",
            name +
            " disconnected. Tic-Tac-Toe ended."
        );

        sendReady(opponent);

        ticTacToeGames.erase(
            ticTacToeGames.begin() + ticTacToeIndex
        );
    }

    int chessIndex = findChessGame(socket);

    if (chessIndex != -1) {
        ChessGame& game = chessGames[chessIndex];

        if (chessIsSpectator(game, socket)) {
            game.spectators.erase(
                remove(
                    game.spectators.begin(),
                    game.spectators.end(),
                    socket
                ),
                game.spectators.end()
            );
            sendChessState(game);
            readyChessPlayers(game);
        }
        else {
            sendChessEnd(
                game,
                name +
                " disconnected. Chess game ended."
            );
            readyChessPlayers(game);

            chessGames.erase(
                chessGames.begin() + chessIndex
            );
        }
    }

    int blackjackIndex = findBlackjackGame(socket);

    if (blackjackIndex != -1) {
        BlackjackGame& game =
            blackjackGames[blackjackIndex];

        if (game.host == socket) {
            vector<Socket> remaining;

            for (const BlackjackPlayer& player : game.players) {
                if (player.socket != socket)
                    remaining.push_back(player.socket);
            }

            for (Socket playerSocket : remaining) {
                sendPacket(
                    playerSocket,
                    "BJ_END",
                    name +
                    " disconnected. The Blackjack table closed."
                );
                sendReady(playerSocket);
            }

            blackjackGames.erase(
                blackjackGames.begin() + blackjackIndex
            );
        }
        else {
            bool wasTurn = game.turn == socket;
            int playerIndex = blackjackPlayerIndex(game, socket);

            if (playerIndex >= 0) {
                game.players.erase(
                    game.players.begin() + playerIndex
                );
            }

            game.status =
                name +
                " disconnected from the Blackjack table.";

            if (game.players.empty()) {
                blackjackGames.erase(
                    blackjackGames.begin() + blackjackIndex
                );
            }
            else if (wasTurn && game.phase == "PLAYING") {
                game.turn = INVALID_SOCK;
                game.turnHandIndex = 0;
                advanceBlackjackTurn(blackjackIndex);
            }
            else {
                sendBlackjackState(game);
                readyBlackjackPlayers(game);
            }
        }
    }


    int rouletteIndex =
        findRouletteGame(socket);

    if (rouletteIndex != -1) {
        RouletteGame& game =
            rouletteGames[rouletteIndex];

        if (game.host == socket) {
            vector<Socket> remaining;

            for (const RoulettePlayer& player : game.players) {
                if (player.socket != socket)
                    remaining.push_back(player.socket);
            }

            string result =
                name +
                " disconnected. The Roulette table closed.";

            for (Socket playerSocket : remaining) {
                sendPacket(
                    playerSocket,
                    "RLT_END",
                    result
                );

                sendReady(playerSocket);
            }

            rouletteGames.erase(
                rouletteGames.begin() +
                rouletteIndex
            );
        }
        else {
            int playerIndex =
                roulettePlayerIndex(
                    game,
                    socket
                );

            if (playerIndex >= 0) {
                game.players.erase(
                    game.players.begin() +
                    playerIndex
                );
            }

            game.status =
                name +
                " disconnected from the Roulette table.";

            if (game.players.empty()) {
                rouletteGames.erase(
                    rouletteGames.begin() +
                    rouletteIndex
                );
            }
            else {
                sendRouletteState(game);
                readyRoulettePlayers(game);
            }
        }
    }

    int pokerIndex = findPokerGame(socket);

    if (pokerIndex != -1) {
        PokerGame& game =
            pokerGames[pokerIndex];

        if (game.phase == "LOBBY") {
            if (game.host == socket) {
                for (const PokerGame::Player& player : game.players) {
                    if (player.socket == socket)
                        continue;
                    sendPacket(
                        player.socket,
                        "POKER_END",
                        name +
                        " disconnected. Poker table closed."
                    );
                    sendReady(player.socket);
                }

                pokerGames.erase(
                    pokerGames.begin() +
                    pokerIndex
                );
            }
            else {
                int playerIndex = pokerPlayerIndex(game, socket);
                if (playerIndex >= 0)
                    game.players.erase(game.players.begin() + playerIndex);

                game.status =
                    name +
                    " disconnected from the Poker table.";

                sendPokerLobbyState(game);
            }
        }
        else {
            removePokerPlayerFromMatch(
                pokerIndex,
                socket,
                name,
                false
            );
        }
    }


    int arenaIndex = findArenaGame(socket);

    if (arenaIndex != -1) {
        ArenaGame& game =
            arenaGames[arenaIndex];

        if (game.phase == "PLAYING") {
            ArenaGame copy = game;

            for (const ArenaPlayer& player : copy.players) {
                if (player.socket == socket)
                    continue;

                sendPacket(
                    player.socket,
                    "ARENA_END",
                    name +
                    " disconnected. Online Arena match ended."
                );
                sendReady(player.socket);
            }

            arenaGames.erase(
                arenaGames.begin() +
                arenaIndex
            );
        }
        else if (game.host == socket) {
            ArenaGame copy = game;

            for (const ArenaPlayer& player : copy.players) {
                if (player.socket == socket)
                    continue;

                sendPacket(
                    player.socket,
                    "ARENA_END",
                    name +
                    " disconnected. Arena lobby closed."
                );

                sendReady(player.socket);
            }

            arenaGames.erase(
                arenaGames.begin() +
                arenaIndex
            );
        }
        else {
            int playerIndex =
                arenaPlayerIndex(
                    game,
                    socket
                );

            if (playerIndex >= 0) {
                game.players.erase(
                    game.players.begin() +
                    playerIndex
                );
            }

            game.status =
                name +
                " disconnected from the Arena lobby.";

            sendArenaState(game);
            readyArenaPlayers(game);
        }
    }


    // Cancel any challenges/invites that this user had sent.
    for (Client& c : clients) {
        if (c.pendingChallenge == socket) {
            bool blackjackInvite = c.pendingGame == "blackjack";
            bool rouletteInvite = c.pendingGame == "roulette";
            bool pokerInvite = c.pendingGame == "poker";
            bool arenaInvite = c.pendingGame == "arena";
            clearPendingChallenge(c);

            sendPacket(
                c.socket,
                blackjackInvite
                    ? "BJ_NOTICE"
                    : (
                        rouletteInvite
                        ? "RLT_NOTICE"
                        : (
                            pokerInvite
                            ? "POKER_NOTICE"
                            : (arenaInvite ? "ARENA_NOTICE" : "GAME")
                          )
                      ),
                blackjackInvite
                    ? "Blackjack invitation cancelled because the host disconnected."
                    : (
                        rouletteInvite
                        ? "Roulette invitation cancelled because the host disconnected."
                        : (
                            pokerInvite
                            ? "Poker invitation cancelled because the host disconnected."
                            : (
                                arenaInvite
                                ? "Arena invitation cancelled because the host disconnected."
                                : "Challenge cancelled because the challenger disconnected."
                              )
                          )
                      )
            );

            sendReady(c.socket);
        }
    }

    CLOSE_SOCKET(socket);

    clients.erase(
        clients.begin() + index
    );

    if (!name.empty()) {
        cout
            << name
            << " disconnected.\n";

        broadcastSystem(
            "*** " +
            name +
            " left the chat ***"
        );
    }
}


// ============================================================
// MAIN
// ============================================================

int main() {
    if (!initializeSocketLibrary()) {
        cout << "Socket initialization failed.\n";
        return 1;
    }

    Socket serverSocket = socket(
        AF_INET,
        SOCK_STREAM,
        0
    );

    if (serverSocket == INVALID_SOCK) {
        cout << "Could not create server socket.\n";
        cleanupSocketLibrary();
        return 1;
    }

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(PORT);
    address.sin_addr.s_addr = INADDR_ANY;

    if (
        bind(
            serverSocket,
            (sockaddr*)&address,
            sizeof(address)
        ) == SOCKET_ERR
    ) {
        cout << "Bind failed.\n";
        CLOSE_SOCKET(serverSocket);
        cleanupSocketLibrary();
        return 1;
    }

    if (
        listen(
            serverSocket,
            SOMAXCONN
        ) == SOCKET_ERR
    ) {
        cout << "Listen failed.\n";
        CLOSE_SOCKET(serverSocket);
        cleanupSocketLibrary();
        return 1;
    }

    cout << "================================\n";
    cout << "        JENG CHAT SERVER\n";
    cout << "================================\n";
    cout << "Port: " << PORT << "\n";
    cout << "Games: Tic-Tac-Toe + Blackjack + Chess + Poker + Roulette + JENG Arena\n";
    cout << "Waiting for players...\n\n";

    while (true) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(serverSocket, &readSet);

        Socket maxSocket = serverSocket;

        for (Client& c : clients) {
            FD_SET(
                c.socket,
                &readSet
            );

            if (c.socket > maxSocket)
                maxSocket = c.socket;
        }

#ifdef _WIN32
        int nfds = 0; // Ignored by Winsock.
#else
        int nfds = maxSocket + 1;
#endif

        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 16000; // ~16 ms timer tick for realtime Arena sync

        if (
            select(
                nfds,
                &readSet,
                nullptr,
                nullptr,
                &timeout
            ) == SOCKET_ERR
        ) {
            break;
        }

        // Handle delayed Blackjack deals even when no socket traffic
        // arrives during this loop iteration.
        processBlackjackDealTimers();
        processArenaRealtimeTick();

        // New connection.
        if (FD_ISSET(serverSocket, &readSet)) {
            Socket newClient = accept(
                serverSocket,
                nullptr,
                nullptr
            );

            if (newClient != INVALID_SOCK) {
                Client c;
                c.socket = newClient;

                clients.push_back(c);

                cout << "New connection received.\n";
            }
        }

        // Existing clients.
        for (int i = 0; i < (int)clients.size();) {
            if (!FD_ISSET(clients[i].socket, &readSet)) {
                i++;
                continue;
            }

            char buffer[BUFFER_SIZE];

            int received = recv(
                clients[i].socket,
                buffer,
                BUFFER_SIZE,
                0
            );

            if (received <= 0) {
                disconnectClient(i);
                continue;
            }

            clients[i].inputBuffer.append(
                buffer,
                received
            );

            while (true) {
                size_t newline =
                    clients[i]
                    .inputBuffer
                    .find('\n');

                if (newline == string::npos)
                    break;

                string line =
                    clients[i]
                    .inputBuffer
                    .substr(
                        0,
                        newline
                    );

                clients[i]
                .inputBuffer
                .erase(
                    0,
                    newline + 1
                );

                if (
                    !line.empty() &&
                    line.back() == '\r'
                ) {
                    line.pop_back();
                }

                // First line from a connection is the username.
                if (clients[i].name.empty()) {
                    clients[i].name = line;

                    cout
                        << line
                        << " connected.\n";

                    sendPacket(
                        clients[i].socket,
                        "SYS",
                        "*** Welcome to JENG CHAT, " +
                        line +
                        "! ***"
                    );

                    sendPacket(
                        clients[i].socket,
                        "SYS",
                        "Type /help for commands."
                    );

                    broadcastSystem(
                        "*** " +
                        line +
                        " joined the chat ***"
                    );

                    continue;
                }

                handleCommand(
                    clients[i],
                    line
                );

                // The sender can type again after the response.
                // Some game helpers also READY both players;
                // duplicate READY packets are harmless because
                // the client suppresses duplicate prompts.
                sendReady(
                    clients[i].socket
                );
            }

            i++;
        }
    }

    for (Client& c : clients) {
        CLOSE_SOCKET(c.socket);
    }

    CLOSE_SOCKET(serverSocket);
    cleanupSocketLibrary();

    return 0;
}
