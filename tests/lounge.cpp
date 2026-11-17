// Lounge presence relay (lounge-v1) checks. See LOUNGE_PROTOCOL.md.
#define main jengchat_server_main
#include "../server.cpp"
#undef main
#include <stdexcept>
void require(bool x, const char* why) { if (!x) throw runtime_error(why); }
void reset() { clients.clear(); }
void add(Socket socket, string name, bool capable = true) {
    Client c; c.socket = socket; c.name = name; c.tlsReady = true; c.loungeCapable = capable;
    clients.push_back(std::move(c));
}
int count(Socket socket, const string& text) {
    int n = 0;
    for (auto& x : getClient(socket)->output) if (x.find(text) != string::npos) ++n;
    return n;
}
bool has(Socket socket, const string& text) { return count(socket, text) > 0; }
bool any(Socket socket, const string& prefix) {
    for (auto& x : getClient(socket)->output) if (x.rfind(prefix, 0) == 0) return true;
    return false;
}
void clearOutput() { for (auto& c : clients) { c.output.clear(); c.outputBytes = 0; } }
using Clock = chrono::steady_clock;

int main() { try {
    // Canonical positions: numbers are bounded plain decimals, anim is one of three words.
    require(lounge::canonicalPosition("1.5|-2|3.141592653|walk").empty(), "too-long yaw accepted");
    require(lounge::canonicalPosition("1.5|-2|3.14|walk") == "1.50|-2.00|3.14|walk", "position not canonical");
    require(lounge::canonicalPosition("-0|0|0|idle") == "0.00|0.00|0.00|idle", "negative zero kept");
    for (string bad : {"", "1|2|3", "1|2|3|walk|x", "13|0|0|idle", "0|-9.6|0|idle", "0|0|4|idle", "0|0|0|run",
                       "1e1|0|0|idle", "0x1|0|0|idle", "nan|0|0|idle", "|0|0|idle", "1.2.3|0|0|idle", "--1|0|0|idle", "0|0|0|walk\n"})
        require(lounge::canonicalPosition(bad).empty(), ("bad position accepted: " + bad).c_str());

    // The capability probe is answered; old clients never see a LOUNGE_ packet.
    reset(); add(100, "Amy", false); add(101, "Bob", false); add(102, "Cat", false);
    handleRecovery(*getClient(100), "CLIENT_CAPS|lounge-v1");
    require(getClient(100)->loungeCapable && has(100, "LOUNGE_CAPS|lounge-v1"), "probe not answered");
    handleRecovery(*getClient(101), "CLIENT_CAPS|lounge-v1");
    clearOutput();

    // Entering: positions relay to others in the lounge, with the server's name for the sender.
    lounge::handle(*getClient(100), "LOUNGE_ENTER|");
    lounge::handle(*getClient(100), "LOUNGE_POS|1|2|0.5|walk");
    require(!any(101, "LOUNGE_"), "position sent to a client that is not in the lounge");
    require(!any(102, "LOUNGE_"), "position sent to a legacy client");
    lounge::handle(*getClient(101), "LOUNGE_ENTER|");
    require(has(101, "LOUNGE_POS|Amy|1.00|2.00|0.50|walk"), "newcomer did not get the snapshot");
    require(!has(100, "LOUNGE_POS|Bob"), "an avatar without a position was announced");
    lounge::handle(*getClient(101), "LOUNGE_POS|-3|4|0|idle");
    require(has(100, "LOUNGE_POS|Bob|-3.00|4.00|0.00|idle"), "position not relayed");
    require(!any(102, "LOUNGE_"), "legacy client got lounge traffic");
    require(!has(101, "LOUNGE_POS|Bob"), "sender got its own position back");

    // Spoofing and junk: the name is never the client's to choose; bad lines are dropped silently.
    clearOutput();
    lounge::handle(*getClient(100), "LOUNGE_POS|Mallory|1|2|0|idle");
    lounge::handle(*getClient(100), "LOUNGE_POS|99|2|0|idle");
    lounge::handle(*getClient(100), "LOUNGE_EMOTE|moonwalk");
    lounge::handle(*getClient(100), "LOUNGE_EMOTE|wave|extra");
    require(!any(101, "LOUNGE_") && !any(100, "ERR"), "invalid lounge lines were relayed or answered");

    // A legacy client's LOUNGE_ lines are swallowed, not relayed or answered.
    lounge::handle(*getClient(102), "LOUNGE_ENTER|");
    lounge::handle(*getClient(102), "LOUNGE_POS|0|0|0|idle");
    require(!getClient(102)->loungeIn && !any(101, "LOUNGE_POS|Cat") && getClient(102)->output.empty(), "legacy client joined the lounge");

    // Rate limit: a burst of 4, then 10 per second.
    reset(); add(100, "Amy"); add(101, "Bob");
    auto t = Clock::now();
    lounge::handle(*getClient(100), "LOUNGE_ENTER|", t); lounge::handle(*getClient(101), "LOUNGE_ENTER|", t);
    getClient(100)->loungeTokensAt = t;
    for (int i = 0; i < 10; ++i) lounge::handle(*getClient(100), "LOUNGE_POS|" + to_string(i) + "|0|0|walk", t);
    require(count(101, "LOUNGE_POS|Amy") == 4, "burst limit not applied");
    clearOutput();
    for (int i = 1; i <= 20; ++i) lounge::handle(*getClient(100), "LOUNGE_POS|1|1|0|walk", t + chrono::milliseconds(100 * i));
    require(count(101, "LOUNGE_POS|Amy") == 20, "steady 10 per second was limited");
    clearOutput();
    for (int i = 1; i <= 40; ++i) lounge::handle(*getClient(100), "LOUNGE_POS|1|1|0|walk", t + chrono::seconds(2) + chrono::milliseconds(25 * i));
    require(count(101, "LOUNGE_POS|Amy") <= 4 + 10 + 1, "flooding was not limited");

    // Emotes: valid ones relay, at most one per 0.8 s.
    clearOutput();
    lounge::handle(*getClient(100), "LOUNGE_EMOTE|wave", t + chrono::seconds(10));
    lounge::handle(*getClient(100), "LOUNGE_EMOTE|cheer", t + chrono::seconds(10) + chrono::milliseconds(300));
    lounge::handle(*getClient(100), "LOUNGE_EMOTE|dance", t + chrono::seconds(11));
    require(has(101, "LOUNGE_EMOTE|Amy|wave") && !has(101, "LOUNGE_EMOTE|Amy|cheer") && has(101, "LOUNGE_EMOTE|Amy|dance"), "emote relay or limit wrong");

    // A slow receiver misses positions instead of being disconnected.
    clearOutput();
    getClient(101)->outputBytes = 70 * 1024;
    lounge::handle(*getClient(100), "LOUNGE_POS|2|2|0|idle", t + chrono::seconds(20));
    require(!any(101, "LOUNGE_POS") && !getClient(101)->closeRequested, "backlogged receiver was not skipped");
    getClient(101)->outputBytes = 0;

    // Rename: the old avatar goes, the new name stands where it was.
    clearOutput();
    getClient(100)->name = "Amelia";
    lounge::renamed(*getClient(100), "Amy");
    require(has(101, "LOUNGE_GONE|Amy") && has(101, "LOUNGE_POS|Amelia|2.00|2.00|0.00|idle"), "rename not relayed");

    // Leaving and disconnecting both announce LOUNGE_GONE; positions stop.
    clearOutput();
    lounge::handle(*getClient(100), "LOUNGE_LEAVE|", t + chrono::seconds(30));
    require(has(101, "LOUNGE_GONE|Amelia") && !getClient(100)->loungeIn, "leave not announced");
    lounge::handle(*getClient(100), "LOUNGE_POS|3|3|0|idle", t + chrono::seconds(31));
    require(!has(101, "LOUNGE_POS|Amelia|3.00"), "position relayed after leaving");
    lounge::handle(*getClient(100), "LOUNGE_ENTER|", t + chrono::seconds(32));
    lounge::handle(*getClient(100), "LOUNGE_POS|3|3|0|idle", t + chrono::seconds(33));
    clearOutput();
    add(102, "Cat"); lounge::handle(*getClient(102), "LOUNGE_ENTER|", t + chrono::seconds(33));
    require(has(102, "LOUNGE_POS|Amelia|3.00|3.00"), "re-entered avatar missing from snapshot");
    clearOutput();
    for (int i = 0; i < (int)clients.size(); ++i) if (clients[i].socket == 100) { disconnectClient(i); break; }
    require(has(101, "LOUNGE_GONE|Amelia") && has(102, "LOUNGE_GONE|Amelia"), "disconnect not announced");

    cout << "Lounge relay checks passed\n";
    return 0;
} catch (const exception& e) { cerr << "FAIL: " << e.what() << "\n"; return 1; } }
