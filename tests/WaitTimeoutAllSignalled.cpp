// Test for correct return value after calling WaitForMultipleObjects with a timeout of zero
// but when all the events are actually set. Contributed by @CraftedHandleInvalid, see issue #5.
#include <stdexcept>
#include <vector>
#include "pevents.h"

int main() {
    std::vector<neosmart::pthread_event_t> lPEvents(63); // Can be any number of events from 1-N.
    for (auto &lEvent : lPEvents) {
        // Manual or Auto-Reset Events (doesn't matter which kind), and all already set.
        //
        // Note that this bug will manifest as long as all the events are
        // set before calling WaitForMultipleEvents.
        lEvent = neosmart::CreateEvent(false, true);
    }

    // Incorrectly returns non-signaled when checking state,
    // (i.e. timeout = 0, [check state and return immediately])
    auto lResult =
        neosmart::WaitForMultipleEvents(lPEvents.data(), static_cast<int>(lPEvents.size()),
                                        true, // Bug only occurs with Wait All = True.
                                        0);   // Don't wait; check state and return immediately.

    // Cleanup first (in case we throw)
    for (auto &lEvent : lPEvents) {
        neosmart::DestroyEvent(lEvent);
    }

    if (lResult != 0) {
        if (lResult == WAIT_TIMEOUT) {
            // Spoilers:  It times out.
            throw std::runtime_error(
                "Events were already set, we WFMO'd it, and it returned TIMEOUT!");
        } else {
            throw std::runtime_error("Events were already set, and WFME didn't return 0!");
        }
    }
}
