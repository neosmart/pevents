/*
 * WIN32 Events for POSIX
 * Author: Mahmoud Al-Qudsi <mqudsi@neosmart.net>
 * Copyright (C) 2011 - 2021 by NeoSmart Technologies
 * This code is released under the terms of the MIT License.
 */

#include <assert.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <signal.h>
#include <thread>

// NB: On Windows, you must include Winbase.h/Synchapi.h/Windows.h before pevents.h
#ifdef _WIN32
#include <Windows.h>
#endif
#include "pevents.h"

#ifdef _WIN32
#define __unused__  [[maybe_unused]]
#else
#define __unused__ __attribute__((unused))
#endif

using namespace neosmart;
using namespace std;

neosmart_event_t events[5];           // letters, numbers, abort, letterSync, numberSync
std::atomic<bool> interrupted{false}; // for signal handling

// By leaving these originally unassigned, any access to unitialized memory
// will be flagged by valgrind.
char letter;
int number;

char lastChar = '\0';
int lastNum = -1;

void intHandler(__unused__ int sig) {
    // Unfortunately you can't use SetEvent here because posix signal handlers
    // shouldn't use any non-reentrant code (like printf).
    // On x86/x64, std::atomic<bool> is just a fancy way of doing a memory
    // barrier and nothing more, so it is safe.
    interrupted = true;
}

void letters() {
    static uint32_t letterIndex = 0;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 3000);

    // Wait a random amount of time, from 0 through 3000 milliseconds, between each print attempt
    while (WaitForEvent(events[2], dis(gen)) == WAIT_TIMEOUT) {
        // Remember that another instance of this function may be executing concurrently, so after
        // the sleep finishes, make sure to obtain exclusive access by means of this auto-reset
        // event.
        auto waitResult = WaitForEvent(events[3]); // only one thread here at a time
        assert(waitResult == 0);
        letter = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[letterIndex % 26];
        ++letterIndex;
        // Signal the main thread to print generated letter
        SetEvent(events[0]);
    }
}

void numbers() {
    static uint32_t numberIndex = 0;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 3000);

    // Wait a random amount of time, from 0 through 3000 milliseconds, between each print attempt
    while (WaitForEvent(events[2], dis(gen)) == WAIT_TIMEOUT) {
        // Remember that another instance of this function may be executing concurrently, so after
        // the sleep finishes, make sure to obtain exclusive access by means of this auto-reset
        // event.
        auto waitResult = WaitForEvent(events[4]); // only one thread here at a time
        assert(waitResult == 0);
        number = numberIndex;
        ++numberIndex;
        // Signal the main thread to print generated number
        SetEvent(events[1]);
    }
}

int main() {
    events[0] = CreateEvent(); // letter available auto-reset event, initially unavailable
    events[1] = CreateEvent(); // number available auto-reset event, initially unavailable
    events[2] = CreateEvent(true, false); // abort manual-reset event
    events[3] = CreateEvent(
        false,
        true); // letter protection auto-reset event (instead of a mutex), initially available
    events[4] = CreateEvent(
        false,
        true); // number protection auto-reset event (instead of a mutex), initially available

#if !defined(_WIN32)
    // After the abort event has been created:
    struct sigaction act {};
    act.sa_handler = intHandler; // trigger abort on ctrl+c
    sigaction(SIGINT, &act, NULL);
#else
    SetConsoleCtrlHandler((PHANDLER_ROUTINE)intHandler, true);
#endif

    vector<std::thread> threads;

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(1, 10);

    uint32_t letterThreadCount = dis(gen);
    uint32_t numberThreadCount = dis(gen);

    for (uint32_t i = 0; i < letterThreadCount; ++i) {
        threads.emplace_back(letters);
    }
    for (uint32_t i = 0; i < numberThreadCount; ++i) {
        threads.emplace_back(numbers);
    }

    printf("Started %u letter threads\n", letterThreadCount);
    printf("Started %u number threads\n", numberThreadCount);

    for (uint32_t i = 0; lastChar != 'Z'; ++i) {
        if (interrupted) {
            printf("Interrupt triggered.. Aborting!\n");
            break;
        }

        int index = -1;
        int result = WaitForMultipleEvents(events, 2, false, WAIT_INFINITE, index);

        if (result == WAIT_TIMEOUT) {
            cout << "Timeout!" << endl;
            assert(false);
        } else if (result != 0) {
            cout << "Error in wait!" << endl;
            assert(false);
        } else if (index == 0) {
            // printf("lastChar: %c, char: %c\n", lastChar, letter);
            assert(letter == 'A' || (lastChar + 1) == letter);
            cout << letter << endl;
            lastChar = letter;
            // Declare it is safe for another thread to enter this loop
            SetEvent(events[3]);
        } else if (index == 1) {
            // printf("lastNum: %d, num: %d\n", lastNum, number);
            assert(number == 0 || lastNum + 1 == number);
            cout << number << endl;
            lastNum = number;
            SetEvent(events[4]); // safe for another thread to enter this loop
        } else {
            cout << "ERROR! Unexpected index: " << index << endl;
            exit(-1);
        }
    }

    // You can't just DestroyEvent() and exit - it'll segfault because `letters()` and `numbers()`
    // will end up calling `SetEvent()` on a destroyed event.
    // You must *never* call `SetEvent`/`ResetEvent` on a destroyed event, so we set an abort event
    // and wait for the helper threads to exit.

    // Signal the abort
    SetEvent(events[2]);

    for (auto &thread : threads) {
        thread.join();
    }

    // Only after we've guaranteed that all usage of the events has ceased:
    for (auto event : events) {
        DestroyEvent(event);
    }
}
