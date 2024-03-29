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
    event = CreateEvent(false, true);
    auto result = WaitForEvent(event, 0);
    if (result != 0) {
        std::cout << "Initial WaitForEvent on autoreset event failed!" << std::endl;
        return WAIT_TIMEOUT;
    }

    result = WaitForEvent(event, 0);
    if (result != WAIT_TIMEOUT) {
        std::cout << "Second WaitForEvent on autoreset event succeeded!" << std::endl;
        std::cout << "Result was " << result << std::endl;
        return 1;
    }

    t1_started = CreateEvent(false, false);
    t1_finished = CreateEvent(false, false);

    std::thread t1(worker);
    WaitForEvent(t1_started);
    if (WaitForEvent(t1_finished, 0) == 0) {
        std::cout << "t1_finished is set even though event has not been set!" << std::endl;
        return 1;
    }

    SetEvent(event);
    result = WaitForEvent(t1_finished, 200);

    if (result != 0) {
        std::cout << "Timeout waiting for t1_finished!" << std::endl;
        return WAIT_TIMEOUT;
    }

    t1.join();

    DestroyEvent(event);
    DestroyEvent(t1_started);
    DestroyEvent(t1_finished);

    return 0;
}
