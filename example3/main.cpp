#include <notifications/NotificationManager.h>
#include "NotificationId.h"
#include <cstdio>
#include <string>

using MindShake::NotificationManager;
using MindShake::NotificationId;

//-------------------------------------
void 
Hello(NotificationId id, const any &data) {
    std::string msg = any_cast<std::string>(data);
    printf("Hello %s from a function\n", msg.c_str());
}

//-------------------------------------
struct S {
    void Hello(NotificationId id, const any &data) {
        std::string msg = any_cast<std::string>(data);
        printf("Hello %s from a method\n", msg.c_str());
    }    
};

struct Button {
    void OnClick() {
        onPressed();
    }

    MindShake::Delegate<void()> onPressed;
};

//-------------------------------------
int 
main(int argc, char *argv[]) {
    S   s;
    int none = 0;

    auto lambda1 = +[](NotificationId id, const any &data) {
        std::string msg = any_cast<std::string>(data);
        printf("Hello %s from a simple lambda as C function\n", msg.c_str());
    };

    auto lambda2 = [](NotificationId id, const any &data) {
        std::string msg = any_cast<std::string>(data);
        printf("Hello %s from a simple lambda\n", msg.c_str());
    };

    auto lambda3 = [&none](NotificationId id, const any &data) {
        std::string msg = any_cast<std::string>(data);
        printf("Hello %s from a complex lambda\n", msg.c_str());
        ++none;
    };

    auto delegate = NotificationManager::GetDelegate(NotificationId::Hello);

    delegate.Add(&Hello);
    delegate.Add(&s, &S::Hello);
    delegate.Add(lambda1);
    auto id2 = delegate.Add(lambda2);
    auto id3 = delegate.Add(lambda3);

    delegate(NotificationId::Hello, std::string("NotMan"));

    printf("Number of delegates: %d\n", int(delegate.GetNumDelegates()));

    delegate.Remove(&Hello);
    delegate.Remove(&s, &S::Hello);
    delegate.Remove(lambda1);
    // For the time being, complex lambdas cannot be removed directly
    //delegate.Remove(lambda2);
    //delegate.Remove(lambda3);
    delegate.RemoveById(id2);
    delegate.RemoveById(id3);

    printf("Number of delegates: %d\n", int(delegate.GetNumDelegates()));

    if(none == 0) {
        printf("This should not have happened");
    }

    return 0;
}               