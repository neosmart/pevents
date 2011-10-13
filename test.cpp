#include "pevents.h"
#include <iostream>
#include <pthread.h>
#include <unistd.h>

using namespace neosmart;
using namespace std;

void *test(void *param);

neosmart_event_t event;
int shared;

void *test(void *param)
{
    for(int i = 0; ; ++i)
    {
        shared = i;
        SetEvent(event);
        sleep(1);
    }
}

int main()
{
    event = CreateEvent();
    
    pthread_t thread;
    pthread_create(&thread, NULL, test, NULL);
    
    for(int i = 0; i < 25; ++i)
    {
        if(WaitForEvent(event, 500) != 0)
            cout << "Timeout!" << endl;
        else
            cout << shared << endl;
    }
    
    DestroyEvent(event);
}
