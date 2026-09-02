project(
  'strahler-tree-experiments',
  'c',
  'cpp',
  version : '0.1',
  default_options : [
    'warning_level=3',
    'c_std=c2x',
    'cpp_std=c++20',
    'b_coverage=false',
  ],
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

lenstree_option = get_option('lenstree')
boost_dep = dependency('boost', required : lenstree_option)
source_includes = include_directories('src')

stree_lib = static_library('stree', 'src/stree.c')
stree_dep = declare_dependency(
  include_directories : source_includes,
  link_with : stree_lib,
)

dot_utils_lib = static_library('dot_utils', 'src/dot_utils.c')
dot_utils_dep = declare_dependency(
  include_directories : source_includes,
  link_with : dot_utils_lib,
)

ordered_tree_lib = static_library(
  'ordered_tree',
  'src/ordered_tree.c',
  dependencies : dot_utils_dep,
)
ordered_tree_dep = declare_dependency(
  dependencies : dot_utils_dep,
  include_directories : source_includes,
  link_with : ordered_tree_lib,
)

# Executables
genstree = executable('genstree',
  'src/genstree.c',
  dependencies : [stree_dep, ordered_tree_dep],
  install : true
)

pgfilt = executable('pgfilt',
  ['src/pgfilt.c', 'src/pargame.c'],
  install : true
)

pms2dot = executable('pms2dot',
  'src/pms2dot.c',
  dependencies : ordered_tree_dep,
  install : true
)

if not lenstree_option.disabled() and boost_dep.found()
  lenstree = executable('lenstree',
    'src/lenstree.cpp',
    dependencies : boost_dep,
    install : true
  )
else
  lenstree = []
endif

str_tree = executable('str-tree',
  'src/str-tree.cc',
  install : true
)

# Unit Tests
test_stree_exe = executable(
  'test_stree',
  'tests/test_stree.c',
  dependencies : stree_dep,
)
test('test_stree', test_stree_exe)

test_ordered_tree_exe = executable(
  'test_ordered_tree',
  'tests/test_ordered_tree.c',
  dependencies : ordered_tree_dep,
)
test('test_ordered_tree', test_ordered_tree_exe)

test_dot_utils_exe = executable(
  'test_dot_utils',
  'tests/test_dot_utils.c',
  dependencies : dot_utils_dep,
)
test('test_dot_utils', test_dot_utils_exe)

# Exercise every supported command-line entry point. These smoke tests are
# intentionally output-agnostic; the regression tests cover stable output.
test('str_tree_all', str_tree)
test('genstree_help', genstree, args : ['--help'])
test('str_tree_one_argument', str_tree, args : ['0'])
test('str_tree_two_arguments', str_tree, args : ['0', '2'])
test('pgfilt_help', pgfilt, args : ['-h'])
test('pms2dot_help_exit', pms2dot, args : ['-h'])
run_with_input = find_program('tests/run-with-input.sh')
test(
  'pgfilt_rejects_incomplete_parser_input',
  run_with_input,
  args : [files('tests/games/incomplete.pg'), pgfilt],
  should_fail : true,
)

# Regression Tests
run_single_test = find_program('tests/run-single-test.sh')

genstree_cases = [
  ['2', '1', '2'],
  ['3', '1', '3'],
  ['4', '2', '4'],
  ['3', '2', '4'],
  ['1', '2', '4'],
  ['1', '2', '2'],
  ['2', '2', '2'],
]

foreach ct : genstree_cases
  k = ct[0]
  t = ct[1]
  h = ct[2]
  base_name = 'k' + k + '_t' + t + '_h' + h

  test('genstree_' + base_name, run_single_test,
    args: [genstree, 'tests/golden/' + base_name + '.out', 'tests/actual/' + base_name + '.out', 'EMPTY', '-k', k, '-t', t, '-h', h],
    workdir: meson.project_source_root())

  test('genstree_j_' + base_name, run_single_test,
    args: [genstree, 'tests/golden/j_' + base_name + '.out', 'tests/actual/j_' + base_name + '.out', 'EMPTY', '-k', k, '-t', t, '-h', h, '-j'],
    workdir: meson.project_source_root())

  test('genstree_d_' + base_name, run_single_test,
    args: [genstree, 'tests/golden/d_' + base_name + '.out', 'tests/actual/d_' + base_name + '.out', 'EMPTY', '-k', k, '-t', t, '-h', h, '-d'],
    workdir: meson.project_source_root())

  test('genstree_p1_' + base_name, run_single_test,
    args: [genstree, 'tests/golden/p1_' + base_name + '.out', 'tests/actual/p1_' + base_name + '.out', 'EMPTY', '-k', k, '-t', t, '-h', h, '-p', '1'],
    workdir: meson.project_source_root())

  if boost_dep.found()
    test('lenstree_' + base_name, run_single_test,
      args: [lenstree, 'tests/golden/len_' + base_name + '.out', 'tests/actual/len_' + base_name + '.out', 'EMPTY', '-k', k, '-t', t, '-h', h],
      workdir: meson.project_source_root())
  endif
endforeach

# Special genstree leaf cases
genstree_l_cases = [
  ['2', '1', '2', '1'], ['2', '1', '2', '3'],
  ['3', '1', '3', '1'], ['3', '1', '3', '5'],
  ['4', '2', '4', '1'], ['4', '2', '4', '31'],
  ['3', '2', '4', '1'], ['3', '2', '4', '103'],
  ['1', '2', '4', '1'],
  ['1', '2', '2', '1'],
  ['2', '2', '2', '1'], ['2', '2', '2', '7'],
]

foreach ct : genstree_l_cases
  k = ct[0]
  t = ct[1]
  h = ct[2]
  l = ct[3]
  name = 'l' + l + '_k' + k + '_t' + t + '_h' + h
  test('genstree_' + name, run_single_test,
    args: [genstree, 'tests/golden/' + name + '.out', 'tests/actual/' + name + '.out', 'EMPTY', '-k', k, '-t', t, '-h', h, '-l', l],
    workdir: meson.project_source_root())
endforeach

pms2dot_cases = [
  ['0', '0|1|'],
  ['1', '0,0|0,1|1,0|1,1|'],
  ['2', '0,e,1|0,1,1|1,e,0|'],
  ['3', '0,0,1,e,1|0,1,1,0,0|'],
  ['4', '0,1|1,0|0,0|'],
  ['5', '|'],
  ['6', 'e,e|0,e|'],
  ['7', '1,0|0,0|1,1|0,1|'],
]

foreach ct : pms2dot_cases
  id = ct[0]
  input = ct[1]
  test('pms2dot_' + id, run_single_test,
    args: [pms2dot, 'tests/golden/pms_' + id + '.out', 'tests/actual/pms_' + id + '.out', input],
    workdir: meson.project_source_root())
endforeach

dot_program = find_program('dot', required : false)
if dot_program.found()
  foreach ct : pms2dot_cases
    id = ct[0]
    test(
      'pms2dot_' + id + '_dot_syntax',
      dot_program,
      args : [
        '-Tdot',
        '-o',
        '/dev/null',
        files('tests/golden/pms_' + id + '.out'),
      ],
    )
  endforeach

  foreach ct : genstree_cases
    base_name = 'k' + ct[0] + '_t' + ct[1] + '_h' + ct[2]
    test(
      'genstree_' + base_name + '_dot_syntax',
      dot_program,
      args : [
        '-Tdot',
        '-o',
        '/dev/null',
        files('tests/golden/d_' + base_name + '.out'),
      ],
    )
  endforeach
endif

test('pms2dot_h', run_single_test,
  args: [pms2dot, 'tests/golden/pms_h.out', 'tests/actual/pms_h.out', 'EMPTY', '-h'],
  workdir: meson.project_source_root())

error_cases = [
  ['gen_err_k', genstree, ['-t', '1', '-h', '2']],
  ['gen_err_k_val', genstree, ['-k', '0', '-t', '1', '-h', '2']],
  ['gen_err_l', genstree, ['-k', '2', '-t', '1', '-h', '2', '-l', '0']],
  ['pms_err_flag', pms2dot, ['-x']],
]

if boost_dep.found()
  error_cases += [
    ['len_err_args', lenstree, ['-k', '1']],
    ['len_err_k', lenstree, ['-k', '0', '-t', '1', '-h', '2']],
  ]
endif

foreach ec : error_cases
  name = ec[0]
  exe = ec[1]
  args = ec[2]
  test(name, run_single_test,
    args: [exe, 'tests/golden/' + name + '.out', 'tests/actual/' + name + '.out', 'EMPTY'] + args,
    workdir: meson.project_source_root())
endforeach
