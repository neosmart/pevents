/*
 * WIN32 Events for POSIX
 * Author: Mahmoud Al-Qudsi <mqudsi@neosmart.net>
 * Copyright (C) 2011 - 2015 by NeoSmart Technologies
 * This code is released under the terms of the MIT License
*/

#ifdef _WIN32

#include <Windows.h>
#include "pevents.h"

namespace neosmart
{
	neosmart_event_t CreateEvent(bool manualReset, bool initialState)
	{
		return static_cast<neosmart_event_t>(::CreateEvent(NULL, manualReset, initialState, NULL));
	}

	int DestroyEvent(neosmart_event_t event)
	{
		HANDLE handle = static_cast<HANDLE>(event);
		return CloseHandle(handle) ? 0 : GetLastError();
	}

	int WaitForEvent(neosmart_event_t event, uint64_t milliseconds)
	{
		uint32_t result = 0;
		HANDLE handle = static_cast<HANDLE>(event);

		//WaitForSingleObject(Ex) and WaitForMultipleObjects(Ex) only support 32-bit timeout
		if (milliseconds == ((uint64_t) -1) || (milliseconds >> 32) == 0)
		{
			result = WaitForSingleObject(handle, static_cast<uint32_t>(milliseconds));
		}
		else
		{
			//Cannot wait for 0xFFFFFFFF because that means infinity to WIN32
			uint32_t waitUnit = (INFINITE - 1);
			uint64_t rounds = milliseconds / waitUnit;
			uint32_t remainder = milliseconds % waitUnit;

			uint32_t result = WaitForSingleObject(handle, remainder);
			while (result == WAIT_TIMEOUT && rounds-- != 0)
			{
				result = WaitForSingleObject(handle, waitUnit);
			}
		}

		if (result == WAIT_OBJECT_0 || result == WAIT_ABANDONED)
		{
			//We must swallow WAIT_ABANDONED because there is no such equivalent on *nix
			return 0;
		}

		if (result == WAIT_TIMEOUT)
		{
			return WAIT_TIMEOUT;
		}

		return GetLastError();
	}

	int SetEvent(neosmart_event_t event)
	{
		HANDLE handle = static_cast<HANDLE>(event);
		return ::SetEvent(handle) ? 0 : GetLastError();
	}

	int ResetEvent(neosmart_event_t event)
	{
		HANDLE handle = static_cast<HANDLE>(event);
		return ::ResetEvent(handle) ? 0 : GetLastError();
	}

#ifdef WFMO
	int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll, uint64_t milliseconds)
	{
		int index = 0;
		return WaitForMultipleEvents(events, count, waitAll, milliseconds, index);
	}

	int WaitForMultipleEvents(neosmart_event_t *events, int count, bool waitAll, uint64_t milliseconds, int &index)
	{
		HANDLE *handles = reinterpret_cast<HANDLE*>(events);
		uint32_t result = 0;

		//WaitForSingleObject(Ex) and WaitForMultipleObjects(Ex) only support 32-bit timeout
		if (milliseconds == ((uint64_t) -1) || (milliseconds >> 32) == 0)
		{
			result = WaitForMultipleObjects(count, handles, waitAll, static_cast<uint32_t>(milliseconds));
		}
		else
		{
			//Cannot wait for 0xFFFFFFFF because that means infinity to WIN32
			uint32_t waitUnit = (INFINITE - 1);
			uint64_t rounds = milliseconds / waitUnit;
			uint32_t remainder = milliseconds % waitUnit;

			uint32_t result = WaitForMultipleObjects(count, handles, waitAll, remainder);
			while (result == WAIT_TIMEOUT && rounds-- != 0)
			{
				result = WaitForMultipleObjects(count, handles, waitAll, waitUnit);
			}
		}

		if (result >= WAIT_OBJECT_0 && result < WAIT_OBJECT_0 + count)
		{
			index = result - WAIT_OBJECT_0;
			return 0;
		}
		else if (result >= WAIT_ABANDONED_0 && result < WAIT_ABANDONED_0 + count)
		{
			index = result - WAIT_ABANDONED_0;
			return 0;
		}

		if (result == WAIT_FAILED)
		{
			return GetLastError();
		}
		return result;
	}
#endif

#ifdef PULSE
	int PulseEvent(neosmart_event_t event)
	{
		HANDLE handle = static_cast<HANDLE>(event);
		return ::PulseEvent(handle) ? 0 : GetLastError();
	}
#endif
}

#endif //_WIN32
