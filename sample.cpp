#include "pevents.h"
#include <iostream>
#include <pthread.h>
#include <unistd.h>
#include <assert.h>

using namespace neosmart;
using namespace std;

void *numbers(void *param);
void *letters(void *param);

neosmart_event_t event[2];
char letter = '?';
int number = -1;

char lastChar = 'Z';
int lastNum = 100;

void *letters(void *param)
{
	for(int i = 0; ; ++i)
	{
		letter = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"[i%26];
		SetEvent(event[0]);
		sleep(3);
	}
}

void *numbers(void *param)
{
	for(int i = 0; ; ++i)
	{
		number = i;
		SetEvent(event[1]);
		sleep(4);
	}
}

int main()
{
	event[0] = CreateEvent();
	event[1] = CreateEvent();
	
	pthread_t thread1, thread2;
	pthread_create(&thread1, NULL, letters, NULL);
	pthread_create(&thread2, NULL, numbers, NULL);
	
	for(int i = 0; i < 25; ++i)
	{
		int index = 7;
		if(WaitForMultipleEvents(event, 2, false, -1, index) != 0)
		{
			cout << "Timeout!" << endl;
		}
		else if(index == 0)
		{
			assert(lastChar != letter);
			cout << letter << endl;
			lastChar = letter;
		}
		else if(index == 1)
		{
			assert(lastNum != number);
			cout << number << endl;
			lastNum = number;
		}
		else
		{
			cout << "ERROR! Index: " << index << endl;
			exit(-1);
		}
	}
	
	DestroyEvent(event[0]);
	DestroyEvent(event[1]);
}
