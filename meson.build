project('strahler-tree-experiments', 'c', 'cpp',
  version : '0.1',
  default_options : [
    'warning_level=3',
    'c_std=c2x',
    'cpp_std=c++20',
    'optimization=3',
    'b_coverage=true'
  ]
)

cc = meson.get_compiler('c')
cpp = meson.get_compiler('cpp')

boost_dep = dependency('boost', required : false)

# Executables
genstree = executable('genstree',
  ['genstree.c', 'prtstree.c'],
  install : true
)

pms2dot = executable('pms2dot',
  ['pms2dot.c', 'prtstree.c'],
  install : true
)

if boost_dep.found()
  lenstree = executable('lenstree',
    'lenstree.cpp',
    dependencies : boost_dep,
    install : true
  )
else
  lenstree = []
endif

str_tree = executable('str-tree',
  'str-tree.cc',
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
]

foreach ct : pms2dot_cases
  id = ct[0]
  input = ct[1]
  test('pms2dot_' + id, run_single_test,
    args: [pms2dot, 'tests/golden/pms_' + id + '.out', 'tests/actual/pms_' + id + '.out', input],
    workdir: meson.project_source_root())
endforeach

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
