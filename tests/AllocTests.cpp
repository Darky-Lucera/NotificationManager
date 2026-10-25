#include "TestCommon.h"
#include <cstdlib>
#include <new>

// This file replaces the global operator new, so it can make an allocation fail on purpose.
// That is why these tests are in their own executable

static TestRunner::BeforeEach gResetManager(ResetManager);

static int gAllocations = 0;
static int gFailAt      = -1;

void *
operator new(size_t size) {
    if (++gAllocations == gFailAt) {
        throw std::bad_alloc();
    }

    if (void *p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }

    throw std::bad_alloc();
}

void operator delete(void *p) noexcept          { std::free(p); }
void operator delete(void *p, size_t) noexcept  { std::free(p); }

// Each allocation that an overwrite makes fails in turn. The queue must stay valid,
// so the notifications still pending are delivered later.
// The debug STL of MSVC allocates inside noexcept vector constructors,
// so there a failed allocation calls std::terminate and the test cannot run
//-------------------------------------
TEST(OverwriteWithoutMemoryKeepsTheQueueValid) {
#if !defined(_MSC_VER) || _ITERATOR_DEBUG_LEVEL == 0
    for (int failAt = 1; failAt <= 16; ++failAt) {
        std::vector<int> got;
        got.reserve(8);

        NM::Clear();
        NM::DisableAutoSend();
        NM::GetDelegate(Id::A).Add([&got](Id, const any &data) { got.push_back(any_cast<int>(data)); });

        NM::SendNotification(Id::A, 1);
        NM::SendNotification(Id::A, 2);
        NM::SendNotification(Id::A, 3);

        bool threw = false;
        gFailAt = gAllocations + failAt;
        try {
            NM::SendNotification(Id::A, 4, true);
        }
        catch (const std::bad_alloc &) {
            threw = true;
        }
        gFailAt = -1;

        NM::ReceivePendingNotifications();

        if (threw) {
            CHECK(got == std::vector<int>{1, 2, 3});
        }
        else {
            CHECK(got == std::vector<int>{4});
        }
    }
#endif
}

//-------------------------------------
int
main(int argc, char *argv[]) {
    return TestRunner::RunAll(argc, argv);
}
