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
bool                            NotificationManager::mEnableMT = true;
std::atomic<bool>               NotificationManager::mAutoSend(true);

// MinGW destroys the thread_local objects of the main thread after the static ones, so the
// ThreadCleaner of the main thread can run when the members above are already gone.
// This guard is defined after them, so it is destroyed first and marks that moment.
static bool gStaticsDestroyed = false;

static struct StaticsGuard {
    ~StaticsGuard() { gStaticsDestroyed = true; }
} gStaticsGuard;

//-------------------------------------
// Removes what a thread left in the NotificationManager when the thread ends. Otherwise a finished thread
// would keep receiving notifications forever, and a new thread that gets the same id would inherit them.
struct NotificationManager::ThreadCleaner {
    ~ThreadCleaner() {
        if (!gStaticsDestroyed) {
            RemoveThread(std::this_thread::get_id());
        }
    }
};

//-------------------------------------
void
NotificationManager::SendNotification(NotificationId id, any data, bool overwrite) {
    // Read once, so the call to this thread and the copies for the others agree even if it changes meanwhile
    const bool autoSend = mAutoSend.load(std::memory_order_relaxed);

    if (autoSend) {
        Delegate *delegate = nullptr;
        {
            const auto lock = Lock();
            auto tidIt = mTIDNotifications.find(std::this_thread::get_id());
            if (tidIt != mTIDNotifications.end()) {
                auto it = tidIt->second.find(id);
                if (it != tidIt->second.end()) {
                    delegate = &it->second;
                }
            }
        }

        if (delegate != nullptr) {
            (*delegate)(id, data);
        }
    }

    // Store it for the rest of the threads
    StoreTIDData(id, data, overwrite, autoSend);
}

//-------------------------------------
NotificationManager::Delegate &
NotificationManager::GetDelegate(NotificationId id) {
    static thread_local ThreadCleaner cleaner;

    const auto lock = Lock();

    return mTIDNotifications[std::this_thread::get_id()][id];
}

//-------------------------------------
void
NotificationManager::StoreTIDData(NotificationId id, const any &data, bool overwrite, bool skipThisThread) {
    const auto  lock = Lock();
    bool        found;

    //for (const auto &[tid, notifications] : mTIDNotifications) {
    for (const auto &pair : mTIDNotifications) {
        const auto &tid           = pair.first;
        const auto &notifications = pair.second;
        if (skipThisThread && tid == std::this_thread::get_id()) {
            continue;
        }

        // is 'notification id' registered for this 'thread id'?
        if (notifications.find(id) != notifications.end()) {
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

            if (!found) {
                mTIDData[tid].emplace_back(std::pair<NotificationId, any>(id, data));
            }
        }
    }
}

//-------------------------------------
void
NotificationManager::SendStoredNotificationsForThisThread() {
    NotData notData;
    Map     *notifications;
    {
        const auto  lock = Lock();
        const TID   tid  = std::this_thread::get_id();

        auto dataIt = mTIDData.find(tid);
        auto tidIt  = mTIDNotifications.find(tid);
        if (dataIt == mTIDData.end() || tidIt == mTIDNotifications.end()) {
            return;
        }

        // Get my notification data
        notData.swap(dataIt->second);
        notifications = &tidIt->second;
    }

    //for (auto &[id, data] : notData) {
    for (const auto &pair : notData) {
        const auto &id   = pair.first;
        const auto &data = pair.second;
        auto it = notifications->find(id);
        if (it != notifications->end()) {
            it->second(id, data);
        }
    }
}

//-------------------------------------
void
NotificationManager::RemoveThread(TID tid) {
    Map     notifications;
    NotData notData;
    {
        const auto lock = Lock();

        auto tidIt = mTIDNotifications.find(tid);
        if (tidIt != mTIDNotifications.end()) {
            notifications.swap(tidIt->second);
            mTIDNotifications.erase(tidIt);
        }

        auto dataIt = mTIDData.find(tid);
        if (dataIt != mTIDData.end()) {
            notData.swap(dataIt->second);
            mTIDData.erase(dataIt);
        }
    }

    // notifications and notData are destroyed here, after releasing the lock: destroying a lambda
    // destroys what it captured, and that code may use the NotificationManager again.
}

//-------------------------------------
void
NotificationManager::ClearThisThread() {
    NotData                 notData;
    std::vector<Delegate *> delegates;
    {
        const auto  lock = Lock();
        const TID   tid  = std::this_thread::get_id();

        auto dataIt = mTIDData.find(tid);
        if (dataIt != mTIDData.end()) {
            notData.swap(dataIt->second);
        }

        // Pointers, not iterators: destroying a lambda may call GetDelegate and grow the map,
        // which invalidates iterators but not the delegates themselves
        auto tidIt = mTIDNotifications.find(tid);
        if (tidIt != mTIDNotifications.end()) {
            for (auto &pair : tidIt->second) {
                delegates.emplace_back(&pair.second);
            }
        }
    }

    // After releasing the lock, for the same reason as in RemoveThread. Only this thread uses its
    // delegates, so they need no lock, and Delegate::Clear() is safe even from one of their callbacks.
    for (auto *delegate : delegates) {
        delegate->Clear();
    }
}

//-------------------------------------
void
NotificationManager::Clear() {
    TIDMap  notifications;
    TIDData notData;
    {
        const auto lock = Lock();

        notifications.swap(mTIDNotifications);
        notData.swap(mTIDData);
    }

    // notifications and notData are destroyed here, after releasing the lock, for the same reason as in RemoveThread
}
