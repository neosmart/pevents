#include <assert.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <random>
#include <signal.h>
#include <thread>

// NB: On Windows, you must include Winbase.h/Synchapi.h/Windows.h before pevents.h
#ifdef _WIN32
#include <Windows.h>
#endif
#include "pevents.h"

#ifdef _WIN32
#define __unused__  [[maybe_unused]]
#else
#define __unused__ __attribute__((unused))
#endif

using namespace neosmart;
using namespace std;

neosmart_event_t events_completed[2];  // events completed for letters, numbers;
neosmart_event_t events_letters[26];   // letter events
neosmart_event_t events_numbers[10];   // number events

void worker_letter(char ch)
{
    int i = ch-'A';
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 3000);
    std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
    assert(0<=i && i<26);
    SetEvent(events_letters[i]);
    cout << ch << endl;
}

void letters() {

    printf("letters(): Starting 26 letter threads\n");

    for (char letter='A';letter<='Z';letter++)
    {
	    std::thread (worker_letter,letter).detach();
    }

    WaitForMultipleEvents(events_letters,26,true /*waitAll*/,WAIT_INFINITE);
    SetEvent(events_completed[0]);
    cout << "letters() completed" << endl;
}


void worker_number(int num)
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dis(0, 5000);
    std::this_thread::sleep_for(std::chrono::milliseconds(dis(gen)));
    SetEvent(events_numbers[num]);
    cout << num << endl;
}

void numbers() {

    printf("numbers(): Starting 10 number threads\n");

    for (int num=0;num<10;num++)
    {
	    std::thread (worker_number,num).detach();
    }

    WaitForMultipleEvents(events_numbers,10,true /*waitAll*/,WAIT_INFINITE);
    SetEvent(events_completed[1]);
    cout << "numbers(): completed" << endl;
}

int main() {

    // create a list of manual-reset events;
    for (int i=0;i<2;i++)   events_completed[i] = CreateEvent(true, false); // manual-reset events;
    for (int i=0;i<26;i++)  events_letters[i] = CreateEvent(true, false);   // manual-reset events;
    for (int i=0;i<10;i++)  events_numbers[i] = CreateEvent(true, false);   // manual-reset events;


    vector<std::thread> threads;
    threads.emplace_back(letters);
    threads.emplace_back(numbers);
    int result;
    
    result = WaitForMultipleEvents(events_completed,2,false/*waitAll*/, 3000);
    if (result == WAIT_TIMEOUT)
    {
        cout << "Timeout! It's fine that none completed" << endl;
    } else if (result == 0) {
        cout << "Letters completed" << endl;
    } else if (result == 1) {
        cout << "Numbers completed" << endl;
    } else {
        cout << "ERROR! Unexpected index: " << result << endl;
        exit(-1);
    }

    result = WaitForMultipleEvents(events_completed,2,true /*waitAll*/, WAIT_INFINITE);
    if (result == WAIT_TIMEOUT) {
        cout << "Timeout!" << endl;
        assert(false);
    } else if (result == 0) {
        cout << "Letters and Numbers completed" << endl;
    } else {
        cout << "ERROR! Unexpected index: " << result << endl;
	exit(-1);
    }

    for (auto& thread : threads)
        thread.join();

    for (int i=0;i<2;i++)   DestroyEvent(events_completed[i]);
    for (int i=0;i<26;i++)  DestroyEvent(events_letters[i]);
    for (int i=0;i<10;i++)  DestroyEvent(events_numbers[i]);
}
