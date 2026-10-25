#include "NotificationManager.h"
#include <new>

//-----------------------------------------------------------------------------
// Copyright (C) 2021 Carlos Aragonés
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt
//-----------------------------------------------------------------------------

using namespace MindShake;

constexpr uint32_t                  NotificationManager::major;
constexpr uint32_t                  NotificationManager::minor;
constexpr uint32_t                  NotificationManager::patch;

bool                                NotificationManager::mMultiThread = true;
std::atomic<bool>                   NotificationManager::mAutoSend(true);

size_t                              NotificationManager::mInitCount = 0;
NotificationManager::State *        NotificationManager::mState     = nullptr;
NotificationManager::StateStorage   NotificationManager::mStateStorage;

//-------------------------------------
NotificationManagerInit::NotificationManagerInit() {
    if (NotificationManager::mInitCount++ == 0) {
        NotificationManager::CreateState();
    }
}

//-------------------------------------
NotificationManagerInit::~NotificationManagerInit() {
    if (--NotificationManager::mInitCount == 0) {
        NotificationManager::DestroyState();
    }
}

//-------------------------------------
void
NotificationManager::CreateState() {
    mState = new (mStateStorage.bytes) State();
    mState->mainThread = std::this_thread::get_id();
    mState->endedThread.mEnded = true;
}

//-------------------------------------
void
NotificationManager::DestroyState() {
    Clear();
    mState->~State();
    mState = nullptr;
}

// Counts the ClearThisThread calls of this thread.
// ReceivePendingNotifications checks it to stop when a callback clears the thread
static thread_local size_t  gNumClears = 0;

// Nonzero while the NotificationManager is calling a delegate of this thread
static thread_local size_t  gCallDepth = 0;

// Set when the ThreadCleaner of this thread is destroyed.
// It has no destructor, so later thread_local destructors can still read it
static thread_local bool    gCleanerDestroyed = false;

// Set when the data of this thread is removed. The main thread keeps its data until the state is destroyed
static thread_local bool    gThreadEnded = false;

//-------------------------------------
// Moves the entries that match to the end of "to", and keeps the order of the ones that stay
template <typename List, typename Predicate>
static void
MoveOut(List &from, List &to, Predicate matches) {
    // Reserved first, so nothing can throw after the first entry has moved
    to.reserve(to.size() + from.size());

    size_t count = 0;
    for (size_t i = 0; i < from.size(); ++i) {
        if (matches(from[i])) {
            to.emplace_back(std::move(from[i]));
        }
        else {
            // Moves the ones we keep forward to fill the gaps left by the removed ones
            if (count != i) {
                from[count] = std::move(from[i]);
            }
            ++count;
        }
    }

    from.erase(from.begin() + ptrdiff_t(count), from.end());
}

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
        gCleanerDestroyed = true;

        // MinGW destroys the thread_local objects of the main thread after the static ones,
        // so the state may already be gone here
        if (mState == nullptr) {
            return;
        }

        // The main thread ends before the globals are destroyed, and they may still use its delegates.
        // DestroyState clears them later
        if (std::this_thread::get_id() == mState->mainThread) {
            return;
        }

        gThreadEnded = true;
        RemoveThread();
    }
};

//-------------------------------------
NotificationManager::Delegate &
NotificationManager::GetDelegate(NotificationId id) {
    State &state = GetState();

    // Its entries are gone, and nobody would remove new ones
    if (gThreadEnded) {
        return state.endedThread;
    }

    // Passing again through a destroyed thread_local is undefined behavior.
    // It happens in the main thread, whose globals are destroyed after its thread_local objects
    if (!gCleanerDestroyed) {
        static thread_local ThreadCleaner cleaner;
    }

    const auto lock = Lock();

    return state.delegatesByThread[std::this_thread::get_id()][id];
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
                MoveOut(pending, overwritten, [id](const PendingList::value_type &entry) { return entry.first == id; });
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
NotificationManager::ReceivePendingNotifications() {
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
            MoveOut(pending, batch, [threadDelegates](const PendingList::value_type &entry) {
                auto it = threadDelegates->find(entry.first);
                return it == threadDelegates->end() || !it->second.IsRunning();
            });
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
NotificationManager::EmptyDelegates(bool allThreads) {
    State     &state = GetState();
    const TID self   = std::this_thread::get_id();

    // Destroying a function or a payload may run code that uses the NotificationManager,
    // even to add functions.
    // So it runs outside the lock, while all the delegates are alive,
    // and it repeats until nothing is left
    bool found;
    do {
        std::vector<Delegate *>  delegates;
        std::vector<PendingList> pendingLists;
        {
            const auto lock = Lock();
            found = false;

            // Pointers, not iterators: a destructor may call GetDelegate and grow the maps
            auto takeDelegates = [&delegates, &found](DelegateMap &threadDelegates) {
                for (auto &pair : threadDelegates) {
                    pair.second.mSubscribed = false;
                    if (pair.second.GetNumDelegates() != 0) {
                        delegates.emplace_back(&pair.second);
                        found = true;
                    }
                }
            };

            auto takePending = [&pendingLists, &found](PendingList &pending) {
                if (!pending.empty()) {
                    pendingLists.emplace_back();
                    pendingLists.back().swap(pending);
                    found = true;
                }
            };

            if (allThreads) {
                for (auto &pair : state.delegatesByThread) {
                    takeDelegates(pair.second);
                }
                for (auto &pair : state.pendingByThread) {
                    takePending(pair.second);
                }
            }
            else {
                auto delegatesIt = state.delegatesByThread.find(self);
                if (delegatesIt != state.delegatesByThread.end()) {
                    takeDelegates(delegatesIt->second);
                }

                auto pendingIt = state.pendingByThread.find(self);
                if (pendingIt != state.pendingByThread.end()) {
                    takePending(pendingIt->second);
                }
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
NotificationManager::RemoveThread() {
    State     &state = GetState();
    const TID self   = std::this_thread::get_id();

    EmptyDelegates(false);

    // Empty delegates and queues run no user code when they are destroyed
    const auto lock = Lock();
    state.delegatesByThread.erase(self);
    state.pendingByThread.erase(self);
}

//-------------------------------------
void
NotificationManager::ClearThisThread() {
    ++gNumClears;
    EmptyDelegates(false);
}

//-------------------------------------
void
NotificationManager::Clear() {
    State &state = GetState();

    EmptyDelegates(true);

    // Empty delegates and queues run no user code when they are destroyed
    const auto lock = Lock();
    state.delegatesByThread.clear();
    state.pendingByThread.clear();
}
