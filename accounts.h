#pragma once
#include <string>
#include <future>
#include <memory>

struct AccountResult { bool ok = false; std::string username; std::string message; };
class Accounts {
public:
    explicit Accounts(const std::string& databasePath);
    ~Accounts();
    std::shared_future<AccountResult> submit(bool create, std::string username, std::string password);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
bool ValidAccountName(const std::string& name);
bool ValidAccountPassword(const std::string& password);
