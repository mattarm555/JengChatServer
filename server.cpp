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
    Socket player1 = INVALID_SOCK;
    Socket player2 = INVALID_SOCK;

    int startingChips = 0;
    string phase = "LOBBY";
    string status = "Table created. Invite a player.";

    int player1Chips = 0;
    int player2Chips = 0;

    int smallBlind = 0;
    int bigBlind = 0;

    Socket dealer = INVALID_SOCK;
    Socket turn = INVALID_SOCK;

    vector<Card> deck;
    vector<Card> player1Hole;
    vector<Card> player2Hole;
    vector<Card> community;

    int pot = 0;
    int player1RoundBet = 0;
    int player2RoundBet = 0;
    int currentBet = 0;
    int lastRaiseSize = 0;

    bool player1Acted = false;
    bool player2Acted = false;

    bool handActive = false;
    int handNumber = 0;
    PokerStage stage = PokerStage::PREFLOP;
};

vector<Client> clients;
vector<TicTacToeGame> ticTacToeGames;
vector<BlackjackGame> blackjackGames;
vector<ChessGame> chessGames;
vector<PokerGame> pokerGames;
vector<RouletteGame> rouletteGames;

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
    }

    return -1;
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
        if (
            pokerGames[i].player1 == socket ||
            pokerGames[i].player2 == socket
        ) {
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
        findRouletteGame(socket) != -1;
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
        : "BLACK";

    string data =
        encodeChessBoard(game) + "|" +
        getName(game.white) + "|" +
        getName(game.black) + "|" +
        getName(game.turn) + "|" +
        yourColor;

    sendPacket(socket, "CHESS_STATE", data);
}

void sendChessState(ChessGame& game) {
    sendChessStateTo(game, game.white);
    sendChessStateTo(game, game.black);
}

void sendChessEnd(
    ChessGame& game,
    const string& text
) {
    sendPacket(game.white, "CHESS_END", text);
    sendPacket(game.black, "CHESS_END", text);
}

void readyChessPlayers(ChessGame& game) {
    sendReady(game.white);
    sendReady(game.black);
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
        Socket whiteSocket = game.white;
        Socket blackSocket = game.black;

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

        sendReady(whiteSocket);
        sendReady(blackSocket);

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
// POKER - HEADS-UP TEXAS HOLD'EM
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

Socket otherPokerPlayer(
    const PokerGame& game,
    Socket socket
) {
    return
        game.player1 == socket
        ? game.player2
        : game.player1;
}

int& pokerChips(PokerGame& game, Socket socket) {
    return
        game.player1 == socket
        ? game.player1Chips
        : game.player2Chips;
}

int& pokerRoundBet(PokerGame& game, Socket socket) {
    return
        game.player1 == socket
        ? game.player1RoundBet
        : game.player2RoundBet;
}

bool& pokerActed(PokerGame& game, Socket socket) {
    return
        game.player1 == socket
        ? game.player1Acted
        : game.player2Acted;
}

vector<Card>& pokerHole(PokerGame& game, Socket socket) {
    return
        game.player1 == socket
        ? game.player1Hole
        : game.player2Hole;
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
    string player1Name =
        game.player1 == INVALID_SOCK
        ? ""
        : getName(game.player1);

    string player2Name =
        game.player2 == INVALID_SOCK
        ? ""
        : getName(game.player2);

    string data =
        getName(game.host) + "|" +
        to_string(game.startingChips) + "|" +
        to_string(game.smallBlind) + "|" +
        to_string(game.bigBlind) + "|" +
        player1Name + "|" +
        player2Name + "|" +
        game.status;

    if (game.player1 != INVALID_SOCK) {
        sendPacket(
            game.player1,
            "POKER_LOBBY",
            data
        );

        sendReady(game.player1);
    }

    if (game.player2 != INVALID_SOCK) {
        sendPacket(
            game.player2,
            "POKER_LOBBY",
            data
        );

        sendReady(game.player2);
    }
}

void sendPokerStateTo(
    PokerGame& game,
    Socket player
) {
    Socket opponent = otherPokerPlayer(game, player);

    string state =
        pokerStageName(game.stage) + "|" +
        getName(opponent) + "|" +
        to_string(pokerChips(game, player)) + "|" +
        to_string(pokerChips(game, opponent)) + "|" +
        to_string(game.pot) + "|" +
        to_string(pokerRoundBet(game, player)) + "|" +
        to_string(pokerRoundBet(game, opponent)) + "|" +
        to_string(game.currentBet) + "|" +
        (game.turn == INVALID_SOCK ? string("") : getName(game.turn)) + "|" +
        getName(game.dealer) + "|" +
        to_string(game.smallBlind) + "|" +
        to_string(game.bigBlind) + "|" +
        (game.handActive ? "1" : "0") + "|" +
        to_string(game.handNumber) + "|" +
        to_string(game.lastRaiseSize);

    sendPacket(player, "POKER_STATE", state);

    vector<Card>& hole = pokerHole(game, player);

    string holeData = "--|--";

    if (hole.size() >= 2) {
        holeData =
            pokerCardCode(hole[0]) + "|" +
            pokerCardCode(hole[1]);
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
    sendPokerStateTo(game, game.player1);
    sendPokerStateTo(game, game.player2);
    sendReady(game.player1);
    sendReady(game.player2);
}

void sendPokerNotice(PokerGame& game, const string& text) {
    sendPacket(game.player1, "POKER_NOTICE", text);
    sendPacket(game.player2, "POKER_NOTICE", text);
}

void sendPokerResult(PokerGame& game, const string& text) {
    sendPacket(game.player1, "POKER_RESULT", text);
    sendPacket(game.player2, "POKER_RESULT", text);
}

void sendPokerReveal(PokerGame& game) {
    if (
        game.player1Hole.size() < 2 ||
        game.player2Hole.size() < 2
    ) {
        return;
    }

    sendPacket(
        game.player1,
        "POKER_REVEAL",
        getName(game.player2) + "|" +
        pokerCardCode(game.player2Hole[0]) + "|" +
        pokerCardCode(game.player2Hole[1])
    );

    sendPacket(
        game.player2,
        "POKER_REVEAL",
        getName(game.player1) + "|" +
        pokerCardCode(game.player1Hole[0]) + "|" +
        pokerCardCode(game.player1Hole[1])
    );
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
    if (
        game.player1Chips == 0 &&
        game.player2RoundBet > game.player1RoundBet
    ) {
        int refund =
            game.player2RoundBet -
            game.player1RoundBet;

        game.player2RoundBet -= refund;
        game.player2Chips += refund;
        game.pot -= refund;
    }

    if (
        game.player2Chips == 0 &&
        game.player1RoundBet > game.player2RoundBet
    ) {
        int refund =
            game.player1RoundBet -
            game.player2RoundBet;

        game.player1RoundBet -= refund;
        game.player1Chips += refund;
        game.pot -= refund;
    }

    game.currentBet = max(
        game.player1RoundBet,
        game.player2RoundBet
    );
}

void pokerShowdown(PokerGame& game) {
    while (game.community.size() < 5)
        game.community.push_back(drawPokerCard(game));

    normalizePokerUncalledBet(game);

    uint64_t player1Score = evaluatePokerBest(
        game.player1Hole,
        game.community
    );

    uint64_t player2Score = evaluatePokerBest(
        game.player2Hole,
        game.community
    );

    sendPokerReveal(game);

    string result;

    if (player1Score > player2Score) {
        game.player1Chips += game.pot;
        result =
            getName(game.player1) +
            " wins " +
            to_string(game.pot) +
            " chips with " +
            pokerHandName(player1Score) +
            ".";
    }
    else if (player2Score > player1Score) {
        game.player2Chips += game.pot;
        result =
            getName(game.player2) +
            " wins " +
            to_string(game.pot) +
            " chips with " +
            pokerHandName(player2Score) +
            ".";
    }
    else {
        int half = game.pot / 2;
        int oddChip = game.pot % 2;

        game.player1Chips += half;
        game.player2Chips += half;
        pokerChips(game, game.dealer) += oddChip;

        result =
            "Split pot - both players have " +
            pokerHandName(player1Score) +
            ".";
    }

    game.pot = 0;
    game.handActive = false;
    game.turn = INVALID_SOCK;
    game.stage = PokerStage::SHOWDOWN;

    sendPokerState(game);
    sendPokerResult(game, result);
}

void runOutPokerBoard(PokerGame& game) {
    while (game.community.size() < 5)
        game.community.push_back(drawPokerCard(game));

    pokerShowdown(game);
}

void startPokerHand(PokerGame& game) {
    game.phase = "PLAYING";
    game.handNumber++;
    game.stage = PokerStage::PREFLOP;
    game.handActive = true;
    game.turn = INVALID_SOCK;

    game.deck = makePokerDeck();
    game.player1Hole.clear();
    game.player2Hole.clear();
    game.community.clear();

    game.pot = 0;
    game.player1RoundBet = 0;
    game.player2RoundBet = 0;
    game.currentBet = 0;
    game.lastRaiseSize = game.bigBlind;
    game.player1Acted = false;
    game.player2Acted = false;

    game.player1Hole.push_back(drawPokerCard(game));
    game.player2Hole.push_back(drawPokerCard(game));
    game.player1Hole.push_back(drawPokerCard(game));
    game.player2Hole.push_back(drawPokerCard(game));

    Socket smallBlindPlayer = game.dealer;
    Socket bigBlindPlayer = otherPokerPlayer(
        game,
        game.dealer
    );

    int smallAmount = min(
        game.smallBlind,
        pokerChips(game, smallBlindPlayer)
    );

    pokerChips(game, smallBlindPlayer) -= smallAmount;
    pokerRoundBet(game, smallBlindPlayer) += smallAmount;
    game.pot += smallAmount;

    int bigAmount = min(
        game.bigBlind,
        pokerChips(game, bigBlindPlayer)
    );

    pokerChips(game, bigBlindPlayer) -= bigAmount;
    pokerRoundBet(game, bigBlindPlayer) += bigAmount;
    game.pot += bigAmount;

    normalizePokerUncalledBet(game);

    game.currentBet = max(
        game.player1RoundBet,
        game.player2RoundBet
    );

    game.player1Acted = game.player1Chips == 0;
    game.player2Acted = game.player2Chips == 0;

    if (
        game.player1Chips == 0 ||
        game.player2Chips == 0
    ) {
        runOutPokerBoard(game);
        return;
    }

    // Heads-up: dealer / small blind acts first preflop.
    game.turn = game.dealer;

    sendPokerState(game);
    sendPokerNotice(
        game,
        "Hand " +
        to_string(game.handNumber) +
        " - cards dealt."
    );
}

void advancePokerStreet(PokerGame& game) {
    game.player1RoundBet = 0;
    game.player2RoundBet = 0;
    game.currentBet = 0;
    game.lastRaiseSize = game.bigBlind;
    game.player1Acted = game.player1Chips == 0;
    game.player2Acted = game.player2Chips == 0;

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

    if (
        game.player1Chips == 0 ||
        game.player2Chips == 0
    ) {
        runOutPokerBoard(game);
        return;
    }

    // Heads-up: the non-dealer acts first after the flop.
    game.turn = otherPokerPlayer(
        game,
        game.dealer
    );

    sendPokerState(game);
}

void finishPokerAction(
    PokerGame& game,
    Socket actor
) {
    normalizePokerUncalledBet(game);

    if (
        game.player1RoundBet == game.player2RoundBet &&
        (
            game.player1Chips == 0 ||
            game.player2Chips == 0
        )
    ) {
        runOutPokerBoard(game);
        return;
    }

    bool roundComplete =
        game.player1Acted &&
        game.player2Acted &&
        game.player1RoundBet == game.player2RoundBet;

    if (roundComplete) {
        advancePokerStreet(game);
        return;
    }

    Socket next = otherPokerPlayer(game, actor);

    if (pokerChips(game, next) == 0) {
        game.turn = actor;
    }
    else {
        game.turn = next;
    }

    sendPokerState(game);
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

        int startingChips = stoi(fields[0]);
        int rounds = stoi(fields[1]);

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
    // Create the table first, then invite a player from the lobby.
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
        game.player1 = client.socket;
        game.player2 = INVALID_SOCK;
        game.startingChips = startingChips;
        game.player1Chips = startingChips;
        game.player2Chips = startingChips;
        game.smallBlind = smallBlind;
        game.bigBlind = bigBlind;
        game.dealer = client.socket;
        game.phase = "LOBBY";
        game.status =
            "Table created. Invite a player to join.";

        pokerGames.push_back(game);

        sendPokerLobbyState(
            pokerGames.back()
        );

        return;
    }


    // --------------------------------------------------------
    // /poker <username>
    // Invite a player to an already-created Poker table.
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

        if (
            game.host != client.socket ||
            game.phase != "LOBBY"
        ) {
            sendPacket(
                client.socket,
                "ERR",
                "Only the host can invite a player before the Poker match starts."
            );
            return;
        }

        if (game.player2 != INVALID_SOCK) {
            sendPacket(
                client.socket,
                "ERR",
                "This heads-up Poker table already has two players."
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

        sendPokerLobbyState(game);
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

        if (game.player2 == INVALID_SOCK) {
            sendPacket(
                client.socket,
                "ERR",
                "Invite a player before starting the Poker match."
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
        int startingChips = client.pendingChips;
        int hands = client.pendingHands;
        int pokerSmallBlind = client.pendingHands;

        Socket challengerSocket = challenger->socket;
        Socket accepterSocket = client.socket;

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

            if (
                game.host != challengerSocket ||
                game.phase != "LOBBY"
            ) {
                clearPendingChallenge(client);

                sendPacket(
                    accepterSocket,
                    "ERR",
                    "That Poker table has already started."
                );
                return;
            }

            if (game.player2 != INVALID_SOCK) {
                clearPendingChallenge(client);

                sendPacket(
                    accepterSocket,
                    "ERR",
                    "That Poker table is already full."
                );
                return;
            }

            game.player2 =
                accepterSocket;

            game.player2Chips =
                game.startingChips;

            clearPendingChallenge(client);

            game.status =
                client.name +
                " joined the Poker table. Host can start the match.";

            sendPokerLobbyState(game);
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
        else if (client.pendingGame == "chess")
            gameName = "Chess";
        else if (client.pendingGame == "poker")
            gameName = "Poker";
        else if (client.pendingGame == "roulette")
            gameName = "Roulette";
        else
            gameName = "Tic-Tac-Toe";

        if (challenger) {
            if (client.pendingGame == "blackjack") {
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
        bool wasRoulette = client.pendingGame == "roulette";
        bool wasPoker = client.pendingGame == "poker";
        clearPendingChallenge(client);

        sendPacket(
            client.socket,
            wasBlackjack
                ? "BJ_NOTICE"
                : (
                    wasRoulette
                    ? "RLT_NOTICE"
                    : (wasPoker ? "POKER_NOTICE" : "GAME")
                  ),
            wasBlackjack
                ? "Blackjack invitation declined."
                : (
                    wasRoulette
                    ? "Roulette invitation declined."
                    : (
                        wasPoker
                        ? "Poker invitation declined."
                        : "Challenge declined."
                      )
                  )
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

        if (pokerRoundBet(game, client.socket) != game.currentBet) {
            sendPacket(client.socket, "ERR", "You cannot check while facing a bet.");
            return;
        }

        pokerActed(game, client.socket) = true;
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

        int amount =
            game.currentBet -
            pokerRoundBet(game, client.socket);

        if (amount <= 0) {
            sendPacket(client.socket, "ERR", "There is nothing to call.");
            return;
        }

        int paid = min(
            amount,
            pokerChips(game, client.socket)
        );

        pokerChips(game, client.socket) -= paid;
        pokerRoundBet(game, client.socket) += paid;
        game.pot += paid;
        pokerActed(game, client.socket) = true;

        normalizePokerUncalledBet(game);

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

        Socket opponent = otherPokerPlayer(game, client.socket);

        int maximum = min(
            pokerRoundBet(game, client.socket) +
                pokerChips(game, client.socket),
            pokerRoundBet(game, opponent) +
                pokerChips(game, opponent)
        );

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
        int payment =
            target -
            pokerRoundBet(game, client.socket);

        pokerChips(game, client.socket) -= payment;
        pokerRoundBet(game, client.socket) = target;
        game.pot += payment;
        game.currentBet = target;

        int raiseSize = target - oldCurrentBet;

        if (raiseSize >= game.lastRaiseSize)
            game.lastRaiseSize = raiseSize;

        pokerActed(game, client.socket) = true;
        pokerActed(game, opponent) = pokerChips(game, opponent) == 0;

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

        Socket winner = otherPokerPlayer(game, client.socket);
        int won = game.pot;

        pokerChips(game, winner) += game.pot;
        game.pot = 0;
        game.handActive = false;
        game.turn = INVALID_SOCK;
        game.stage = PokerStage::SHOWDOWN;

        sendPokerState(game);
        sendPokerResult(
            game,
            client.name +
            " folds. " +
            getName(winner) +
            " wins " +
            to_string(won) +
            " chips."
        );
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

        if (game.player1Chips <= 0 || game.player2Chips <= 0) {
            Socket winner =
                game.player1Chips > game.player2Chips
                ? game.player1
                : game.player2;

            string result =
                getName(winner) +
                " wins the Poker match!";

            sendPacket(game.player1, "POKER_END", result);
            sendPacket(game.player2, "POKER_END", result);
            sendReady(game.player1);
            sendReady(game.player2);

            pokerGames.erase(
                pokerGames.begin() + gameIndex
            );

            return;
        }

        game.dealer = otherPokerPlayer(
            game,
            game.dealer
        );

        startPokerHand(game);
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

            Socket opponent =
                game.white == client.socket
                ? game.black
                : game.white;

            Socket white = game.white;
            Socket black = game.black;

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

            sendReady(white);
            sendReady(black);

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
                    if (game.player2 != INVALID_SOCK) {
                        sendPacket(
                            game.player2,
                            "POKER_END",
                            client.name +
                            " closed the Poker table."
                        );
                        sendReady(game.player2);
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

                // Guest leaves: keep host's lobby alive.
                game.player2 = INVALID_SOCK;
                game.player2Chips =
                    game.startingChips;
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

            Socket opponent =
                otherPokerPlayer(
                    game,
                    client.socket
                );

            string result =
                client.name +
                " resigned from Poker. " +
                getName(opponent) +
                " wins the match.";

            sendPacket(
                game.player1,
                "POKER_END",
                result
            );

            sendPacket(
                game.player2,
                "POKER_END",
                result
            );

            sendReady(game.player1);
            sendReady(game.player2);

            pokerGames.erase(
                pokerGames.begin() +
                pokerIndex
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
            sendPacket(
                challenger->socket,
                "GAME",
                "Challenge cancelled because " +
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
        ChessGame game = chessGames[chessIndex];

        Socket opponent =
            game.white == socket
            ? game.black
            : game.white;

        sendPacket(
            opponent,
            "CHESS_END",
            name +
            " disconnected. Chess game ended."
        );

        sendReady(opponent);

        chessGames.erase(
            chessGames.begin() + chessIndex
        );
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
                if (game.player2 != INVALID_SOCK) {
                    sendPacket(
                        game.player2,
                        "POKER_END",
                        name +
                        " disconnected. Poker table closed."
                    );
                    sendReady(game.player2);
                }

                pokerGames.erase(
                    pokerGames.begin() +
                    pokerIndex
                );
            }
            else {
                game.player2 =
                    INVALID_SOCK;

                game.player2Chips =
                    game.startingChips;

                game.status =
                    name +
                    " disconnected from the Poker table.";

                sendPokerLobbyState(game);
            }
        }
        else {
            PokerGame gameCopy =
                game;

            Socket opponent =
                otherPokerPlayer(
                    gameCopy,
                    socket
                );

            string result =
                name +
                " disconnected. Poker match ended.";

            sendPacket(
                opponent,
                "POKER_END",
                result
            );
            sendReady(opponent);

            pokerGames.erase(
                pokerGames.begin() +
                pokerIndex
            );
        }
    }

    // Cancel any challenges/invites that this user had sent.
    for (Client& c : clients) {
        if (c.pendingChallenge == socket) {
            bool blackjackInvite = c.pendingGame == "blackjack";
            bool rouletteInvite = c.pendingGame == "roulette";
            bool pokerInvite = c.pendingGame == "poker";
            clearPendingChallenge(c);

            sendPacket(
                c.socket,
                blackjackInvite
                    ? "BJ_NOTICE"
                    : (
                        rouletteInvite
                        ? "RLT_NOTICE"
                        : (pokerInvite ? "POKER_NOTICE" : "GAME")
                      ),
                blackjackInvite
                    ? "Blackjack invitation cancelled because the host disconnected."
                    : (
                        rouletteInvite
                        ? "Roulette invitation cancelled because the host disconnected."
                        : (
                            pokerInvite
                            ? "Poker invitation cancelled because the host disconnected."
                            : "Challenge cancelled because the challenger disconnected."
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
    cout << "Games: Tic-Tac-Toe + Blackjack + Chess\n";
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
        timeout.tv_usec = 100000; // 100 ms timer tick

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
