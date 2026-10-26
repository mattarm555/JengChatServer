#pragma once
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/crypto.h>
#include <chrono>
#include <map>
#include <string>
#include <stdexcept>
#include <algorithm>

// Process-local credentials: only hashes are retained. Clock injection keeps expiry tests deterministic.
class SessionStore {
public:
    using Clock=std::chrono::steady_clock;
    struct Entry { std::string account; Clock::time_point created,detached; bool active=true; };
    static std::string hash(const std::string& token) {
        if(token.size()!=64 || !std::all_of(token.begin(),token.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');})) return {};
        unsigned char bytes[SHA256_DIGEST_LENGTH]; SHA256((const unsigned char*)token.data(),token.size(),bytes);
        static const char hex[]="0123456789abcdef"; std::string result;
        for(auto b:bytes){result+=hex[b>>4];result+=hex[b&15];} return result;
    }
    void prune(Clock::time_point now=Clock::now()) {
        for(auto it=entries.begin();it!=entries.end();) {
            const auto& s=it->second;
            if(now-s.created>=std::chrono::hours(24) || (!s.active&&now-s.detached>=std::chrono::minutes(5))) it=entries.erase(it); else ++it;
        }
    }
    std::string issue(const std::string& account,Clock::time_point now=Clock::now()) {
        prune(now); if(entries.size()>=1024)return {};
        unsigned char bytes[32];if(RAND_bytes(bytes,sizeof(bytes))!=1)return {};
        static const char hex[]="0123456789abcdef";std::string token;
        for(auto b:bytes){token+=hex[b>>4];token+=hex[b&15];} OPENSSL_cleanse(bytes,sizeof(bytes));
        entries.emplace(hash(token),Entry{account,now,now,true});return token;
    }
    const Entry* find(const std::string& digest,Clock::time_point now=Clock::now()) {
        prune(now);auto it=entries.find(digest);return it==entries.end()?nullptr:&it->second;
    }
    void attach(const std::string& digest){auto it=entries.find(digest);if(it!=entries.end())it->second.active=true;}
    void detach(const std::string& digest,Clock::time_point now=Clock::now()) {
        auto it=entries.find(digest);if(it!=entries.end()&&it->second.active){it->second.active=false;it->second.detached=now;}
    }
    void revoke(const std::string& digest){entries.erase(digest);}
    void revokeAccount(const std::string& name){for(auto it=entries.begin();it!=entries.end();)if(it->second.account==name)it=entries.erase(it);else ++it;}
private:
    std::map<std::string,Entry> entries;
};
