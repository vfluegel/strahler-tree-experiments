project(
  'strahler-tree-experiments',
  'c',
  'cpp',
  version : run_command('python3', files('ci/version.py'), check : true).stdout().strip(),
  meson_version : '>=1.4.0',
  default_options : [
    'warning_level=3',
    # Apple Clang releases can still use the draft spelling for C23.
    'c_std=c23,c2x',
    'cpp_std=c++20',
    'b_coverage=false',
  ],
)

add_project_arguments('-fvisibility=hidden', language : 'c')

lenstree_option = get_option('lenstree')
boost_dep = dependency('boost', required : false)
if lenstree_option.disabled()
  boost_dep = disabler()
elif not boost_dep.found() and meson.get_compiler('cpp').has_header('boost/functional/hash.hpp', required : lenstree_option)
  boost_dep = declare_dependency()
endif
source_includes = include_directories('src')

version_configuration = configuration_data()
version_configuration.set_quoted(
  'STRAHLER_TREE_VERSION',
  meson.project_version(),
)
version_configuration.set_quoted('STRAHLER_TREE_GIT', '@VCS_TAG@')
version_template = configure_file(
  output : 'strahler_tree_version.h.in',
  configuration : version_configuration,
)
version_header = vcs_tag(
  input : version_template,
  output : 'strahler_tree_version.h',
  command : [find_program('ci/git-version.sh'), meson.project_source_root()],
  # Meson replaces an empty fallback with the project version.
  fallback : 'no git',
)
cli_version_lib = static_library(
  'cli_version',
  ['src/cli_version.c', 'src/cli_commands.c', version_header],
)
cli_version_dep = declare_dependency(
  sources : version_header,
  include_directories : source_includes,
  link_with : cli_version_lib,
)

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

pg_set_lib = static_library('pg_set', 'src/pg_set.c')
pg_set_dep = declare_dependency(
  include_directories : source_includes,
  link_with : pg_set_lib,
)

json_utils_lib = static_library('json_utils', 'src/json_utils.c', dependencies : pg_set_dep)
json_utils_dep = declare_dependency(
  dependencies : pg_set_dep,
  include_directories : source_includes,
  link_with : json_utils_lib,
)

ordered_tree_lib = static_library(
  'ordered_tree',
  'src/ordered_tree.c',
  dependencies : [dot_utils_dep, json_utils_dep],
)
ordered_tree_dep = declare_dependency(
  dependencies : [dot_utils_dep, json_utils_dep],
  include_directories : source_includes,
  link_with : ordered_tree_lib,
)

pg_game_lib = static_library('pg_game', 'src/pg_game.c')
pg_game_dep = declare_dependency(
  include_directories : source_includes,
  link_with : pg_game_lib,
)

pg_attractor_lib = static_library(
  'pg_attractor',
  'src/pg_attractor.c',
  dependencies : [pg_game_dep, pg_set_dep],
)
pg_attractor_dep = declare_dependency(
  dependencies : [pg_game_dep, pg_set_dep],
  include_directories : source_includes,
  link_with : pg_attractor_lib,
)

pg_preprocess_lib = static_library(
  'pg_preprocess',
  'src/pg_preprocess.c',
  dependencies : pg_attractor_dep,
)
pg_preprocess_dep = declare_dependency(
  dependencies : pg_attractor_dep,
  include_directories : source_includes,
  link_with : pg_preprocess_lib,
)

ad_tree_lib = static_library(
  'ad_tree',
  ['src/ad_tree.c', 'src/ad_trace.c', 'src/ad_tree_json.c'],
  dependencies : [pg_attractor_dep, json_utils_dep],
)
ad_tree_dep = declare_dependency(
  dependencies : [pg_attractor_dep, json_utils_dep],
  include_directories : source_includes,
  link_with : ad_tree_lib,
)

zielonka_lib = static_library(
  'zielonka',
  'src/zielonka.c',
  dependencies : ad_tree_dep,
)
zielonka_dep = declare_dependency(
  dependencies : ad_tree_dep,
  include_directories : source_includes,
  link_with : zielonka_lib,
)

ad_lift_tree_lib = static_library(
  'ad_lift_tree',
  'src/ad_lift_tree.c',
  dependencies : [ordered_tree_dep, pg_game_dep],
)
ad_lift_tree_dep = declare_dependency(
  dependencies : [ordered_tree_dep, pg_game_dep],
  include_directories : source_includes,
  link_with : ad_lift_tree_lib,
)

ad_lift_lib = static_library(
  'ad_lift',
  ['src/ad_lift.c', 'src/ad_lift_solve.c', 'src/ad_sym.c'],
  dependencies : [ad_lift_tree_dep, ad_tree_dep, stree_dep],
)
ad_lift_dep = declare_dependency(
  dependencies : [ad_lift_tree_dep, ad_tree_dep, stree_dep],
  include_directories : source_includes,
  link_with : ad_lift_lib,
)

pg_dot_lib = static_library('pg_dot', 'src/pg_dot.c',
  dependencies : [pg_game_dep, pg_set_dep])
pg_dot_dep = declare_dependency(
  dependencies : [pg_game_dep, pg_set_dep],
  include_directories : source_includes,
  link_with : pg_dot_lib,
)

ad_tree_dot_lib = static_library(
  'ad_tree_dot',
  'src/ad_tree_dot.c',
  dependencies : [ad_tree_dep, pg_dot_dep],
)
ad_tree_dot_dep = declare_dependency(
  dependencies : [ad_tree_dep, pg_dot_dep],
  include_directories : source_includes,
  link_with : ad_tree_dot_lib,
)

pm_lift_lib = static_library(
  'pm_lift',
  ['src/pm_lift.c', 'src/pm_solve.c', 'src/strpm.c', 'src/sspm.c'],
  dependencies : [pg_attractor_dep, json_utils_dep],
)
pm_lift_dep = declare_dependency(
  dependencies : pg_attractor_dep,
  include_directories : source_includes,
  link_with : pm_lift_lib,
)

# Executables
genstree = executable('genstree',
  ['src/genstree.c', 'src/genstree_command.c'],
  dependencies : [stree_dep, ordered_tree_dep, cli_version_dep],
  install : true
)

pgfilt = executable('pgfilt',
  'src/pgfilt.c',
  dependencies : [pg_game_dep, cli_version_dep],
  install : true
)

pms2dot = executable('pms2dot',
  ['src/pms2dot.c', 'src/pms2dot_command.c'],
  dependencies : [ordered_tree_dep, cli_version_dep],
  install : true
)

pg2adot = executable(
  'pg2adot',
  ['src/pg2adot.c', 'src/pg2adot_command.c'],
  dependencies : [ad_lift_dep, zielonka_dep, ad_tree_dot_dep, cli_version_dep, pg_preprocess_dep],
  install : true,
)

pm_tree_dot_lib = static_library('pm_tree_dot', 'src/pm_tree_dot.c',
  dependencies : [pm_lift_dep, ordered_tree_dep, pg_dot_dep])
pm_tree_dot_dep = declare_dependency(
  dependencies : [pm_lift_dep, ordered_tree_dep, pg_dot_dep],
  include_directories : source_includes,
  link_with : pm_tree_dot_lib,
)
pg2pmdot = executable('pg2pmdot', ['src/pg2pmdot.c', 'src/pg2pmdot_command.c'],
  dependencies : [pm_tree_dot_dep, pg_preprocess_dep, cli_version_dep],
  install : true)

if not lenstree_option.disabled() and boost_dep.found()
  lenstree = executable('lenstree',
    'src/lenstree.cpp',
    dependencies : [boost_dep, cli_version_dep],
    install : true
  )
else
  lenstree = []
endif

str_tree = executable('str-tree',
  'src/str-tree.cc',
  dependencies : cli_version_dep,
  install : true
)

# Unit Tests
test_cli_parse_exe = executable(
  'test_cli_parse',
  'tests/test_cli_parse.cpp',
  include_directories : source_includes,
)
test('test_cli_parse', test_cli_parse_exe)
test(
  'test_wrappers',
  find_program('tests/test-wrappers.sh'),
  args : [genstree, pgfilt, meson.project_version()],
  workdir : meson.project_source_root(),
)

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
  dependencies : [dot_utils_dep, json_utils_dep],
)
test('test_dot_utils', test_dot_utils_exe)

test_pg_game_exe = executable(
  'test_pg_game',
  'tests/test_pg_game.c',
  dependencies : pg_game_dep,
)
test('test_pg_game', test_pg_game_exe,
  args : [meson.project_source_root() / 'tests/games'])
test_pg_game_alloc_exe = executable('test_pg_game_alloc',
  ['tests/test_pg_game_alloc.c', 'src/pg_game.c'],
  include_directories : source_includes,
  c_args : ['-DPG_GAME_TEST_ALLOCATOR'])
test('test_pg_game_alloc', test_pg_game_alloc_exe)

test_pg_set_exe = executable(
  'test_pg_set',
  'tests/test_pg_set.c',
  dependencies : pg_set_dep,
)
test('test_pg_set', test_pg_set_exe)

test_pg_attractor_exe = executable(
  'test_pg_attractor',
  ['tests/test_pg_attractor.c', 'tests/reference_attractor.c'],
  dependencies : [pg_attractor_dep, json_utils_dep],
)
test('test_pg_attractor', test_pg_attractor_exe)

test_pg_preprocess_exe = executable(
  'test_pg_preprocess',
  ['tests/test_pg_preprocess.c', 'tests/reference_solver.c'],
  dependencies : [pg_preprocess_dep, ad_lift_dep, zielonka_dep],
)
test('test_pg_preprocess', test_pg_preprocess_exe,
  workdir : meson.project_source_root())
foreach rule : ['loops', 'wcwc', 'all']
  test('test_pg_preprocess_scaling_' + rule, test_pg_preprocess_exe,
    args : [rule], timeout : 30)
endforeach

test_zielonka_exe = executable(
  'test_zielonka',
  ['tests/test_zielonka.c', 'tests/reference_solver.c'],
  dependencies : zielonka_dep,
)
test('test_zielonka', test_zielonka_exe)

test_ad_lift_tree_exe = executable(
  'test_ad_lift_tree',
  'tests/test_ad_lift_tree.c',
  dependencies : [ad_lift_tree_dep, stree_dep],
)
test('test_ad_lift_tree', test_ad_lift_tree_exe)

test_ad_lift_exe = executable(
  'test_ad_lift',
  ['tests/test_ad_lift.c', 'tests/reference_solver.c'],
  dependencies : [ad_lift_dep, zielonka_dep],
)
test('test_ad_lift', test_ad_lift_exe, timeout : 120,
  workdir : meson.project_source_root())

python = find_program('python3')
meson.add_dist_script(python, files('ci/version.py'), '--dist', meson.project_version())
test('test_version', python,
  args : files('tests/test_version.py'), suite : 'python')
test('test_pg_game_property', python,
  args : [files('tests/test_pg_game_property.py'), pgfilt], suite : 'python')
test('test_pg_game_memory', python,
  args : [files('tests/test_pg_game_memory.py'), pgfilt, get_option('b_sanitize')],
  suite : 'python', timeout : 60)
test(
  'test_research',
  python,
  args : [files('tests/test_research.py'), pg2adot],
  workdir : meson.project_source_root(),
  suite : 'python',
  timeout : 120,
)

test_ad_sym_exe = executable(
  'test_ad_sym',
  ['tests/test_ad_sym.c', 'tests/reference_solver.c'],
  dependencies : [ad_lift_dep, zielonka_dep, pg_preprocess_dep],
)
test('test_ad_sym', test_ad_sym_exe, timeout : 120,
  workdir : meson.project_source_root())

test_ad_tree_dot_exe = executable(
  'test_ad_tree_dot',
  'tests/test_ad_tree_dot.c',
  dependencies : [ad_lift_dep, zielonka_dep, ad_tree_dot_dep],
)
test('test_ad_tree_dot', test_ad_tree_dot_exe)

# Exercise every supported command-line entry point. These smoke tests are
# intentionally output-agnostic; the regression tests cover stable output.
test('str_tree_all', str_tree)
test('genstree_help', genstree, args : ['--help'])
test('str_tree_index', str_tree, args : ['--index=0'])
test('str_tree_index_level', str_tree, args : ['--index=0', '--p-level=2'])
test('pgfilt_help', pgfilt, args : ['--help'])
test('pg2adot_help', pg2adot, args : ['--help'])
test(
  'pg2adot_rejects_bad_adaptive_t',
  pg2adot,
  args : ['--adaptive-t=bad'],
  should_fail : true,
)
test(
  'pgfilt_rejects_bad_priority_mode',
  pgfilt,
  args : ['--priority-mode=bad'],
  should_fail : true,
)
test(
  'pg2adot_rejects_bad_priority_mode',
  pg2adot,
  args : ['--priority-mode=bad'],
  should_fail : true,
)
test(
  'pg2adot_rejects_bad_view',
  pg2adot,
  args : ['--view=bad'],
  should_fail : true,
)
test(
  'pg2adot_rejects_bad_algorithm',
  pg2adot,
  args : ['--algorithm=bad'],
  should_fail : true,
)
foreach invalid_shave : ['bad', 'loops,', 'all,loops', 'none,wcwc']
  test('pg2adot_rejects_shave_' + invalid_shave, pg2adot,
    args : ['--initial-shave=' + invalid_shave, files('tests/games/strahler_two.pg')],
    should_fail : true)
endforeach
foreach removed : ['--shave=all', '--incremental']
  foreach program : [pg2adot, pg2pmdot]
    test(program.name() + '_rejects_removed_' + removed, program,
      args : [removed, files('tests/games/strahler_two.pg')], should_fail : true)
  endforeach
endforeach
foreach algorithm : ['zielonka', 'zielonka-sym']
  test('pg2adot_rejects_iter_shave_' + algorithm, pg2adot,
    args : ['--algorithm=' + algorithm, '--iter-shave',
      files('tests/games/strahler_two.pg')], should_fail : true)
endforeach
test('pg2adot_rejects_iter_shave_argument', pg2adot,
  args : ['--iter-shave=all', files('tests/games/strahler_two.pg')], should_fail : true)
test(
  'pg2adot_rejects_zero_tree_k',
  pg2adot,
  args : ['--tree-k=0'],
  should_fail : true,
)
test(
  'pg2adot_rejects_bad_start_t',
  pg2adot,
  args : ['--adaptive-t', '--start-t=x'],
  should_fail : true,
)
test(
  'pg2adot_rejects_zero_kary',
  pg2adot,
  args : ['--kary=0'],
  should_fail : true,
)
test(
  'pg2adot_rejects_bad_host_limit',
  pg2adot,
  args : ['--max-host-leaves=x'],
  should_fail : true,
)
test(
  'pg2adot_rejects_two_hosts',
  pg2adot,
  args : ['--tree-k=1', '--kary=2'],
  should_fail : true,
)
test(
  'pg2adot_rejects_start_t_without_adaptive',
  pg2adot,
  args : ['--start-t=2'],
  should_fail : true,
)
test(
  'pg2adot_rejects_zielonka_lifting_options',
  pg2adot,
  args : ['--algorithm=zielonka', '--kary=2'],
  should_fail : true,
)
test(
  'pg2adot_rejects_missing_tree_file',
  pg2adot,
  args : ['--tree-file=/nonexistent/host.tree'],
  should_fail : true,
)
test('pms2dot_help_exit', pms2dot, args : ['--help'])
check_version = find_program('tests/check-version.sh')
cli_programs = [genstree, pgfilt, pms2dot, pg2adot, pg2pmdot, str_tree]
if boost_dep.found()
  cli_programs += [lenstree]
endif
foreach program : cli_programs
  test(program.name() + '_version', check_version,
    args : [program, meson.project_version()])
  test(program.name() + '_cli', python,
    args : [files('tests/test_cli.py'), program, meson.project_version()],
    suite : 'python')
endforeach
test('source_version', python,
  args : [files('tests/test_source_version.py'), meson.project_version(), find_program('meson').full_path()],
  suite : 'python', timeout : 120)

run_with_input = find_program('tests/run-with-input.sh')
test(
  'pgfilt_rejects_missing_semicolon',
  run_with_input,
  args : [files('tests/games/incomplete.pg'), pgfilt],
  should_fail : true,
)

# Regression Tests
run_single_test = find_program('tests/run-single-test.sh')

foreach name, arguments : {
  'fixed' : ['strahler_two', ['--tree-k=1']],
  'kary' : ['strahler_two', ['--kary=1', '--view=jurdzinski']],
  'tree' : ['iter_shave', ['--tree-file=tests/trees/chain_three.tree', '--view=tree-relative']],
  'adaptive' : ['strahler_two', ['--adaptive-t']],
  'incremental' : ['strahler_two', ['--adaptive-t=incremental']],
  'sym' : ['strahler_two', ['--algorithm=lifting-sym', '--tree-k=1']],
  'capped' : ['strahler_two', ['--adaptive-t', '--tree-k=1']],
  'limit' : ['strahler_two', ['--adaptive-t', '--max-host-leaves=1']],
  'pre' : ['shave_partial', ['--initial-shave=loops', '--compress', '--adaptive-t']],
  'empty' : ['shave_single', ['--initial-shave=all', '--adaptive-t']],
  'no_progress' : ['disagree_lifting_strahler', ['--kary=1']],
}
  test('pg2adot_iter_' + name, run_single_test,
    args : [pg2adot, 'tests/golden/pg2adot_iter_' + name + '.out',
      'tests/actual/pg2adot_iter_' + name + '.out',
      '@tests/games/' + arguments[0] + '.pg', '--iter-shave', '--stats',
      '--labels=sets'] + arguments[1], workdir : meson.project_source_root())
endforeach

foreach algorithm : ['zielonka-sym', 'lifting-sym']
  foreach name, arguments : {
    'sets' : ['pg2adot_sets', ['--labels=sets', '--max-set-items=1']],
    'relative' : ['pg2adot_tree_relative_sets', [
      '--view=tree-relative', '--labels=sets', '--max-set-items=1']],
    'jurdzinski' : ['pg2adot_jurdzinski_sets', [
      '--view=jurdzinski', '--labels=sets', '--max-set-items=1']],
    'counts' : ['pg2adot_counts', []],
    'no_verify' : ['pg2adot_counts', ['--no-verify']],
  }
    test('pg2adot_' + algorithm + '_' + name, run_single_test,
      args : [pg2adot, 'tests/golden/' + arguments[0] + '.out',
        'tests/actual/pg2adot_' + algorithm + '_' + name + '.out',
        '@tests/games/ordered_two_children.pg', '--algorithm=' + algorithm] + arguments[1],
      workdir : meson.project_source_root())
  endforeach
endforeach
foreach host_option : ['--tree-k=1', '--kary=1']
  test('pg2adot_lifting_sym_partial_' + host_option, run_single_test,
    args : [pg2adot, 'tests/golden/pg2adot_kary_partial_sets.out',
      'tests/actual/pg2adot_lifting_sym_partial_' + host_option + '.out',
      '@tests/games/strahler_two.pg', '--algorithm=lifting-sym',
      '--labels=sets', host_option], workdir : meson.project_source_root())
endforeach
foreach name, arguments : {
  'sym_adaptive' : ['--algorithm=lifting-sym', '--adaptive-t'],
  'sym_incremental' : ['--algorithm=lifting-sym', '--adaptive-t=incremental'],
  'sym_zielonka_host' : ['--algorithm=zielonka-sym', '--kary=2'],
  'sym_zielonka_incremental' : ['--algorithm=zielonka-sym', '--adaptive-t=incremental'],
}
  test('pg2adot_rejects_' + name, pg2adot,
    args : arguments, should_fail : true)
endforeach
foreach name, arguments : {
  'sym_h4' : ['thesis_h4', ['--algorithm=zielonka-sym']],
  'sym_f4' : ['thesis_f4', ['--algorithm=zielonka-sym']],
  'sym_lifting_stats' : ['strahler_two', ['--algorithm=lifting-sym', '--kary=1']],
}
  test('pg2adot_' + name, run_single_test,
    args : [pg2adot, 'tests/golden/pg2adot_' + name + '.out',
      'tests/actual/pg2adot_' + name + '.out',
      '@tests/games/' + arguments[0] + '.pg', '--stats',
      '--player=even', '--labels=none'] + arguments[1],
    workdir : meson.project_source_root())
endforeach

preprocess_cases = {
  'shave_loops' : ['shave_loops', ['--initial-shave=loops', '--labels=sets']],
  'shave_wcwc' : ['shave_wcwc', ['--initial-shave=wcwc', '--labels=sets']],
  'shave_single' : ['shave_single', ['--initial-shave=single', '--labels=sets']],
  'shave_relative' : ['shave_wcwc', [
    '--initial-shave=loops,wcwc', '--view=tree-relative', '--labels=sets',
    '--max-set-items=2',
  ]],
  'shave_empty_jurdzinski' : ['shave_single', ['--initial-shave=all', '--view=jurdzinski']],
  'compress_original' : ['compress', ['--compress', '--labels=sets']],
  'compress_compact' : ['compress', ['--compress', '--priority-mode=compact']],
  'compress_jurdzinski' : ['compress', ['--compress', '--view=jurdzinski']],
}
foreach name, arguments : preprocess_cases
  foreach algorithm : ['lifting', 'zielonka', 'zielonka-sym', 'lifting-sym']
    test('pg2adot_' + name + '_' + algorithm, run_single_test,
      args : [pg2adot, 'tests/golden/pg2adot_' + name + '.out',
        'tests/actual/pg2adot_' + name + '_' + algorithm + '.out',
        '@tests/games/' + arguments[0] + '.pg', '--algorithm=' + algorithm] + arguments[1],
      workdir : meson.project_source_root())
  endforeach
endforeach
foreach algorithm : ['lifting', 'zielonka']
  test('pg2adot_shave_stats_' + algorithm, run_single_test,
    args : [pg2adot, 'tests/golden/pg2adot_shave_stats_' + algorithm + '.out',
      'tests/actual/pg2adot_shave_stats_' + algorithm + '.out',
      '@tests/games/shave_single.pg', '--algorithm=' + algorithm,
      '--initial-shave=all', '--compress', '--stats', '--labels=none'],
    workdir : meson.project_source_root())
endforeach
test('pg2adot_shave_empty_adaptive', run_single_test,
  args : [pg2adot, 'tests/golden/pg2adot_shave_single.out',
    'tests/actual/pg2adot_shave_empty_adaptive.out',
    '@tests/games/shave_single.pg', '--initial-shave=single', '--adaptive-t',
    '--max-host-leaves=0', '--labels=sets'],
  workdir : meson.project_source_root())
test('pg2adot_shave_partial', run_single_test,
  args : [pg2adot, 'tests/golden/pg2adot_shave_partial.out',
    'tests/actual/pg2adot_shave_partial.out', '@tests/games/shave_partial.pg',
    '--initial-shave=loops', '--tree-k=1', '--labels=sets'],
  workdir : meson.project_source_root())

test(
  'pgfilt_sparse_summary',
  run_single_test,
  args : [
    pgfilt,
    'tests/golden/pgfilt_summary.out',
    'tests/actual/pgfilt_summary.out',
    '@tests/games/sparse.pg',
  ],
  workdir : meson.project_source_root(),
)

test(
  'pgfilt_sparse_normalize',
  run_single_test,
  args : [
    pgfilt,
    'tests/golden/pgfilt_normalize.out',
    'tests/actual/pgfilt_normalize.out',
    '@tests/games/sparse.pg',
    '--normalize',
  ],
  workdir : meson.project_source_root(),
)

test(
  'pgfilt_gap_summary',
  run_single_test,
  args : [
    pgfilt,
    'tests/golden/pgfilt_gap_summary.out',
    'tests/actual/pgfilt_gap_summary.out',
    '@tests/games/priority_gap_start.pg',
  ],
  workdir : meson.project_source_root(),
)

test(
  'pgfilt_gap_normalize',
  run_single_test,
  args : [
    pgfilt,
    'tests/golden/pgfilt_gap_normalize.out',
    'tests/actual/pgfilt_gap_normalize.out',
    '@tests/games/priority_gap_start.pg',
    '--normalize',
  ],
  workdir : meson.project_source_root(),
)

test(
  'pgfilt_gap_compact_summary',
  run_single_test,
  args : [
    pgfilt,
    'tests/golden/pgfilt_gap_compact_summary.out',
    'tests/actual/pgfilt_gap_compact_summary.out',
    '@tests/games/priority_gap_start.pg',
    '--priority-mode=compact',
  ],
  workdir : meson.project_source_root(),
)

test(
  'pgfilt_gap_compact_normalize',
  run_single_test,
  args : [
    pgfilt,
    'tests/golden/pgfilt_gap_compact_normalize.out',
    'tests/actual/pgfilt_gap_compact_normalize.out',
    '@tests/games/priority_gap_start.pg',
    '--priority-mode=compact',
    '--normalize',
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_counts',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_counts.out',
    'tests/actual/pg2adot_counts.out',
    '@tests/games/ordered_two_children.pg',
    '-',
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_sets_truncated',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_sets.out',
    'tests/actual/pg2adot_sets.out',
    'EMPTY',
    '--labels=sets',
    '--max-set-items=1',
    files('tests/games/ordered_two_children.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_tree_relative_counts',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_tree_relative_counts.out',
    'tests/actual/pg2adot_tree_relative_counts.out',
    'EMPTY',
    '--view=tree-relative',
    files('tests/games/ordered_two_children.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_tree_relative_sets_truncated',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_tree_relative_sets.out',
    'tests/actual/pg2adot_tree_relative_sets.out',
    'EMPTY',
    '--view=tree-relative',
    '--labels=sets',
    '--max-set-items=1',
    files('tests/games/ordered_two_children.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_jurdzinski_counts',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_jurdzinski_counts.out',
    'tests/actual/pg2adot_jurdzinski_counts.out',
    'EMPTY',
    '--view=jurdzinski',
    files('tests/games/ordered_two_children.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_jurdzinski_sets',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_jurdzinski_sets.out',
    'tests/actual/pg2adot_jurdzinski_sets.out',
    'EMPTY',
    '--view=jurdzinski',
    '--labels=sets',
    '--max-set-items=1',
    files('tests/games/ordered_two_children.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_gap_original_priorities',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_gap_original.out',
    'tests/actual/pg2adot_gap_original.out',
    'EMPTY',
    files('tests/games/priority_gap_start.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_gap_compact_priorities',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_gap_compact.out',
    'tests/actual/pg2adot_gap_compact.out',
    'EMPTY',
    '--priority-mode=compact',
    files('tests/games/priority_gap_start.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_zielonka_counts',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_counts.out',
    'tests/actual/pg2adot_zielonka_counts.out',
    'EMPTY',
    '--algorithm=zielonka',
    files('tests/games/ordered_two_children.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_tree_file_complete',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_counts.out',
    'tests/actual/pg2adot_tree_file_complete.out',
    'EMPTY',
    '--tree-file=tests/trees/two_children.tree',
    files('tests/games/ordered_two_children.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_lifted_shape_sets',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_lifted_shape_sets.out',
    'tests/actual/pg2adot_lifted_shape_sets.out',
    'EMPTY',
    '--labels=sets',
    files('tests/games/lifted_shape.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_lifted_shape_zielonka_sets',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_lifted_shape_zielonka_sets.out',
    'tests/actual/pg2adot_lifted_shape_zielonka_sets.out',
    'EMPTY',
    '--algorithm=zielonka',
    '--labels=sets',
    files('tests/games/lifted_shape.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_partial_sets',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_partial_sets.out',
    'tests/actual/pg2adot_partial_sets.out',
    'EMPTY',
    '--tree-k=1',
    '--labels=sets',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_partial_tree_relative',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_partial_tree_relative.out',
    'tests/actual/pg2adot_partial_tree_relative.out',
    'EMPTY',
    '--tree-k=1',
    '--view=tree-relative',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_partial_jurdzinski_tree_file',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_partial_jurdzinski_tree_file.out',
    'tests/actual/pg2adot_partial_jurdzinski_tree_file.out',
    'EMPTY',
    '--tree-file=tests/trees/one_child.tree',
    '--view=jurdzinski',
    '--labels=sets',
    files('tests/games/ordered_two_children.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_adaptive_stats',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_adaptive_stats.out',
    'tests/actual/pg2adot_adaptive_stats.out',
    'EMPTY',
    '--adaptive-t',
    '--stats',
    '--player=even',
    '--labels=none',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_incremental_stats',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_incremental_stats.out',
    'tests/actual/pg2adot_incremental_stats.out',
    'EMPTY',
    '--adaptive-t=incremental',
    '--stats',
    '--player=even',
    '--labels=none',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_adaptive_limit',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_adaptive_limit.out',
    'tests/actual/pg2adot_adaptive_limit.out',
    'EMPTY',
    '--adaptive-t',
    '--max-host-leaves=1',
    '--labels=none',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_host_limit',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_host_limit.out',
    'tests/actual/pg2adot_host_limit.out',
    'EMPTY',
    '--max-host-leaves=5',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_invalid_tree',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_invalid_tree.out',
    'tests/actual/pg2adot_invalid_tree.out',
    'EMPTY',
    '--tree-file=tests/trees/invalid.tree',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_adaptive_capped',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_adaptive_capped.out',
    'tests/actual/pg2adot_adaptive_capped.out',
    'EMPTY',
    '--adaptive-t',
    '--tree-k=1',
    '--player=even',
    '--labels=none',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_kary_partial_sets',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_kary_partial_sets.out',
    'tests/actual/pg2adot_kary_partial_sets.out',
    'EMPTY',
    '--kary=1',
    '--labels=sets',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_kary_stats',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_kary_stats.out',
    'tests/actual/pg2adot_kary_stats.out',
    'EMPTY',
    '--kary=2',
    '--stats',
    '--player=even',
    '--labels=none',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

test(
  'pg2adot_zielonka_stats',
  run_single_test,
  args : [
    pg2adot,
    'tests/golden/pg2adot_zielonka_stats.out',
    'tests/actual/pg2adot_zielonka_stats.out',
    'EMPTY',
    '--algorithm=zielonka',
    '--stats',
    '--player=even',
    '--labels=none',
    files('tests/games/strahler_two.pg'),
  ],
  workdir : meson.project_source_root(),
)

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
    args: [genstree, 'tests/golden/' + base_name + '.out', 'tests/actual/' + base_name + '.out', 'EMPTY', '--strahler=' + k, '--log-leaves=' + t, '--height=' + h],
    workdir: meson.project_source_root())

  test('genstree_j_' + base_name, run_single_test,
    args: [genstree, 'tests/golden/j_' + base_name + '.out', 'tests/actual/j_' + base_name + '.out', 'EMPTY', '--strahler=' + k, '--log-leaves=' + t, '--height=' + h, '--count'],
    workdir: meson.project_source_root())

  test('genstree_d_' + base_name, run_single_test,
    args: [genstree, 'tests/golden/d_' + base_name + '.out', 'tests/actual/d_' + base_name + '.out', 'EMPTY', '--strahler=' + k, '--log-leaves=' + t, '--height=' + h, '--dot'],
    workdir: meson.project_source_root())

  test('genstree_p1_' + base_name, run_single_test,
    args: [genstree, 'tests/golden/p1_' + base_name + '.out', 'tests/actual/p1_' + base_name + '.out', 'EMPTY', '--strahler=' + k, '--log-leaves=' + t, '--height=' + h, '--p-level=1'],
    workdir: meson.project_source_root())

  if boost_dep.found()
    test('lenstree_' + base_name, run_single_test,
      args: [lenstree, 'tests/golden/len_' + base_name + '.out', 'tests/actual/len_' + base_name + '.out', 'EMPTY', '--strahler=' + k, '--log-leaves=' + t, '--height=' + h],
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
    args: [genstree, 'tests/golden/' + name + '.out', 'tests/actual/' + name + '.out', 'EMPTY', '--strahler=' + k, '--log-leaves=' + t, '--height=' + h, '--leaf=' + l],
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

test(
  'pms2dot_check_order',
  run_single_test,
  args : [
    pms2dot,
    'tests/golden/pms_0.out',
    'tests/actual/pms_check_order.out',
    '0|1|',
    '--check-order',
  ],
  workdir : meson.project_source_root(),
)

test(
  'pms2dot_reorder',
  run_single_test,
  args : [
    pms2dot,
    'tests/golden/pms_1.out',
    'tests/actual/pms_reorder.out',
    '1,1|0,1|1,0|0,0|',
    '--reorder',
  ],
  workdir : meson.project_source_root(),
)

test(
  'pms2dot_check_order_rejects_interleaving',
  run_single_test,
  args : [
    pms2dot,
    'tests/golden/pms_err_order.out',
    'tests/actual/pms_err_order.out',
    '0,0|1,0|0,1|',
    '--check-order',
  ],
  workdir : meson.project_source_root(),
)

dot_program = find_program('dot', required : false)
if dot_program.found()
  foreach name, arguments : preprocess_cases
    test('pg2adot_' + name + '_dot_syntax', dot_program,
      suite : 'graphviz',
      args : ['-Tdot', '-o', '/dev/null',
        files('tests/golden/pg2adot_' + name + '.out')])
  endforeach
  test('pg2adot_shave_partial_dot_syntax', dot_program,
    suite : 'graphviz',
    args : ['-Tdot', '-o', '/dev/null',
      files('tests/golden/pg2adot_shave_partial.out')])
  test(
    'pg2adot_counts_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_counts.out'),
    ],
  )
  test(
    'pg2adot_sets_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_sets.out'),
    ],
  )
  test(
    'pg2adot_tree_relative_counts_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_tree_relative_counts.out'),
    ],
  )
  test(
    'pg2adot_tree_relative_sets_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_tree_relative_sets.out'),
    ],
  )
  test(
    'pg2adot_jurdzinski_counts_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_jurdzinski_counts.out'),
    ],
  )
  test(
    'pg2adot_jurdzinski_sets_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_jurdzinski_sets.out'),
    ],
  )
  test(
    'pg2adot_gap_original_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_gap_original.out'),
    ],
  )
  test(
    'pg2adot_lifted_shape_sets_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_lifted_shape_sets.out'),
    ],
  )
  test(
    'pg2adot_lifted_shape_zielonka_sets_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_lifted_shape_zielonka_sets.out'),
    ],
  )
  test(
    'pg2adot_partial_sets_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_partial_sets.out'),
    ],
  )
  test(
    'pg2adot_partial_tree_relative_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_partial_tree_relative.out'),
    ],
  )
  test(
    'pg2adot_partial_jurdzinski_tree_file_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_partial_jurdzinski_tree_file.out'),
    ],
  )
  test(
    'pg2adot_kary_partial_sets_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_kary_partial_sets.out'),
    ],
  )
  test(
    'pg2adot_gap_compact_dot_syntax',
    dot_program,
    suite : 'graphviz',
    args : [
      '-Tdot',
      '-o',
      '/dev/null',
      files('tests/golden/pg2adot_gap_compact.out'),
    ],
  )
  foreach ct : pms2dot_cases
    id = ct[0]
    test(
      'pms2dot_' + id + '_dot_syntax',
      dot_program,
      suite : 'graphviz',
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
      suite : 'graphviz',
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
  args: [pms2dot, 'tests/golden/pms_h.out', 'tests/actual/pms_h.out', 'EMPTY', '--help'],
  workdir: meson.project_source_root())

error_cases = [
  ['gen_err_k', genstree, ['--log-leaves=1', '--height=2']],
  ['gen_err_k_val', genstree, ['--strahler=0', '--log-leaves=1', '--height=2']],
  ['gen_err_l', genstree, ['--strahler=2', '--log-leaves=1', '--height=2', '--leaf=0']],
  ['pms_err_flag', pms2dot, ['-x']],
]

if boost_dep.found()
  error_cases += [
    ['len_err_args', lenstree, ['--strahler=1']],
    ['len_err_k', lenstree, ['--strahler=0', '--log-leaves=1', '--height=2']],
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


test_pm_lift_exe = executable('test_pm_lift', 'tests/test_pm_lift.c',
  dependencies : [pm_lift_dep, pg_preprocess_dep, zielonka_dep])
test('test_pm_lift', test_pm_lift_exe, workdir : meson.project_source_root())

test('pg2pmdot_help', pg2pmdot, args : ['--help'])
foreach bad : [
  ['--algorithm=bad'], ['--format=bad'], ['--player=bad'], ['--labels=bad'],
  ['--priority-mode=bad'], ['--initial-shave=loops,'], ['--k=0', '--t=0'], ['--k=1'],
  ['--t=1'], ['--bits=1'], ['--algorithm=succinct', '--k=1', '--t=1'],
  ['--algorithm=succinct', '--bits=-1'], ['--k=999999999999999999999', '--t=1'],
  ['--max-set-items=x'], ['--bits=x'], ['--k=1', '--t=x'],
  ['--no-such-option'], ['one', 'two'], ['missing-file.pg'],
]
  test('pg2pmdot_rejects_' + '_'.join(bad), pg2pmdot,
    args : bad, should_fail : true)
endforeach

pm_cases = {
  'counts': ['tests/games/ordered_two_children.pg'],
  'sets': ['--labels=sets', '--max-set-items=1', 'tests/games/strahler_two.pg'],
  'none': ['--labels=none', 'tests/games/split_winners.pg'],
  'leaves': ['--format=leaves', 'tests/games/pm_cycle.pg'],
  'partial': ['--labels=sets', 'tests/games/strahler_two.pg'],
  'partial_leaves': ['--format=leaves', 'tests/games/strahler_two.pg'],
  'incremental': ['--iter-shave', '--labels=sets', 'tests/games/strahler_two.pg'],
  'incremental_leaves': ['--iter-shave', '--format=leaves', 'tests/games/strahler_two.pg'],
  'iter_stats': ['--iter-shave', '--stats', '--format=leaves', 'tests/games/strahler_two.pg'],
  'shave_compress': ['--initial-shave=loops', '--compress', '--labels=sets', 'tests/games/shave_partial.pg'],
  'shave_empty': ['--initial-shave=all', '--compress', '--labels=sets', 'tests/games/shave_single.pg'],
  'compress_original': ['--compress', '--labels=sets', 'tests/games/compress.pg'],
  'compress_compact': ['--compress', '--priority-mode=compact', 'tests/games/compress.pg'],
  'stats': ['--stats', '--format=leaves', 'tests/games/pm_cycle.pg'],
  'preprocess_stats': ['--stats', '--format=leaves', '--initial-shave=loops', '--compress', 'tests/games/shave_partial.pg'],
}
foreach algorithm : ['strahler', 'succinct']
  foreach name, arguments : pm_cases
    if name.startswith('partial')
      arguments = (algorithm == 'strahler' ? ['--k=1', '--t=1'] : ['--bits=0']) + arguments
    endif
    test('pg2pmdot_' + algorithm + '_' + name, run_single_test,
      args : [pg2pmdot, 'tests/golden/pm_' + algorithm + '_' + name + '.out',
        'tests/actual/pm_' + algorithm + '_' + name + '.out', 'EMPTY',
        '--algorithm=' + algorithm] + arguments,
      workdir : meson.project_source_root())
    if dot_program.found() and name not in ['leaves', 'partial_leaves', 'incremental_leaves', 'iter_stats', 'stats', 'preprocess_stats']
      test('pg2pmdot_' + algorithm + '_' + name + '_dot_syntax', dot_program,
        suite : 'graphviz',
        args : ['-Tdot', '-o', '/dev/null', files('tests/golden/pm_' + algorithm + '_' + name + '.out')])
    endif
  endforeach
  test('pg2pmdot_' + algorithm + '_stdin', run_single_test,
    args : [pg2pmdot, 'tests/golden/pm_' + algorithm + '_leaves.out',
      'tests/actual/pm_' + algorithm + '_stdin.out', '@tests/games/pm_cycle.pg',
      '--algorithm=' + algorithm, '--format=leaves', '-'],
    workdir : meson.project_source_root())
  test('pg2pmdot_' + algorithm + '_no_verify', run_single_test,
    args : [pg2pmdot, 'tests/golden/pm_' + algorithm + '_leaves.out',
      'tests/actual/pm_' + algorithm + '_no_verify.out', '@tests/games/pm_cycle.pg',
      '--algorithm=' + algorithm, '--format=leaves', '--no-verify'],
    workdir : meson.project_source_root())
  test('pg2pmdot_' + algorithm + '_oracle', find_program('tests/check-pm-oracle.sh'),
    args : [pg2pmdot, algorithm, 'tests/golden/oink_' + (algorithm == 'strahler' ? 'strpm' : 'sspm') + '_cycle.out'],
    workdir : meson.project_source_root())
endforeach

python3 = find_program('python3', required : false)
if python3.found()
  test('test_gen_decomp', python3,
    args : [files('tests/test_gen_decomp.py'), meson.current_build_dir()],
    suite : 'python',
    depends : [pg2adot, pg2pmdot, pms2dot])
  test('test_make_trees', python3,
    args : [files('tests/test_make_trees.py'), meson.current_build_dir()],
    suite : 'python',
    depends : [pg2adot, pg2pmdot, pms2dot])
  test('test_gen_keiren', python3,
    args : [files('tests/test_gen_keiren.py'), meson.current_build_dir()],
    suite : 'python', timeout : 120,
    depends : [pg2adot, pg2pmdot, pms2dot])
  # Exit 77 skips this live oracle unless OINK_BIN is set at test time.
  test('oink-oracle', python3,
    args : [files('scripts/compare_oink.py'), pg2pmdot,
      '--failures-dir', meson.current_build_dir() / 'oink-failures'],
    suite : ['oracle', 'python'], timeout : 600)
endif

trace_check = executable('trace_check', 'tests/trace_check.c',
  dependencies : [ad_tree_dep, pg_preprocess_dep], c_args : ['-UNDEBUG'])
trace_count_pg2adot = executable('trace_count_pg2adot', ['src/pg2adot.c', 'src/pg2adot_command.c'],
  dependencies : [ad_lift_dep, zielonka_dep, ad_tree_dot_dep, cli_version_dep, pg_preprocess_dep],
  c_args : ['-UNDEBUG', '-DAD_TRACE_COUNT_ONLY'])
add_test_setup('default', exclude_suites : ['slow'], is_default : true)
add_test_setup('all')
foreach mode : ['paper', 'limits', 'hosts', 'host-errors']
  test('test_trace_' + mode, python,
    args : [files('tests/test_trace.py'), pg2adot, trace_check, mode],
    workdir : meson.project_source_root(), suite : 'python', timeout : 120,
    depends : [pg2pmdot, trace_count_pg2adot])
endforeach
foreach backend : ['lifting', 'lifting-adaptive', 'lifting-sym', 'zielonka', 'zielonka-sym']
  foreach iteration : (backend.startswith('zielonka') ? [''] : ['', '+iter'])
    foreach pre : ['', '+pre']
      name = backend + pre + iteration
      foreach mode : ['games', 'examples', 'property']
        test('test_trace_' + mode + '_' + name, python,
          args : [files('tests/test_trace.py'), pg2adot, trace_check, mode, name],
          workdir : meson.project_source_root(), suite : 'python', timeout : 120,
          depends : trace_count_pg2adot)
      endforeach
    endforeach
  endforeach
endforeach
foreach game, bases : {
  'cabp_swp.ehoa.pg' : ['lifting-adaptive', 'zielonka-sym'],
  'thesis_f4.pg' : ['lifting'],
}
  foreach base : bases
    foreach suffix : (base.startswith('zielonka') ? ['', '+pre'] : ['', '+pre', '+iter', '+pre+iter'])
      backend = base + suffix
      test('test_trace_' + game + '_' + backend, python,
        args : [files('tests/test_trace.py'), pg2adot, trace_check, 'slow', game + '/' + backend],
        workdir : meson.project_source_root(), suite : ['slow', 'python'], timeout : 2400,
        depends : trace_count_pg2adot)
    endforeach
  endforeach
endforeach

test_ad_trace = executable('test_ad_trace', 'tests/test_ad_trace.c',
  dependencies : [ad_lift_dep, zielonka_dep], c_args : ['-UNDEBUG'])
test('test_ad_trace', test_ad_trace)
test_ad_trace_alloc = executable('test_ad_trace_alloc',
  ['tests/test_ad_trace_alloc.c', 'src/ad_trace.c', 'src/ad_tree_json.c', 'src/json_utils.c', 'src/ad_tree.c',
   'src/pg_set.c', 'src/pg_attractor.c', 'src/zielonka.c', 'src/ad_lift.c',
   'src/ad_sym.c', 'src/ad_lift_solve.c'],
  dependencies : [ad_lift_tree_dep, stree_dep, pg_game_dep],
  c_args : ['-UNDEBUG', '-D_POSIX_C_SOURCE=200809L', '-include', meson.project_source_root() / 'tests/trace_allocator.h'])
test('test_ad_trace_alloc', test_ad_trace_alloc, timeout : 120)

foreach mode : ['tools', 'limits', 'goldens']
  test('test_json_' + mode, python,
    args : [files('tests/test_json.py'), meson.current_build_dir(), mode],
    workdir : meson.project_source_root(), suite : 'python', timeout : 120,
    depends : [genstree, pms2dot, pg2adot, pg2pmdot])
endforeach
foreach backend : ['lifting', 'lifting-adaptive', 'lifting-sym', 'zielonka', 'zielonka-sym', 'pm-strahler', 'pm-succinct']
  foreach iteration : (backend.startswith('zielonka') ? [''] : ['', '+iter'])
    foreach pre : ['', '+pre']
      name = backend + pre + iteration
      foreach mode : ['games', 'examples']
        test('test_json_' + mode + '_' + name, python,
          args : [files('tests/test_json.py'), meson.current_build_dir(), mode, name],
          workdir : meson.project_source_root(), suite : 'python', timeout : 120,
          depends : [pg2adot, pg2pmdot, pms2dot])
      endforeach
    endforeach
  endforeach
endforeach
foreach game, bases : {
  'cabp_swp.ehoa.pg' : ['lifting-adaptive', 'zielonka-sym', 'pm-strahler', 'pm-succinct'],
  'thesis_f4.pg' : ['lifting'],
  'FairSchedulern=2_compact.gm' : ['pm-strahler', 'pm-succinct'],
}
  foreach base : bases
    foreach suffix : (base.startswith('zielonka') ? ['', '+pre'] : ['', '+pre', '+iter', '+pre+iter'])
      name = base + suffix
      test('test_json_' + game + '_' + name, python,
        args : [files('tests/test_json.py'), meson.current_build_dir(), 'slow', name, game],
        workdir : meson.project_source_root(), suite : ['slow', 'python'], timeout : 2400,
        depends : [pg2adot, pg2pmdot, pms2dot])
    endforeach
  endforeach
endforeach

test_json_alloc = executable('test_json_alloc',
  ['tests/test_json_alloc.c', 'src/json_utils.c', 'src/ad_tree_json.c',
   'src/ordered_tree.c', 'src/pm_tree_dot.c', 'src/pg_set.c'],
  dependencies : [zielonka_dep, pm_tree_dot_dep],
  c_args : ['-UNDEBUG', '-D_POSIX_C_SOURCE=200809L', '-include', meson.project_source_root() / 'tests/trace_allocator.h'])
test('test_json_alloc', test_json_alloc, timeout : 120)

# Large result-event comparisons reuse the counting observer and production
# JSON writer; the corpus cases exercise complete trace streams.
foreach backend : ['lifting', 'lifting-adaptive', 'lifting-sym', 'zielonka', 'zielonka-sym']
  foreach iteration : (backend.startswith('zielonka') ? [''] : ['', '+iter'])
    foreach pre : ['', '+pre']
      name = backend + pre + iteration
      foreach game : ['Button.tlsf.ehoa.pg', 'FairSchedulern=2_compact.gm', 'cabp_swp.ehoa.pg', 'thesis_f4.pg']
        test('test_json_trace_' + game + '_' + name, python,
          args : [files('tests/test_json.py'), meson.current_build_dir(), 'trace', name, game],
          workdir : meson.project_source_root(), suite : ['slow', 'python'], timeout : 2400,
          depends : [pg2adot, trace_count_pg2adot])
      endforeach
    endforeach
  endforeach
endforeach

# The CLIs retain static linkage. The shared ABI links the same implementations.
python_option = get_option('python')
if python_option.enabled()
  python_module = import('python').find_installation(pure : true)
  python_module.install_sources(
    'python/strahler/__init__.py', 'python/strahler/_native.py',
    'python/strahler/_replay.py', 'python/strahler/_widget_state.py',
    'python/strahler/widgets.py', subdir : 'strahler',
    install_tag : 'python-runtime')
endif
strahler_sources = files('src/strahler.c', 'src/json_input.c',
  'src/pg2adot_command.c', 'src/pg2pmdot_command.c',
  'src/genstree_command.c', 'src/pms2dot_command.c')
strahler_dependencies = [ad_lift_dep, zielonka_dep, ad_tree_dot_dep,
  pm_tree_dot_dep, pg_preprocess_dep, cli_version_dep]
libstrahler = shared_library('strahler', strahler_sources,
  dependencies : strahler_dependencies,
  # Meson requires a numeric library filename version; the API reports the full version.
  soversion : '0',
  gnu_symbol_visibility : 'hidden',
  # Compiler runtimes such as libgcov can otherwise add public symbols.
  link_args : meson.get_compiler('c').get_supported_link_arguments('-Wl,--exclude-libs,ALL'),
  install : true,
  install_tag : 'python-runtime')
install_headers('src/strahler.h')

api_test = executable('test_strahler', 'tests/test_strahler.c',
  include_directories : source_includes, link_with : libstrahler,
  c_args : ['-UNDEBUG'])
test('test_strahler', api_test)
# Inject into every implementation reached by the ABI, as in trace/json tests.
api_alloc_sources = files('src/cli_version.c', 'src/cli_commands.c',
  'src/stree.c', 'src/ordered_tree.c', 'src/dot_utils.c', 'src/json_utils.c',
  'src/pg_game.c', 'src/pg_set.c', 'src/pg_attractor.c', 'src/pg_preprocess.c',
  'src/ad_tree.c', 'src/ad_tree_json.c', 'src/ad_trace.c', 'src/zielonka.c',
  'src/ad_lift_tree.c', 'src/ad_lift.c', 'src/ad_lift_solve.c', 'src/ad_sym.c',
  'src/ad_tree_dot.c', 'src/pg_dot.c', 'src/pm_lift.c', 'src/pm_solve.c',
  'src/strpm.c', 'src/sspm.c', 'src/pm_tree_dot.c')
api_alloc_test = executable('test_strahler_alloc',
  ['tests/test_strahler_alloc.c', strahler_sources, api_alloc_sources, version_header],
  include_directories : source_includes,
  c_args : ['-UNDEBUG', '-D_POSIX_C_SOURCE=200809L', '-include',
    meson.project_source_root() / 'tests/trace_allocator.h'])
test('test_strahler_alloc', api_alloc_test, timeout : 120)

if python_option.enabled()
  python_test_env = environment()
  python_test_env.prepend('PYTHONPATH', meson.project_source_root() / 'python')
  python_test_env.set('STRAHLER_LIB', meson.current_build_dir())
  python_test_env.set('STRAHLER_BUILD_DIR', meson.current_build_dir())
  test('test_widgets', python_module,
    args : ['-m', 'pytest', files('tests/test_widget_state.py'), files('tests/test_widgets.py'),
      files('tests/test_widgets_js.py'), '-q'],
    env : python_test_env, suite : 'python', timeout : 120, depends : libstrahler)
  test('test_widgets_browser', python_module,
    args : ['-m', 'pytest', files('tests/test_widgets_browser.py'), '-q', '-s'],
    env : python_test_env, suite : 'python', timeout : 180, depends : libstrahler)
  test('test_notebooks', python_module,
    args : ['-m', 'pytest', files('tests/test_notebooks.py'), '-q', '-s'],
    env : python_test_env, suite : 'python', timeout : 480, depends : libstrahler)
  foreach slow : [false, true]
    test(slow ? 'test_python_slow' : 'test_python', python_module,
      args : ['-m', 'pytest', files('tests/test_python.py'), files('tests/test_lexicographic.py'), '-q', '-rs',
        '-m', slow ? 'slow' : 'not slow', '--durations=12'],
      env : python_test_env, suite : slow ? ['python', 'slow'] : ['python'],
      timeout : slow ? 7200 : 600,
      depends : [libstrahler, pg2adot, pg2pmdot, genstree, pms2dot])
  endforeach
endif
