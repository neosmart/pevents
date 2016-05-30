all: release

CPPFLAGS += -Wall -Wpedantic -Wextra -Winit-self -Wnull-dereference -Wuninitialized -ansi

debug: CPPFLAGS += -DDEBUG -g
debug: sample

release: CPPFLAGS += -DNDEBUG -DNODEBUG -Ofast -march=native
release: sample

pevents:
	$(CXX) $(CPPFLAGS) -DWFMO -DPULSE -std=c++11 ./pevents.cpp -c -o pevents -g
	
sample: pevents
	$(CXX) $(CPPFLAGS) -DWFMO -DPULSE -g -lpthread -std=c++11 ./sample.cpp ./pevents -o sample

clean:
	rm -f *.o
	rm -f *.a
	rm -f */*.a
	rm -f */*.o
	rm -f ./sample ./pevents
