#include "accounts.h"
#include <sodium.h>
#include <sqlite3.h>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <stdexcept>

bool ValidAccountName(const std::string& name) {
    if (name.size() < 3 || name.size() > 16) return false;
    for (unsigned char c : name)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    return true;
}
bool ValidAccountPassword(const std::string& password) {
    if (password.size() < 8 || password.size() > 128) return false;
    for (unsigned char c : password) if (c < 32 || c > 126) return false;
    return true;
}
struct Accounts::Impl {
    enum class Action { CREATE, LOGIN, CHANGE_USERNAME, CHANGE_PASSWORD, DELETE_ACCOUNT };
    struct Job {
        Action action;
        std::string name;
        std::string password;
        std::string replacement;
        std::promise<AccountResult> result;
    };
    sqlite3* db = nullptr;
    std::string dummy;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<Job> jobs;
    bool stopping = false;
    std::thread worker;

    AccountResult process(Job& job) {
        if (!ValidAccountName(job.name) || !ValidAccountPassword(job.password)) {
            bool signingIn = job.action == Action::CREATE || job.action == Action::LOGIN;
            return {false, "", signingIn
                ? "Use a 3-16 character username and an 8-128 character password."
                : "Current password is invalid."};
        }
        sqlite3_stmt* stmt = nullptr;
        if (job.action == Action::CREATE) {
            char hash[crypto_pwhash_STRBYTES];
            if (crypto_pwhash_str(hash, job.password.data(), job.password.size(),
                crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
                return {false, "", "Account service busy. Please retry."};
            if (sqlite3_prepare_v2(db, "INSERT INTO users(username,password_hash) VALUES(?,?)", -1, &stmt, nullptr) != SQLITE_OK)
                return {false, "", "Account storage unavailable."};
            sqlite3_bind_text(stmt, 1, job.name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, hash, -1, SQLITE_TRANSIENT);
            int rc = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            sodium_memzero(hash, sizeof(hash));
            if (rc != SQLITE_DONE) return {false, "", rc == SQLITE_CONSTRAINT ? "That username is unavailable." : "Unable to save account."};
            return {true, job.name, ""};
        }

        if (job.action == Action::CHANGE_USERNAME && !ValidAccountName(job.replacement))
            return {false, "", "Username: 3-16 letters, numbers, underscores or hyphens."};
        if (job.action == Action::CHANGE_PASSWORD && !ValidAccountPassword(job.replacement))
            return {false, "", "New password must be 8-128 printable characters."};

        if (sqlite3_prepare_v2(db, "SELECT username,password_hash FROM users WHERE username=? COLLATE NOCASE", -1, &stmt, nullptr) != SQLITE_OK)
            return {false, "", "Account storage unavailable."};
        sqlite3_bind_text(stmt, 1, job.name.c_str(), -1, SQLITE_TRANSIENT);
        int rc = sqlite3_step(stmt);
        std::string canonical, hash = dummy;
        if (rc == SQLITE_ROW) {
            canonical = (const char*)sqlite3_column_text(stmt, 0);
            hash = (const char*)sqlite3_column_text(stmt, 1);
        }
        sqlite3_finalize(stmt);
        bool valid = crypto_pwhash_str_verify(hash.c_str(), job.password.data(), job.password.size()) == 0;
        if (!valid || canonical.empty())
            return {false, "", job.action == Action::LOGIN ? "Incorrect username or password." : "Current password is incorrect."};

        if (job.action == Action::LOGIN)
            return {true, canonical, ""};

        if (job.action == Action::CHANGE_USERNAME) {
            if (sqlite3_prepare_v2(db, "UPDATE users SET username=? WHERE username=? COLLATE NOCASE", -1, &stmt, nullptr) != SQLITE_OK)
                return {false, "", "Account storage unavailable."};
            sqlite3_bind_text(stmt, 1, job.replacement.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, canonical.c_str(), -1, SQLITE_TRANSIENT);
            int update = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if (update != SQLITE_DONE)
                return {false, "", update == SQLITE_CONSTRAINT ? "That username is unavailable." : "Unable to change username."};
            return {true, job.replacement, "Username changed."};
        }

        if (job.action == Action::CHANGE_PASSWORD) {
            char replacementHash[crypto_pwhash_STRBYTES];
            if (crypto_pwhash_str(replacementHash, job.replacement.data(), job.replacement.size(),
                crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0)
                return {false, "", "Account service busy. Please retry."};
            if (sqlite3_prepare_v2(db, "UPDATE users SET password_hash=? WHERE username=? COLLATE NOCASE", -1, &stmt, nullptr) != SQLITE_OK) {
                sodium_memzero(replacementHash, sizeof(replacementHash));
                return {false, "", "Account storage unavailable."};
            }
            sqlite3_bind_text(stmt, 1, replacementHash, -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, canonical.c_str(), -1, SQLITE_TRANSIENT);
            int update = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            sodium_memzero(replacementHash, sizeof(replacementHash));
            if (update != SQLITE_DONE) return {false, "", "Unable to change password."};
            return {true, canonical, "Password changed."};
        }

        if (sqlite3_prepare_v2(db, "DELETE FROM users WHERE username=? COLLATE NOCASE", -1, &stmt, nullptr) != SQLITE_OK)
            return {false, "", "Account storage unavailable."};
        sqlite3_bind_text(stmt, 1, canonical.c_str(), -1, SQLITE_TRANSIENT);
        int removed = sqlite3_step(stmt);
        sqlite3_finalize(stmt);
        if (removed != SQLITE_DONE) return {false, "", "Unable to delete account."};
        return {true, canonical, "Account deleted."};
    }

    std::shared_future<AccountResult> enqueue(Job job) {
        auto future = job.result.get_future().share();
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (jobs.size() >= 8) {
                sodium_memzero(job.password.data(), job.password.size());
                sodium_memzero(job.replacement.data(), job.replacement.size());
                job.result.set_value({false, "", "Account service busy. Please retry."});
                return future;
            }
            jobs.push_back(std::move(job));
        }
        wake.notify_one();
        return future;
    }
};
Accounts::Accounts(const std::string& path) : impl(new Impl) {
    if (sodium_init() < 0) throw std::runtime_error("Password library initialization failed.");
    if (sqlite3_open(path.c_str(), &impl->db) != SQLITE_OK) {
        sqlite3_close(impl->db); impl->db = nullptr;
        throw std::runtime_error("Cannot open account database.");
    }
    sqlite3_busy_timeout(impl->db, 3000);
    if (sqlite3_exec(impl->db, "CREATE TABLE IF NOT EXISTS users("
        "id INTEGER PRIMARY KEY, username TEXT NOT NULL UNIQUE COLLATE NOCASE,"
        "password_hash TEXT NOT NULL, created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP)", nullptr, nullptr, nullptr) != SQLITE_OK) {
        sqlite3_close(impl->db); impl->db = nullptr;
        throw std::runtime_error("Cannot initialize account database.");
    }
    char dummy[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(dummy, "unusable-dummy-password", 22,
        crypto_pwhash_OPSLIMIT_INTERACTIVE, crypto_pwhash_MEMLIMIT_INTERACTIVE) != 0) {
        sqlite3_close(impl->db); impl->db = nullptr;
        throw std::runtime_error("Cannot initialize password hashing.");
    }
    impl->dummy = dummy;
    impl->worker = std::thread([this] {
        for (;;) {
            Impl::Job job;
            {
                std::unique_lock<std::mutex> lock(impl->mutex);
                impl->wake.wait(lock, [this]{ return impl->stopping || !impl->jobs.empty(); });
                if (impl->jobs.empty() && impl->stopping) break;
                job = std::move(impl->jobs.front()); impl->jobs.pop_front();
            }
            AccountResult result;
            try { result = impl->process(job); }
            catch (...) { result = {false, "", "Account service unavailable."}; }
            sodium_memzero(job.password.data(), job.password.size());
            sodium_memzero(job.replacement.data(), job.replacement.size());
            job.result.set_value(std::move(result));
        }
    });
}
Accounts::~Accounts() {
    { std::lock_guard<std::mutex> lock(impl->mutex); impl->stopping = true; }
    impl->wake.notify_one();
    if (impl->worker.joinable()) impl->worker.join();
    sqlite3_close(impl->db);
}
std::shared_future<AccountResult> Accounts::submit(bool create, std::string name, std::string password) {
    return impl->enqueue({
        create ? Impl::Action::CREATE : Impl::Action::LOGIN,
        std::move(name), std::move(password), "", {}});
}
std::shared_future<AccountResult> Accounts::changeUsername(
    std::string name, std::string currentPassword, std::string newUsername) {
    return impl->enqueue({Impl::Action::CHANGE_USERNAME, std::move(name),
        std::move(currentPassword), std::move(newUsername), {}});
}
std::shared_future<AccountResult> Accounts::changePassword(
    std::string name, std::string currentPassword, std::string newPassword) {
    return impl->enqueue({Impl::Action::CHANGE_PASSWORD, std::move(name),
        std::move(currentPassword), std::move(newPassword), {}});
}
std::shared_future<AccountResult> Accounts::deleteAccount(
    std::string name, std::string currentPassword) {
    return impl->enqueue({Impl::Action::DELETE_ACCOUNT, std::move(name),
        std::move(currentPassword), "", {}});
}
