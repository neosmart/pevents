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
#include <atomic>
#include <signal.h>
#include <random>
//On Windows, you must include Winbase.h/Synchapi.h/Windows.h before pevents.h
#ifdef _WIN32
#include <Windows.h>
#endif
#include "pevents.h"

using namespace neosmart;
using namespace std;

neosmart_event_t events[3]; //letters, numbers, abort
std::atomic<bool> interrupted { false };
char letter = '?';
int number = -1;

char lastChar = '\0';
int lastNum = -1;

void intHandler(int sig) {
	interrupted = true;
	//unfortunately you can't use SetEvent here because posix signal handlers
	//shouldn't use any non-reentrant code (like printf)
	//on x86/x64, std::atomic<bool> is just a fancy way of doing a memory
	//barrier and nothing more, so it is safe
}

void letters()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> dis(0, 3000);
	for (uint32_t i = 0; WaitForEvent(events[2], dis(gen)) == WAIT_TIMEOUT; ++i)
	{
		letter = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[i%26];
		SetEvent(events[0]);
	}
}

void numbers()
{
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> dis(0, 3000);
	for (uint32_t i = 0; WaitForEvent(events[2], dis(gen)) == WAIT_TIMEOUT; ++i)
	{
		number = i;
		SetEvent(events[1]);
	}
}

int main()
{
	events[0] = CreateEvent(); //letters auto-reset event
	events[1] = CreateEvent(); //numbers auto-reset event
	events[2] = CreateEvent(true); //abort manual-reset event

	//after the abort event has been created
	struct sigaction act = {0};
	act.sa_handler = intHandler; //trigger abort on ctrl+c
	sigaction(SIGINT, &act, NULL);
	
	std::thread thread1(letters);
	std::thread thread2(numbers);
	
	for (uint32_t i = 0; lastChar != 'Z'; ++i)
	{
		if (interrupted)
		{
			printf("Interrupt triggered.. Aborting!\n");
			break;
		}

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
