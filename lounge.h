// Lounge presence relay (lounge-v1). See LOUNGE_PROTOCOL.md.
//
// Clients that probe CLIENT_CAPS|lounge-v1 may report where their avatar stands in the 3D lounge
// while the lounge is on their screen. The server only relays: it validates and rewrites every
// field into canonical text, uses its own account name for the sender, rate-limits each client,
// and never stores anything beyond the last position. Clients that did not probe never receive a
// LOUNGE_ packet, and their LOUNGE_ lines are swallowed without a reply.
#pragma once

namespace lounge {

// The lounge floor is ±12 by ±9 metres; a little slack covers rounding.
constexpr float HalfX = 12.5f, HalfZ = 9.5f, Pi = 3.1416f;
// Position updates: a burst of 4, refilled at 10 per second. Emotes: one per 0.8 s.
constexpr float BurstTokens = 4.0f, TokensPerSecond = 10.0f;
constexpr auto EmoteInterval = chrono::milliseconds(800);
// Positions are ephemeral: a receiver with this much unsent output simply misses some.
constexpr size_t BacklogLimit = 64 * 1024;

// A plain decimal ("-3.25", "0", "12.5"), at most 8 characters, within [lo, hi].
inline bool parseNumber(const string& s, float lo, float hi, float& out) {
    if (s.empty() || s.size() > 8) return false;
    size_t i = s[0] == '-' ? 1 : 0;
    bool digits = false, dot = false;
    for (; i < s.size(); ++i) {
        char ch = s[i];
        if (ch >= '0' && ch <= '9') digits = true;
        else if (ch == '.' && !dot) dot = true;
        else return false;
    }
    if (!digits) return false;
    float value = strtof(s.c_str(), nullptr);
    if (!(value >= lo && value <= hi)) return false;
    out = value;
    return true;
}

inline string format(float value) {
    char buffer[16];
    snprintf(buffer, sizeof(buffer), "%.2f", value);
    string text = buffer;
    return text == "-0.00" ? "0.00" : text;
}

inline bool validAnim(const string& a) { return a == "idle" || a == "walk" || a == "sit"; }
inline bool validEmote(const string& e) { return e == "wave" || e == "cheer" || e == "dance"; }

// "x|z|yaw|anim" rewritten canonically ("1.50|-2.00|3.14|walk"); empty when anything is invalid.
inline string canonicalPosition(const string& data) {
    vector<string> parts;
    size_t start = 0;
    while (parts.size() < 5) {
        size_t bar = data.find('|', start);
        parts.push_back(data.substr(start, bar == string::npos ? string::npos : bar - start));
        if (bar == string::npos) break;
        start = bar + 1;
    }
    if (parts.size() != 4) return "";
    float x, z, yaw;
    if (!parseNumber(parts[0], -HalfX, HalfX, x) || !parseNumber(parts[1], -HalfZ, HalfZ, z) ||
        !parseNumber(parts[2], -Pi, Pi, yaw) || !validAnim(parts[3]))
        return "";
    return format(x) + "|" + format(z) + "|" + format(yaw) + "|" + parts[3];
}

inline void sendTo(Client& to, const string& line) {
    if (to.outputBytes > BacklogLimit) return;
    sendAll(to.socket, line);
}

// To everyone else who is signed in, probed lounge-v1 and has the lounge on screen.
inline void broadcast(const Client& from, const string& line) {
    for (Client& c : clients)
        if (c.socket != from.socket && c.loungeCapable && c.loungeIn && !c.name.empty()) sendTo(c, line);
}

// The client's avatar leaves the others' lounges (LOUNGE_LEAVE, disconnect).
inline void leave(Client& c) {
    if (!c.loungeIn) return;
    c.loungeIn = false;
    c.loungePosition.clear();
    if (!c.name.empty()) broadcast(c, "LOUNGE_GONE|" + c.name + "\n");
}

// An account rename: the old avatar goes, the new name appears where it stood.
inline void renamed(Client& c, const string& oldName) {
    if (!c.loungeIn) return;
    if (!oldName.empty()) broadcast(c, "LOUNGE_GONE|" + oldName + "\n");
    if (!c.loungePosition.empty()) broadcast(c, "LOUNGE_POS|" + c.name + "|" + c.loungePosition + "\n");
}

// Handles a LOUNGE_ line from a signed-in client. Never replies with errors.
inline void handle(Client& c, const string& line, chrono::steady_clock::time_point now = chrono::steady_clock::now()) {
    if (!c.loungeCapable || c.name.empty()) return;
    if (line == "LOUNGE_ENTER|") {
        if (c.loungeIn) return;
        c.loungeIn = true;
        // The newcomer sees everyone already there; the others see the newcomer at its first position.
        for (Client& other : clients)
            if (other.socket != c.socket && other.loungeIn && !other.name.empty() && !other.loungePosition.empty())
                sendTo(c, "LOUNGE_POS|" + other.name + "|" + other.loungePosition + "\n");
        return;
    }
    if (line == "LOUNGE_LEAVE|") { leave(c); return; }
    if (!c.loungeIn) return;
    if (line.rfind("LOUNGE_POS|", 0) == 0) {
        float elapsed = chrono::duration<float>(now - c.loungeTokensAt).count();
        c.loungeTokens = min(BurstTokens, c.loungeTokens + max(0.0f, elapsed) * TokensPerSecond);
        c.loungeTokensAt = now;
        if (c.loungeTokens < 1.0f) return;
        string position = canonicalPosition(line.substr(11));
        if (position.empty()) return;
        c.loungeTokens -= 1.0f;
        c.loungePosition = position;
        broadcast(c, "LOUNGE_POS|" + c.name + "|" + position + "\n");
        return;
    }
    if (line.rfind("LOUNGE_EMOTE|", 0) == 0) {
        string emote = line.substr(13);
        if (!validEmote(emote) || now - c.loungeEmoteAt < EmoteInterval) return;
        c.loungeEmoteAt = now;
        broadcast(c, "LOUNGE_EMOTE|" + c.name + "|" + emote + "\n");
    }
}

}  // namespace lounge
