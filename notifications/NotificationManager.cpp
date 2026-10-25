#include "NotificationManager.h"
#include <new>

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

bool                                    NotificationManager::mEnableMT = true;
std::atomic<bool>                       NotificationManager::mAutoSend(true);

size_t                                  NotificationManager::mInitCount = 0;
NotificationManager::State *            NotificationManager::mState     = nullptr;
NotificationManager::StateStorage       NotificationManager::mStateStorage;

//-------------------------------------
NotificationManagerInit::NotificationManagerInit() {
    if (NotificationManager::mInitCount++ == 0) {
        NotificationManager::mState = new (NotificationManager::mStateStorage.bytes) NotificationManager::State();
    }
}

//-------------------------------------
NotificationManagerInit::~NotificationManagerInit() {
    if (--NotificationManager::mInitCount == 0) {
        NotificationManager::Clear();
        NotificationManager::mState->~State();
        NotificationManager::mState = nullptr;
    }
}

// Counts the ClearThisThread calls of this thread.
// SendStoredNotificationsForThisThread checks it to stop when a callback clears the thread
static thread_local size_t gNumClears = 0;

// Nonzero while the NotificationManager is calling a delegate of this thread
static thread_local size_t gCallDepth = 0;

// Set by the ThreadCleaner. It has no destructor, so later thread_local destructors can still read it
static thread_local bool gThreadEnded = false;

//-------------------------------------
struct CallDepthGuard {
    CallDepthGuard()  { ++gCallDepth; }
    ~CallDepthGuard() { --gCallDepth; }
};

//-------------------------------------
// Removes the data of a thread when it ends.
// Without it, a finished thread would keep receiving notifications,
// and a new thread with the same id would inherit them
struct NotificationManager::ThreadCleaner {
    ~ThreadCleaner() {
        gThreadEnded = true;

        // MinGW destroys the thread_local objects of the main thread after the static ones,
        // so the state may already be gone here
        if (mState != nullptr) {
            RemoveThread(std::this_thread::get_id());
        }
    }
};

//-------------------------------------
NotificationManager::Delegate &
NotificationManager::GetDelegate(NotificationId id) {
    // Passing again through a destroyed thread_local is undefined behavior
    if (!gThreadEnded) {
        static thread_local ThreadCleaner cleaner;
    }

    State &state = GetState();

    const auto lock = Lock();

    return state.delegatesByThread[std::this_thread::get_id()][id];
}

//-------------------------------------
size_t
NotificationDelegate::Subscribe(size_t id) {
    // Nobody would remove a function added after the thread ended
    if (gThreadEnded) {
        Base::RemoveById(id);
        return size_t(-1);
    }

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

    // Call delegate after DistributeNotification,
    // so the other threads get a notification sent from a callback after this one
    if (delegate != nullptr) {
        const CallDepthGuard depth;
        (*delegate)(id, *payload);
    }
}

//-------------------------------------
NotificationManager::Delegate *
NotificationManager::DistributeNotification(NotificationId id, const Payload &data, bool overwrite, bool callHere) {
    State     &state    = GetState();
    const TID self     = std::this_thread::get_id();
    Delegate  *delegate = nullptr;

    // The overwritten notifications are destroyed after releasing the lock,
    // because a payload destructor may use the NotificationManager
    PendingList overwritten;
    {
        const auto  lock = Lock();

        //for (auto &[tid, threadDelegates] : state.delegatesByThread) {
        for (auto &pair : state.delegatesByThread) {
            const auto &tid             = pair.first;
            auto       &threadDelegates = pair.second;

            // is 'notification id' registered for this 'thread id'?
            auto it = threadDelegates.find(id);
            if (it == threadDelegates.end() || !it->second.mSubscribed) {
                continue;
            }

            auto &pending = state.pendingByThread[tid];
            if (overwrite) {
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

            // A running delegate would ignore the call, so the notification is stored instead.
            // Only this thread runs its delegates,
            // so this one cannot start running before SendNotification calls it
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
    State       &state = GetState();
    PendingList batch;
    DelegateMap *threadDelegates;
    {
        const auto  lock = Lock();
        const TID   tid  = std::this_thread::get_id();

        auto pendingIt   = state.pendingByThread.find(tid);
        auto delegatesIt = state.delegatesByThread.find(tid);
        if (pendingIt == state.pendingByThread.end() || delegatesIt == state.delegatesByThread.end()) {
            return;
        }

        threadDelegates = &delegatesIt->second;

        auto &pending = pendingIt->second;
        if (gCallDepth == 0) {
            batch.swap(pending);
        }
        else {
            // From a callback: a running delegate would ignore its notifications,
            // so they stay pending, in order
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
    State &state = GetState();

    // Destroying a lambda may run code that uses the NotificationManager,
    // even GetDelegate for this thread.
    // So everything is destroyed outside the lock, and it repeats until nothing is left
    bool found;
    do {
        DelegateMap threadDelegates;
        PendingList pending;
        {
            const auto lock = Lock();
            found = false;

            auto delegatesIt = state.delegatesByThread.find(tid);
            if (delegatesIt != state.delegatesByThread.end()) {
                threadDelegates.swap(delegatesIt->second);
                state.delegatesByThread.erase(delegatesIt);
                found = true;
            }

            auto pendingIt = state.pendingByThread.find(tid);
            if (pendingIt != state.pendingByThread.end()) {
                pending.swap(pendingIt->second);
                state.pendingByThread.erase(pendingIt);
                found = true;
            }
        }
    } while (found);
}

//-------------------------------------
void
NotificationManager::ClearThisThread() {
    State     &state = GetState();
    const TID tid    = std::this_thread::get_id();

    ++gNumClears;

    // Repeats until nothing is left, as in RemoveThread
    bool found;
    do {
        std::vector<Delegate *> delegates;
        PendingList             pending;
        {
            const auto lock = Lock();
            found = false;

            // Pointers, not iterators: a destructor may call GetDelegate and grow the map
            auto delegatesIt = state.delegatesByThread.find(tid);
            if (delegatesIt != state.delegatesByThread.end()) {
                for (auto &pair : delegatesIt->second) {
                    pair.second.mSubscribed = false;
                    if (pair.second.GetNumDelegates() != 0) {
                        delegates.emplace_back(&pair.second);
                        found = true;
                    }
                }
            }

            auto pendingIt = state.pendingByThread.find(tid);
            if (pendingIt != state.pendingByThread.end() && !pendingIt->second.empty()) {
                pending.swap(pendingIt->second);
                found = true;
            }
        }

        // Delegate::Clear() is safe even from one of their callbacks
        for (auto *delegate : delegates) {
            delegate->Clear();
        }
    } while (found);
}

//-------------------------------------
void
NotificationManager::Clear() {
    State &state = GetState();

    // Repeats until nothing is left, as in RemoveThread
    bool found;
    do {
        DelegatesByThread delegatesByThread;
        PendingByThread   pendingByThread;
        {
            const auto lock = Lock();
            found = !state.delegatesByThread.empty() || !state.pendingByThread.empty();

            delegatesByThread.swap(state.delegatesByThread);
            pendingByThread.swap(state.pendingByThread);
        }
    } while (found);
}
