project('strahler-tree-experiments', 'c', 'cpp',
  version : '0.1',
  default_options : [
    'warning_level=3',
    'c_std=c2x',
    'cpp_std=c++17',
    'optimization=3',
    'b_coverage=true'
  ]
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

boost_dep = dependency('boost', required : true)

# Executables
genstree = executable('genstree',
  ['genstree.c', 'prtstree.c'],
  install : true
)

pms2dot = executable('pms2dot',
  ['pms2dot.c', 'prtstree.c'],
  install : true
)

lenstree = executable('lenstree',
  'lenstree.cpp',
  dependencies : boost_dep,
  install : true
)

# Unit Tests
unit_tests_exe = executable('unit_tests',
  ['tests/unit_tests.c', 'prtstree.c'],
  c_args : '-DUNIT_TEST',
  include_directories : include_directories('.')
)

test('unit_tests', unit_tests_exe)

# Regression Tests
# We add the build directory to the PATH so that the script can find the binaries.
test('regression_tests',
  find_program('tests/run-regression.sh'),
  env : ['PATH=' + meson.current_build_dir() + ':' + '/usr/bin:/bin'],
  timeout : 600,
  depends : [genstree, pms2dot, lenstree],
  workdir : meson.current_source_dir()
)
