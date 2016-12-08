/*
 * WIN32 Events for POSIX
 * Author: Mahmoud Al-Qudsi <mqudsi@neosmart.net>
 * Copyright (C) 2011 - 2015 by NeoSmart Technologies
 * This code is released under the terms of the MIT License
*/

#include <iostream>
#include <assert.h>
#include <thread>
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

namespace neosmart
{
	extern uint64_t insertTime;
	extern uint64_t removeTime;
	extern uint64_t resizeTime;
	extern uint64_t cleanupTime;
	extern uint64_t randTime;
	extern uint64_t rdtscOverhead;
	extern uint32_t neosmart_wfmo_t_size;
	extern uint32_t neosmart_event_t_size;
}

neosmart_event_t events[5]; //letters, numbers, abort, letterSync, numberSync
std::atomic<bool> interrupted { false }; //for signal handling

template <typename T, std::size_t N>
constexpr std::size_t countof(T const (&)[N]) noexcept
{
	return N;
}

//by leaving these originally unassigned, any access to unitialized memory 
//will be flagged by valgrind
char letter;
int number;

char lastChar = '\0';
int lastNum = -1;

void intHandler(int sig __attribute__ ((unused))) {
	interrupted = true;
	//unfortunately you can't use SetEvent here because posix signal handlers
	//shouldn't use any non-reentrant code (like printf)
	//on x86/x64, std::atomic<bool> is just a fancy way of doing a memory
	//barrier and nothing more, so it is safe
}

enum class ThreadMode : uint8_t
{
	Letters,
	Numbers
};

void generator(ThreadMode mode)
{
	static uint32_t letterIndex = 0;
	static uint32_t numberIndex = 0;

	assert(mode == ThreadMode::Letters || mode == ThreadMode::Numbers);

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> dis(100, 300);
	
	neosmart_event_t localEvents[] = { events[2], events[3 + (uint8_t)(mode)] };
	while (true)
	{
		//wait a random amount of time, from 100 through 300 milliseconds, between each print attempt.
		//BUT threads should literally never call the sleep() function - instead, wait with a timeout on the
		//abort event - that way your app will stay responsive!
		auto timeout = dis(gen);
		if (WaitForEvent(events[2], timeout) == 0)
		{
			//abort triggered
			break;
		}

		//remember that another instance of this function may be executing concurrently, so after
		//the sleep finishes, make sure to obtain exclusive access by means of this auto-reset event
		//Life tip: never, ever WFSO on anything other than an abort event
		//anything else should be WFMO'd in conjunction w/ the abort event to keep your app responsive
		uint32_t waitIndex = 0;
#ifdef DEBUG
		auto result = 
#endif
		WaitForMultipleEvents(localEvents, 2, false, -1, &waitIndex);

		assert(result == 0);
		if (waitIndex == 0)
		{
			//abort happened
			break;
		}
		
		if (mode == ThreadMode::Letters)
		{
			letter = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[letterIndex%26];
			++letterIndex;
		}
		else if (mode == ThreadMode::Numbers)
		{
			number = numberIndex++;
		}

		SetEvent(events[0 + (uint8_t)(mode)]); //signal main thread to print generated letter/number
	}
}

int main()
{
	events[0] = CreateEvent(); //letter available auto-reset event, initially unavailable
	events[1] = CreateEvent(); //number available auto-reset event, initially unavailable
	events[2] = CreateEvent(false, true); //abort manual-reset event
	events[3] = CreateEvent(true); //letter protection auto-reset event (instead of a mutex), initially available
	events[4] = CreateEvent(true); //number protection auto-reset event (instead of a mutex), initially available

	//after the abort event has been created
	struct sigaction act = {};
	act.sa_handler = intHandler; //trigger abort on ctrl+c
	sigaction(SIGINT, &act, NULL);

	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<int> dis(1, 10);

	printf("Size of neosmart_event_t: %u\n", neosmart_event_t_size);
	printf("Size of neosmart_wfmo_t:  %u\n", neosmart_wfmo_t_size);

	uint32_t rounds = 10;
	uint64_t cycles = 0;
	for (uint32_t i = 0; !interrupted && i < rounds; ++i)
	{
		lastChar = '\0';
		ResetEvent(events[2]); //make sure the abort is cleared

		uint32_t letterThreadCount = dis(gen);
		uint32_t numberThreadCount = dis(gen);

		vector<std::thread> threads;
		for (uint32_t i = 0; i < letterThreadCount; ++i)
		{
			threads.emplace_back([] () { generator(ThreadMode::Letters); });
		}
		for (uint32_t i = 0; i < numberThreadCount; ++i)
		{
			threads.emplace_back([] () { generator(ThreadMode::Numbers); });
		}
		
		printf("Started %u letter threads\n", letterThreadCount);
		printf("Started %u number threads\n", numberThreadCount);
		
		while (lastChar != 'Z')
		{
			++cycles;
			if (interrupted)
			{
				printf("Interrupt triggered.. Aborting!\n");
				break;
			}

			uint32_t index = -1u;
			int result = WaitForMultipleEvents(events, 2, false, -1, &index);

			if (result == WAIT_TIMEOUT)
			{
				cout << "Timeout!" << endl;
				assert(false);
			}
			else if (result != 0)
			{
				cout << "Error in wait!" << endl;
				assert(false);
			}
			else if (index == 0)
			{
				//printf("lastChar: %c, char: %c\n", lastChar, letter);
				assert(letter == 'A' || (lastChar + 1) == letter);
				cout << letter << '\n';
				lastChar = letter;
				SetEvent(events[3]); //safe for another thread to enter this loop
			}
			else if (index == 1)
			{
				//printf("lastNum: %d, num: %d\n", lastNum, number);
				assert(number == 0 || lastNum + 1 == number);
				cout << number << '\n';
				lastNum = number;
				SetEvent(events[4]); //safe for another thread to enter this loop
			}
			else
			{
				cout << "ERROR! Unexpected index: " << index << endl;
				exit(-1);
			}
		}

		//You can't just DestroyEvent() and exit - it'll segfault 
		//That's because generator threads will call SetEvent() on a destroyed event
		//You must *never* call SetEvent/ResetEvent on a destroyed event! (system undefined behavior)
		//So we set an abort event and wait for the helper threads to exit

		//Set the abort..
		SetEvent(events[2]);

		//..then wait for them to wrap up and exit
		for (auto &thread : threads)
		{
			thread.join();
		}
	}

	for (auto event : events)
	{
		DestroyEvent(event);
	}

	//printf("Test completed with %u letter threads and %u number threads\n", letterThreadCount, numberThreadCount);
	printf("%s rounds completed%s\n", to_string(cycles).c_str(), interrupted ? " (with early termination)" : "");
	//printf("Total time expected: %s microseconds\n", std::to_string(expected * 1000).c_str());
	//printf("Actual wait time:    %s microseconds\n", std::to_string(actual).c_str());
	//printf("Overhead: %8.3g%%\n", static_cast<double>(actual/1000 - expected)/expected*100);
	double rdtscAdjusted = rdtscOverhead/(double)5;
	printf("rdtsc overhead: %s\n", to_string(rdtscOverhead/(double)countof(events)).c_str());
	printf("Insert cycles per round:  %s\n", to_string(insertTime/cycles - rdtscAdjusted).c_str());
	printf("Remove cycles per round:  %s\n", to_string(removeTime/cycles - rdtscAdjusted).c_str());
	printf("Resize cycles per round:  %s\n", to_string(resizeTime/cycles - rdtscAdjusted).c_str());
	printf("Cleanup cycles per round: %s\n", to_string(cleanupTime/cycles - rdtscAdjusted).c_str());
	printf("Rand cycles per round: %s\n", to_string(randTime/cycles - rdtscAdjusted).c_str());
	//printf("False unlocks per round: %s\n", to_string(((double)falseUnlocks)/cycles).c_str());
}
