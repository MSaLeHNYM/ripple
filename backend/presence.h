#pragma once
#include "models.h"
#include <mutex>
#include <optional>
#include <unordered_map>
#include <unordered_set>

class Presence {
public:
    void track(std::int64_t user_id, pulse::Channel ch) {
        void* key = ch.impl().get();
        std::lock_guard<std::mutex> lk(mu_);
        channel_user_[key] = user_id;
        user_channels_[user_id].insert(key);
    }

    void untrack(pulse::Channel ch) {
        void* key = ch.impl().get();
        std::lock_guard<std::mutex> lk(mu_);
        auto it = channel_user_.find(key);
        if (it == channel_user_.end()) return;
        const auto user_id = it->second;
        channel_user_.erase(it);
        auto uit = user_channels_.find(user_id);
        if (uit != user_channels_.end()) {
            uit->second.erase(key);
            if (uit->second.empty()) user_channels_.erase(uit);
        }
    }

    std::optional<std::int64_t> user_of(const pulse::Channel& ch) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = channel_user_.find(ch.impl().get());
        if (it == channel_user_.end()) return std::nullopt;
        return it->second;
    }

    bool online(std::int64_t user_id) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = user_channels_.find(user_id);
        return it != user_channels_.end() && !it->second.empty();
    }

    Json online_user_ids() const {
        std::lock_guard<std::mutex> lk(mu_);
        Json ids = Json::array();
        for (const auto& [id, chs] : user_channels_)
            if (!chs.empty()) ids.push_back(id);
        return ids;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<void*, std::int64_t>                     channel_user_;
    std::unordered_map<std::int64_t, std::unordered_set<void*>> user_channels_;
};

/** Maps a Pulse channel to its active call media room (call:<chat_id>). */
class CallRooms {
public:
    void set(pulse::Channel ch, std::string room) {
        std::lock_guard<std::mutex> lk(mu_);
        rooms_[ch.impl().get()] = std::move(room);
    }
    void clear(pulse::Channel ch) {
        std::lock_guard<std::mutex> lk(mu_);
        rooms_.erase(ch.impl().get());
    }
    std::optional<std::string> get(const pulse::Channel& ch) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = rooms_.find(ch.impl().get());
        if (it == rooms_.end()) return std::nullopt;
        return it->second;
    }

private:
    mutable std::mutex mu_;
    std::unordered_map<void*, std::string> rooms_;
};
