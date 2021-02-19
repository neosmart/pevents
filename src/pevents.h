/*
 * WIN32 Events for POSIX
 * Author: Mahmoud Al-Qudsi <mqudsi@neosmart.net>
 * Copyright (C) 2011 - 2019 by NeoSmart Technologies
 * This code is released under the terms of the MIT License
 */

#pragma once

#if defined(_WIN32) && !defined(CreateEvent)
#error Must include Windows.h prior to including pevents.h!
#endif
#ifndef WAIT_TIMEOUT
#include <errno.h>
#define WAIT_TIMEOUT ETIMEDOUT
#endif

#include <stdint.h>

namespace neosmart {
    // Type declarations
    struct pthread_event_t_;
    typedef pthread_event_t_ *pthread_event_t;

    // Constant declarations
    const uint64_t WAIT_INFINITE = ~((unsigned long long)0);

    // Function declarations
    pthread_event_t CreateEvent(bool manualReset = false, bool initialState = false);
    int DestroyEvent(pthread_event_t event);
    int WaitForEvent(pthread_event_t event, uint64_t milliseconds = WAIT_INFINITE);
    int SetEvent(pthread_event_t event);
    int ResetEvent(pthread_event_t event);
#ifdef WFMO
    int WaitForMultipleEvents(pthread_event_t *events, int count, bool waitAll,
                              uint64_t milliseconds);
    int WaitForMultipleEvents(pthread_event_t *events, int count, bool waitAll,
                              uint64_t milliseconds, int &index);
#endif
#ifdef PULSE
    int PulseEvent(pthread_event_t event);
#endif
} // namespace neosmart
