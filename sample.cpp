/*
 * WIN32 Events for POSIX
 * Author: Mahmoud Al-Qudsi <mqudsi@neosmart.net>
 * Copyright (C) 2011 - 2015 by NeoSmart Technologies
 * This code is released under the terms of the MIT License
*/

#include <iostream>
#include <assert.h>
#include <thread>
#include <chrono>
//On Windows, you must include Winbase.h/Synchapi.h/Windows.h before pevents.h
#ifdef _WIN32
#include <Windows.h>
#endif
#include "pevents.h"

using namespace neosmart;
using namespace std;

neosmart_event_t events[3]; //letters, numbers, abort
char letter = '?';
int number = -1;

char lastChar = '\0';
int lastNum = -1;

void letters()
{
	for (uint32_t i = 0; WaitForEvent(events[2], 0) == WAIT_TIMEOUT; ++i)
	{
		letter = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[i%26];
		SetEvent(events[0]);
		this_thread::sleep_for(chrono::seconds(1));
	}
}

void numbers()
{
	for (uint32_t i = 0; WaitForEvent(events[2], 0) == WAIT_TIMEOUT; ++i)
	{
		number = i;
		SetEvent(events[1]);
		this_thread::sleep_for(chrono::seconds(4));
	}
}

int main()
{
	events[0] = CreateEvent(); //letters auto-reset event
	events[1] = CreateEvent(); //numbers auto-reset event
	events[2] = CreateEvent(true); //abort manual-reset event
	
	std::thread thread1(letters);
	std::thread thread2(numbers);
	
	for (uint32_t i = 0; lastChar != 'Z'; ++i)
	{
		int index = -1;
		int result = WaitForMultipleEvents(events, 2, false, -1, index);

		if (result == WAIT_TIMEOUT)
		{
			cout << "Timeout!" << endl;
		}
		else if (result != 0)
		{
			cout << "Error in wait!" << endl;
		}
		else if (index == 0)
		{
			assert(lastChar != letter);
			cout << letter << endl;
			lastChar = letter;
		}
		else if (index == 1)
		{
			assert(lastNum != number);
			cout << number << endl;
			lastNum = number;
		}
		else
		{
			cout << "ERROR! Unexpected index: " << index << endl;
			exit(-1);
		}
	}

	//You can't just DestroyEvent() and exit - it'll segfault 
	//That's because letters() and numbers() will call SetEvent on a destroyed event
	//You must *never* call SetEvent/ResetEvent on a destroyed event!
	//So we set an abort event and wait for the helper threads to exit

	//Set the abort
	SetEvent(events[2]);

	thread1.join();
	thread2.join();
	
	for (auto event : events)
	{
		DestroyEvent(event);
	}
}
