#include "TestCommon.h"
#include <algorithm>
#include <type_traits>

static TestRunner::BeforeEach gResetManager(ResetManager);

using Base = Delegate<void(Id, const any &)>;

//-------------------------------------
template <typename T, typename = void>
struct IsCallable : std::false_type {};

template <typename T>
struct IsCallable<T, decltype(void(std::declval<T &>()(Id::A, std::declval<const any &>())))> : std::true_type {};

// Users can add and remove functions, but they cannot call the delegate, reach its base class or move its functions out
//-------------------------------------
TEST(DelegateIsRestricted) {
    CHECK(IsCallable<Base>::value);
    CHECK(!IsCallable<NM::Delegate>::value);
    CHECK(!std::is_convertible<NM::Delegate &, Base &>::value);
    CHECK(!std::is_move_constructible<NM::Delegate>::value);
    CHECK(!std::is_move_assignable<NM::Delegate>::value);
}

//-------------------------------------
static int gOverloadedCalls = 0;

void Overloaded(Id, const any &)        { ++gOverloadedCalls; }
void Overloaded(Id, const any &, int)   {}

// NotificationDelegate needs its own Add(TFunc) for this, because a template cannot take an overloaded function by name
//-------------------------------------
TEST(AddOverloadedFunction) {
    gOverloadedCalls = 0;

    CHECK(NM::GetDelegate(Id::A).Add(&Overloaded) != size_t(-1));
    NM::SendNotification(Id::A);
    CHECK(gOverloadedCalls == 1);

    CHECK(NM::GetDelegate(Id::A).Remove(&Overloaded));
    NM::SendNotification(Id::A);
    CHECK(gOverloadedCalls == 1);
}

//-------------------------------------
TEST(RemoveById) {
    int  calls = 0;
    auto &delegate = NM::GetDelegate(Id::A);

    const size_t id = delegate.Add([&calls](Id, const any &) { ++calls; });
    CHECK(delegate.RemoveById(id));
    CHECK(!delegate.RemoveById(id));

    NM::SendNotification(Id::A);
    CHECK(calls == 0);
}

// An Add that fails must not subscribe the thread
//-------------------------------------
TEST(AddNullDoesNotSubscribe) {
    std::atomic<int> step {0};
    NM::TID          tid;

    std::thread thread([&] {
        CHECK(NM::GetDelegate(Id::A).Add(nullptr) == size_t(-1));
        tid  = std::this_thread::get_id();
        step = 1;
        while (step != 2) {
            std::this_thread::yield();
        }
    });

    while (step != 1) {
        std::this_thread::yield();
    }

    NM::SendNotification(Id::A);
    CHECK(Peek::Pending(tid) == 0);

    step = 2;
    thread.join();
}

// Using the version constants by reference needs a definition outside the class in C++11
//-------------------------------------
TEST(VersionIsLinkable) {
    const uint32_t zero = 0;

    CHECK(std::max(NM::major, zero) == NM::major);
    CHECK(std::max(NM::minor, zero) == NM::minor);
    CHECK(std::max(NM::patch, zero) == NM::patch);
}

//-------------------------------------
int
main(int argc, char *argv[]) {
    return TestRunner::RunAll(argc, argv);
}
