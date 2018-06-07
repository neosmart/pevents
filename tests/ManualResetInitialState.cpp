#ifdef _WIN32
#include <Windows.h>
#endif
#include <pevents.h>
#include <iostream>

using namespace neosmart;

bool InitiallySet() {
	auto event = CreateEvent(true, true);
	auto result = WaitForEvent(event, 0);
	return result == 0;
}

bool InitiallyUnset() {
	auto event = CreateEvent(true, false);
	auto result = WaitForEvent(event, 0);
	return result == WAIT_TIMEOUT;
}

int main(int argc, const char *argv[]) {
	if (!InitiallySet()) {
		std::cout << "InitiallySet test failed!" << std::endl;
		return 1;
	}
	if (!InitiallySet()) {
		std::cout << "InitiallyUnset test failed!" << std::endl;
		return 1;
	}
}
