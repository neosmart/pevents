/*
 * WIN32 Events for POSIX
 * Author: Mahmoud Al-Qudsi <mqudsi@neosmart.net>
 * Copyright (C) 2011 - 2015 by NeoSmart Technologies
 * This code is released under the terms of the MIT License
*/

#ifndef _WIN32

#include "pevents.h"
#include <assert.h>
#include <errno.h>
#include <sys/time.h>
#include <pthread.h>
#include <stdio.h>

//How many WFMO registered waits to allocate room for on event creation
#define REGISTERED_WAIT_PREALLOC 1

namespace neosmart
{
#ifdef WFMO
	//Each call to WaitForMultipleObjects initializes a neosmart_wfmo_t object which tracks
	//the progress of the caller's multi-object wait and dispatches responses accordingly.
	//One neosmart_wfmo_t struct is shared for all events in a single WFMO call
	struct neosmart_wfmo_t_
	{
		pthread_mutex_t Mutex;
		pthread_cond_t CVariable;
		union
		{
			neosmart_event_t_ *FiredEvent; //WFSO or WFMO w/ !WaitAll
			int EventsLeft; //WFMO
		} Status;
		bool WaitAll;
	};
	typedef neosmart_wfmo_t_ *neosmart_wfmo_t;
#endif

	//The basic event structure, passed to the caller as an opaque pointer when creating events
	struct neosmart_event_t_
	{
		pthread_cond_t CVariable;
		pthread_mutex_t Mutex;
#ifdef WFMO
		neosmart_wfmo_t_ **RegisteredWaits; //array of pointers to WFMO/WFSO calls waiting on this event
#endif
		int RegisteredWaitLength;
		bool AutoReset;
		bool State;
	};

	neosmart_event_t CreateEvent(bool manualReset, bool initialState)
	{
		neosmart_event_t event = (neosmart_event_t) malloc(sizeof(neosmart_event_t_));

		int result = pthread_cond_init(&event->CVariable, 0);
		assert(result == 0);
		result = pthread_mutex_init(&event->Mutex, 0);
		assert(result == 0);

		event->State = false;
		event->AutoReset = !manualReset;
#ifdef WFMO
		//Allocate room for at least one event in our RegisteredWaits "array"
		event->RegisteredWaitLength = REGISTERED_WAIT_PREALLOC;
		event->RegisteredWaits = (neosmart_wfmo_t_ **) malloc(sizeof(neosmart_wfmo_t_ *) * event->RegisteredWaitLength);
		event->RegisteredWaits[0] = nullptr;
#endif

		if (initialState)
		{
			result = SetEvent(event);
			assert(result == 0);
		}

		return event;
	}

	int UnlockedWaitForEvent(neosmart_event_t event, uint64_t milliseconds)
	{
		int result = 0;
		if (!event->State)
		{
			//Zero-timeout event state check optimization
			if (milliseconds == 0)
			{
				return WAIT_TIMEOUT;
			}

			timespec ts;
			if (milliseconds != (uint64_t) -1)
			{
				timeval tv;
				gettimeofday(&tv, NULL);

				uint64_t nanoseconds = ((uint64_t) tv.tv_sec) * 1000 * 1000 * 1000 + milliseconds * 1000 * 1000 + ((uint64_t) tv.tv_usec) * 1000;

				ts.tv_sec = nanoseconds / 1000 / 1000 / 1000;
				ts.tv_nsec = (nanoseconds - ((uint64_t) ts.tv_sec) * 1000 * 1000 * 1000);
			}

			do
			{
				//Regardless of whether it's an auto-reset or manual-reset event:
				//wait to obtain the event, then lock anyone else out
				if (milliseconds != (uint64_t) -1)
				{
					result = pthread_cond_timedwait(&event->CVariable, &event->Mutex, &ts);
				}
				else
				{
					result = pthread_cond_wait(&event->CVariable, &event->Mutex);
				}
			} while (result == 0 && !event->State);

			if (result == 0 && event->AutoReset)
			{
				//We've only accquired the event if the wait succeeded
				event->State = false;
			}
		}
		else if (event->AutoReset)
		{
			//It's an auto-reset event that's currently available;
			//we need to stop anyone else from using it
			result = 0;
			event->State = false;
		}
		//Else we're trying to obtain a manual reset event with a signaled state;
		//don't do anything

		return result;
	}

	int WaitForEvent(neosmart_event_t event, uint64_t milliseconds)
	{
		int tempResult;
		if (milliseconds == 0)
		{
			tempResult = pthread_mutex_trylock(&event->Mutex);
			if (tempResult == EBUSY)
			{
				return WAIT_TIMEOUT;
			}
		}
		else
		{
			tempResult = pthread_mutex_lock(&event->Mutex);
		}

		assert(tempResult == 0);

		int result = UnlockedWaitForEvent(event, milliseconds);

		tempResult = pthread_mutex_unlock(&event->Mutex);
		assert(tempResult == 0);

		return result;
	}

#ifdef WFMO
	inline void UnlockedRegisterWait(neosmart_event_t_ *event, neosmart_wfmo_t_ *wait)
	{
		for (size_t i = 0; i < event->RegisteredWaitLength; ++i)
		{
			if (event->RegisteredWaits[i] == nullptr)
			{
				event->RegisteredWaits[i] = wait;
				return;
			}
		}

		//Not enough room. Need to expand the array of registered waits to fit this wait
		event->RegisteredWaits = (neosmart_wfmo_t_ **) realloc(event->RegisteredWaits, sizeof(neosmart_wfmo_t **) * event->RegisteredWaitLength * 2);
		assert(event->RegisteredWaits != nullptr);
		event->RegisteredWaits[event->RegisteredWaitLength] = wait;
		event->RegisteredWaitLength = event->RegisteredWaitLength * 2;
	}

	inline void UnlockedDeregisterWait(neosmart_event_t_ *event, neosmart_wfmo_t_ *wait)
	{
		//Since order isn't guaranteed, set this wait to last and last to nullptr
		size_t matchingIndex = -1;
		for (size_t i = 0; i < event->RegisteredWaitLength; ++i)
		{
			if (event->RegisteredWaits[i] == wait)
			{
				matchingIndex = i;
			}
			//Even if it's the same one that just matched (i.e. matching index is last index)
			if (event->RegisteredWaits[i + 1] == nullptr)
			{
				//It's hard to find a way of guaranteeing DeregisterWait isn't called for events that didn't need registration
				if (matchingIndex != -1)
				{
					assert(matchingIndex != -1);
					event->RegisteredWaits[matchingIndex] = event->RegisteredWaits[i];
					event->RegisteredWaits[i] = nullptr;
				}
				return;
			}
		}
	}

	inline void UnlockedPopWait(neosmart_event_t_ *event)
	{
		//Since order isn't guaranteed, set first to last and last to nullptr
		event->RegisteredWaits[0] = nullptr; //in case this is the only wait
		for (size_t i = 1; event->RegisteredWaits[i] != nullptr; ++i)
		{
			//Find and swap the last one
			if (event->RegisteredWaits[i + 1] == nullptr)
			{
				event->RegisteredWaits[0] = event->RegisteredWaits[i];
				event->RegisteredWaits[i] = nullptr;
				return;
			}
		}
	}

	int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll, uint64_t milliseconds)
	{
		int unused;
		return WaitForMultipleEvents(events, count, waitAll, milliseconds, unused);
	}

	int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll, uint64_t milliseconds, int &waitIndex)
	{
		neosmart_wfmo_t_ wfmo;

		int result = 0;

		int tempResult = pthread_mutex_init(&wfmo.Mutex, 0);
		assert(tempResult == 0);
		tempResult = pthread_cond_init(&wfmo.CVariable, 0);
		assert(tempResult == 0);

		wfmo.WaitAll = waitAll;
		wfmo.Status.FiredEvent = nullptr; //required for done check

		if (waitAll)
		{
			wfmo.Status.EventsLeft = count;
		}

		tempResult = pthread_mutex_lock(&wfmo.Mutex);
		assert(tempResult == 0);

		bool done = false;

		int registerRange = 0; //number of events we registered with, so we don't needlessly attempt a wait de-registration
		for (int i = 0; i < count; ++i)
		{
			//Must not release lock until RegisteredWait is potentially added
			tempResult = pthread_mutex_lock(&events[i]->Mutex);
			assert(tempResult == 0);

			//Maybe the event is already unlocked, no need to wait
			if (UnlockedWaitForEvent(events[i], 0) == 0)
			{
				tempResult = pthread_mutex_unlock(&events[i]->Mutex);
				assert(tempResult == 0);

				if (waitAll)
				{
					--wfmo.Status.EventsLeft;
					done = wfmo.Status.EventsLeft == 0;
					assert(wfmo.Status.EventsLeft >= 0);
				}
				else
				{
					wfmo.Status.FiredEvent = events[i];
					waitIndex = i;
					done = true;
					break;
				}
			}
			else
			{
				registerRange = i;
				UnlockedRegisterWait(events[i], &wfmo);

				tempResult = pthread_mutex_unlock(&events[i]->Mutex);
				assert(tempResult == 0);
			}
		}

		timespec ts;
		if (!done)
		{
			if (milliseconds == 0)
			{
				result = WAIT_TIMEOUT;
				done = true;
			}
			else if (milliseconds != (uint64_t) -1)
			{
				timeval tv;
				gettimeofday(&tv, NULL);

				uint64_t nanoseconds = ((uint64_t) tv.tv_sec) * 1000 * 1000 * 1000 + milliseconds * 1000 * 1000 + ((uint64_t) tv.tv_usec) * 1000;

				ts.tv_sec = nanoseconds / 1000 / 1000 / 1000;
				ts.tv_nsec = (nanoseconds - ((uint64_t) ts.tv_sec) * 1000 * 1000 * 1000);
			}
		}

		while (!done)
		{
			//One (or more) of the events we're monitoring has been triggered?

			//If we're waiting for all events, assume we're done and check if there's an event that hasn't fired
			//But if we're waiting for just one event, assume we're not done until we find a fired event
			done = (waitAll && wfmo.Status.EventsLeft == 0) || (!waitAll && wfmo.Status.FiredEvent != nullptr);

			if (!done)
			{
				if (milliseconds != (uint64_t) -1)
				{
					result = pthread_cond_timedwait(&wfmo.CVariable, &wfmo.Mutex, &ts);
				}
				else
				{
					result = pthread_cond_wait(&wfmo.CVariable, &wfmo.Mutex);
				}

				if (result != 0)
				{
					break;
				}
			}
		}
		assert(done);

		//Determine fired event index, unregister waits
		//TODO: change this back to i < registerRange after assertion failure is fixed
		for (int i = 0; i < count; ++i)
		{
			//A triggered event automatically de-registers triggered WFMOs on its own, so don't remove it here
			if (events[i] == wfmo.Status.FiredEvent)
			{
				waitIndex = i;
			}
			else
			{
				tempResult = pthread_mutex_lock(&events[i]->Mutex);
				assert(tempResult == 0);
				UnlockedDeregisterWait(events[i], &wfmo);
				tempResult = pthread_mutex_unlock(&events[i]->Mutex);
				assert(tempResult == 0);
			}
		}

		//Done. Release resources and get out of here.
		pthread_mutex_destroy(&wfmo.Mutex);
		pthread_cond_destroy(&wfmo.CVariable);

		return result;
	}
#endif

	int DestroyEvent(neosmart_event_t event)
	{
		int result = 0;

#ifdef WFMO
		result = pthread_mutex_lock(&event->Mutex);
		assert(result == 0);
		free(event->RegisteredWaits);
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

	int SetEvent(neosmart_event_t_ *event)
	{
		int result = pthread_mutex_lock(&event->Mutex);
		assert(result == 0);

		event->State = true;

		//Depending on the event type, we either trigger everyone or only one
		if (event->AutoReset)
		{
#ifdef WFMO
			for (neosmart_wfmo_t_ **wfmo = event->RegisteredWaits; *wfmo != nullptr; ++wfmo)
			{
				result = pthread_mutex_lock(&(*wfmo)->Mutex);
				printf("pthread_mutex_lock returned %d\n", result);
				assert(result == 0);

				event->State = false;

				if ((*wfmo)->WaitAll)
				{
					--(*wfmo)->Status.EventsLeft;
					assert((*wfmo)->Status.EventsLeft >= 0);
				}
				else
				{
					(*wfmo)->Status.FiredEvent = event;
				}

				result = pthread_mutex_unlock(&(*wfmo)->Mutex);
				assert(result == 0);
				result = pthread_cond_signal(&(*wfmo)->CVariable);
				assert(result == 0);

				UnlockedPopWait(event);

				result = pthread_mutex_unlock(&event->Mutex);
				assert(result == 0);

				return 0;
			}
#endif
			//event->State can be false if compiled with WFMO support
			if (event->State)
			{
				result = pthread_mutex_unlock(&event->Mutex);
				assert(result == 0);

				result = pthread_cond_signal(&event->CVariable);
				assert(result == 0);

				return 0;
			}
		}
		else
		{
#ifdef WFMO
			for (neosmart_wfmo_t_ **wfmo = event->RegisteredWaits; *wfmo != nullptr; ++wfmo)
			{
				result = pthread_mutex_lock(&(*wfmo)->Mutex);
				assert(result == 0);

				if ((*wfmo)->WaitAll)
				{
					--(*wfmo)->Status.EventsLeft;
					assert((*wfmo)->Status.EventsLeft >= 0);
				}
				else
				{
					(*wfmo)->Status.FiredEvent = event;
				}

				result = pthread_mutex_unlock(&(*wfmo)->Mutex);
				assert(result == 0);

				result = pthread_cond_signal(&(*wfmo)->CVariable);
				assert(result == 0);
			}
			event->RegisteredWaits[0] = nullptr;
#endif
			result = pthread_mutex_unlock(&event->Mutex);
			assert(result == 0);

			result = pthread_cond_broadcast(&event->CVariable);
			assert(result == 0);
		}

		return 0;
	}

	int ResetEvent(neosmart_event_t event)
	{
		int result = pthread_mutex_lock(&event->Mutex);
		assert(result == 0);

		event->State = false;

		result = pthread_mutex_unlock(&event->Mutex);
		assert(result == 0);

		return 0;
	}

#ifdef PULSE
	int PulseEvent(neosmart_event_t event)
	{
		//This may look like it's a horribly inefficient kludge with the sole intention of reducing code duplication,
		//but in reality this is what any PulseEvent() implementation must look like. The only overhead (function 
		//calls aside, which your compiler will likely optimize away, anyway), is if only WFMO auto-reset waits are active
		//there will be overhead to unnecessarily obtain the event mutex for ResetEvent() after. In all other cases (being 
		//no pending waits, WFMO manual-reset waits, or any WFSO waits), the event mutex must first be released for the
		//waiting thread to resume action prior to locking the mutex again in order to set the event state to unsignaled, 
		//or else the waiting threads will loop back into a wait (due to checks for spurious CVariable wakeups).

		int result = SetEvent(event);
		assert(result == 0);
		result = ResetEvent(event);
		assert(result == 0);

		return 0;
	}
#endif
}

#endif //_WIN32
