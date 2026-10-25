#pragma once

#include "TestRunner.h"
#include "notifications/NotificationManager.h"
#include <atomic>
#include <chrono>
#include <memory>
#include <thread>
#include <vector>

//-------------------------------------
namespace MindShake {

    enum class NotificationId {
        A,
        B,
        C,
    };

} // end of namespace

using namespace MindShake;
using NM = NotificationManager;
using Id = NotificationId;

//-------------------------------------
// Reads the internal state of the NotificationManager
struct Peek : NotificationManager {
    ~Peek() = delete;

    static size_t Pending(TID tid) {
        const auto lock    = Lock();
        const auto &queues = GetState().pendingByThread;
        auto       it      = queues.find(tid);
        return it == queues.end() ? 0 : it->second.size();
    }

    static bool Registered(TID tid) {
        const auto lock = Lock();
        return GetState().delegatesByThread.count(tid) != 0;
    }

    static size_t NumFunctions(TID tid) {
        const auto lock      = Lock();
        const auto &byThread = GetState().delegatesByThread;
        auto       it        = byThread.find(tid);
        size_t     count     = 0;
        if (it != byThread.end()) {
            for (const auto &pair : it->second) {
                count += pair.second.GetNumDelegates();
            }
        }
        return count;
    }
};

// The function of one delegate keeps alive an object that uses another delegate of the same thread when it is destroyed.
// Each standard library destroys the delegates of a map in its own order, so both creation orders are tested
//-------------------------------------
struct UsesOtherDelegate {
    NM::Delegate *other = nullptr;

    void On(Id, const any &) {}
    ~UsesOtherDelegate() { other->Remove(this, &UsesOtherDelegate::On); }
};

inline void
AddWithReferenceToOtherDelegate(bool otherFirst) {
    NM::GetDelegate(otherFirst ? Id::B : Id::A);

    auto object = std::make_shared<UsesOtherDelegate>();
    object->other = &NM::GetDelegate(Id::B);
    object->other->Add(object.get(), &UsesOtherDelegate::On);
    NM::GetDelegate(Id::A).Add([object](Id, const any &) {});
}

//-------------------------------------
// ctest runs each test in its own process. Running all the tests of a file in one process needs a clean manager
inline void
ResetManager() {
    NM::Clear();
    NM::EnableAutoSend();
    NM::EnableMultiThread();
}
