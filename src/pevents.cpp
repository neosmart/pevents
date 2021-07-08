/*
 * WIN32 Events for POSIX
 * Author: Mahmoud Al-Qudsi <mqudsi@neosmart.net>
 * Copyright (C) 2011 - 2019 by NeoSmart Technologies
 * This code is released under the terms of the MIT License
 */

#ifndef _WIN32

#include "pevents.h"
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/time.h>
#include <atomic>
#include <memory>
#ifdef WFMO
#include <algorithm>
#include <deque>
#endif

namespace neosmart {
#ifdef WFMO
    // Each call to WaitForMultipleObjects initializes a neosmart_wfmo_t object which tracks
    // the progress of the caller's multi-object wait and dispatches responses accordingly.
    // One neosmart_wfmo_t struct is shared for all events in a single WFMO call
    struct neosmart_wfmo_t_ {
        pthread_mutex_t Mutex;
        pthread_cond_t CVariable;
        std::atomic<int> RefCount;
        union {
            int FiredEvent; // WFSO
            int EventsLeft; // WFMO
        } Status;
        bool WaitAll;
        bool StillWaiting;

        void Destroy() {
            pthread_mutex_destroy(&Mutex);
            pthread_cond_destroy(&CVariable);
        }
    };
    typedef neosmart_wfmo_t_ *neosmart_wfmo_t;

    // A neosmart_wfmo_info_t object is registered with each event waited on in a WFMO
    // This reference to neosmart_wfmo_t_ is how the event knows whom to notify when triggered
    struct neosmart_wfmo_info_t_ {
        neosmart_wfmo_t Waiter;
        int WaitIndex;
    };
    typedef neosmart_wfmo_info_t_ *neosmart_wfmo_info_t;
#endif // WFMO

    // The basic event structure, passed to the caller as an opaque pointer when creating events
    struct neosmart_event_t_ {
        pthread_cond_t CVariable;
        pthread_mutex_t Mutex;
        bool AutoReset;
        std::atomic<bool> State;
#ifdef WFMO
        std::deque<neosmart_wfmo_info_t_> RegisteredWaits;
#endif
    };

#ifdef WFMO
    static bool RemoveExpiredWaitHelper(neosmart_wfmo_info_t_ wait) {
        if (wait.Waiter->StillWaiting) {
            return false;
        }

        int ref_count = --wait.Waiter->RefCount;
        assert(ref_count >= 0);

        if (ref_count == 0) {
            wait.Waiter->Destroy();
            delete wait.Waiter;
        }
        return true;
    }
#endif // WFMO

    neosmart_event_t CreateEvent(bool manualReset, bool initialState) {
        neosmart_event_t event = new neosmart_event_t_;

        int result = pthread_cond_init(&event->CVariable, 0);
        assert(result == 0);

        result = pthread_mutex_init(&event->Mutex, 0);
        assert(result == 0);

        // memory_order_relaxed: Newly created event is guaranteed to not have any waiters
        event->State.store(false, std::memory_order_relaxed);
        event->AutoReset = !manualReset;

        if (initialState) {
            result = SetEvent(event);
            assert(result == 0);
        }

        return event;
    }

    static int UnlockedWaitForEvent(neosmart_event_t event, uint64_t milliseconds) {
        int result = 0;
        // memory_order_relaxed: unlocking/ordering is guaranteed prior to calling this function
        if (!event->State.load(std::memory_order_relaxed)) {
            // Zero-timeout event state check optimization
            if (milliseconds == 0) {
                return WAIT_TIMEOUT;
            }

            timespec ts;
            if (milliseconds != WAIT_INFINITE) {
                timeval tv;
                gettimeofday(&tv, NULL);

                uint64_t nanoseconds = ((uint64_t)tv.tv_sec) * 1000 * 1000 * 1000 +
                                       milliseconds * 1000 * 1000 + ((uint64_t)tv.tv_usec) * 1000;

                ts.tv_sec = (time_t) (nanoseconds / 1000 / 1000 / 1000);
                ts.tv_nsec = (long) (nanoseconds - ((uint64_t)ts.tv_sec) * 1000 * 1000 * 1000);
            }

            do {
                // Regardless of whether it's an auto-reset or manual-reset event:
                // wait to obtain the event, then lock anyone else out
                if (milliseconds != WAIT_INFINITE) {
                    result = pthread_cond_timedwait(&event->CVariable, &event->Mutex, &ts);
                } else {
                    result = pthread_cond_wait(&event->CVariable, &event->Mutex);
                }
                // memory_order_relaxed: ordering is guaranteed by the mutex
            } while (result == 0 && !event->State.load(std::memory_order_relaxed));

            if (result == 0 && event->AutoReset) {
                // We've only accquired the event if the wait succeeded
                // memory_order_release: Prevent overlapping/interleaved Set/Reset contexts
                event->State.store(false, std::memory_order_release);
            }
        } else if (event->AutoReset) {
            // It's an auto-reset event that's currently available;
            // we need to stop anyone else from using it
            result = 0;
            // memory_order_release: Prevent overlapping/interleaved Set/Reset contexts
            event->State.store(false, std::memory_order_release);
        }
        // Else we're trying to obtain a manual reset event with a signaled state;
        // don't do anything

        return result;
    }

    int WaitForEvent(neosmart_event_t event, uint64_t milliseconds) {
        // Optimization: bypass acquiring the event lock if the state atomic is unavailable.
        // memory_order_relaxed: This is just an optimization, it's OK to be biased towards a stale
        // value here, and preferable to synchronizing CPU caches to get a more accurate result.
        if (milliseconds == 0 && !event->State.load(std::memory_order_relaxed)) {
            return WAIT_TIMEOUT;
        } else {
            int result = pthread_mutex_lock(&event->Mutex);
            assert(result == 0);
        }

        int result = UnlockedWaitForEvent(event, milliseconds);

        int tempResult = pthread_mutex_unlock(&event->Mutex);
        assert(tempResult == 0);

        return result;
    }

#ifdef WFMO
    int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll,
                              uint64_t milliseconds) {
        int unused;
        return WaitForMultipleEvents(events, count, waitAll, milliseconds, unused);
    }

    int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll,
                              uint64_t milliseconds, int &waitIndex) {
        neosmart_wfmo_t wfmo = new neosmart_wfmo_t_;

        int result = 0;
        int tempResult = pthread_mutex_init(&wfmo->Mutex, 0);
        assert(tempResult == 0);

        tempResult = pthread_cond_init(&wfmo->CVariable, 0);
        assert(tempResult == 0);

        neosmart_wfmo_info_t_ waitInfo;
        waitInfo.Waiter = wfmo;
        waitInfo.WaitIndex = -1;

        wfmo->WaitAll = waitAll;
        wfmo->StillWaiting = true;
        wfmo->RefCount = 1;

        if (waitAll) {
            wfmo->Status.EventsLeft = count;
        } else {
            wfmo->Status.FiredEvent = -1;
        }

        tempResult = pthread_mutex_lock(&wfmo->Mutex);
        assert(tempResult == 0);

        bool done = false;
        waitIndex = -1;

        for (int i = 0; i < count; ++i) {
            waitInfo.WaitIndex = i;

            // Must not release lock until RegisteredWait is potentially added
            tempResult = pthread_mutex_lock(&events[i]->Mutex);
            assert(tempResult == 0);

            // Before adding this wait to the list of registered waits, let's clean up old, expired
            // waits while we have the event lock anyway
            events[i]->RegisteredWaits.erase(std::remove_if(events[i]->RegisteredWaits.begin(),
                                                            events[i]->RegisteredWaits.end(),
                                                            RemoveExpiredWaitHelper),
                                             events[i]->RegisteredWaits.end());

            if (UnlockedWaitForEvent(events[i], 0) == 0) {
                tempResult = pthread_mutex_unlock(&events[i]->Mutex);
                assert(tempResult == 0);

                if (waitAll) {
                    --wfmo->Status.EventsLeft;
                    assert(wfmo->Status.EventsLeft >= 0);
                } else {
                    wfmo->Status.FiredEvent = i;
                    waitIndex = i;
                    done = true;
                    break;
                }
            } else {
                events[i]->RegisteredWaits.push_back(waitInfo);
                wfmo->RefCount.fetch_add(1, std::memory_order_relaxed);

                tempResult = pthread_mutex_unlock(&events[i]->Mutex);
                assert(tempResult == 0);
            }
        }

        // We set the `done` flag above in case of WaitAny and at least one event was set.
        // But we need to check again here if we were doing a WaitAll or else we'll incorrectly
        // return WAIT_TIMEOUT.
        if (waitAll && wfmo->Status.EventsLeft == 0) {
            done = true;
        }

        timespec ts;
        if (!done) {
            if (milliseconds == 0) {
                result = WAIT_TIMEOUT;
                done = true;
            } else if (milliseconds != WAIT_INFINITE) {
                timeval tv;
                gettimeofday(&tv, NULL);

                uint64_t nanoseconds = ((uint64_t)tv.tv_sec) * 1000 * 1000 * 1000 +
                                       milliseconds * 1000 * 1000 + ((uint64_t)tv.tv_usec) * 1000;

                ts.tv_sec = (time_t) (nanoseconds / 1000 / 1000 / 1000);
                ts.tv_nsec = (long) (nanoseconds - ((uint64_t)ts.tv_sec) * 1000 * 1000 * 1000);
            }
        }

        while (!done) {
            // One (or more) of the events we're monitoring has been triggered?

            // If we're waiting for all events, assume we're done and check if there's an event that
            // hasn't fired But if we're waiting for just one event, assume we're not done until we
            // find a fired event
            done = (waitAll && wfmo->Status.EventsLeft == 0) ||
                   (!waitAll && wfmo->Status.FiredEvent != -1);

            if (!done) {
                if (milliseconds != WAIT_INFINITE) {
                    result = pthread_cond_timedwait(&wfmo->CVariable, &wfmo->Mutex, &ts);
                } else {
                    result = pthread_cond_wait(&wfmo->CVariable, &wfmo->Mutex);
                }

                if (result != 0) {
                    break;
                }
            }
        }

        waitIndex = wfmo->Status.FiredEvent;
        wfmo->StillWaiting = false;

        tempResult = pthread_mutex_unlock(&wfmo->Mutex);
        assert(tempResult == 0);

        int ref_count = --wfmo->RefCount;
        assert(ref_count >= 0);
        if (ref_count == 0) {
            wfmo->Destroy();
            delete wfmo;
        }

        return result;
    }
#endif // WFMO

    int DestroyEvent(neosmart_event_t event) {
        int result = 0;

#ifdef WFMO
        result = pthread_mutex_lock(&event->Mutex);
        assert(result == 0);
        event->RegisteredWaits.erase(std::remove_if(event->RegisteredWaits.begin(),
                                                    event->RegisteredWaits.end(),
                                                    RemoveExpiredWaitHelper),
                                     event->RegisteredWaits.end());
        result = pthread_mutex_unlock(&event->Mutex);
        assert(result == 0);
#endif

        result = pthread_cond_destroy(&event->CVariable);
        assert(result == 0);

        result = pthread_mutex_destroy(&event->Mutex);
        assert(result == 0);

        delete event;

        return 0;
    }

    int SetEvent(neosmart_event_t event) {
        int result = pthread_mutex_lock(&event->Mutex);
        assert(result == 0);

        // memory_order_release: Unblock threads waiting for the event
        event->State.store(true, std::memory_order_release);

        // Depending on the event type, we either trigger everyone or only one
        if (event->AutoReset) {
#ifdef WFMO
            while (!event->RegisteredWaits.empty()) {
                neosmart_wfmo_info_t i = &event->RegisteredWaits.front();

                result = pthread_mutex_lock(&i->Waiter->Mutex);
                assert(result == 0);

                int ref_count = --i->Waiter->RefCount;
                assert(ref_count >= 0);
                if (!i->Waiter->StillWaiting) {
                    result = pthread_mutex_unlock(&i->Waiter->Mutex);
                    assert(result == 0);

                    if (ref_count == 0) {
                        i->Waiter->Destroy();
                        delete i->Waiter;
                    }
                    event->RegisteredWaits.pop_front();
                    continue;
                }
                else {
                    assert(ref_count > 0);
                }

                if (i->Waiter->WaitAll) {
                    --i->Waiter->Status.EventsLeft;
                    assert(i->Waiter->Status.EventsLeft >= 0);
                    // We technically should do i->Waiter->StillWaiting = Waiter->Status.EventsLeft
                    // != 0 but the only time it'll be equal to zero is if we're the last event, so
                    // no one else will be checking the StillWaiting flag. We're good to go without
                    // it.
                } else {
                    i->Waiter->Status.FiredEvent = i->WaitIndex;
                    i->Waiter->StillWaiting = false;
                }

                result = pthread_mutex_unlock(&i->Waiter->Mutex);
                assert(result == 0);

                result = pthread_cond_signal(&i->Waiter->CVariable);
                assert(result == 0);

                event->RegisteredWaits.pop_front();

                // memory_order_release: Prevent overlapping of sequential Set/Reset states.
                event->State.store(false, std::memory_order_release);

                result = pthread_mutex_unlock(&event->Mutex);
                assert(result == 0);

                return 0;
            }
#endif // WFMO
            // event->State can be false if compiled with WFMO support
            // memory_order_relaxed: ordering is ensured by the mutex
            if (event->State.load(std::memory_order_relaxed)) {
                result = pthread_mutex_unlock(&event->Mutex);
                assert(result == 0);

                result = pthread_cond_signal(&event->CVariable);
                assert(result == 0);

                return 0;
            }
        } else { // this is a manual reset event
#ifdef WFMO
            for (size_t i = 0; i < event->RegisteredWaits.size(); ++i) {
                neosmart_wfmo_info_t info = &event->RegisteredWaits[i];

                result = pthread_mutex_lock(&info->Waiter->Mutex);
                assert(result == 0);

                --info->Waiter->RefCount;
                assert(info->Waiter->RefCount >= 0);

                if (!info->Waiter->StillWaiting) {
                    bool destroy = info->Waiter->RefCount == 0;
                    result = pthread_mutex_unlock(&info->Waiter->Mutex);
                    assert(result == 0);
                    if (destroy) {
                        info->Waiter->Destroy();
                        delete info->Waiter;
                    }
                    continue;
                }

                if (info->Waiter->WaitAll) {
                    --info->Waiter->Status.EventsLeft;
                    assert(info->Waiter->Status.EventsLeft >= 0);
                    // We technically should do i->Waiter->StillWaiting = Waiter->Status.EventsLeft
                    // != 0 but the only time it'll be equal to zero is if we're the last event, so
                    // no one else will be checking the StillWaiting flag. We're good to go without
                    // it.
                } else {
                    info->Waiter->Status.FiredEvent = info->WaitIndex;
                    info->Waiter->StillWaiting = false;
                }

                result = pthread_mutex_unlock(&info->Waiter->Mutex);
                assert(result == 0);

                result = pthread_cond_signal(&info->Waiter->CVariable);
                assert(result == 0);
            }
            event->RegisteredWaits.clear();
#endif // WFMO
            result = pthread_mutex_unlock(&event->Mutex);
            assert(result == 0);

            result = pthread_cond_broadcast(&event->CVariable);
            assert(result == 0);
        }

        return 0;
    }

    int ResetEvent(neosmart_event_t event) {
        int result = pthread_mutex_lock(&event->Mutex);
        assert(result == 0);

        event->State.store(false, std::memory_order_release);

        result = pthread_mutex_unlock(&event->Mutex);
        assert(result == 0);

        return 0;
    }

#ifdef PULSE
    int PulseEvent(neosmart_event_t event) {
        // This may look like it's a horribly inefficient kludge with the sole intention of reducing
        // code duplication, but in reality this is what any PulseEvent() implementation must look
        // like. The only overhead (function calls aside, which your compiler will likely optimize
        // away, anyway), is if only WFMO auto-reset waits are active there will be overhead to
        // unnecessarily obtain the event mutex for ResetEvent() after. In all other cases (being no
        // pending waits, WFMO manual-reset waits, or any WFSO waits), the event mutex must first be
        // released for the waiting thread to resume action prior to locking the mutex again in
        // order to set the event state to unsignaled, or else the waiting threads will loop back
        // into a wait (due to checks for spurious CVariable wakeups).

        int result = SetEvent(event);
        assert(result == 0);
        result = ResetEvent(event);
        assert(result == 0);

        return 0;
    }
#endif
} // namespace neosmart

#else // _WIN32

#include <Windows.h>
#include "pevents.h"

namespace neosmart {
    neosmart_event_t CreateEvent(bool manualReset, bool initialState) {
        return static_cast<neosmart_event_t>(::CreateEvent(NULL, manualReset, initialState, NULL));
    }

    int DestroyEvent(neosmart_event_t event) {
        HANDLE handle = static_cast<HANDLE>(event);
        return CloseHandle(handle) ? 0 : GetLastError();
    }

    int WaitForEvent(neosmart_event_t event, uint64_t milliseconds) {
        uint32_t result = 0;
        HANDLE handle = static_cast<HANDLE>(event);

        // WaitForSingleObject(Ex) and WaitForMultipleObjects(Ex) only support 32-bit timeout
        if (milliseconds == WAIT_INFINITE || (milliseconds >> 32) == 0) {
            result = WaitForSingleObject(handle, static_cast<uint32_t>(milliseconds));
        } else {
            // Cannot wait for 0xFFFFFFFF because that means infinity to WIN32
            uint32_t waitUnit = static_cast<uint32_t>(WAIT_INFINITE) - 1;
            uint64_t rounds = milliseconds / waitUnit;
            uint32_t remainder = milliseconds % waitUnit;

            result = WaitForSingleObject(handle, remainder);
            while (result == WAIT_TIMEOUT && rounds-- != 0) {
                result = WaitForSingleObject(handle, waitUnit);
            }
        }

        if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED) {
            // We must swallow WAIT_ABANDONED because there is no such equivalent on *nix
            return 0;
        }

        if (result == WAIT_TIMEOUT) {
            return WAIT_TIMEOUT;
        }

        return GetLastError();
    }

    int SetEvent(neosmart_event_t event) {
        HANDLE handle = static_cast<HANDLE>(event);
        return ::SetEvent(handle) ? 0 : GetLastError();
    }

    int ResetEvent(neosmart_event_t event) {
        HANDLE handle = static_cast<HANDLE>(event);
        return ::ResetEvent(handle) ? 0 : GetLastError();
    }

#ifdef WFMO
    int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll,
                              uint64_t milliseconds) {
        int index = 0;
        return WaitForMultipleEvents(events, count, waitAll, milliseconds, index);
    }

    int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll,
                              uint64_t milliseconds, int &index) {
        HANDLE *handles = reinterpret_cast<HANDLE *>(events);
        uint32_t result = 0;

        // WaitForSingleObject(Ex) and WaitForMultipleObjects(Ex) only support 32-bit timeout
        if (milliseconds == WAIT_INFINITE || (milliseconds >> 32) == 0) {
            result = WaitForMultipleObjects(count, handles, waitAll,
                                            static_cast<uint32_t>(milliseconds));
        } else {
            // Cannot wait for 0xFFFFFFFF because that means infinity to WIN32
            uint32_t waitUnit = static_cast<uint32_t>(WAIT_INFINITE) - 1;
            uint64_t rounds = milliseconds / waitUnit;
            uint32_t remainder = milliseconds % waitUnit;

            result = WaitForMultipleObjects(count, handles, waitAll, remainder);
            while (result == WAIT_TIMEOUT && rounds-- != 0) {
                result = WaitForMultipleObjects(count, handles, waitAll, waitUnit);
            }
        }

        if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count) {
            index = result - WAIT_OBJECT_0;
            return 0;
        } else if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + count) {
            index = result - WAIT_ABANDONED_0;
            return 0;
        }

        if (result == WAIT_FAILED) {
            return GetLastError();
        }
        return result;
    }
#endif

#ifdef PULSE
    int PulseEvent(neosmart_event_t event) {
        HANDLE handle = static_cast<HANDLE>(event);
        return ::PulseEvent(handle) ? 0 : GetLastError();
    }
#endif
} // namespace neosmart

#endif //_WIN32
