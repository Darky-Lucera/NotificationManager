#include "NotificationManager.h"

//-----------------------------------------------------------------------------
// Copyright (C) 2021 Carlos Aragonés
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt
//-----------------------------------------------------------------------------

using namespace MindShake;

NotificationManager::TIDMap     NotificationManager::mTIDNotifications;
NotificationManager::TIDData    NotificationManager::mTIDData;
std::mutex                      NotificationManager::mMutex;
fake_mutex                      NotificationManager::mFakeMutex;
bool                            NotificationManager::mEnableMT = true;
bool                            NotificationManager::mAutoSend = true;

//-------------------------------------
void
NotificationManager::SendNotification(NotificationId id, any data, bool overwrite) {
    if(mAutoSend) {
        GetMutex().lock();
            Map &notifications = mTIDNotifications[std::this_thread::get_id()];
            const auto &it = notifications.find(id);
        GetMutex().unlock();

        if (it != notifications.end()) {
            it->second(id, data);
        }
    }

    // Store it for the rest of the threads
    StoreTIDData(id, data, overwrite);
}

//-------------------------------------
NotificationManager::Delegate &
NotificationManager::GetDelegateForThisThread(NotificationId id) {
    const std::lock_guard<std::mutex> lock(GetMutex());

    return mTIDNotifications[std::this_thread::get_id()][id];
}

//-------------------------------------
void
NotificationManager::StoreTIDData(NotificationId id, const any &data, bool overwrite) {
    const std::lock_guard<std::mutex>   lock(GetMutex());
    bool                                found;

    //for(const auto &[tid, notifications] : mTIDNotifications) {
    for (const auto &pair : mTIDNotifications) {
        const auto &tid           = pair.first;
        const auto &notifications = pair.second;
        if(mAutoSend && tid == std::this_thread::get_id())
            continue;

        // is 'notification id' registered for this 'thread id'?
        if(notifications.find(id) != notifications.end()) {
            found = false;
            if (overwrite) {
                auto    &notData = mTIDData[tid];
                size_t  size = notData.size();
                for (size_t i = 0; i < size; ++i) {
                    if (notData[i].first == id) {
                        notData[i].second = data;
                        found = true;
                        break;
                    }
                }
            }

            if((overwrite | found) == false) {
                mTIDData[tid].emplace_back(std::pair<NotificationId, any>(id, data));
            }
        }
    }
}

//-------------------------------------
void
NotificationManager::SendStoredNotificationsForThisThread() {
    // Get my notification data
    GetMutex().lock();
        NotData notData = std::move(mTIDData[std::this_thread::get_id()]);
    GetMutex().unlock();

    auto &notifications = mTIDNotifications[std::this_thread::get_id()];
    //for(auto &[id, data] : notData) {
    for (const auto &pair : notData) {
        const auto &id   = pair.first;
        const auto &data = pair.second;
        notifications[id](id,  data);
    }
}

//-------------------------------------
void
NotificationManager::Clear() {
    const std::lock_guard<std::mutex>   lock(GetMutex());

    mTIDNotifications.clear();
    mTIDData.clear();
}
