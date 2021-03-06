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

#if __cplusplus >= 201703L || (defined(_MSVC_LANG) && _MSVC_LANG >= 201703L)
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
        NotificationManager::GetDelegate(NotificationId::Hello).Add(this, &Runner::Hello);

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

        NotificationManager::GetDelegate(NotificationId::Hello).Remove(this, &Runner::Hello);
        if(NotificationManager::GetDelegate(NotificationId::Hello).GetNumDelegates() != 0) {
            printf("Error: Delegate was not removed\n");
        }
        NotificationManager::SendNotification(NotificationId::Dead, mId);
    }

    void Hello(NotificationId id, const any &data) {
        size_t i = any_cast<size_t>(data);
        mMessages[i] = mMessages[i] + 1;
        ++mMessagesReceived;
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

    NotificationManager::GetDelegate(NotificationId::Dead)
        .Add([&](NotificationId id, const any &data) {
            size_t i = any_cast<size_t>(data);
            if(runners[i].mThread.joinable())
                runners[i].mThread.join();
            ++total;
            printf("Thread %u joined\n", uint32_t(i));
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
            printf("%2u ", uint32_t(runners[i].mMessages[j]));
        }
        printf("\n");
    }

    NotificationManager::Clear();

    return 0;
}
