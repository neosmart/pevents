#ifdef _WIN32
#include <Windows.h>
#endif
#include <cassert>
#include <chrono>
#include <iostream>
#include <pevents.h>
#include <thread>
#include <vector>

using namespace neosmart;

const int THREADS = 64;

int shared_resource = -1;
neosmart_event_t workers[THREADS];
neosmart_event_t shutdown;
neosmart_event_t done[THREADS];
int done_count = 0;

void worker(size_t index) {
    neosmart_event_t events[] = { workers[index], shutdown };

    for (int i = 0; i < 5; ++i) {
        int event_idx = -1;
        int result = WaitForMultipleEvents(events, 2, false, 45000, event_idx);
        if (result == WAIT_TIMEOUT) {
            std::cerr << "[" << index << "] Error waiting for signalling event" << std::endl;
            SetEvent(shutdown);
            break;
        }
        else if (event_idx == 1) {
            // Another thread errored out
            break;
        }
        else if (shared_resource != -1) {
            std::cerr << "[" << index << "] Shared resource accessed by more than one thread!" << std::endl;
            SetEvent(shutdown);
            break;
        }

        shared_resource = index;
        // std::this_thread::sleep_for(std::chrono::milliseconds{rand() % 42});
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        ++done_count;
        shared_resource = -1;

        if (i == 4) {
            SetEvent(done[index]);
        }

        if (WaitForMultipleEvents(done, THREADS, true, 0) == 0) {
            // All threads completed
            break;
        }

        // Wake next thread
        size_t next_thread;
        do {
            next_thread = rand() % THREADS;
        } while (WaitForEvent(done[next_thread], 0) != WAIT_TIMEOUT);
        // std::cout << "[" << index << "] Waking worker " << next_thread << " (done_count: " << done_count << ")" << std::endl;
        SetEvent(workers[next_thread]);
    }

    // std::cout << "[" << index << "] Completed!" << std::endl;
    SetEvent(done[index]);
}

int main() {
    srand(time(NULL));

    for (int i = 0; i < THREADS; ++i) {
        workers[i] = CreateEvent(false, false);
        done[i] = CreateEvent(true, false);
    }
    shutdown = CreateEvent(true, false);

    std::vector<std::thread> threads(THREADS);
    for (int i = 0; i < THREADS; ++i) {
        std::thread t1([i] { worker(i); });
        threads.push_back(std::move(t1));
    }

    // Wake the first thread
    SetEvent(workers[0]);

    // Wait for all threads to finish
    int result = WaitForMultipleEvents(done, THREADS, true, 45000);
    if (result == WAIT_TIMEOUT) {
        std::cerr << "Timeout waiting for worker threads" << std::endl;
        return -1;
    }

    if (WaitForEvent(shutdown, 0) == 0) {
        return -1;
    }

    DestroyEvent(shutdown);
    for (const auto &event : workers) {
        DestroyEvent(event);
    }
    for (const auto &event : done) {
        DestroyEvent(event);
    }

    for (auto &thread: threads) {
        thread.join();
    }

    return 0;
}
