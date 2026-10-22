#pragma once

//-----------------------------------------------------------------------------
// Copyright (C) 2021 Carlos Aragonés
//
// Distributed under the Boost Software License, Version 1.0.
// See accompanying file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt
//-----------------------------------------------------------------------------

#include <vector>
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
    class NotificationDelegate : public MindShake::Delegate<void(NotificationId, const any &)> {
        using Base = MindShake::Delegate<void(NotificationId, const any &)>;
        using Base::operator();

        // False after ClearThisThread, until the thread calls GetDelegate again. Other threads read it,
        // so it is only used under the lock of the NotificationManager.
        bool    mSubscribed = true;

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
            // nothing more until it calls GetDelegate again. It is safe to call it from a callback, and the
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
            static void         StoreTIDData(NotificationId id, const any &data, bool overwrite, bool skipThisThread);

            static void         RemoveThread(TID tid);

        private:
            struct ThreadCleaner;

                                NotificationManager()                            = delete;
            virtual             ~NotificationManager()                           = delete;
                                NotificationManager(const NotificationManager &) = delete;
                                NotificationManager(NotificationManager &&)      = delete;
            NotificationManager &operator=(const NotificationManager &)          = delete;
            NotificationManager &operator=(NotificationManager &&)               = delete;

        protected:
            using Map      = std::unordered_map<NotificationId, Delegate>;
            using TIDMap   = std::unordered_map<TID, Map>;
            using NotData  = std::vector<std::pair<NotificationId, any>>;
            using TIDData  = std::unordered_map<TID, NotData>;

        protected:
            static TIDMap               mTIDNotifications;
            static TIDData              mTIDData;
            static std::mutex           mMutex;
            static bool                 mEnableMT;
            static std::atomic<bool>    mAutoSend;
    };

} // end of namespace
