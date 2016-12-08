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
#include <time.h>
#include <string.h> //why on earth is memset in string.h?!?
#include "fastrand.h"

#ifndef REGISTERED_WAIT_PREALLOC
#define REGISTERED_WAIT_PREALLOC 2
#endif

namespace neosmart
{
	__attribute__((always_inline)) inline void ValidateReturn(const char *function __attribute__ ((unused)), const uint32_t line __attribute__ ((unused)), uint8_t result __attribute__ ((unused)))
	{
#ifdef DEBUG
		if (result != 0)
		{
			fprintf(stderr, "%s:%d returned %u: %s\n", function, line, result, strerror(result));
		}
		assert(result == 0);
#endif
	}
#define Validate(x) ValidateReturn(__FUNCTION__, __LINE__ - 1, x)

	__attribute__((always_inline)) inline uint64_t _rdtsc()
	{
		uint32_t a;
		uint32_t d;
		asm volatile
			(".byte 0x0f, 0x31 #rdtsc\n" // edx:eax
			:"=a"(a), "=d"(d)::);
		return (((uint64_t) d) << 32) | (uint64_t) a;
	}

#ifdef WFMO
	//How many WFMO registered waits to allocate room for on event creation
	uint64_t insertTime = 0;
	uint64_t resizeTime = 0;
	uint64_t removeTime = 0;
	uint64_t cleanupTime = 0;
	uint64_t randTime = 0;
	uint64_t rdtscOverhead = 0;

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
			uint32_t EventsLeft; //WFMO w/ WaitAll
		} Status;
		uint32_t ReferenceCount;
		bool WaitAll;
	};
	typedef neosmart_wfmo_t_ *neosmart_wfmo_t;

	uint32_t neosmart_wfmo_t_size = sizeof(neosmart_wfmo_t_);
#endif

	//The basic event structure, passed to the caller as an opaque pointer when creating events
	struct neosmart_event_t_
	{
		pthread_cond_t CVariable;
		pthread_mutex_t Mutex;
#ifdef WFMO
		neosmart_wfmo_t_ **RegisteredWaits; //array of pointers to WFMO/WFSO calls waiting on this event
		uint32_t HeadOffset;
		uint32_t TailOffset;
		uint32_t RegisteredWaitLength;
#endif
		bool AutoReset;
		bool State;
	};
	uint32_t neosmart_event_t_size = sizeof(neosmart_wfmo_t_);

	neosmart_event_t CreateEvent(bool initialState, bool manualReset)
	{
		//if (rdtscOverhead == 0)
		{
			uint64_t rdtsc1 = _rdtsc();
			uint64_t rdtsc2 = _rdtsc();
			rdtscOverhead += rdtsc2 - rdtsc1;
		}

		neosmart_event_t event = (neosmart_event_t) malloc(sizeof(neosmart_event_t_));

		int result = pthread_cond_init(&event->CVariable, 0);
		Validate(result);
		result = pthread_mutex_init(&event->Mutex, 0);
		Validate(result);

		event->State = false;
		event->AutoReset = !manualReset;
#ifdef WFMO
		//Allocate room for at least one event in our RegisteredWaits "array"
		event->RegisteredWaitLength = REGISTERED_WAIT_PREALLOC;
		event->RegisteredWaits = (neosmart_wfmo_t_ **) calloc(event->RegisteredWaitLength, sizeof(neosmart_wfmo_t_*));
		event->HeadOffset = 0;
		event->TailOffset = 0;
#endif

		if (initialState)
		{
			result = SetEvent(event);
			Validate(result);
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

		Validate(tempResult);

		int result = UnlockedWaitForEvent(event, milliseconds);

		tempResult = pthread_mutex_unlock(&event->Mutex);
		Validate(tempResult);

		return result;
	}

#ifdef WFMO
	inline void UnlockWfmo(neosmart_wfmo_t_ *wfmo)
	{
		int tempResult = 0;
		if (--wfmo->ReferenceCount == 0)
		{
			tempResult = pthread_mutex_unlock(&wfmo->Mutex);
			Validate(tempResult);
			tempResult = pthread_mutex_destroy(&wfmo->Mutex);
			Validate(tempResult);
			tempResult = pthread_cond_destroy(&wfmo->CVariable);
			Validate(tempResult);
			free(wfmo);
		}
		else
		{
			tempResult = pthread_mutex_unlock(&wfmo->Mutex);
			Validate(tempResult);
		}
	}

	inline void UnlockedEnqueueWait(neosmart_event_t_ *event, neosmart_wfmo_t_ *wait)
	{
		printf("push in: Wait length: %u, head: %u, tail: %u, head value: %p\n", event->RegisteredWaitLength, event->HeadOffset, event->TailOffset, event->RegisteredWaits[event->HeadOffset]);
		auto rdtsc = _rdtsc();
		event->RegisteredWaits[event->TailOffset] = wait;
		event->TailOffset = (event->TailOffset + 1) % event->RegisteredWaitLength;
		insertTime += _rdtsc() - rdtsc;

		if (event->TailOffset == event->HeadOffset && event->RegisteredWaits[event->HeadOffset] != nullptr)
		{
			printf("RegisteredWaits resize\n");
			rdtsc = _rdtsc();
			//resize needed
			//we don't guarantee order, so we can just set head = 0 and tail = old length - 1 after resize
			event->RegisteredWaits = (neosmart_wfmo_t_ **) realloc(event->RegisteredWaits, sizeof(neosmart_wfmo_t_ *) * event->RegisteredWaitLength * 2);
			assert(event->RegisteredWaits != nullptr);
			event->HeadOffset = 0;
			event->TailOffset = event->RegisteredWaitLength;
			event->RegisteredWaitLength *= 2;
			resizeTime += _rdtsc() - rdtsc;
			assert(event->RegisteredWaits[event->HeadOffset] != nullptr);
		}

		assert(event->HeadOffset == event->TailOffset || event->RegisteredWaits[event->HeadOffset] != nullptr);
		printf("push out: Wait length: %u, head: %u, tail: %u, head value: %p\n", event->RegisteredWaitLength, event->HeadOffset, event->TailOffset, event->RegisteredWaits[event->HeadOffset]);
		return;
		/*for (size_t i = 0; i < event->RegisteredWaitLength; ++i)
		{
			if (event->RegisteredWaits[i] == nullptr)
			{
				event->RegisteredWaits[i] = wait;
				insertTime += _rdtsc() - rdtsc;
				return;
			}
		}

		rdtsc = _rdtsc();
		//Not enough room. Need to expand the array of registered waits to fit this wait
		event->RegisteredWaits = (neosmart_wfmo_t_ **) realloc(event->RegisteredWaits, sizeof(neosmart_wfmo_t_ *) * event->RegisteredWaitLength * 2);
		assert(event->RegisteredWaits != nullptr);
		memset(event->RegisteredWaits + event->RegisteredWaitLength, 0, event->RegisteredWaitLength * sizeof(neosmart_wfmo_t_ *)); //zero out the newly-assigned second half of the array
		event->RegisteredWaits[event->RegisteredWaitLength] = wait;
		event->RegisteredWaitLength = event->RegisteredWaitLength * 2;
		resizeTime = _rdtsc() - rdtsc;*/
		//event->RegisteredWaits->push_back(wait);
		insertTime += _rdtsc() - rdtsc;
	}

	inline neosmart_wfmo_t_ *UnlockedDequeueWait(neosmart_event_t_ *event)
	{
		//Since order isn't guaranteed, set first to last and last to nullptr
		/*neosmart_wfmo_t_ *result = event->RegisteredWaits[0];
		event->RegisteredWaits[0] = nullptr; //in case this is the only wait (or the queue is empty)
		for (size_t i = 1; i < event->RegisteredWaitLength && event->RegisteredWaits[i] != nullptr; ++i)
		{
			//Find and swap the last one
			if (i == event->RegisteredWaitLength - 1 || event->RegisteredWaits[i + 1] == nullptr)
			{
				event->RegisteredWaits[0] = event->RegisteredWaits[i];
				event->RegisteredWaits[i] = nullptr;
				break;
			}
		}
		removeTime += _rdtsc() - rdtsc;*/
		printf("pop in: Wait length: %u, head: %u, tail: %u, head value: %p\n", event->RegisteredWaitLength, event->HeadOffset, event->TailOffset, event->RegisteredWaits[event->HeadOffset]);
		auto rdtsc = _rdtsc();
		neosmart_wfmo_t_ *result;
		uint32_t newHead = (event->HeadOffset + 1) % event->RegisteredWaitLength;
		if (event->HeadOffset == event->TailOffset)
		{
			//already empty, we would underflow
			result = nullptr;
		}
		else
		{
			result = event->RegisteredWaits[event->HeadOffset];
			event->HeadOffset = newHead;
		}
		removeTime += _rdtsc() - rdtsc;
		printf("pop out: Wait length: %u, head: %u, tail: %u, head value: %p\n", event->RegisteredWaitLength, event->HeadOffset, event->TailOffset, event->RegisteredWaits[event->HeadOffset]);
		return result;
	}

	int WaitForMultipleEvents(neosmart_event_t *events, uint32_t count, bool waitAll, uint64_t milliseconds, uint32_t *waitIndex)
	{
		fast_srand(time(nullptr));
		uint32_t waitIndexResult = -1u;
		neosmart_wfmo_t_ *wfmo = (neosmart_wfmo_t_ *)malloc(sizeof(neosmart_wfmo_t_));

		int result = 0;
		int tempResult = pthread_mutex_init(&wfmo->Mutex, 0);
		Validate(tempResult);
		tempResult = pthread_cond_init(&wfmo->CVariable, 0);
		Validate(tempResult);

		wfmo->WaitAll = waitAll;
		wfmo->Status.FiredEvent = nullptr; //required for done check
		wfmo->ReferenceCount = 1; //us

		if (waitAll)
		{
			wfmo->Status.EventsLeft = count;
		}

		tempResult = pthread_mutex_lock(&wfmo->Mutex);
		Validate(tempResult);

		bool done = false;
		for (uint32_t i = 0; i < count; ++i)
		{
			//Must not release lock until RegisteredWait is potentially added
			tempResult = pthread_mutex_lock(&events[i]->Mutex);
			Validate(tempResult);

			//Maybe the event is already unlocked, no need to register wait
			if (UnlockedWaitForEvent(events[i], 0) == 0)
			{
				tempResult = pthread_mutex_unlock(&events[i]->Mutex);
				Validate(tempResult);

				if (waitAll)
				{
					--wfmo->Status.EventsLeft;
					done = wfmo->Status.EventsLeft == 0;
				}
				else
				{
					wfmo->Status.FiredEvent = events[i];
					waitIndexResult = i;
					done = true;
					break;
				}
			}
			else
			{
				++wfmo->ReferenceCount;
				//UnlockedEnqueueWait(events[i], wfmo);

				//try cleaning up a "random" wait
				//number of entries in a buffer:
				//two cases: tail < head and tail <= head
				//case 1: eg head = 2, tail = 4, length = 7 -> count = ((tail + length) - head) % length = 2 (which is correct; inclusive indices 2-3)
				//case 2: eg tail = 2, head = 4, length = 7 -> count = (tail + length) - head = 5 (which is correct; inclusive indices 4-6 and 0-1)
				//both of which are covered by formula for case 1
				uint64_t rdtsc = _rdtsc();
				auto event = events[i];
				if (event->HeadOffset != event->TailOffset)
				{
					//randTime -= (_rdtsc() - rdtsc);
					/*uint32_t count = ((event->TailOffset + event->RegisteredWaitLength) - event->HeadOffset) % event->RegisteredWaitLength;
					neosmart_wfmo_t_ **wRand = event->RegisteredWaits + (event->HeadOffset + ((event->TailOffset ^ event->HeadOffset) % count)) % event->RegisteredWaitLength;
					randTime += _rdtsc() - rdtsc;*/
					neosmart_wfmo_t_ **wRand = event->RegisteredWaits + (event->TailOffset - 1) % event->RegisteredWaitLength;
					//if (!(*wRand)->WaitAll && (*wRand)->Status.FiredEvent != nullptr)
					{
						//this wait has been fulfilled
						tempResult = pthread_mutex_trylock(&(*wRand)->Mutex);
						if (tempResult == 0)
						{
							//double-checked locking
							if (!(*wRand)->WaitAll && (*wRand)->Status.FiredEvent != nullptr)
							{
								//printf("Replacing existing\n");
								UnlockWfmo(*wRand);
								//and put ours in its place
								*wRand = wfmo;
								goto addComplete;
							}
						}
					}
				}
				//printf("Adding new\n");
				UnlockedEnqueueWait(event, wfmo);
addComplete:
				cleanupTime += _rdtsc() - rdtsc;
				tempResult = pthread_mutex_unlock(&event->Mutex);
				Validate(tempResult);
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
			//One (or more) of the events we're monitoring has been triggered? (or none; remember, spurious waits!)

			//If we're waiting for all events, assume we're done and check if there's an event that hasn't fired
			//But if we're waiting for just one event, assume we're not done until we find a fired event
			done = (waitAll && wfmo->Status.EventsLeft == 0) || (!waitAll && wfmo->Status.FiredEvent != nullptr);

			if (!done)
			{
				if (milliseconds != (uint64_t) -1)
				{
					result = pthread_cond_timedwait(&wfmo->CVariable, &wfmo->Mutex, &ts);
				}
				else
				{
					result = pthread_cond_wait(&wfmo->CVariable, &wfmo->Mutex);
				}

				if (result != 0)
				{
					break;
				}
			}
		}
		assert(done);

		//Determine fired event index if unknown (but don't do this if the caller doesn't care about it)
		//If we terminated early (during wait registration), the waitIndex is already determined
		if (waitIndex != nullptr)
		{
			if (waitIndexResult == -1u)
			{
				for (uint32_t i = 0; i < count; ++i)
				{
					if (events[i] == wfmo->Status.FiredEvent)
					{
						*waitIndex = i;
						break;
					}
				}
			}
			else
			{
				*waitIndex = waitIndexResult;
			}
		}

#if false
		//Early cleanup? 
		for (neosmart_event_t_ *event = events; event - events < count; ++event)
		{
			tempResult = pthread_mutex_trylock(&event->Mutex);
			if (tempResult != 0)
			{
				//don't waste time
				continue;
			}
			uint32_t headOffset = event->HeadOffset;
			for (neosmart_wfmo_t_ **oldWfmo = event->RegisteredWaits + headOffset; headOffset != event->TailOffset; oldWfmo = event->RegisteredWaits + (headOffset = (headOffset + 1) % event->RegisteredWaitLength))
			{
				//this is safe to read without locking because 
				//a) we have the event mutex which means it can't be deleted
				//b) it's <= 32 bytes, which means it can be read atomically
				//c) we'll double-check the results after obtaining the lock if we care about the result (no we won't, because WaitAll never changes!)
				if (!oldWfmo->WaitAll)
				{
					//we can skip anything that's WaitAll, since that's guaranteed to clean up immediately
					tempResult = pthread_mutex_trylock(&oldWfmo->Mutex);
					if (tempResult != 0)
					{
						//don't waste time
						continue;
					}
					if (oldWfmo)
				}
			}
		}
#endif

		//Done. Release resources and get out of here.
		UnlockWfmo(wfmo);
		//do not need to signal the CV because we are the only ones to ever wait on it

		return result;
	}
#endif

	int DestroyEvent(neosmart_event_t event)
	{
		int result = 0;

#ifdef WFMO
		result = pthread_mutex_lock(&event->Mutex);
		Validate(result);
		free(event->RegisteredWaits);
		//delete(event->RegisteredWaits);
		result = pthread_mutex_unlock(&event->Mutex);
		Validate(result);
#endif

		result = pthread_cond_destroy(&event->CVariable);
		Validate(result);

		result = pthread_mutex_destroy(&event->Mutex);
		Validate(result);

		free(event);

		return 0;
	}

	int SetEvent(neosmart_event_t_ *event)
	{
		int result = pthread_mutex_lock(&event->Mutex);
		Validate(result);

		bool signal = false; //see branching vs context switch question below
		event->State = true;

		//Depending on the event type, we either trigger everyone or only one
		if (event->AutoReset)
		{
#ifdef WFMO
			while (true)
			{
				neosmart_wfmo_t_ *wfmo = UnlockedDequeueWait(event);
				if (wfmo == nullptr)
				{
					assert(event->State);
					break;
				}

				result = pthread_mutex_lock(&wfmo->Mutex);
				Validate(result);

				if (wfmo->WaitAll)
				{
					//RegisteredWaits with WaitAll are guaranteed not to have been fulfilled...
					//An autoreset event *must* instantly switch to reset state after being successfully triggered
					event->State = false;
					--wfmo->Status.EventsLeft;
					signal = wfmo->Status.EventsLeft == 0; //see branching vs context switch question below
				}
				//...on the other hand, !WaitAll could have been already fulfilled
				else if (wfmo->Status.FiredEvent == nullptr)
				{
					//An autoreset event *must* instantly switch to reset state after being successfully triggered
					event->State = false;
					wfmo->Status.FiredEvent = event;
					signal = true; //see branching vs context switch question below
				}

				//I'm not sure what's cheaper: a branch or an unnecessary kernel context switch
				//assuming branching for now... otherwise we'd signal unconditionally
				if (signal)
				{
					//it's not safe to do this after unlocking the WFMO because it can be deleted in the time in between
					result = pthread_cond_signal(&wfmo->CVariable);
					Validate(result);
				}

				//Done. Release resources and get out of here.
				UnlockWfmo(wfmo);

				//if the wfmo was already fulfilled, state will still be true
				//try again, if possible (this could go in the while precondition, but this is saner for code review)
				if (event->State)
				{
					continue;
				}
				else
				{
					result = pthread_mutex_unlock(&event->Mutex);
					Validate(result);
					return 0;
				}
			}
#endif //WFMO
			result = pthread_mutex_unlock(&event->Mutex);
			Validate(result);
			result = pthread_cond_signal(&event->CVariable);
			Validate(result);
		}
		else
		{
#ifdef WFMO
			//for (neosmart_wfmo_t_ **wfmo = event->RegisteredWaits; *wfmo != nullptr; ++wfmo)
			//for (auto i = 0; i < event->RegisteredWaits->size(); ++i)
			uint32_t headOffset = event->HeadOffset;
			printf("Wait length: %u, head: %u, tail: %u, head value: %p\n", event->RegisteredWaitLength, event->HeadOffset, event->TailOffset, event->RegisteredWaits[event->HeadOffset]);
			for (neosmart_wfmo_t_ **wfmo = event->RegisteredWaits + headOffset; headOffset != event->TailOffset; wfmo = event->RegisteredWaits + (headOffset = (headOffset + 1) % event->RegisteredWaitLength))
			{
				//neosmart_wfmo_t_ *temp = event->RegisteredWaits->at(i);
				//neosmart_wfmo_t_ **wfmo = &temp;
				result = pthread_mutex_lock(&(*wfmo)->Mutex);
				Validate(result);

				bool signal = false; //see branching vs context switch question below
				if ((*wfmo)->WaitAll)
				{
					--(*wfmo)->Status.EventsLeft;
					signal = (*wfmo)->Status.EventsLeft == 0; //see branching vs context switch question below
				}
				else if ((*wfmo)->Status.FiredEvent == nullptr)
				{
					signal = true; //see branching vs context switch question below
					(*wfmo)->Status.FiredEvent = event;
				}

				//I'm not sure what's cheaper: a branch or an unnecessary kernel context switch
				//assuming branching for now... otherwise we'd signal unconditionally
				if (signal)
				{
					//it's not safe to do this after unlocking the WFMO because it can be deleted in the time in between
					result = pthread_cond_signal(&(*wfmo)->CVariable);
					Validate(result);
				}

				UnlockWfmo(*wfmo);
			}
			//clear all waits
			memset(event->RegisteredWaits, 0, event->RegisteredWaitLength * sizeof(neosmart_wfmo_t_*));
			event->HeadOffset = 0;
			event->TailOffset = 0;
			//event->RegisteredWaits->clear();
#endif
			result = pthread_mutex_unlock(&event->Mutex);
			Validate(result);

			result = pthread_cond_broadcast(&event->CVariable);
			Validate(result);
		}

		return 0;
	}

	int ResetEvent(neosmart_event_t event)
	{
		int result = pthread_mutex_lock(&event->Mutex);
		Validate(result);

		event->State = false;

		result = pthread_mutex_unlock(&event->Mutex);
		Validate(result);

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
		Validate(result);
		result = ResetEvent(event);
		Validate(result);

		return 0;
	}
#endif
}

#endif //_WIN32
