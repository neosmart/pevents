project('pevents', ['cpp'],
		default_options: ['warning_level=3', 'cpp_std=c++11'])

args = []
if get_option('wfmo')
	args += '-DWFMO'
endif
if get_option('pulse')
	args += '-DPULSE'
endif

pthreads = dependency('threads')
incdir = include_directories('src/')

srcs = ['src/pevents.cpp']
# pevents = both_libraries('pevents', srcs,
pevents = static_library('pevents', srcs,
	cpp_args: args,
	dependencies: [pthreads])

pevents = declare_dependency(
	include_directories: include_directories('.'),
	link_with: pevents,
	dependencies: [pthreads])

# Tests that don't required wfmo
basic_tests = ['ManualResetInitialState',
		'AutoResetInitialState',
		'ManualResetBasicTests',
		'AutoResetBasicTests',
		'EventContention',
	]
# Tests that required wfmo
wfmo_tests = [
    'WaitTimeoutAllSignalled',
    'AtomicWaitAll',
  ]

test_std = 'c++11'
# Windows has deprecated C++11 support, use C++17 to get [[maybe_unused]]
if build_machine.system() == 'windows'
	test_std = 'c++17'
endif

# Single file include
custom_target('pevents.hpp',
	build_by_default: true,
	command: ['sed', '/#include "pevents.h"/d', '@INPUT@'],
	capture: true,
	input: ['src/pevents.h', srcs],
	output: 'pevents.hpp'
  )

# Sample program demonstrating capabilities
if get_option('wfmo')
	sample = executable('sample', ['examples/sample.cpp'],
		include_directories: incdir,
		cpp_args: args,
		   override_options: ['cpp_std=' + test_std],
		dependencies: [pevents])
endif

tests = []
test_args = []
foreach test : basic_tests
  tests += test
endforeach
if get_option('wfmo')
  test_args += '-DWFMO'
  foreach test : wfmo_tests
	tests += test
  endforeach
endif

foreach test : tests
	exe = executable(test, ['tests/' + test + '.cpp'],
		# build_by_default is currently broken for tests
		# See https://github.com/mesonbuild/meson/issues/3662
		build_by_default: false,
		cpp_args: test_args,
               override_options: ['cpp_std=' + test_std],
		include_directories: incdir,
		dependencies: pevents)
	test(test, exe)
endforeach

# Add the option of running tests under valgrind, and do it by default if it's found.
valgrind = find_program('valgrind', required: false)
add_test_setup('valgrind',
		exe_wrapper: find_program('valgrind.sh'),
		gdb: false,
		is_default: valgrind.found(),
		timeout_multiplier: 1
)
