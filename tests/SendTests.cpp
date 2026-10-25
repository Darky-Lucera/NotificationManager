#include "TestCommon.h"

static TestRunner::BeforeEach gResetManager(ResetManager);

//-------------------------------------
TEST(NoDataIsEmptyAny) {
    bool called = false;
    bool empty  = false;

    NM::GetDelegate(Id::A).Add([&](Id, const any &data) {
        called = true;
        empty  = data.type() == typeid(void);
    });
    NM::SendNotification(Id::A);

    CHECK(called);
    CHECK(empty);
}

// A sends B and B sends A: the second A finds its delegate running, so it is stored instead of lost
//-------------------------------------
TEST(ReentrantNotificationIsNotLost) {
    int a = 0;
    int b = 0;

    NM::GetDelegate(Id::A).Add([&](Id, const any &) {
        ++a;
        if (a == 1) {
            NM::SendNotification(Id::B);
        }
    });
    NM::GetDelegate(Id::B).Add([&](Id, const any &) {
        ++b;
        NM::SendNotification(Id::A);
    });

    NM::SendNotification(Id::A);
    CHECK(a == 1);
    CHECK(b == 1);

    NM::ReceivePendingNotifications();
    CHECK(a == 2);
}

//-------------------------------------
TEST(SelfResendIsDeliveredLater) {
    int count = 0;

    NM::GetDelegate(Id::A).Add([&](Id, const any &) {
        ++count;
        NM::SendNotification(Id::A);
    });

    NM::SendNotification(Id::A);
    for (int i = 0; i < 3; ++i) {
        NM::ReceivePendingNotifications();
    }

    CHECK(count == 4);
}

// The other threads receive a notification sent from a callback after the one that caused it
//-------------------------------------
TEST(NestedSendGoesAfter) {
    std::atomic<bool> ready {false};
    std::atomic<bool> sent  {false};
    std::vector<int>  other;

    std::thread thread([&] {
        NM::GetDelegate(Id::A).Add([&](Id, const any &data) { other.push_back(any_cast<int>(data)); });
        ready = true;
        while (!sent) {
            std::this_thread::yield();
        }
        NM::ReceivePendingNotifications();
    });

    while (!ready) {
        std::this_thread::yield();
    }

    NM::GetDelegate(Id::A).Add([](Id, const any &data) {
        if (any_cast<int>(data) == 1) {
            NM::SendNotification(Id::A, 2);
        }
    });
    NM::SendNotification(Id::A, 1);

    sent = true;
    thread.join();

    CHECK(other == std::vector<int>{1, 2});
}

// From a callback, ReceivePendingNotifications leaves the notifications of the running delegate
// pending, in order, and delivers the others
//-------------------------------------
TEST(NestedReceivePending) {
    std::vector<int> a;
    int              b = 0;

    NM::DisableAutoSend();
    NM::GetDelegate(Id::B).Add([&](Id, const any &) { ++b; });
    NM::GetDelegate(Id::A).Add([&](Id, const any &data) {
        a.push_back(any_cast<int>(data));
        if (a.size() == 1) {
            NM::SendNotification(Id::A, 2);
            NM::SendNotification(Id::B);
            NM::SendNotification(Id::A, 3);
            NM::SendNotification(Id::A, 4, true);
            NM::ReceivePendingNotifications();
            CHECK(b == 1);
        }
    });

    NM::SendNotification(Id::A, 1);
    NM::ReceivePendingNotifications();
    NM::ReceivePendingNotifications();

    CHECK(a == std::vector<int>{1, 4});
    CHECK(b == 1);
}

// A notification that arrives from another thread while its delegate is running
// is not lost by a nested ReceivePendingNotifications
//-------------------------------------
TEST(NestedReceivePendingFromThread) {
    std::vector<int> got;
    std::atomic<int> step {0};

    NM::GetDelegate(Id::A).Add([&](Id, const any &data) {
        got.push_back(any_cast<int>(data));
        if (got.size() == 1) {
            step = 1;
            while (step != 2) {
                std::this_thread::yield();
            }
            NM::ReceivePendingNotifications();
        }
    });

    std::thread thread([&] {
        NM::SendNotification(Id::A, 1);
        while (step != 1) {
            std::this_thread::yield();
        }
        NM::SendNotification(Id::A, 2);
        step = 2;
    });

    while (got.empty()) {
        NM::ReceivePendingNotifications();
    }
    thread.join();
    NM::ReceivePendingNotifications();

    CHECK(got == std::vector<int>{1, 2});
}

// A program with only one thread can turn off the mutex
//-------------------------------------
TEST(WorksWithoutMutex) {
    std::vector<int> got;

    NM::DisableMultiThread();
    NM::GetDelegate(Id::A).Add([&](Id, const any &data) { got.push_back(any_cast<int>(data)); });

    NM::SendNotification(Id::A, 1);
    NM::DisableAutoSend();
    NM::SendNotification(Id::A, 2);
    NM::SendNotification(Id::A, 3, true);
    CHECK(got == std::vector<int>{1});

    NM::ReceivePendingNotifications();
    CHECK(got == std::vector<int>{1, 3});
}

//-------------------------------------
int
main(int argc, char *argv[]) {
    return TestRunner::RunAll(argc, argv);
}
