#ifdef _WIN32
#include <Windows.h>
#endif
#include <iostream>
#include <pevents.h>
#include <thread>

using namespace neosmart;

neosmart_event_t event;
neosmart_event_t t1_started;
neosmart_event_t t1_finished;

void worker() {
    WaitForEvent(event, 0);
    SetEvent(t1_started);
    WaitForEvent(event, WAIT_INFINITE);
    SetEvent(t1_finished);
}

int main() {
    event = CreateEvent(true, false);
    t1_started = CreateEvent(true, false);
    t1_finished = CreateEvent(true, false);

    std::thread t1(worker);
    WaitForEvent(t1_started);
    if (WaitForEvent(t1_finished, 0) == 0) {
        std::cout << "t1_finished is set even though event has not been set!" << std::endl;
        return 1;
    }

    SetEvent(event);
    auto result = WaitForEvent(t1_finished, 200);

    if (result != 0) {
        std::cout << "Timeout waiting for t1_finished!" << std::endl;
        return WAIT_TIMEOUT;
    }

    DestroyEvent(event);
    DestroyEvent(t1_started);
    DestroyEvent(t1_finished);

    t1.join();

    return 0;
}
