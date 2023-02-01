# SPDX-License-Identifier: Apache-2.0
# Copyright (C) 2022 OKTET Labs Ltd. All rights reserved.

rss_tests_lib_dir = include_directories('.')

rss_tests_lib_sources = [
    'common_rss.c',
]

rss_tests_lib = static_library('rss_tests', rss_tests_lib_sources,
                               include_directories: [lib_dir,
                                                     rss_tests_lib_dir],
                               dependencies: dep_tirpc)

rss_test_deps = test_deps
rss_test_deps += declare_dependency(include_directories: rss_tests_lib_dir,
                                    link_with: rss_tests_lib)

tests = [
    'af_xdp',
    'af_xdp_rx_rule',
    'change_channels',
    'epilogue',
    'hash_key_get',
    'hash_key_set',
    'indir_table_set',
    'prologue',
    'rx_rule_tcp_udp',
    'rx_rules_full_part',
]

foreach test : tests
    test_exe = test
    test_c = test + '.c'
    package_tests_c += [ test_c ]
    executable(test_exe, test_c, install: true, install_dir: package_dir,
               dependencies: rss_test_deps)
endforeach

tests_info_xml = custom_target(package_dir.underscorify() + 'tests-info-xml',
                               install: true, install_dir: package_dir,
                               input: package_tests_c,
                               output: 'tests-info.xml', capture: true,
                               command: [ te_tests_info_sh,
                               meson.current_source_dir() ])

install_data([ 'package.xml', 'package.dox' ],
             install_dir: package_dir)
