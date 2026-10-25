#include "TestCommon.h"

static TestRunner::BeforeEach gResetManager(ResetManager);

static int gStale = 0;

//-------------------------------------
static void
OnA(Id, const any &) {}

// Only adding a function subscribes the thread. GetDelegate and Remove do not, not even after ClearThisThread
//-------------------------------------
TEST(OnlyAddSubscribes) {
    std::atomic<int> step {0};
    NM::TID          tid;

    std::thread thread([&] {
        NM::GetDelegate(Id::A);
        NM::GetDelegate(Id::B).Add(&OnA);
        NM::ClearThisThread();
        NM::GetDelegate(Id::B).Remove(&OnA);

        tid  = std::this_thread::get_id();
        step = 1;
        while (step != 2) {
            std::this_thread::yield();
        }
    });

    while (step != 1) {
        std::this_thread::yield();
    }

    NM::SendNotification(Id::A);
    NM::SendNotification(Id::B);
    CHECK(Peek::Pending(tid) == 0);

    step = 2;
    thread.join();
}

// After ClearThisThread, the other threads stop storing notifications for this thread
//-------------------------------------
TEST(ClearThisThreadStopsQueueing) {
    std::atomic<int> step {0};
    NM::TID          tid;

    std::thread thread([&] {
        NM::GetDelegate(Id::A).Add([](Id, const any &) {});
        NM::ClearThisThread();
        tid  = std::this_thread::get_id();
        step = 1;
        while (step != 2) {
            std::this_thread::yield();
        }
    });

    while (step != 1) {
        std::this_thread::yield();
    }

    for (int i = 0; i < 1000; ++i) {
        NM::SendNotification(Id::A, i);
    }
    CHECK(Peek::Pending(tid) == 0);

    step = 2;
    thread.join();
}

// Adding a function after ClearThisThread subscribes again, also through a reference kept from before
//-------------------------------------
TEST(ClearThisThreadThenAddSubscribes) {
    std::atomic<int> step    {0};
    int              viaGet  = 0;
    int              viaKept = 0;
    NM::TID          tid;

    std::thread thread([&] {
        auto &kept = NM::GetDelegate(Id::B);
        NM::GetDelegate(Id::A).Add([](Id, const any &) {});
        NM::ClearThisThread();

        NM::GetDelegate(Id::A).Add([&](Id, const any &) { ++viaGet; });
        kept.Add([&](Id, const any &) { ++viaKept; });

        tid  = std::this_thread::get_id();
        step = 1;
        while (step != 2) {
            std::this_thread::yield();
        }
        NM::ReceivePendingNotifications();
    });

    while (step != 1) {
        std::this_thread::yield();
    }

    NM::SendNotification(Id::A);
    NM::SendNotification(Id::B);
    CHECK(Peek::Pending(tid) == 2);

    step = 2;
    thread.join();

    CHECK(viaGet == 1);
    CHECK(viaKept == 1);
}

// ClearThisThread from a callback drops the notifications already taken out, so a function added after it
// does not receive them
//-------------------------------------
TEST(ClearThisThreadFromCallback) {
    int stale = 0;

    NM::DisableAutoSend();
    auto &a = NM::GetDelegate(Id::A);
    auto &b = NM::GetDelegate(Id::B);
    a.Add([&](Id, const any &) {
        NM::ClearThisThread();
        b.Add([&](Id, const any &) { ++stale; });
    });

    NM::SendNotification(Id::A);
    NM::SendNotification(Id::B);
    NM::ReceivePendingNotifications();

    CHECK(stale == 0);
}

// Build with AddressSanitizer to see memory errors
//-------------------------------------
TEST(ClearThisThreadWithDestructorRemovingItself) {
    struct Listener {
        void On(Id, const any &) {}
        ~Listener() { NM::GetDelegate(Id::A).Remove(this, &Listener::On); }
    };

    auto  listener = std::make_shared<Listener>();
    auto &delegate = NM::GetDelegate(Id::A);

    delegate.Add(listener.get(), &Listener::On);
    delegate.Add([listener](Id, const any &) {});
    listener.reset();

    NM::ClearThisThread();

    CHECK(delegate.GetNumDelegates() == 0);
}

// A destructor that calls GetDelegate while ClearThisThread clears the functions does not leave the thread subscribed
//-------------------------------------
TEST(ClearThisThreadWithDestructorCallingGetDelegate) {
    struct Listener {
        void On(Id, const any &) {}
        ~Listener() { NM::GetDelegate(Id::B).Remove(this, &Listener::On); }
    };

    std::atomic<int> step {0};
    NM::TID          tid;

    std::thread thread([&] {
        auto listener = std::make_shared<Listener>();
        NM::GetDelegate(Id::B).Add(listener.get(), &Listener::On);
        NM::GetDelegate(Id::A).Add([listener](Id, const any &) {});
        listener.reset();

        NM::ClearThisThread();

        tid  = std::this_thread::get_id();
        step = 1;
        while (step != 2) {
            std::this_thread::yield();
        }
    });

    while (step != 1) {
        std::this_thread::yield();
    }

    for (int i = 0; i < 100; ++i) {
        NM::SendNotification(Id::A, i);
        NM::SendNotification(Id::B, i);
    }
    CHECK(Peek::Pending(tid) == 0);

    step = 2;
    thread.join();
}

// ClearThisThread destroys the pending payloads.
// A destructor that removes its function there does not subscribe the thread again
//-------------------------------------
TEST(ClearThisThreadWithPayloadDestructor) {
    struct Listener {
        void On(Id, const any &) {}
        ~Listener() { NM::GetDelegate(Id::A).Remove(this, &Listener::On); }
    };

    NM::GetDelegate(Id::A).Add([](Id, const any &) {});
    NM::GetDelegate(Id::B).Add([](Id, const any &) {});
    std::thread([] { NM::SendNotification(Id::B, std::make_shared<Listener>()); }).join();

    NM::ClearThisThread();

    std::thread([] { NM::SendNotification(Id::A); }).join();
    CHECK(Peek::Pending(std::this_thread::get_id()) == 0);
}

//-------------------------------------
TEST(ClearThisThreadWithDestructorAdding) {
    struct AddsOnDestroy {
        ~AddsOnDestroy() { NM::GetDelegate(Id::B).Add([](Id, const any &) { ++gStale; }); }
    };

    gStale = 0;

    auto object = std::make_shared<AddsOnDestroy>();
    NM::GetDelegate(Id::A).Add([object](Id, const any &) {});
    object.reset();

    NM::ClearThisThread();
    CHECK(Peek::NumFunctions(std::this_thread::get_id()) == 0);

    NM::GetDelegate(Id::B).Add([](Id, const any &) {});
    NM::SendNotification(Id::B);
    CHECK(gStale == 0);
}

// The other thread does not use the manager while Clear() runs, as Clear() requires
//-------------------------------------
TEST(ClearRemovesAllThreads) {
    std::atomic<int> step {0};
    NM::TID          tid;

    std::thread thread([&] {
        NM::GetDelegate(Id::A).Add([](Id, const any &) {});
        tid  = std::this_thread::get_id();
        step = 1;
        while (step != 2) {
            std::this_thread::yield();
        }
    });

    while (step != 1) {
        std::this_thread::yield();
    }

    NM::SendNotification(Id::A);
    CHECK(Peek::Pending(tid) == 1);

    NM::Clear();
    CHECK(!Peek::Registered(tid));
    CHECK(Peek::Pending(tid) == 0);

    step = 2;
    thread.join();
}

// Build with AddressSanitizer to see memory errors
//-------------------------------------
TEST(ClearWithReferenceToOtherDelegate) {
    for (int otherFirst = 0; otherFirst < 2; ++otherFirst) {
        AddWithReferenceToOtherDelegate(otherFirst != 0);
        NM::Clear();
        CHECK(!Peek::Registered(std::this_thread::get_id()));
    }
}

//-------------------------------------
TEST(ClearWithDestructorAdding) {
    struct AddsOnDestroy {
        ~AddsOnDestroy() { NM::GetDelegate(Id::B).Add([](Id, const any &) {}); }
    };

    auto object = std::make_shared<AddsOnDestroy>();
    NM::GetDelegate(Id::A).Add([object](Id, const any &) {});
    object.reset();

    NM::Clear();

    CHECK(!Peek::Registered(std::this_thread::get_id()));
}

//-------------------------------------
int
main(int argc, char *argv[]) {
    return TestRunner::RunAll(argc, argv);
}
