#include "TestCommon.h"

static TestRunner::BeforeEach gResetManager(ResetManager);

//-------------------------------------
TEST(ReplacesAllPending) {
    std::vector<int> got;

    NM::DisableAutoSend();
    NM::GetDelegate(Id::A).Add([&](Id, const any &data) { got.push_back(any_cast<int>(data)); });

    NM::SendNotification(Id::A, 1);
    NM::SendNotification(Id::A, 2);
    NM::SendNotification(Id::A, 3, true);
    NM::ReceivePendingNotifications();

    CHECK(got == std::vector<int>{3});
}

//-------------------------------------
TEST(KeepsOtherIdsInOrder) {
    std::vector<int> got;

    NM::DisableAutoSend();
    NM::GetDelegate(Id::A).Add([&](Id, const any &data) { got.push_back(any_cast<int>(data)); });
    NM::GetDelegate(Id::B).Add([&](Id, const any &data) { got.push_back(100 + any_cast<int>(data)); });

    NM::SendNotification(Id::A, 1);
    NM::SendNotification(Id::B, 1);
    NM::SendNotification(Id::A, 2);
    NM::SendNotification(Id::B, 2);
    NM::SendNotification(Id::A, 3, true);
    NM::ReceivePendingNotifications();

    CHECK(got == std::vector<int>{101, 102, 3});
}

// An overwrite delivered at once to the sending thread also removes its older pending notifications
//-------------------------------------
TEST(DropsPendingOfSender) {
    std::vector<int> got;

    NM::GetDelegate(Id::A).Add([&](Id, const any &data) { got.push_back(any_cast<int>(data)); });

    std::thread([] { NM::SendNotification(Id::A, 1); }).join();
    NM::SendNotification(Id::A, 2, true);
    NM::ReceivePendingNotifications();

    CHECK(got == std::vector<int>{2});
}

// An overwrite sent from a callback replaces, in the other threads, the notification that caused it
//-------------------------------------
TEST(FromCallbackWins) {
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
            NM::SendNotification(Id::A, 2, true);
        }
    });
    NM::SendNotification(Id::A, 1, true);

    sent = true;
    thread.join();

    CHECK(other == std::vector<int>{2});
}

//-------------------------------------
int
main(int argc, char *argv[]) {
    return TestRunner::RunAll(argc, argv);
}
