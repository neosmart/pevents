#ifdef _WIN32
#include <Windows.h>
#endif
#include <cassert>
#include <chrono>
#include <iostream>
#include <pevents.h>
#include <thread>

using namespace neosmart;

int main() {
    neosmart::neosmart_event_t lEvents[3];
    lEvents[0] = neosmart::CreateEvent( false, true );  // Already Signaled AutoReset
    lEvents[1] = neosmart::CreateEvent( false, false ); // Not Signaled AutoReset
    lEvents[2] = neosmart::CreateEvent( false, true );  // Already Signaled AutoReset

    // WFMO is non-destructive if a wait-all with any timeout value fails on auto-reset events.
    if ( neosmart::WaitForMultipleEvents( lEvents, 3, true, 0 ) == 0 )
        throw std::runtime_error( "Must not be signaled!" );

    // FAILS!!
    if ( neosmart::WaitForEvent( lEvents[0], 0 ) != 0 )
        throw std::runtime_error( "Must be signaled" );

    if ( neosmart::WaitForEvent( lEvents[1], 0 ) != WAIT_TIMEOUT )
        throw std::runtime_error( "Must not be signaled" );

    // FAILS!!
    if ( neosmart::WaitForEvent( lEvents[2], 0 ) != 0 )
        throw std::runtime_error( "Must be signaled" );


    // WFMO is destructive if a wait-all succeeds with any timeout value on auto-reset events.
    for ( auto& lEvent : lEvents )
        neosmart::SetEvent( lEvent );
    if ( neosmart::WaitForMultipleEvents( lEvents, 3, true, 0 ) != 0 )  // OK
        throw std::runtime_error( "Must be signaled!" );
    for ( auto& lEvent : lEvents )
    {
        if ( neosmart::WaitForEvent( lEvent, 0 ) != WAIT_TIMEOUT ) // OK
            throw std::runtime_error( "Must not be signaled" );
        neosmart::DestroyEvent( lEvent );
    }
}
