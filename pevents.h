/*
 * WIN32 Events for POSIX
 * Author: Mahmoud Al-Qudsi <mqudsi@neosmart.net>
 * Copyright (C) 2011 - 2012 by NeoSmart Technologies
 * This code is released under the terms of the MIT License
 */

#pragma once

#include <pthread.h>
#include <stdint.h>

/* Compile-time options:
 
   #define WFMO
 
 * Define WFMO to enable WFMO support (recommended to leave disabled if not using WFMO)
 * Compiling with WFMO support will add some overhead to all event objects
 
   #define PTHREADCHK
 
 * Define PTHTREADCHK to enable error checking results of underlying pthread functions
 * Generally speaking, if you do not anticpate you will reach the platform's memory and resource limitations,
 * it is OK to disable pthread error checking, giving slightly better performance
 
 */

namespace neosmart
{
	//Type declarations
	struct neosmart_event_t_;
	typedef neosmart_event_t_ * neosmart_event_t;
	
	//WIN32-style functions
	neosmart_event_t CreateEvent(bool manualReset = false, bool initialState = false);
	int DestroyEvent(neosmart_event_t event);
	int WaitForEvent(neosmart_event_t event, uint64_t milliseconds = -1);
	int SetEvent(neosmart_event_t event);
	int ResetEvent(neosmart_event_t event);
#ifdef WFMO
	int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll, uint64_t milliseconds);
	int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll, uint64_t milliseconds, int &index);
#endif
    
	//POSIX-style functions
	//TBD
}
