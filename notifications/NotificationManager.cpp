#include "NotificationManager.h"

//-----------------------------------------------------------------------------
// Copyright (C) 2021 Carlos Aragonés
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt
//-----------------------------------------------------------------------------

using namespace MindShake;

constexpr uint32_t              NotificationManager::major;
constexpr uint32_t              NotificationManager::minor;
constexpr uint32_t              NotificationManager::patch;

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

// Lets SendStoredNotificationsForThisThread know that a callback called ClearThisThread, so the
// notifications it already took out must not be delivered either
static thread_local size_t gNumClears = 0;

// Calls from the NotificationManager to delegates of this thread that are running. While it is zero, no
// delegate of this thread is running, so SendStoredNotificationsForThisThread can take every notification.
static thread_local size_t gCallDepth = 0;

struct CallDepthGuard {
    CallDepthGuard()  { ++gCallDepth; }
    ~CallDepthGuard() { --gCallDepth; }
};

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
    const bool autoSend   = mAutoSend.load(std::memory_order_relaxed);
    bool       calledHere = false;

    if (autoSend) {
        Delegate *delegate = nullptr;
        {
            const auto lock = Lock();
            auto tidIt = mTIDNotifications.find(std::this_thread::get_id());
            if (tidIt != mTIDNotifications.end()) {
                auto it = tidIt->second.find(id);
                if (it != tidIt->second.end() && it->second.mSubscribed) {
                    delegate = &it->second;
                }
            }
        }

        // A delegate ignores calls while it runs, and then returns false. So a notification that comes back
        // to it from one of its callbacks is stored for this thread instead of lost.
        if (delegate != nullptr) {
            const CallDepthGuard depth;
            calledHere = (*delegate)(id, data);
        }
    }

    StoreTIDData(id, data, overwrite, calledHere);
}

//-------------------------------------
NotificationManager::Delegate &
NotificationManager::GetDelegate(NotificationId id) {
    static thread_local ThreadCleaner cleaner;

    const auto lock = Lock();

    Delegate &delegate   = mTIDNotifications[std::this_thread::get_id()][id];
    delegate.mSubscribed = true;

    return delegate;
}

//-------------------------------------
void
NotificationManager::StoreTIDData(NotificationId id, const any &data, bool overwrite, bool skipThisThread) {
    // Destroying a payload may run code that uses the NotificationManager, so the overwritten
    // ones are destroyed here, after releasing the lock
    NotData overwritten;
    {
        const auto  lock = Lock();

        //for (const auto &[tid, notifications] : mTIDNotifications) {
        for (const auto &pair : mTIDNotifications) {
            const auto &tid           = pair.first;
            const auto &notifications = pair.second;
            if (skipThisThread && tid == std::this_thread::get_id()) {
                continue;
            }

            // is 'notification id' registered for this 'thread id'?
            auto it = notifications.find(id);
            if (it == notifications.end() || !it->second.mSubscribed) {
                continue;
            }

            auto &notData = mTIDData[tid];
            if (overwrite) {
                // Removes every older notification with this id, so the new one is the last one received
                size_t count = 0;
                for (size_t i = 0; i < notData.size(); ++i) {
                    if (notData[i].first == id) {
                        overwritten.emplace_back(std::move(notData[i]));
                    }
                    else {
                        if (count != i) {
                            notData[count] = std::move(notData[i]);
                        }
                        ++count;
                    }
                }
                notData.erase(notData.begin() + ptrdiff_t(count), notData.end());
            }

            notData.emplace_back(id, data);
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

        notifications = &tidIt->second;

        auto &pending = dataIt->second;
        if (gCallDepth == 0) {
            notData.swap(pending);
        }
        else {
            // Called from a callback. A running delegate would ignore its notifications, so they stay
            // pending, in order, for a later call.
            size_t count = 0;
            for (size_t i = 0; i < pending.size(); ++i) {
                auto it = notifications->find(pending[i].first);
                if (it != notifications->end() && it->second.IsRunning()) {
                    if (count != i) {
                        pending[count] = std::move(pending[i]);
                    }
                    ++count;
                }
                else {
                    notData.emplace_back(std::move(pending[i]));
                }
            }
            pending.erase(pending.begin() + ptrdiff_t(count), pending.end());
        }
    }

    const size_t         numClears = gNumClears;
    const CallDepthGuard depth;

    //for (auto &[id, data] : notData) {
    for (const auto &pair : notData) {
        if (gNumClears != numClears) {
            break;
        }

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
    // Destroying a lambda destroys what it captured, and that code may use the NotificationManager again.
    // So everything is destroyed after releasing the lock, and it repeats because that code may even call
    // GetDelegate and register this thread again.
    bool found;
    do {
        Map     notifications;
        NotData notData;
        {
            const auto lock = Lock();
            found = false;

            auto tidIt = mTIDNotifications.find(tid);
            if (tidIt != mTIDNotifications.end()) {
                notifications.swap(tidIt->second);
                mTIDNotifications.erase(tidIt);
                found = true;
            }

            auto dataIt = mTIDData.find(tid);
            if (dataIt != mTIDData.end()) {
                notData.swap(dataIt->second);
                mTIDData.erase(dataIt);
                found = true;
            }
        }
    } while (found);
}

//-------------------------------------
void
NotificationManager::ClearThisThread() {
    const TID               tid = std::this_thread::get_id();
    std::vector<Delegate *> delegates;
    {
        const auto lock = Lock();

        // Pointers, not iterators: destroying a lambda may call GetDelegate and grow the map,
        // which invalidates iterators but not the delegates themselves
        auto tidIt = mTIDNotifications.find(tid);
        if (tidIt != mTIDNotifications.end()) {
            for (auto &pair : tidIt->second) {
                delegates.emplace_back(&pair.second);
            }
        }
    }

    ++gNumClears;

    // After releasing the lock, for the same reason as in RemoveThread. Only this thread uses its
    // delegates, so they need no lock, and Delegate::Clear() is safe even from one of their callbacks.
    for (auto *delegate : delegates) {
        delegate->Clear();
    }

    // After the Clear() calls, so a destructor that called GetDelegate does not leave the thread subscribed,
    // and the notifications stored meanwhile are dropped too
    NotData notData;
    {
        const auto lock = Lock();

        auto tidIt = mTIDNotifications.find(tid);
        if (tidIt != mTIDNotifications.end()) {
            for (auto &pair : tidIt->second) {
                pair.second.mSubscribed = false;
            }
        }

        auto dataIt = mTIDData.find(tid);
        if (dataIt != mTIDData.end()) {
            notData.swap(dataIt->second);
        }
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
