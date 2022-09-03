// This tests contention on an auto-reset event that is always available at a high level, to verify
// that other threads acquiring the protective pthread mutex don't result in a spurious WAIT_TIMEOUT
// error for any `WaitForEvent()` callers.
//
// See https://github.com/neosmart/pevents/issues/18

#ifdef _WIN32
#include <Windows.h>
#endif
#include <cassert>
#include <iostream>
#include <pevents.h>
#include <thread>
#include <vector>

using namespace neosmart;

int main() {
    // Create an auto-reset event that is initially signalled
    neosmart_event_t event = CreateEvent(false, true);

    std::vector<std::thread> threads;
    // Create n threads to constantly call SetEvent() in a tight loop
    for (int i = 0; i < 16; ++i) {
        std::thread t1([&] {
            int result = SetEvent(event);
            assert(result == 0 && "SetEvent() call failed!");
        });
        threads.push_back(std::move(t1));
    }

    // Call WaitForEvent() in a tight loop; we can expect it to always be available.
    for (int i = 0; i < 200000; ++i) {
        int result = WaitForEvent(event, 0);
        assert(result == 0 && "WaitForEvent() call failed!");
        // Guarantee this thread always calls `WaitForEvent()` on a signalled event
        SetEvent(event);
    }

    for (auto &t : threads) {
        assert(t.joinable() && "thread added to join list isn't joinable");
        t.join();
    }

    DestroyEvent(event);

    return 0;
}
