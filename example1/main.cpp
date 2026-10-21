#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdio>
#include <cstdlib>
#include "notifications/NotificationManager.h"
#include "NotificationId.h"

using MindShake::NotificationManager;
using MindShake::NotificationId;
using std::chrono::milliseconds;

// Global vars
//-------------------------------------
constexpr size_t                    kNumThreads  = 25;
constexpr size_t                    kNumMessages = 10;
std::atomic<size_t>                 gNumRegistered(0);

//-------------------------------------
struct Runner {
    using Map = std::unordered_map<size_t, size_t>;

    explicit Runner(size_t id) : mId(id), mGenerator(unsigned(id) + 1), mDistribution(0, int(kNumThreads) - 1) {
        for (size_t i=0; i<kNumThreads; ++i) {
            mMessages[i] = 0;
        }

        mThread = std::thread(&Runner::run, this);
    }

    void run() {
        NotificationManager::GetDelegate(NotificationId::Hello).Add(this, &Runner::Hello);

        // A runner that started sending before the others registered would send them nothing,
        // and they would wait for those messages forever
        ++gNumRegistered;
        while (gNumRegistered < kNumThreads) {
            std::this_thread::yield();
        }

        while (mMessagesSent < kNumMessages) {
            if (size_t(mDistribution(mGenerator)) == mId) {
                ++mMessagesSent;
                NotificationManager::SendNotification(NotificationId::Hello, mId);
            }
            std::this_thread::sleep_for(milliseconds(1));
            NotificationManager::SendStoredNotificationsForThisThread();
        }

        while (mMessagesReceived < kNumThreads * kNumMessages) {
            std::this_thread::sleep_for(milliseconds(50));
            NotificationManager::SendStoredNotificationsForThisThread();
        }

        NotificationManager::GetDelegate(NotificationId::Hello).Remove(this, &Runner::Hello);
        if (NotificationManager::GetDelegate(NotificationId::Hello).GetNumDelegates() != 0) {
            printf("Error: Delegate was not removed\n");
        }
        NotificationManager::SendNotification(NotificationId::Dead, mId);
    }

    void Hello(NotificationId id, const any &data) {
        size_t i = any_cast<size_t>(data);
        mMessages[i] = mMessages[i] + 1;
        ++mMessagesReceived;
    }


    size_t                              mId;
    size_t                              mMessagesSent {};
    size_t                              mMessagesReceived {};
    // Each runner has its own generator: they run in different threads, and a shared one would be a data race
    std::default_random_engine          mGenerator;
    std::uniform_int_distribution<int>  mDistribution;
    std::thread                         mThread;
    Map                                 mMessages;
};

//-------------------------------------
int
main(int argc, char *argv[]) {
    std::vector<Runner>  runners;
    volatile size_t total = 0;

    NotificationManager::GetDelegate(NotificationId::Dead)
        .Add([&](NotificationId id, const any &data) {
            size_t i = any_cast<size_t>(data);
            if (runners[i].mThread.joinable()) {
                runners[i].mThread.join();
            }
            ++total;
            printf("Thread %u joined\n", uint32_t(i));
        }
    );

    runners.reserve(kNumThreads);
    for (size_t i=0; i<kNumThreads; ++i) {
        runners.emplace_back(i);
    }

    while (total < kNumThreads) {
        std::this_thread::sleep_for(milliseconds(500));
        NotificationManager::SendStoredNotificationsForThisThread();
    }

    for (size_t i=0; i<kNumThreads; ++i) {
        for (size_t j=0; j<kNumThreads; ++j) {
            printf("%2u ", uint32_t(runners[i].mMessages[j]));
        }
        printf("\n");
    }

    NotificationManager::Clear();

    return 0;
}
