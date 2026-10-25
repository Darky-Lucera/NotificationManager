//-----------------------------------------------------------------------------
// The main thread asks a loader thread to load some assets, and shows the progress while it waits.
// The two threads only talk with notifications: no shared variables, no locks of their own
//-----------------------------------------------------------------------------

#include "notifications/NotificationManager.h"
#include "NotificationId.h"
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

using MindShake::NotificationManager;
using MindShake::NotificationId;

//-------------------------------------
struct Progress {
    std::string asset;
    int         percent;
};

//-------------------------------------
static void
Wait(int milliseconds) {
    std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
}

//-------------------------------------
static void
LoaderThread() {
    std::vector<std::string> pending;
    bool                     quit = false;

    NotificationManager::GetDelegate(NotificationId::LoadAsset).Add([&pending](NotificationId, const any &data) {
        pending.push_back(any_cast<const std::string &>(data));
    });
    NotificationManager::GetDelegate(NotificationId::Quit).Add([&quit](NotificationId, const any &) {
        quit = true;
    });

    // Without it, the main thread could send the requests before this thread listens to them
    NotificationManager::SendNotification(NotificationId::LoaderReady);

    while (!quit) {
        NotificationManager::ReceivePendingNotifications();

        if (pending.empty()) {
            Wait(10);
            continue;
        }

        const std::string asset = pending.front();
        pending.erase(pending.begin());

        for (int percent = 10; percent <= 100; percent += 10) {
            Wait(15);

            // The main thread only shows the last progress, so each one replaces the one still pending
            NotificationManager::SendNotification(NotificationId::LoadProgress, Progress { asset, percent }, true);
        }

        NotificationManager::SendNotification(NotificationId::AssetLoaded, asset);
    }

    // The functions above use local variables of this function, so they must go before it returns
    NotificationManager::ClearThisThread();
}

//-------------------------------------
int
main() {
    const std::vector<std::string> assets = { "textures.pak", "sounds.pak", "level1.map" };

    bool   loaderReady = false;
    size_t numLoaded   = 0;

    NotificationManager::GetDelegate(NotificationId::LoaderReady).Add([&loaderReady](NotificationId, const any &) {
        loaderReady = true;
    });
    NotificationManager::GetDelegate(NotificationId::LoadProgress).Add([](NotificationId, const any &data) {
        const Progress &progress = any_cast<const Progress &>(data);
        printf("  Loading %s: %d%%\n", progress.asset.c_str(), progress.percent);
    });
    NotificationManager::GetDelegate(NotificationId::AssetLoaded).Add([&numLoaded](NotificationId, const any &data) {
        ++numLoaded;
        printf("Loaded %s\n", any_cast<const std::string &>(data).c_str());
    });

    std::thread loader(LoaderThread);

    while (!loaderReady) {
        NotificationManager::ReceivePendingNotifications();
        Wait(1);
    }

    for (const std::string &asset : assets) {
        NotificationManager::SendNotification(NotificationId::LoadAsset, asset);
    }

    // Each loop is a frame of the user interface, slower than the loader, so some progress values never show
    while (numLoaded < assets.size()) {
        NotificationManager::ReceivePendingNotifications();
        Wait(40);
    }

    NotificationManager::SendNotification(NotificationId::Quit);
    loader.join();

    printf("All assets loaded\n");

    NotificationManager::Clear();
    return 0;
}
