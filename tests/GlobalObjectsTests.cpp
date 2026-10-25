#include "TestCommon.h"

// Global objects can use the manager in their constructors and destructors,
// whatever order the globals of the different files are built in.
// This file is linked before the library, so with most linkers its globals are built before the ones of the library.
// It needs its own executable for that, and it does not reset the manager, because that would invalidate gKeepsDelegate

static int gReceived = 0;

//-------------------------------------
struct UsesManagerAlways {
    UsesManagerAlways() {
        NM::GetDelegate(Id::A).Add(this, &UsesManagerAlways::On);
        NM::SendNotification(Id::A);
    }

    ~UsesManagerAlways() {
        NM::GetDelegate(Id::A).Remove(this, &UsesManagerAlways::On);
        NM::SendNotification(Id::A);
    }

    void On(Id, const any &) { ++gReceived; }
};

// Does not use the manager until it is destroyed
//-------------------------------------
struct UsesManagerOnExit {
    ~UsesManagerOnExit() {
        NM::GetDelegate(Id::B).Add([](Id, const any &) {});
        NM::SendNotification(Id::B, 1);
    }
};

// Keeps the reference that GetDelegate returned, and uses it in its destructor
//-------------------------------------
struct KeepsDelegate {
    NM::Delegate &delegate = NM::GetDelegate(Id::C);

    KeepsDelegate()  { delegate.Add(this, &KeepsDelegate::On); }
    ~KeepsDelegate() { delegate.Remove(this, &KeepsDelegate::On); }

    void On(Id, const any &) {}
};

static UsesManagerOnExit gOnExit;
static UsesManagerAlways gAlways;
static KeepsDelegate     gKeepsDelegate;

//-------------------------------------
TEST(GlobalObjectsUseManager) {
    CHECK(gReceived == 1);
}

//-------------------------------------
int
main(int argc, char *argv[]) {
    return TestRunner::RunAll(argc, argv);
}
