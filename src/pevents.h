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
    struct pevent_t_;
    typedef pevent_t_ *pevent_t;

    // Constant declarations
    const uint64_t WAIT_INFINITE = ~((uint64_t)0);

    // Function declarations
    pevent_t CreateEvent(bool manualReset = false, bool initialState = false);
    int DestroyEvent(pevent_t event);
    int WaitForEvent(pevent_t event, uint64_t milliseconds = WAIT_INFINITE);
    int SetEvent(pevent_t event);
    int ResetEvent(pevent_t event);
#ifdef WFMO
    int WaitForMultipleEvents(pevent_t *events, int count, bool waitAll,
                              uint64_t milliseconds);
    int WaitForMultipleEvents(pevent_t *events, int count, bool waitAll,
                              uint64_t milliseconds, int &index);
#endif
#ifdef PULSE
    int PulseEvent(pevent_t event);
#endif
} // namespace neosmart
