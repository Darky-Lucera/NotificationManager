# NotificationManager

NotificationManager is a small, thread-safe C++ library to send and receive notifications. It lets the modules of an application talk to each other without knowing each other, and it makes it easy to send data from one thread to another.

A module sends a notification with an id and, if it wants, some data. Every thread that listens to that id receives it, and each thread decides when it processes its notifications.

This library is based on a previous one that we used in the **MindShake** game engine of **Lucera Project**. That version used _C++98_ and our own _variant_ type instead of _any_, but it worked in the same way. It was also ported to other languages like _Python_, to use it in a roguelike.

It needs _C++11_ or later.

## Quick example

Each project defines its own ids in a file called `NotificationId.h`. The code in this README uses these ones:

```cpp
#pragma once

namespace MindShake {

    enum class NotificationId {
        Log,
        EnemyKilled,
        ScoreChanged,
        WindowResized,
        CollectStats,
        Quit,
    };

} // end of namespace
```

A thread listens to an id by adding a function to it. Another thread sends the notification, and the first thread receives it when it calls `ReceivePendingNotifications`:

```cpp
#include "notifications/NotificationManager.h"
#include "NotificationId.h"
#include <cstdio>
#include <string>
#include <thread>

using MindShake::NotificationManager;
using MindShake::NotificationId;

int
main() {
    // The main thread prints the messages that any thread sends
    NotificationManager::GetDelegate(NotificationId::Log).Add([](NotificationId, const any &data) {
        printf("%s\n", any_cast<const std::string &>(data).c_str());
    });

    std::thread worker([] {
        NotificationManager::SendNotification(NotificationId::Log, std::string("Hello from another thread"));
        NotificationManager::ClearThisThread();
    });
    worker.join();

    // The notification waits for the main thread until it asks for it
    NotificationManager::ReceivePendingNotifications();

    NotificationManager::Clear();
}
```

## How it works

* Each thread has its own functions. `GetDelegate(id)` returns the delegate of the **calling thread** for that id.
* A thread listens to an id when it adds a function to its delegate.
* `SendNotification` sends a notification to all the threads that listen to its id. The other threads keep it in a queue until they call `ReceivePendingNotifications`. The thread that sends it receives it at once, inside `SendNotification`, unless you turn off AutoSend.
* A callback receives the id and the data: `void (NotificationId id, const any &data)`.

## Adding and removing functions

`GetDelegate(id)` returns the delegate of the calling thread for that id. Use it to add and remove functions. You cannot call it directly: send notifications with `SendNotification`, so they also reach the other threads.

`Add` returns an id for the function, or `size_t(-1)` if it could not add it, for example a null function. `Remove` returns `false` if it does not find the function.

**Functions:**

```cpp
void
OnLog(NotificationId id, const any &data) {
    printf("%s\n", any_cast<const std::string &>(data).c_str());
}

NotificationManager::GetDelegate(NotificationId::Log).Add(&OnLog);
// ...
NotificationManager::GetDelegate(NotificationId::Log).Remove(&OnLog);
```

**Methods.** Remove a method before its object is destroyed. The destructor is a good place to do it:

```cpp
struct Logger {
    Logger() {
        NotificationManager::GetDelegate(NotificationId::Log).Add(this, &Logger::OnLog);
    }

    ~Logger() {
        NotificationManager::GetDelegate(NotificationId::Log).Remove(this, &Logger::OnLog);
    }

    void OnLog(NotificationId id, const any &data) {
        printf("%s\n", any_cast<const std::string &>(data).c_str());
    }
};
```

If a class has a const and a non-const version of the same method, use `MindShake::GetConstMethod` or `MindShake::GetNonConstMethod` to choose one. See the [Delegate](https://github.com/Darky-Lucera/delegate) README.

**Lambdas.** A lambda without captures is stored as a plain function, so you can remove it like one. For a lambda with captures, keep the id that `Add` returns and use `RemoveById`:

```cpp
auto &delegate = NotificationManager::GetDelegate(NotificationId::Log);

auto simple = [](NotificationId id, const any &data) { /* ... */ };
delegate.Add(simple);
delegate.Remove(simple);

FILE   *logFile   = stdout;
size_t functionId = delegate.Add([logFile](NotificationId id, const any &data) {
    fprintf(logFile, "%s\n", any_cast<const std::string &>(data).c_str());
});
delegate.RemoveById(functionId);
```

If a function throws an exception, the delegate catches it, writes it to `stderr` and calls the next function. To use your own logger, define `DELEGATE_LOG_ERROR` before you include `NotificationManager.h`. The [Delegate](https://github.com/Darky-Lucera/delegate) README explains how.

## Sending notifications

```cpp
static void SendNotification(NotificationId id, any data = any(), bool overwrite = false);
```

The data can be any copyable type. Without data, the callbacks receive an empty `any`. The type in `any_cast` must be the same type that you sent. For example, `SendNotification(id, "text")` sends a `const char *`, not a `std::string`:

```cpp
NotificationManager::SendNotification(NotificationId::Log, std::string("Something happened"));
NotificationManager::SendNotification(NotificationId::ScoreChanged, score);
NotificationManager::SendNotification(NotificationId::EnemyKilled, enemy);     // A struct, as in the GameEvents example
NotificationManager::SendNotification(NotificationId::WindowResized, std::make_tuple(width, height));
NotificationManager::SendNotification(NotificationId::Quit);
```

**The data is shared.** It is not copied for each receiver: every callback, in every thread, receives a const reference to the same object. It is destroyed once, in the thread that releases it last, which can be any of them. So its const member functions must be safe to call from several threads at the same time. This is true for the standard library types. If the receivers need to change something that they share, send a `std::shared_ptr` to it, and make that object thread-safe yourself:

```cpp
struct Stats {
    std::atomic<int> enemiesKilled {0};
};

auto stats = std::make_shared<Stats>();
NotificationManager::SendNotification(NotificationId::CollectStats, stats);

// In a callback, in any thread:
any_cast<const std::shared_ptr<Stats> &>(data)->enemiesKilled++;
```

**Overwrite.** With `overwrite` set to `true`, the new notification replaces all the pending notifications with the same id, in every thread. It is useful when only the last value matters. For example, when a window changes its size many times, the other modules only need the last size.

**The sending thread.** If the thread that sends a notification also listens to its id, it receives the notification at once, inside `SendNotification`. There is one exception: a delegate ignores calls while it is running. This happens, for example, when a callback sends the id that it is handling, or an id whose callback sends it back. Then the notification is stored, and the thread receives it in its next call to `ReceivePendingNotifications`. Nothing is lost. The other threads receive it as usual.

## Receiving notifications

A thread cannot be interrupted at any point, so each thread decides when it receives its pending notifications. It calls `ReceivePendingNotifications`, usually once per loop:

```cpp
while (keepRunning) {
    // ...
    NotificationManager::ReceivePendingNotifications();
    // ...
}
```

It delivers the notifications in the order they arrived. A notification that one of those callbacks sends to this thread is delivered at once, so it can arrive before older notifications with the same id. You can also call it from a callback: it delivers the pending notifications, except the ones for delegates that are running, which stay in the queue for a later call.

## Listening and stopping

A thread listens to an id from the moment it adds a function to it. Calling `GetDelegate` alone, or removing a function, does not make the thread listen. This matters for destructors: an object can remove its function with `GetDelegate(id).Remove(...)` at any time, and it never makes the thread listen again.

A thread keeps listening to an id even if it removes all its functions. Its notifications keep arriving, and they are dropped when the thread calls `ReceivePendingNotifications`. A thread that stops listening, but keeps running, should call `ClearThisThread`:

```cpp
NotificationManager::ClearThisThread();
```

`ClearThisThread` removes all the functions and the pending notifications of the calling thread, and the thread receives nothing more until it adds a function again. You can call it from a callback: the notifications that were going to be delivered after that callback are dropped too. The references that `GetDelegate` returned stay valid, and adding a function through one of them makes the thread listen to that id again.

## Cleaning up

Clean up what you use:

* Before a thread that uses the NotificationManager ends, call `ClearThisThread()`.
* At the end of `main`, after joining the other threads, call `Clear()`.

```cpp
std::thread worker([] {
    // ... add functions, send and receive notifications ...
    NotificationManager::ClearThisThread();
});

// ...
worker.join();
NotificationManager::Clear();
```

`Clear()` destroys the delegates of all the threads. So call it only when no other thread uses the NotificationManager, and never from a callback. After it, do not use the references that `GetDelegate` returned before, not even in the destructors of global objects: call `GetDelegate` again. To clear only the current thread, use `ClearThisThread()`, which does not have these limits.

**If you forget.** The NotificationManager also cleans up on its own. This is a safety net, not a reason to skip the calls above:

* When a thread ends, the NotificationManager removes its functions and its pending notifications. So a finished thread does not keep collecting notifications, and a new thread that gets the same thread id does not inherit them. Destructors that run while the thread ends, including the ones of `thread_local` objects, can still use `GetDelegate` and `Remove`. A function that they add at that point is ignored.
* Global objects can use the NotificationManager in their constructors and destructors. Its state is created before the globals of any file that includes `NotificationManager.h`, and it is destroyed after them, together with anything that is still in it.

## Configuration

**Multithreading.** By default, a mutex protects the shared state. If your application has only one thread, you can turn it off to avoid its cost. Change it only before any other thread uses the NotificationManager:

```cpp
static void SetMultiThread(bool set);
static void EnableMultiThread();
static void DisableMultiThread();
static bool GetMultiThread();
```

**AutoSend.** By default, the thread that sends a notification receives it at once, inside `SendNotification`. Turn AutoSend off if you want that thread to receive it in its next call to `ReceivePendingNotifications`, like the other threads. You can change it at any time:

```cpp
static void SetAutoSend(bool set);
static void EnableAutoSend();
static void DisableAutoSend();
static bool GetAutoSend();
```

## C++11, C++17 and any

By default, the library supports _C++11_, so it uses its own copy of _any_: the implementation by @thelink2012 ([GitHub](https://github.com/thelink2012/any)), in the `any` folder.

If your project uses _C++17_ or later, you should use `std::any` instead. It comes with your compiler, it is maintained and optimized together with the rest of its standard library, and it can use the features of the newer standards. The bundled copy is there only so the library also works with _C++11_ and _C++14_. To use `std::any`:

* With CMake, turn on the option `NOTIFICATIONMANAGER_USE_STD_ANY`. It also makes the library and the code that links it use at least _C++17_.
* Without CMake, define the macro `NOTIFICATIONMANAGER_USE_STD_ANY` for every file that includes `NotificationManager.h`, including `NotificationManager.cpp`.

The library and all the code that uses it must make the same choice. Otherwise the program does not link. If you define the macro with a standard older than _C++17_, the compiler stops with an error.

`NotificationManager.h` makes `any`, `any_cast` and `bad_any_cast` available in the global namespace. Use these names instead of `std::any` or `linb::any`, so your code works with both choices.

## Using it in your project

**Copying the files**:

Copy `NotificationManager.h`, `NotificationManager.cpp`, `Delegate.h` and the `any` folder to your project. The `any` folder is not needed if you use `NOTIFICATIONMANAGER_USE_STD_ANY`. Then write your own `NotificationId.h`, like the one in the quick example. `notifications` is a good name for the folder.

`NotificationId` must be an `enum class` in the `MindShake` namespace, and its underlying type must be `int` (the default), because `NotificationManager.h` declares it as `enum class NotificationId;`.

**With CMake**:

Add this repository with `add_subdirectory` and link the library:

```cmake
add_subdirectory(NotificationManager)

target_link_libraries(MyApp
PRIVATE
    NotificationManager::NotificationManager
)
```

Then include it as `#include "notifications/NotificationManager.h"`.

These are the CMake options:

| Option | Default | What it does |
| --- | --- | --- |
| `NOTIFICATIONMANAGER_USE_STD_ANY` | `OFF` | Uses `std::any` and _C++17_ instead of the bundled `any` |
| `NOTIFICATIONMANAGER_BUILD_EXAMPLES` | `ON` only if it is the main project | Builds the examples |
| `NOTIFICATIONMANAGER_BUILD_TESTS` | `ON` only if it is the main project | Builds the tests |

## Examples

The `examples` folder has two complete programs. Each one has its own `NotificationId.h`.

* **GameEvents**: game systems that do not know each other. When an enemy dies, the enemies send `EnemyKilled`, and the score board, the achievements and the audio react to it. The score board sends `ScoreChanged` with `overwrite`, so the HUD shows the score only once per frame, even when several enemies die in the same frame. It runs in one thread.
* **BackgroundLoading**: the main thread asks a loader thread to load some assets, and shows the progress while it waits. The two threads talk only with notifications, in both directions. The loader sends its progress with `overwrite`, so the main thread only shows the last value of each frame.

## Tests

The tests are in the `tests` folder. They need nothing but the standard library. Build the project and run them with `ctest`:

```sh
cmake -S . -B build
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

Each test file builds one program, and each `TEST(...)` in it is a separate `ctest` test with a name like `Overwrite.ReplacesAllPending`. So each test runs in its own process, and a test that crashes or hangs does not stop the others. A failed test prints each check that failed, with its file and line.

You can also run a test program directly. Without arguments, it runs all its tests and prints a summary. With the name of a test, it runs only that test. The folder of the programs depends on the generator, for example with Visual Studio:

```sh
build/tests/Debug/OverwriteTests
build/tests/Debug/OverwriteTests ReplacesAllPending
```

In VS Code, with CMake Tools, this setting groups the tests by file in the Testing view:

```json
"cmake.ctest.testSuiteDelimiter": "\\."
```

## Limitations

* **One thread per delegate.** Use the delegate that `GetDelegate` returns only from the thread that got it.
* **DLLs.** The NotificationManager is not designed to be shared between modules. Each module (the executable or a DLL) that builds or links its files gets its own NotificationManager, so notifications do not go from one module to another. Also, `Remove` with a function, or with an object and a method, only finds functions that were added from the same module. Use `RemoveById` to remove a function from another module.
* **Sending during delivery.** `ReceivePendingNotifications` takes the pending notifications out of the queue before it delivers them. If one of those callbacks sends a notification to this thread, it is delivered at once, before the notifications with the same id that are still waiting in that delivery. And an overwrite sent from one of those callbacks does not replace them.

## Extra: Delegate<...>

The library needs to find a function again to remove it, and `std::function` has no `operator==`. That is why it has its own class for lists of functions: `MindShake::Delegate<...>`, in `Delegate.h`. You can also use it on its own, for example as a simple signal and slot tool:

```cpp
struct Button {
    void OnClick() {
        onPressed();
    }

    MindShake::Delegate<void()> onPressed;
};

Button button;
button.onPressed.Add([]() { printf("Button was pressed\n"); });
button.OnClick();
```

The [Delegate](https://github.com/Darky-Lucera/delegate) repository has its full documentation.

## FAQ

* **Why is NotificationManager a static class?**

    Because there is only one instance. I prefer not to use a Singleton, to avoid calling `GetInstance` all the time. I also don't want to use ~~an over-designed, ahem~~ a more complicated pattern for this simple task.

* **Why is it not a header-only library?**

    It could be, even in _C++11_, by keeping the shared state in static variables inside `inline` functions. But that makes the code harder to read, and I prefer simple code. A small `.cpp` file is a low price for that.
