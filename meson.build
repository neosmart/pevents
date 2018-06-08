project('pevents', ['cpp'])

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

# tests that don't required wfmo
basic_tests = ['ManualResetInitialState',
		'AutoResetInitialState',
		'ManualResetBasicTests',
		'AutoResetBasicTests',
	]
# tests that required wfmo
wfmo_tests = []

sample = executable('sample', ['examples/sample.cpp'],
	include_directories: incdir,
	cpp_args: args,
	link_with: pevents,
	dependencies: pthreads)

tests = []
foreach test : basic_tests
  tests += test
endforeach
if get_option('wfmo')
  foreach test : wfmo_tests
	tests += test
  endforeach
endif

foreach test : tests
	exe = executable(test, ['tests/' + test + '.cpp'],
		build_by_default: false,
		include_directories: incdir,
		link_with: pevents,
		dependencies: pthreads)
	test(test, exe)
endforeach

