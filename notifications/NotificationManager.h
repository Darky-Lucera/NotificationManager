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
#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
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
    // The delegate that GetDelegate returns. Users can Add and Remove functions, but only the
    // NotificationManager can call it, so every notification goes through SendNotification.
    class NotificationDelegate : private MindShake::Delegate<void(NotificationId, const any &)> {
        using Base = MindShake::Delegate<void(NotificationId, const any &)>;

        public:
                                NotificationDelegate()                                  = default;
                                NotificationDelegate(NotificationDelegate &&)           = delete;
            NotificationDelegate &operator=(NotificationDelegate &&)                    = delete;

            // Adding a function subscribes the thread to this id. Only Add does it, so code that only removes,
            // like a destructor, does not subscribe the thread again after ClearThisThread or when it ends.
            template <typename... Params>
            size_t              Add(Params &&...params)                                 { return Subscribe(Base::Add(std::forward<Params>(params)...)); }
            // A template cannot take an overloaded function by name, but this overload can
            size_t              Add(TFunc func)                                         { return Subscribe(Base::Add(func));                            }

            using Base::Remove;
            using Base::RemoveById;
            using Base::Clear;
            using Base::GetNumDelegates;
            using Base::IsRunning;

        private:
            using Base::operator();

            size_t              Subscribe(size_t id);

        private:
            // False until a function is added, and again after ClearThisThread. Other threads read it under the
            // lock of the NotificationManager. Only this thread writes it, so this thread can read it without the lock.
            bool                mSubscribed = false;

            friend class NotificationManager;
    };

    //-------------------------------------
    class NotificationManager {
        public:
            using Delegate = NotificationDelegate;
            using TID      = std::thread::id;

        public:
            static constexpr uint32_t   major = 1;
            static constexpr uint32_t   minor = 3;
            static constexpr uint32_t   patch = 0;

        public:
            static Delegate &   GetDelegate(NotificationId id);
            static void         SendNotification(NotificationId id, any data = any(), bool overwrite = false);

            static void         SendStoredNotificationsForThisThread();

            // Removes the functions and the pending notifications of the calling thread, and the thread receives
            // nothing more until it adds a function again. It is safe to call it from a callback, and the
            // references returned by GetDelegate stay valid.
            static void         ClearThisThread();

        // Configuration
        public:
            // Use a mutex to protect the critical parts or disable it to reduce
            // the mutex overhead in case the user knows that it is safe or
            // is working on a monothread application.
            // Use it carefully.
            // Change it before any other thread uses the NotificationManager.
            static void         SetMT(bool set)         { mEnableMT = set;   }
            static void         EnableMT()              { mEnableMT = true;  }
            static void         DisableMT()             { mEnableMT = false; }
            static bool         GetMT()                 { return mEnableMT;  }

            // Automatically send notifications on call to SendNotification or
            // wait until the user calls SendStoredNotificationsForThisThread.
            // This could reduce interruptions in a monothread applications.
            static void         SetAutoSend(bool set)   { mAutoSend.store(set, std::memory_order_relaxed);   }
            static void         EnableAutoSend()        { mAutoSend.store(true, std::memory_order_relaxed);  }
            static void         DisableAutoSend()       { mAutoSend.store(false, std::memory_order_relaxed); }
            static bool         GetAutoSend()           { return mAutoSend.load(std::memory_order_relaxed);  }

        // Finalize
        public:
            // Removes everything.
            // It destroys the delegates, so call it only when no other thread uses
            // the NotificationManager, never from a callback, and do not use the
            // references returned by GetDelegate afterwards.
            static void         Clear();

        protected:
            static std::unique_lock<std::mutex> Lock() {
                std::unique_lock<std::mutex> lock(mMutex, std::defer_lock);
                if (mEnableMT) {
                    lock.lock();
                }
                return lock;
            }

        protected:
            // Returns the delegate of this thread when it has to be called directly, instead of storing the notification for it.
            static Delegate *   DistributeNotification(NotificationId id, const std::shared_ptr<const any> &data, bool overwrite, bool callHere);

            static void         RemoveThread(TID tid);

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
            using DelegateMap       = std::unordered_map<NotificationId, Delegate>;
            using DelegatesByThread = std::unordered_map<TID, DelegateMap>;
            // All the receivers share one read-only payload. Copying, moving or destroying a payload runs user code,
            // and the queues are changed under the lock, so they only handle pointers.
            using Payload           = std::shared_ptr<const any>;
            using PendingList       = std::vector<std::pair<NotificationId, Payload>>;
            using PendingByThread   = std::unordered_map<TID, PendingList>;

        protected:
            static DelegatesByThread    mDelegatesByThread;
            static PendingByThread      mPendingByThread;
            static std::mutex           mMutex;
            static bool                 mEnableMT;
            static std::atomic<bool>    mAutoSend;
        };

} // end of namespace
