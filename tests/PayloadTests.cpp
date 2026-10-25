#include "TestCommon.h"

static TestRunner::BeforeEach gResetManager(ResetManager);

// In this file, a payload that runs user code under the lock makes the test hang, and ctest stops it

//-------------------------------------
TEST(CopyIsOutsideLock) {
    struct UsesManager {
        UsesManager() = default;
        UsesManager(const UsesManager &) { NM::GetDelegate(Id::B); }
    };

    std::atomic<int> step {0};

    std::thread thread([&] {
        NM::GetDelegate(Id::A).Add([](Id, const any &) {});
        step = 1;
        while (step != 2) {
            std::this_thread::yield();
        }
    });

    while (step != 1) {
        std::this_thread::yield();
    }

    NM::SendNotification(Id::A, UsesManager());

    step = 2;
    thread.join();
}

// Also when the queue grows or overwrite compacts it
//-------------------------------------
TEST(UserCodeIsOutsideLock) {
    struct UsesManager {
        explicit UsesManager(int v) : value(v) {}
        UsesManager(const UsesManager &other) : value(other.value) { NM::GetDelegate(Id::B); }
        UsesManager(UsesManager &&other) noexcept : value(other.value) { NM::GetDelegate(Id::B); }
        ~UsesManager() { NM::GetDelegate(Id::B); }

        int value;
    };

    std::atomic<bool> ready {false};
    std::atomic<bool> sent  {false};
    std::vector<int>  got;

    std::thread thread([&] {
        NM::GetDelegate(Id::A).Add([&](Id, const any &data) { got.push_back(any_cast<const UsesManager &>(data).value); });
        ready = true;
        while (!sent) {
            std::this_thread::yield();
        }
        NM::ReceivePendingNotifications();
    });

    while (!ready) {
        std::this_thread::yield();
    }

    for (int i = 0; i < 100; ++i) {
        NM::SendNotification(Id::A, UsesManager(i));
    }
    for (int i = 100; i < 200; ++i) {
        NM::SendNotification(Id::A, UsesManager(i), true);
    }

    sent = true;
    thread.join();

    CHECK(got == std::vector<int>{199});
}

//-------------------------------------
TEST(OverwrittenIsDestroyedOutsideLock) {
    struct UsesManager {
        ~UsesManager() { NM::GetDelegate(Id::C); }
    };

    std::atomic<int> step {0};

    std::thread thread([&] {
        NM::GetDelegate(Id::A).Add([](Id, const any &) {});
        step = 1;
        while (step != 2) {
            std::this_thread::yield();
        }
    });

    while (step != 1) {
        std::this_thread::yield();
    }

    NM::SendNotification(Id::A, std::make_shared<UsesManager>());
    NM::SendNotification(Id::A, std::make_shared<UsesManager>(), true);

    step = 2;
    thread.join();
}

// The payload is not copied for each receiver: all the threads see the same object
//-------------------------------------
TEST(SameObjectForAllReceivers) {
    std::atomic<bool> ready  {false};
    std::atomic<bool> sent   {false};
    const void        *mine  = nullptr;
    const void        *other = nullptr;

    std::thread thread([&] {
        NM::GetDelegate(Id::A).Add([&](Id, const any &data) { other = &any_cast<const int &>(data); });
        ready = true;
        while (!sent) {
            std::this_thread::yield();
        }
        NM::ReceivePendingNotifications();
    });

    while (!ready) {
        std::this_thread::yield();
    }

    NM::GetDelegate(Id::A).Add([&](Id, const any &data) { mine = &any_cast<const int &>(data); });
    NM::SendNotification(Id::A, 5);

    sent = true;
    thread.join();

    CHECK(mine != nullptr);
    CHECK(mine == other);
}

//-------------------------------------
int
main(int argc, char *argv[]) {
    return TestRunner::RunAll(argc, argv);
}
