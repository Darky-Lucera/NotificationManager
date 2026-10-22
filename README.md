# NotificationManager

NotificationManager is a thread-safe, easy-to-use utility for sending and receiving notifications. It allows you to decouple different modules of your application. It can also simplify multi-threaded communication in many cases.

This utility is based on a previous one we used in **Lucera Project**'s **MindShake** game engine. But the previous version was programmed in _C++98_ and instead of _any_ it used our own implementation of _variant_. But in essence, it is the same.

When it is built with _C++17_ or later, it uses the std::any library feature. With older standards it uses a _C++11_ fallback: the implementation from @thelink2012 [GitHub](https://github.com/thelink2012/any). The CMake project builds with _C++11_ by default.

Due to that it is recommended to use plain ```any, any_cast, bad_any_cast``` instead of ```std::any, std::any_cast, std::bad_any_cast``` to maximize compatibility.

It was also successfully ported to _C#_ to be able to use it with _Unity_ and to _Python_ to use it in a roguelike.

## User Interface

**```GetDelegate(NotificationId id)```:** Gets the delegate **_for the current thread_**. With this, you can Add or Remove 'Callables' (functions, methods, or lambdas) to the specified notification id. You cannot call it directly: send notifications with `SendNotification`, so they also reach the other threads.

```cpp
void
Logger(NotificationId id, const any &data) {
    const std::string &msg = any_cast<std::string>(data);
    fprintf(logFile, "%s\n", msg.c_str());
}

NotificationManager::GetDelegate(NotificationId::Log).Add(&Logger);
...
NotificationManager::GetDelegate(NotificationId::Log).Remove(&Logger);
```

```cpp
struct Logger {
    Logger() {
        NotificationManager::GetDelegate(NotificationId::Log).Add(this, &Logger::Log);
    }

    ~Logger() {
        NotificationManager::GetDelegate(NotificationId::Log).Remove(this, &Logger::Log);
    }

    void Log(NotificationId id, const any &data) {
        const std::string &msg = any_cast<std::string>(data);
        fprintf(logFile, "%s\n", msg.c_str());
    }
};
```

```cpp
NotificationManager::GetDelegate(NotificationId::Log)
    .Add([&logFile](NotificationId id, const any &data) {
        const std::string &msg = any_cast<std::string>(data);
        fprintf(logFile, "%s\n", msg.c_str());
    }
);
```

For the time being, complex lambdas cannot be removed directly. So we need to save an id in case we want to remove them.

```cpp
auto logger = [&logFile](NotificationId id, const any &data) {
    const std::string &msg = any_cast<std::string>(data);
    fprintf(logFile, "%s\n", msg.c_str());
};

auto &delegate = NotificationManager::GetDelegate(NotificationId::Log);
...
auto id = delegate.Add(logger);
...
delegate.RemoveById(id);
```

_**Note:** I have had to implement my own wrapper for callables (```Delegate<...>```) since I needed to identify the callable in case the user wants to remove it, because ```std::function``` lacks the ```operator ==```._

**```SendNotification(NotificationId id, any data = any(), bool overwrite = false)```:** Allows sending a notification from anywhere, with whatever data. Without data, the callbacks receive an empty `any`. It also allows the user to overwrite pending notifications: with `overwrite`, the new notification replaces all the pending ones with the same id. For instance, it's uncommon that someone needs all the UI windows to reshape notifications, just the last one is enough.

By default, the notifications are sent to the current thread if there is an associated delegate for the specified NotificationId.

```cpp
NotificationManager::SendNotification(NotificationId::Log, "Something happened"s);

NotificationManager::SendNotification(NotificationId::EnemyKilled, enemy);

NotificationManager::SendNotification(NotificationId::Reshape, std::tuple(width, height));
```

_**Note:** A `Delegate` ignores calls made while it is running (see the [Delegate](https://github.com/Darky-Lucera/delegate) README). So a notification for a delegate that is running in the same thread is not delivered at once. For instance, when a callback sends the NotificationId it is handling (or one that led to it), or calls `SendStoredNotificationsForThisThread`. The notification stays stored, and the thread receives it in a later call to `SendStoredNotificationsForThisThread`. Other threads receive it as usual._

**```SendStoredNotificationsForThisThread()```:** As it is not possible to interrupt a thread while executing, every thread must call this function at the point the user desires to receive the pending notifications. Also, it's interesting that you call this in the main thread to get notifications sent from different threads.

```cpp
while (keepRunning) {
    ...
    NotificationManager::SendStoredNotificationsForThisThread();
    ...
}
```

**When a thread ends**, the NotificationManager removes its delegates and the notifications waiting for it. So a finished thread does not keep collecting notifications, and a new thread does not inherit them.

While a thread is running, it keeps receiving the notifications of every id it called `GetDelegate` for (since its last call to `ClearThisThread`), even if it removed all its functions. They are dropped when the thread calls `SendStoredNotificationsForThisThread`. A thread that stops listening but keeps running should call `ClearThisThread`.

**```ClearThisThread()```:** Removes all the functions and the pending notifications of the calling thread, and the thread receives nothing more until it subscribes again with `GetDelegate`. It is safe to call it from a callback: the pending notifications that `SendStoredNotificationsForThisThread` has not delivered yet are dropped too. The references returned by `GetDelegate` stay valid, but a function added through a reference kept from before `ClearThisThread` receives nothing until the thread calls `GetDelegate` again for that id.

```cpp
NotificationManager::ClearThisThread();
```

## Configuration

There are also some methods to configure the behavior of the utility for special cases.

**Enable/Disable MultiThread:** Sometimes the application could be mono-thread and it is a waste of time to use mutexes in that case. So the user can Enable/Disable the use of mutexes in this case:

```cpp
void         SetMT(bool set);
void         EnableMT();
void         DisableMT();
bool         GetMT();
```

**Enable/Disable AutoSend:** Sometimes the user doesn't want to send notifications to the current thread automatically, and prefers to wait until the next call to ```SendStoredNotificationsForThisThread```. You can modify the default behavior (send them automatically) using these functions:

```cpp
void         SetAutoSend(bool set);
void         EnableAutoSend();
void         DisableAutoSend();
bool         GetAutoSend();
```

## Extra: Delegates<...>

Due to the fact that I had to implement my own wrapper for callables, users have the possibility to use them in their own projects as, for example, a simple signal/slot utility.

```cpp
struct Button {
    ...
    void OnClick() {
        onPressed();
    }

    ...
    MindShake::Delegate<void()> onPressed;
};

...

Button btn;
btn.onPressed.Add([]() { printf("Button was pressed"); });

...

btn.OnClick();

```

## How to use it

Just drop the files **NotificationManager.h**, **NotificationManager.cpp**, **Delegate.h** and _**NotificationId.h**_, and the **any** folder, to your project (**notifications** is a good name for the folder containing them). The **any** folder is only needed before _C++17_.

The **notifications** folder here contains an empty **NotificationId.h** file that you have to fill with your own notification ids.

_Note: **example1**, **example2** and **example3** have their own **NotificationId.h** files with different ids for each project._

You do not need to call anything before exiting your program: each thread removes its own data when it ends. To reset the whole NotificationManager, call ```Clear()```:

```cpp
NotificationManager::Clear();
```

`Clear()` destroys the delegates of all the threads. So call it only when no other thread uses the NotificationManager, never from a callback, and do not use the references returned by `GetDelegate` afterwards. To clear only the current thread, use `ClearThisThread()`, which does not have these limits.

## FAQ

* Why is NotificationManager a static class?

    Because it will be only one instance and, in this case, I prefer not to use a Singleton, to avoid having to continually call ```GetInstance```.
    I also don't want to use ~~(an over-designed, ahem)~~ a more complicated pattern to accomplish this simple task.

* Why is it not a header-only utility?

    Because I want it to be compatible with C++11, and static inline variables are a _C++17_ feature. Sorry.
