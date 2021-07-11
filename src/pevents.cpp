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
        std::atomic<bool> StillWaiting;

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
        if (wait.Waiter->StillWaiting.load(std::memory_order_relaxed)) {
            return false;
        }

        int ref_count = wait.Waiter->RefCount.fetch_sub(1, std::memory_order_acq_rel);
        assert(ref_count > 0);

        if (ref_count == 1) {
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

        event->AutoReset = !manualReset;
        // memory_order_release: if `initialState == true`, allow a load with acquire semantics to
        // see the value.
        event->State.store(initialState, std::memory_order_release);

        return event;
    }

    static int UnlockedWaitForEvent(neosmart_event_t event, uint64_t milliseconds) {
        int result = 0;
        // memory_order_relaxed: `State` is only set to true with the mutex held, and we require
        // that this function only be called after the mutex is obtained.
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
                // memory_order_relaxed: ordering is guaranteed by the mutex, as `State = true` is
                // only ever written with the mutex held.
            } while (result == 0 && !event->State.load(std::memory_order_relaxed));
        } else if (event->AutoReset) {
            // It's an auto-reset event that's currently available;
            // we need to stop anyone else from using it
            result = 0;
        }
        else {
            // We're trying to obtain a manual reset event with a signaled state; don't do anything
        }

        if (result == 0 && event->AutoReset) {
            // We've only accquired the event if the wait succeeded
            // memory_order_relaxed: we never act on `State == true` without fully synchronizing
            // or grabbing the mutex, so it's OK to use relaxed semantics here.
            event->State.store(false, std::memory_order_relaxed);
        }

        return result;
    }

    int WaitForEvent(neosmart_event_t event, uint64_t milliseconds) {
        // Optimization: bypass acquiring the event lock if the state atomic is unavailable.
        // memory_order_relaxed: This is just an optimization, it's OK to be biased towards a stale
        // value here, and preferable to synchronizing CPU caches to get a more accurate result.
        if (milliseconds == 0 && !event->State.load(std::memory_order_relaxed)) {
            return WAIT_TIMEOUT;
        }
        // Optimization: early return in case of success for manual reset events only.
        if (!event->AutoReset && event->State.load(std::memory_order_relaxed)) {
            // A memory barrier is required here. This is still cheaper than a syscall.
            // See https://github.com/neosmart/pevents/issues/18
            if (event->State.load(std::memory_order_acquire)) {
                return 0;
            }
        }

        int tempResult = pthread_mutex_lock(&event->Mutex);
        assert(tempResult == 0);

        int result = UnlockedWaitForEvent(event, milliseconds);

        tempResult = pthread_mutex_unlock(&event->Mutex);
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

        if (waitAll) {
            wfmo->Status.EventsLeft = count;
        } else {
            wfmo->Status.FiredEvent = -1;
        }

        wfmo->WaitAll = waitAll;
        wfmo->StillWaiting.store(true, std::memory_order_release);
        // memory_order_release: this is the initial value other threads should see
        wfmo->RefCount.store(1 + count, std::memory_order_release);
        // Separately keep track of how many refs to decrement after the initialization loop, to
        // avoid repeatedly clearing the cache line.
        int skipped_refs = 0;

        tempResult = pthread_mutex_lock(&wfmo->Mutex);
        assert(tempResult == 0);

        bool done = false;
        waitIndex = -1;

        for (int i = 0; i < count; ++i) {
            waitInfo.WaitIndex = i;

            // Skip obtaining the mutex for manual reset events. This requires a memory barrier to
            // ensure correctness.
            bool skipLock = false;
            if (!events[i]->AutoReset) {
                if (events[i]->State.load(std::memory_order_relaxed) &&
                    events[i]->State.load(std::memory_order_acquire)) {
                    skipLock = true;
                }
            }

            if (!skipLock) {
                // Must not release lock until RegisteredWait is potentially added
                tempResult = pthread_mutex_lock(&events[i]->Mutex);
                assert(tempResult == 0);

                // Before adding this wait to the list of registered waits, let's clean up old,
                // expired waits while we have the event lock anyway.
                events[i]->RegisteredWaits.erase(std::remove_if(events[i]->RegisteredWaits.begin(),
                                                                events[i]->RegisteredWaits.end(),
                                                                RemoveExpiredWaitHelper),
                                                 events[i]->RegisteredWaits.end());
            }

            if (skipLock || UnlockedWaitForEvent(events[i], 0) == 0) {
                if (!skipLock) {
                    tempResult = pthread_mutex_unlock(&events[i]->Mutex);
                    assert(tempResult == 0);
                }

                if (waitAll) {
                    ++skipped_refs;
                    --wfmo->Status.EventsLeft;
                    assert(wfmo->Status.EventsLeft >= 0);
                } else {
                    skipped_refs += (count - i);
                    wfmo->Status.FiredEvent = i;
                    waitIndex = i;
                    done = true;
                    break;
                }
            } else {
                events[i]->RegisteredWaits.push_back(waitInfo);

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
        // memory_order_relaxed: this is only checked outside the mutex to determine if waiting has
        // terminated meaning it's safe to decrement the ref count. If it's true (which we write
        // with release semantics), then the mutex is always entered.
        wfmo->StillWaiting.store(false, std::memory_order_relaxed);

        tempResult = pthread_mutex_unlock(&wfmo->Mutex);
        assert(tempResult == 0);

        // memory_order_seq_cst: Ensure this is run after the wfmo mutex is unlocked
        int ref_count = wfmo->RefCount.fetch_sub(1 + skipped_refs, std::memory_order_seq_cst);
        assert(ref_count > 0);
        if (ref_count == 1 + skipped_refs) {
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

        // Depending on the event type, we either trigger everyone or only one
        if (event->AutoReset) {
#ifdef WFMO
            while (!event->RegisteredWaits.empty()) {
                neosmart_wfmo_info_t i = &event->RegisteredWaits.front();

                // memory_order_relaxed: this is just an optimization to see if it is OK to skip
                // this waiter, and if it's observed to be false then it's OK to bypass the mutex at
                // that point.
                if (!i->Waiter->StillWaiting.load(std::memory_order_relaxed)) {
                    int ref_count = i->Waiter->RefCount.fetch_sub(1, std::memory_order_acq_rel);
                    assert(ref_count > 0);
                    if (ref_count == 1) {
                        i->Waiter->Destroy();
                        delete i->Waiter;
                    }

                    event->RegisteredWaits.pop_front();
                    continue;
                }

                result = pthread_mutex_lock(&i->Waiter->Mutex);
                assert(result == 0);

                // We have to check `Waiter->StillWaiting` twice, once before locking as an
                // optimization to bypass the mutex altogether, and then again after locking the
                // WFMO mutex because we could have !waitAll and another event could have ended the
                // wait, in which case we must not unlock the same waiter or else a SetEvent() call
                // on an auto-reset event may end up with a lost wakeup.
                if (!i->Waiter->StillWaiting.load(std::memory_order_relaxed)) {
                    result = pthread_mutex_unlock(&i->Waiter->Mutex);
                    assert(result == 0);

                    // memory_order_seq_cst: Ensure this is run after the wfmo mutex is unlocked
                    int ref_count = i->Waiter->RefCount.fetch_sub(1, std::memory_order_seq_cst);
                    assert(ref_count > 0);
                    if (ref_count == 1) {
                        i->Waiter->Destroy();
                        delete i->Waiter;
                    }

                    event->RegisteredWaits.pop_front();
                    continue;
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
                    // memory_order_relaxed: The flip to false is only lazily observed as an
                    // optimization to bypass the mutex for cleanup.
                    i->Waiter->StillWaiting.store(false, std::memory_order_relaxed);
                }

                result = pthread_mutex_unlock(&i->Waiter->Mutex);
                assert(result == 0);

                result = pthread_cond_signal(&i->Waiter->CVariable);
                assert(result == 0);

                // memory_order_seq_cst: Ensure this is run after the wfmo mutex is unlocked
                int ref_count = i->Waiter->RefCount.fetch_sub(1, std::memory_order_seq_cst);
                assert(ref_count > 0);
                if (ref_count == 1) {
                    i->Waiter->Destroy();
                    delete i->Waiter;
                }

                event->RegisteredWaits.pop_front();

                result = pthread_mutex_unlock(&event->Mutex);
                assert(result == 0);

                return 0;
            }
#endif // WFMO
            // memory_order_release: this is the synchronization point for any threads spin-waiting
            // for the event to become available.
            event->State.store(true, std::memory_order_release);

            result = pthread_mutex_unlock(&event->Mutex);
            assert(result == 0);

            result = pthread_cond_signal(&event->CVariable);
            assert(result == 0);

            return 0;
        } else { // this is a manual reset event
            // memory_order_release: this is the synchronization point for any threads spin-waiting
            // for the event to become available.
            event->State.store(true, std::memory_order_release);
#ifdef WFMO
            for (size_t i = 0; i < event->RegisteredWaits.size(); ++i) {
                neosmart_wfmo_info_t info = &event->RegisteredWaits[i];

                // memory_order_relaxed: this is just an optimization to see if it is OK to skip
                // this waiter, and if it's observed to be false then it's OK to bypass the mutex at
                // that point.
                if (!info->Waiter->StillWaiting.load(std::memory_order_relaxed)) {
                    int ref_count = info->Waiter->RefCount.fetch_sub(1, std::memory_order_acq_rel);
                    if (ref_count == 1) {
                        info->Waiter->Destroy();
                        delete info->Waiter;
                    }
                    continue;
                }

                result = pthread_mutex_lock(&info->Waiter->Mutex);
                assert(result == 0);

                // Waiter->StillWaiting may have become true by now, but we're just going to pretend
                // it hasn't. So long as we hold a reference to the WFMO, this is safe since manual
                // reset events are not one-time use.

                if (info->Waiter->WaitAll) {
                    --info->Waiter->Status.EventsLeft;
                    assert(info->Waiter->Status.EventsLeft >= 0);
                    // We technically should do i->Waiter->StillWaiting = Waiter->Status.EventsLeft
                    // != 0 but the only time it'll be equal to zero is if we're the last event, so
                    // no one else will be checking the StillWaiting flag. We're good to go without
                    // it.
                } else {
                    info->Waiter->Status.FiredEvent = info->WaitIndex;
                    // memory_order_relaxed: The flip to false is only lazily observed as an
                    // optimization to bypass the mutex for cleanup.
                    info->Waiter->StillWaiting.store(false, std::memory_order_relaxed);
                }

                result = pthread_mutex_unlock(&info->Waiter->Mutex);
                assert(result == 0);

                result = pthread_cond_signal(&info->Waiter->CVariable);
                assert(result == 0);

                // memory_order_seq_cst: Ensure this is run after the wfmo mutex is unlocked
                int ref_count = info->Waiter->RefCount.fetch_sub(1, std::memory_order_seq_cst);
                assert(ref_count > 0);
                if (ref_count == 1) {
                    info->Waiter->Destroy();
                    delete info->Waiter;
                }
                continue;
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
        // memory_order_relaxed and no mutex: there can't be any guarantees about concurrent calls
        // to either of WFMO()/SetEvent() and ResetEvent() because they're racy by nature. Only the
        // behavior of concurrent WFMO() and SetEvent() calls is strongly defined.
        event->State.store(false, std::memory_order_relaxed);
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
