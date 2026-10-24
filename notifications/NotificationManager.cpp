#include "NotificationManager.h"

//-----------------------------------------------------------------------------
// Copyright (C) 2021 Carlos Aragonés
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt
//-----------------------------------------------------------------------------

using namespace MindShake;

constexpr uint32_t                      NotificationManager::major;
constexpr uint32_t                      NotificationManager::minor;
constexpr uint32_t                      NotificationManager::patch;

NotificationManager::DelegatesByThread  NotificationManager::mDelegatesByThread;
NotificationManager::PendingByThread    NotificationManager::mPendingByThread;
std::mutex                              NotificationManager::mMutex;
bool                                    NotificationManager::mEnableMT = true;
std::atomic<bool>                       NotificationManager::mAutoSend(true);

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
NotificationManager::Delegate &
NotificationManager::GetDelegate(NotificationId id) {
    static thread_local ThreadCleaner cleaner;

    const auto lock = Lock();

    return mDelegatesByThread[std::this_thread::get_id()][id];
}

//-------------------------------------
size_t
NotificationDelegate::Subscribe(size_t id) {
    if (id != size_t(-1) && !mSubscribed) {
        const auto lock = NotificationManager::Lock();
        mSubscribed = true;
    }

    return id;
}

//-------------------------------------
void
NotificationManager::SendNotification(NotificationId id, any data, bool overwrite) {
    const Payload payload  = std::make_shared<const any>(std::move(data));
    Delegate      *delegate = DistributeNotification(id, payload, overwrite, mAutoSend.load(std::memory_order_relaxed));

    // Called after DistributeNotification because a callback may send a new notification,
    // and the other threads must receive it after this one (or only the new one, with overwrite).
    if (delegate != nullptr) {
        const CallDepthGuard depth;
        (*delegate)(id, *payload);
    }
}

//-------------------------------------
NotificationManager::Delegate *
NotificationManager::DistributeNotification(NotificationId id, const Payload &data, bool overwrite, bool callHere) {
    const TID self     = std::this_thread::get_id();
    Delegate  *delegate = nullptr;

    // Destroying a payload may run code that uses the NotificationManager,
    // so the overwritten ones are destroyed after releasing the lock
    PendingList overwritten;
    {
        const auto  lock = Lock();

        //for (auto &[tid, threadDelegates] : mDelegatesByThread) {
        for (auto &pair : mDelegatesByThread) {
            const auto &tid             = pair.first;
            auto       &threadDelegates = pair.second;

            // is 'notification id' registered for this 'thread id'?
            auto it = threadDelegates.find(id);
            if (it == threadDelegates.end() || !it->second.mSubscribed) {
                continue;
            }

            auto &pending = mPendingByThread[tid];
            if (overwrite) {
                // Removes every older notification with this id, so the new one is the last one received
                size_t count = 0;
                for (size_t i = 0; i < pending.size(); ++i) {
                    if (pending[i].first == id) {
                        overwritten.emplace_back(std::move(pending[i]));
                    }
                    else {
                        // Moves the ones we keep forward to fill the gaps left by the removed ones
                        if (count != i) {
                            pending[count] = std::move(pending[i]);
                        }
                        ++count;
                    }
                }

                pending.erase(pending.begin() + ptrdiff_t(count), pending.end());
            }

            // A running delegate would ignore the call, so then the notification is stored for this thread
            // instead of lost. Only this thread runs its delegates, so it cannot start running before the call.
            if (callHere && tid == self && !it->second.IsRunning()) {
                delegate = &it->second;
                continue;
            }

            pending.emplace_back(id, data);
        }
    }

    return delegate;
}

//-------------------------------------
void
NotificationManager::SendStoredNotificationsForThisThread() {
    PendingList batch;
    DelegateMap *threadDelegates;
    {
        const auto  lock = Lock();
        const TID   tid  = std::this_thread::get_id();

        auto pendingIt   = mPendingByThread.find(tid);
        auto delegatesIt = mDelegatesByThread.find(tid);
        if (pendingIt == mPendingByThread.end() || delegatesIt == mDelegatesByThread.end()) {
            return;
        }

        threadDelegates = &delegatesIt->second;

        auto &pending = pendingIt->second;
        if (gCallDepth == 0) {
            batch.swap(pending);
        }
        else {
            // Called from a callback. A running delegate would ignore its notifications, so they stay
            // pending, in order, for a later call.
            size_t count = 0;
            for (size_t i = 0; i < pending.size(); ++i) {
                auto it = threadDelegates->find(pending[i].first);
                if (it != threadDelegates->end() && it->second.IsRunning()) {
                    // Moves the ones we keep forward to fill the gaps left by the removed ones
                    if (count != i) {
                        pending[count] = std::move(pending[i]);
                    }
                    ++count;
                }
                else {
                    batch.emplace_back(std::move(pending[i]));
                }
            }

            pending.erase(pending.begin() + ptrdiff_t(count), pending.end());
        }
    }

    const size_t         numClears = gNumClears;
    const CallDepthGuard depth;

    //for (auto &[id, data] : batch) {
    for (const auto &pair : batch) {
        if (gNumClears != numClears) {
            break;
        }

        const auto &id   = pair.first;
        const auto &data = *pair.second;
        auto it = threadDelegates->find(id);
        if (it != threadDelegates->end()) {
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
        DelegateMap threadDelegates;
        PendingList pending;
        {
            const auto lock = Lock();
            found = false;

            auto delegatesIt = mDelegatesByThread.find(tid);
            if (delegatesIt != mDelegatesByThread.end()) {
                threadDelegates.swap(delegatesIt->second);
                mDelegatesByThread.erase(delegatesIt);
                found = true;
            }

            auto pendingIt = mPendingByThread.find(tid);
            if (pendingIt != mPendingByThread.end()) {
                pending.swap(pendingIt->second);
                mPendingByThread.erase(pendingIt);
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
        auto delegatesIt = mDelegatesByThread.find(tid);
        if (delegatesIt != mDelegatesByThread.end()) {
            for (auto &pair : delegatesIt->second) {
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

    // After the Clear() calls, so a destructor that added a function does not leave the thread subscribed,
    // and the notifications stored meanwhile are dropped too
    PendingList pending;
    {
        const auto lock = Lock();

        auto delegatesIt = mDelegatesByThread.find(tid);
        if (delegatesIt != mDelegatesByThread.end()) {
            for (auto &pair : delegatesIt->second) {
                pair.second.mSubscribed = false;
            }
        }

        auto pendingIt = mPendingByThread.find(tid);
        if (pendingIt != mPendingByThread.end()) {
            pending.swap(pendingIt->second);
        }
    }
}

//-------------------------------------
void
NotificationManager::Clear() {
    DelegatesByThread delegatesByThread;
    PendingByThread   pendingByThread;
    {
        const auto lock = Lock();

        delegatesByThread.swap(mDelegatesByThread);
        pendingByThread.swap(mPendingByThread);
    }

    // delegatesByThread and pendingByThread are destroyed here, after releasing the lock, for the same reason as in RemoveThread
}
