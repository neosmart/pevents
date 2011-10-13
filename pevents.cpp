/*
 * WIN32 Events for POSIX
 * Author: Mahmoud Al-Qudsi <mqudsi@neosmart.net>
 * Copyright (C) 2011 by NeoSmart Technologies
 * This code is released under the terms of the MIT License
 */

#include "pevents.h"
#include <sys/time.h>

namespace neosmart
{
	struct neosmart_event_t_
	{
		bool AutoReset;
		pthread_cond_t CVariable;
		pthread_mutex_t Mutex;
		bool State;
	};
	
	neosmart_event_t CreateEvent(bool manualReset, bool initialState)
	{
		neosmart_event_t event = new neosmart_event_t_;
		
		int result = pthread_cond_init(&event->CVariable, 0);
		if(result != 0)
			return NULL;
		
		result = pthread_mutex_init(&event->Mutex, 0);
		if(result != 0)
			return NULL;
		
		event->State = false;
		event->AutoReset = !manualReset;
		
		if(initialState && SetEvent(event) != 0)
			return NULL; //Shouldn't ever happen
		
		return event;
	}
	
	int WaitForEvent(neosmart_event_t event, uint32_t milliseconds)
	{
		int result = pthread_mutex_lock(&event->Mutex);
		if(result != 0)
			return result;
        
		if(!event->State)
		{	
			//Regardless of whether it's an auto-reset or manual-reset event:
			//wait to obtain the event, then lock anyone else out
			if(milliseconds != -1)
			{
				timeval tv;
				gettimeofday(&tv, NULL);
				
				uint64_t nanoseconds = tv.tv_sec * 1000 * 1000 * 1000 + milliseconds * 1000 * 1000 + tv.tv_usec * 1000;
				
				timespec ts;
				ts.tv_sec = nanoseconds / 1000 / 1000 / 1000;
				ts.tv_nsec = (nanoseconds - ts.tv_sec * 1000 * 1000 * 1000);
				
				result = pthread_cond_timedwait(&event->CVariable, &event->Mutex, &ts);
				if(result == 0)
				{
					//We've only accquired the event if the wait succeeded
					event->State = false;
				}
			}
			else
			{
				result = pthread_cond_wait(&event->CVariable, &event->Mutex);
				if(result == 0)
				{
					event->State = false;
				}
			}
		}
		else if(event->AutoReset)
		{
			//It's an auto-reset event that's currently available;
			//we need to stop anyone else from using it
			result = 0;
			event->State = false;
		}
		//Else we're trying to obtain a manual reset event with a signalled state;
		//don't do anything
		
		pthread_mutex_unlock(&event->Mutex);
		
		return result;
	}
	
	int DestroyEvent(neosmart_event_t event)
	{
		int result = pthread_cond_destroy(&event->CVariable);
		if(result != 0)
			return result;
        
		result = pthread_mutex_destroy(&event->Mutex);
		if(result != 0)
			return result;
		
		delete event;
		
		return 0;
	}
	
	int SetEvent(neosmart_event_t event)
	{
		int result = pthread_mutex_lock(&event->Mutex);
		if(result != 0)
			return result;
		
		event->State = true;
		
		//Depending on the event type, we either trigger everyone or only one
		if(event->AutoReset)
		{
			result = pthread_cond_signal(&event->CVariable);
		}
		else
		{
			result = pthread_cond_broadcast(&event->CVariable);
		}
		
		pthread_mutex_unlock(&event->Mutex);
		
		return result;
	}
	
	int ResetEvent(neosmart_event_t event)
	{
		int result = pthread_mutex_lock(&event->Mutex);
		if(result != 0)
			return result;
		
		event->State = false;
		
		pthread_mutex_unlock(&event->Mutex);
		
		return result;
	}
}
