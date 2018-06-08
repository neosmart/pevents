# pevents

`pevents` is a cross-platform low-level library meant to provide an
implementation of the WIN32 events for POSIX systems. pevents is built
on pthreads and provides *most* of the functionality of both manual-
and auto-reset events on Windows, most-notably including simultaneous
waits on multiple events (à la `WaitForMultipleObjects`).

pevents also doubles as a thin, sane wrapper for `CreateEvent()` & co. on
Windows, meaning you can use pevents directly in your cross-platform
code without `#ifdefs` for Windows/pthreads.

## License and Authorship

pevents is developed and maintained by Mahmoud Al-Qudsi
\<[mqudsi@neosmart.net](mailto:mqudsi@neosmart.net)\> of NeoSmart Technologies
\<[https://neosmart.net/](https://neosmart.net/)\> and is distributed under the
open source MIT public license. Refer to the `LICENSE` file for more information.

## About pevents

While POSIX condition variables (`pthread_cond_t`) and WIN32 events both
provide the essential building blocks of the synchronization primitives
required to write multithreaded code with signaling, the nature of the
differences between the two have lent their way towards creating
different synchronization and multithreaded-programming paradigms.

Developers accustomed to WIN32 events might have a hard time
transitioning to condition variables; pevents aims to ease the
transition for Windows developers looking to write multithreaded code on
*nix by providing a familiar synchronization primitive that will allow
them to duplicate the essential features of WIN32 auto/manual-reset
events.

As mentioned earlier, pevents provides most of the functionality of
WIN32 events. The only features not included are only named events and
support for security attributes. To the author's best knowledge, this is the only
implementation of WIN32 events available for Linux and other posix platforms
that provides support for simultaneously waiting on multiple events.

Depending on your needs, we've been told that pevents may be used as a lightweight
alternative to libuv/libev while still allowing your code to embrace asynchronous event
handling with ease.

### Supported platforms

pevents has been used as an extremely simple and lightweight cross-platform synchronization
library in code used across multiple platforms (including Windows, FreeBSD, Linux, macOS,
iOS, Android, and more).
## pevents API

The pevents API is modeled after the Windows `CreateEvent()`, `WaitForSingleObject()`,
and `WaitForMultipleObjects()` functions. Users familiar with WIN32 events
should have no problem switching the codebase over to the pevents API.

Additionally, pevents is also free of spurious wakeups - returns from waits are guaranteed
correct¹.

¹ *Spurious wakeups are a normal part of system programming under
Linux, and a common pitfall for developers coming from the Windows world.*

```cpp
neosmart_event_t CreateEvent(bool manualReset, bool initialState);

int DestroyEvent(neosmart_event_t event);

int WaitForEvent(neosmart_event_t event, uint64_t milliseconds);

int WaitForMultipleEvents(neosmart_event_t *events, int count,
		bool waitAll, uint64_t milliseconds);

int WaitForMultipleEvents(neosmart_event_t *events, int count,
		bool waitAll, uint64_t milliseconds, int &index);

int SetEvent(neosmart_event_t event);

int ResetEvent(neosmart_event_t event);

int PulseEvent(neosmart_event_t event);
```

## Building and using pevents

All the code is contained within `pevents.cpp` and `pevents.h`. You should
include these two files in your project as needed. All functions are in
the `neosmart` namespace.

### Code structure

* Core `pevents` code is in the `src/` directory
* Unit tests (deployable via meson) are in `tests/`
* A sample cross-platform application demonstrating the usage of pevents can be found
in the `examples/` folder. More examples are to come. (Pull requests welcomed!)

### Optional build system

Experimental support for building pevents via the meson build system has recently landed.
Currently, this is only used to support automated building/testing of pevents core and
its supporting utilities and unit tests. To repeat: do not worry about the build system,
pevents is purposely written in plain C/C++ and avoids the need for complex configuration
or platform-dependent build instructions.

### Compilation options:

The following preprocessor definitions may be defined (`-DOPTION`) at
compile time to enable different features.

* `WFMO`: Enables WFMO support in pevents. It is recommended to only compile
with WFMO support if you are taking advantage of the
`WaitForMultipleEvents` function, as it adds a (small) overhead to all
event objects.

* `PULSE`: Enables the `PulseEvent` function. `PulseEvent()` on Windows is
fundamentally broken and should not be relied upon — it will almost
never do what you think you're doing when you call it. pevents includes
this function only to make porting existing (flawed) code from WIN32 to
*nix platforms easier, and this function is not compiled into pevents by
default.

