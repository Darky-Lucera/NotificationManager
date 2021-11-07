# NotificationManager

​NotificationManager is a thread-safe, easy-to-use utility for sending and receiving notifications. It allows you to decouple different modules of your application. It can also simplify multi-threaded communication in many cases.

This utility is based on a previous one we used in **Lucera Project**'s **MindShake** game engine. But the previous version was programmed in _C++98_ and instead of _any_ it used our own implementation of _variant_. But in essence, it is the same.

By default, it uses _C++17_ std::any library feature but it has a _C++11_ fallback using the  implementation from @thelink2012 [GitHub](https://github.com/thelink2012/any).

Due to that it is recommended to use plain ```any, any_cast, bad_any_cast``` instead of ```std::any, std::any_cast, std::bad_any_cast``` to maximize compatibility.

It was also successfully ported to _C#_ to be able to use it with _Unity_ and to _Python_ to use it in a roguelike.

## User Interface

**```GetDelegate(NotificationId id)```:** Gets the delegate **_for the current thread_**. With this, you can Add or Remove 'Callables' (functions, methods, or lambdas) to the specified notification id.

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

auto delegate = NotificationManager::GetDelegate(NotificationId::Log);
...
auto id = delegate.Add(logger);
...
delegate.RemoveById(id);
```

_**Note:** I have had to implement my own wrapper for callables (```Delegate<...>```) since I needed to identify the callable in case the user wants to remove it, because ```std::function``` lacks the ```operator ==```._


**```SendNotification(NotificationId id, std::any data, bool overwrite = false)```:** Allows sending a notification from anywhere, with whatever data. It also allows the user to overwrite pending notifications. For instance, It's uncommon that someone needs all the UI windows to reshape notifications, just the last one is enough.

By default, the notifications are sent to the current thread if there is an associated delegate for the specified NotificationId.

```cpp
NotificationManager::SendNotification(NotificationId::Log, "Something happened"s);

NotificationManager::SendNotification(NotificationId::EnemyKilled, enemy);

NotificationManager::SendNotification(NotificationId::Reshape, std::tuple(width, height));
```

**```SendStoredNotificationsForThisThread()```:** As it is not possible to interrupt a thread while executing, every thread must call this function at the point the user desire to receive the pending notifications. Also, it's interesting that you call this in the main thread to get notifications sent from different threads.

```cpp
while(keepRunning) {
    ...
    NotificationManager::SendStoredNotificationsForThisThread();
    ...
}
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

**Enable/Disble AutoSend:** Sometimes the user don't want to send automatically notifications for the current thread, and wait until the next call to ```SendStoredNotificationsForThisThread```. You can modify the default behavior (send them automatically) using these functions:

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

Just drop the files **NotificationManager.h**, **NotificationManager.cpp** and _**NotificationId.h**_ to your project (**notifications** is a good name for the folder containing them).

The **notifications** folder here contains an empty **NotificationId.h** file that you have to fill with your own notification ids.

_Note: **example1**, **example2** and **example3** have their own **NotificationId.h** files with different ids for each project._

Before exiting your program it is a good idea to call ```Clear()``` to free some resources.

```cpp
NotificationManager::Clear();
```

## FAQ

* Why is NotificationManager a static class?

    Because it will be only one instance and, in this case, I prefer not to use a Singleton, to avoid having to continually call ```GetInstance```.
    I also don't want to use ~~(an over-designed, ahem)~~ a more complicated pattern to accomplish this simple task.

* Why is not a header only utility?

    Because I want it to be compatible with C++11, and static inline variables are a _C++17_ feature. Sorry.
