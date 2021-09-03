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
    class fake_mutex : public std::mutex {
        public:
            void lock()     { }
            bool try_lock() { return true; }
            void unlock()   { }
    };

    //-------------------------------------
    class NotificationManager {
        public:
            using Delegate = MindShake::Delegate<void(NotificationId, const any &)>;
            using TID      = std::thread::id;

        public:
            static Delegate &   GetDelegate(NotificationId id);
            static void         SendNotification(NotificationId id, any data, bool overwrite = false);

            static void         SendStoredNotificationsForThisThread();

        // Configuration
        public:
            // Use a mutex to protect the critical parts or disable it to reduce the mutex overhead
            // in case the user knows that it is safe or is working on a monothread application.
            // Use it carefully.
            static void         SetMT(bool set)         { mEnableMT = set;   }
            static void         EnableMT()              { mEnableMT = true;  }
            static void         DisableMT()             { mEnableMT = false; }
            static bool         GetMT()                 { return mEnableMT;  }

            // Automatically send notifications on call to SendNotification or wait until the user
            // calls SendStoredNotificationsForThisThread.
            // This could reduce interruptions in a monothread applications.
            static void         SetAutoSend(bool set)   { mAutoSend = set;   }
            static void         EnableAutoSend()        { mAutoSend = true;  }
            static void         DisableAutoSend()       { mAutoSend = false; }
            static bool         GetAutoSend()           { return mAutoSend;  }

        // Finalize
        public:
            static void         Clear();

        protected:
            static std::mutex & GetMutex()  { return mEnableMT ? mMutex : mFakeMutex; }

        protected:
            static void         StoreTIDData(NotificationId id, const any &data, bool overwrite);

        private:
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
            static TIDMap       mTIDNotifications;
            static TIDData      mTIDData;
            static std::mutex   mMutex;
            static fake_mutex   mFakeMutex;
            static bool         mEnableMT;
            static bool         mAutoSend;
    };

} // end of namespace
