#include "TestCommon.h"

static TestRunner::BeforeEach gResetManager(ResetManager);

//-------------------------------------
static void
OnA(Id, const any &) {}

// Destroying the functions of a finished thread may call GetDelegate and register it again
//-------------------------------------
TEST(DeadThreadIsRemoved) {
    struct Listener {
        void On(Id, const any &) {}
        ~Listener() { NM::GetDelegate(Id::B).Remove(this, &Listener::On); }
    };

    NM::TID tid;

    std::thread thread([&] {
        auto listener = std::make_shared<Listener>();
        NM::GetDelegate(Id::B).Add(listener.get(), &Listener::On);
        NM::GetDelegate(Id::A).Add([listener](Id, const any &) {});
        tid = std::this_thread::get_id();
    });
    thread.join();

    CHECK(!Peek::Registered(tid));

    for (int i = 0; i < 1000; ++i) {
        NM::SendNotification(Id::B, i);
    }
    CHECK(Peek::Pending(tid) == 0);
}

// Build with AddressSanitizer to see memory errors
//-------------------------------------
TEST(DeadThreadWithReferenceToOtherDelegate) {
    for (int otherFirst = 0; otherFirst < 2; ++otherFirst) {
        NM::TID tid;
        std::thread([&] {
            AddWithReferenceToOtherDelegate(otherFirst != 0);
            tid = std::this_thread::get_id();
        }).join();

        CHECK(!Peek::Registered(tid));
    }
}

// Build with AddressSanitizer to see memory errors
//-------------------------------------
TEST(DeadThreadWithSavedReference) {
    struct Listener {
        NM::Delegate *delegate = nullptr;

        void On(Id, const any &) {}
        ~Listener() { delegate->Remove(this, &Listener::On); }
    };

    NM::TID tid;

    std::thread thread([&] {
        auto listener = std::make_shared<Listener>();
        listener->delegate = &NM::GetDelegate(Id::A);
        listener->delegate->Add(listener.get(), &Listener::On);
        listener->delegate->Add([listener](Id, const any &) {});
        tid = std::this_thread::get_id();
    });
    thread.join();

    CHECK(!Peek::Registered(tid));
}

// While a thread ends, a destructor adds a function through a reference it kept.
// The thread is still removed, with no functions and no pending notifications
//-------------------------------------
TEST(DestructorAddsThroughSavedReferenceWhileThreadEnds) {
    struct AddsOnDestroy {
        NM::Delegate *other = nullptr;
        ~AddsOnDestroy() { other->Add([](Id, const any &) {}); }
    };

    NM::TID tid;

    std::thread thread([&] {
        auto object = std::make_shared<AddsOnDestroy>();
        object->other = &NM::GetDelegate(Id::B);
        NM::GetDelegate(Id::A).Add([object](Id, const any &) {});
        tid = std::this_thread::get_id();
    });
    thread.join();

    NM::SendNotification(Id::B);
    CHECK(!Peek::Registered(tid));
    CHECK(Peek::Pending(tid) == 0);
}

// Threads that have ended share one delegate, and several of them can use it at the same time.
// It ignores every change. Build with ThreadSanitizer to see data races
//-------------------------------------
TEST(EndedThreadsShareAnIgnoredDelegate) {
    static std::atomic<int> numAdded;
    static std::atomic<int> numFunctions;
    numAdded     = 0;
    numFunctions = 0;

    struct UsesManagerOnExit {
        ~UsesManagerOnExit() {
            auto &delegate = NM::GetDelegate(Id::A);
            if (delegate.Add([](Id, const any &) {}) != size_t(-1)) {
                ++numAdded;
            }
            if (delegate.Add(&OnA) != size_t(-1)) {
                ++numAdded;
            }
            delegate.Remove(&OnA);
            delegate.RemoveById(0);
            delegate.Clear();
            numFunctions += int(delegate.GetNumDelegates());
        }
    };

    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([] {
            static thread_local UsesManagerOnExit onExit;
            NM::GetDelegate(Id::A).Add(&OnA);
        });
    }
    for (auto &thread : threads) {
        thread.join();
    }

    CHECK(numAdded == 0);
    CHECK(numFunctions == 0);
}

// A thread_local object created before the first GetDelegate is destroyed after the ThreadCleaner.
// Using the manager from its destructor does not register the finished thread again
//-------------------------------------
TEST(ThreadLocalDestructorUsesManager) {
    struct UsesManagerOnExit {
        ~UsesManagerOnExit() {
            NM::GetDelegate(Id::A).Remove(&OnA);
            NM::GetDelegate(Id::A).Add([](Id, const any &) {});
        }
    };

    NM::TID tid;

    std::thread thread([&] {
        static thread_local UsesManagerOnExit onExit;
        NM::GetDelegate(Id::A).Add(&OnA);
        tid = std::this_thread::get_id();
    });
    thread.join();

    for (int i = 0; i < 100; ++i) {
        NM::SendNotification(Id::A, i);
    }
    CHECK(Peek::Pending(tid) == 0);
    CHECK(Peek::NumFunctions(tid) == 0);
    CHECK(!Peek::Registered(tid));
}

// It only fails by crashing or hanging, so it is most useful with ThreadSanitizer or AddressSanitizer
//-------------------------------------
TEST(Stress) {
    std::atomic<bool>        stop      {false};
    std::atomic<bool>        wrongData {false};
    std::vector<std::thread> threads;

    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&stop, &wrongData, t] {
            int received = 0;
            while (!stop) {
                NM::GetDelegate(Id::A).Add([&](Id, const any &) { ++received; });
                NM::GetDelegate(Id::B).Add([&](Id, const any &data) {
                    if (any_cast<int>(data) < 0) {
                        wrongData = true;
                    }
                });
                for (int i = 0; i < 50; ++i) {
                    NM::SendNotification(Id::A, i, (i & 1) != 0);
                    NM::SendNotification(Id::B, i);
                    NM::ReceivePendingNotifications();
                }
                if ((t & 1) != 0) {
                    NM::ClearThisThread();
                }
            }
        });
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    stop = true;
    for (auto &thread : threads) {
        thread.join();
    }

    CHECK(!wrongData);
}

//-------------------------------------
int
main(int argc, char *argv[]) {
    return TestRunner::RunAll(argc, argv);
}
