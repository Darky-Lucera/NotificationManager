#pragma once

//-----------------------------------------------------------------------------
// Copyright (C) 2021 Carlos Aragonés
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt
//-----------------------------------------------------------------------------

#include <vector>
#include <memory>
#include <unordered_map>
#include <thread>
#include <mutex>
#include <atomic>
#if defined(NOTIFICATIONMANAGER_USE_STD_ANY)
    #if __cplusplus < 201703L && (!defined(_MSVC_LANG) || _MSVC_LANG < 201703L)
        #error "NOTIFICATIONMANAGER_USE_STD_ANY needs C++17 or later"
    #endif
    #include <any>
    using std::any;
    using std::any_cast;
    using std::bad_any_cast;
#else
    #include "any/any.h"
    using linb::any;
    using linb::any_cast;
    using linb::bad_any_cast;
#endif
//-------------------------------------
#include "Delegate.h"

//-------------------------------------
namespace MindShake {

    //-------------------------------------
    enum class NotificationId;

    //-------------------------------------
    // Users can add and remove functions, but only the NotificationManager can call them
    class NotificationDelegate : private MindShake::Delegate<void(NotificationId, const any &)> {
        using Base = MindShake::Delegate<void(NotificationId, const any &)>;

        public:
                                NotificationDelegate()                          = default;
                                NotificationDelegate(NotificationDelegate &&)   = delete;
            NotificationDelegate &operator=(NotificationDelegate &&)            = delete;

            template <typename... Params>
            size_t              Add(Params &&...params)                         { return mEnded ? size_t(-1) : Subscribe(Base::Add(std::forward<Params>(params)...)); }
            size_t              Add(TFunc func)                                 { return mEnded ? size_t(-1) : Subscribe(Base::Add(func));                            }

            void                Clear()                                         { if (!mEnded) { Base::Clear(); } }

            using Base::Remove;
            using Base::RemoveById;
            using Base::GetNumDelegates;
            using Base::IsRunning;

        private:
            using Base::operator();

            size_t              Subscribe(size_t id);

        private:
            // Only its own thread writes it; other threads read it under the lock
            bool    mSubscribed = false;
            // Set only in the delegate that ended threads share, so it never changes
            bool    mEnded      = false;

            friend class NotificationManager;
    };

    //-------------------------------------
    class NotificationManager {
        public:
            using Delegate = NotificationDelegate;
            using TID      = std::thread::id;

        public:
            static constexpr uint32_t   major = 2;
            static constexpr uint32_t   minor = 0;
            static constexpr uint32_t   patch = 0;

        public:
            static Delegate &   GetDelegate(NotificationId id);

            static void         SendNotification(NotificationId id, any data = any(), bool overwrite = false);

            static void         ReceivePendingNotifications();

        // Configuration
        public:
            // Use a mutex to protect the critical parts or disable it to reduce
            // the mutex overhead in case the user knows that it is safe or
            // is working on a monothread application.
            // Change it before any other thread uses the NotificationManager.
            // Use it carefully.
            static void         SetMultiThread(bool set)    { mMultiThread = set;   }
            static void         EnableMultiThread()         { mMultiThread = true;  }
            static void         DisableMultiThread()        { mMultiThread = false; }
            static bool         GetMultiThread()            { return mMultiThread;  }

            // Automatically send notifications on call to SendNotification or
            // wait until the user calls ReceivePendingNotifications.
            // This could reduce interruptions in a monothread applications.
            static void         SetAutoSend(bool set)       { mAutoSend.store(set, std::memory_order_relaxed);   }
            static void         EnableAutoSend()            { mAutoSend.store(true, std::memory_order_relaxed);  }
            static void         DisableAutoSend()           { mAutoSend.store(false, std::memory_order_relaxed); }
            static bool         GetAutoSend()               { return mAutoSend.load(std::memory_order_relaxed);  }

        // Finalize
        public:
            // Removes the functions and the pending notifications of the calling thread,
            // and the thread receives nothing more until it adds a function again.
            // It is safe to call it from a callback,
            // and the references returned by GetDelegate stay valid.
            static void         ClearThisThread();

            // Removes everything.
            // It destroys the delegates, so call it only when no other thread uses
            // the NotificationManager, never from a callback, and do not use the
            // references returned by GetDelegate afterwards.
            static void         Clear();

        protected:
            static std::unique_lock<std::mutex> Lock() {
                std::unique_lock<std::mutex> lock(GetState().mutex, std::defer_lock);
                if (mMultiThread) {
                    lock.lock();
                }
                return lock;
            }

        protected:
            // Returns the delegate of this thread when it has to be called directly,
            // instead of storing the notification for it
            static Delegate *   DistributeNotification(NotificationId id, const std::shared_ptr<const any> &data, bool overwrite, bool callHere);

            // Keeps every delegate alive while it empties them, because a destructor may use any of them
            static void         EmptyDelegates(bool allThreads);

            static void         RemoveThread();

            static void         CreateState();
            static void         DestroyState();

        private:
            struct ThreadCleaner;

            friend class NotificationDelegate;

                                NotificationManager()                            = delete;
            virtual             ~NotificationManager()                           = delete;
                                NotificationManager(const NotificationManager &) = delete;
                                NotificationManager(NotificationManager &&)      = delete;
            NotificationManager &operator=(const NotificationManager &)          = delete;
            NotificationManager &operator=(NotificationManager &&)               = delete;

        protected:
            // C++11 does not require std::hash to support enums, it came in C++14
            struct IdHash {
                size_t operator()(NotificationId id) const { return static_cast<size_t>(id); }
            };

            using DelegateMap       = std::unordered_map<NotificationId, Delegate, IdHash>;
            using DelegatesByThread = std::unordered_map<TID, DelegateMap>;
            // All the receivers share one read-only payload,
            // so the queues only move pointers and no user code runs under the lock
            using Payload           = std::shared_ptr<const any>;
            using PendingList       = std::vector<std::pair<NotificationId, Payload>>;
            using PendingByThread   = std::unordered_map<TID, PendingList>;

            struct State {
                // First, so it is destroyed after the maps it protects
                std::mutex              mutex;
                DelegatesByThread       delegatesByThread;
                PendingByThread         pendingByThread;
                // The thread that runs the constructors of the globals, and later their destructors
                TID                     mainThread;
                // What GetDelegate returns to a thread that has ended. It ignores every change
                NotificationDelegate    endedThread;
            };

            static State &      GetState()  { return *mState; }

        protected:
            static bool                 mMultiThread;
            static std::atomic<bool>    mAutoSend;

        private:
            struct alignas(State) StateStorage {
                unsigned char   bytes[sizeof(State)];
            };

            static size_t               mInitCount;
            static State                *mState;
            static StateStorage         mStateStorage;

            friend struct NotificationManagerInit;
    };

    //-------------------------------------
    // Each file that includes this header gets one of these objects,
    // defined before the globals of that file.
    // So the state of the NotificationManager is created before those globals and
    // destroyed after them
    static struct NotificationManagerInit {
        NotificationManagerInit();
        ~NotificationManagerInit();
    } sNotificationManagerInit;

} // end of namespace
