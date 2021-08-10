#include <thread>
#include <atomic>
#include <chrono>
#include <random>
#include <functional>
#include <string>
#include <cstdio>
#include <cstdlib>
#include "notifications/NotificationManager.h"
#include "NotificationId.h"

using MindShake::NotificationManager;
using MindShake::NotificationId;

#if __cplusplus >= 201703L
using namespace std::chrono_literals;
#else
constexpr std::chrono::milliseconds operator"" ms(unsigned long long value) noexcept {
    return std::chrono::milliseconds(value);
}
#endif

// Global vars
//-------------------------------------
constexpr size_t                    kNumThreads  = 25;
constexpr size_t                    kNumMessages = 10;
std::default_random_engine          gGenerator;
std::uniform_int_distribution<int>  gDistribution(0, kNumThreads-1);
auto                                getRandom = std::bind(gDistribution, gGenerator);

//-------------------------------------
struct Runner {
    using Map = std::unordered_map<size_t, size_t>;

    explicit Runner(size_t id) : mId(id) {
        for(size_t i=0; i<kNumThreads; ++i) {
            mMessages[i] = 0;
        }

        mThread = std::thread(&Runner::run, this);
    }

    void run() {
        NotificationManager::GetDelegateForThisThread(NotificationId::Hello)
            .Add([this](NotificationId id, const any &data) {
                size_t i = any_cast<size_t>(data);
                mMessages[i] = mMessages[i] + 1;
                ++mMessagesReceived;
            }
        );

        std::this_thread::sleep_for(100ms);
        while(mMessagesSent < kNumMessages) {
            if((getRandom() % kNumThreads) == mId) {
                ++mMessagesSent;
                NotificationManager::SendNotification(NotificationId::Hello, mId);
            }
            std::this_thread::sleep_for(1ms);
            NotificationManager::SendStoredNotificationsForThisThread();
        }

        while(mMessagesReceived < kNumThreads * kNumMessages) {
            std::this_thread::sleep_for(50ms);
            NotificationManager::SendStoredNotificationsForThisThread();
        }
        NotificationManager::SendNotification(NotificationId::Dead, mId);
    }

    size_t      mId;
    size_t      mMessagesSent {};
    size_t      mMessagesReceived {};
    std::thread mThread;
    Map         mMessages;
};

//-------------------------------------
int
main(int argc, char *argv[]) {
    std::vector<Runner>  runners;
    volatile size_t total = 0;

    NotificationManager::GetDelegateForThisThread(NotificationId::Dead)
        .Add([&](NotificationId id, const any &data) {
            size_t i = any_cast<size_t>(data);
            if(runners[i].mThread.joinable())
                runners[i].mThread.join();
            ++total;
            printf("Thread %lld joined\n", i);
        }
    );

    runners.reserve(kNumThreads);
    for(size_t i=0; i<kNumThreads; ++i) {
        runners.emplace_back(i);
    }

    while(total < kNumThreads) {
        std::this_thread::sleep_for(500ms);
        NotificationManager::SendStoredNotificationsForThisThread();
    }

    for(size_t i=0; i<kNumThreads; ++i) {
        for(size_t j=0; j<kNumThreads; ++j) {
            printf("%2lld ", runners[i].mMessages[j]);
        }
        printf("\n");
    }

    NotificationManager::Clear();

    return 0;
}
