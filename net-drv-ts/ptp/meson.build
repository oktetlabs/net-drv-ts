# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.

tests = [
    'adj_frequency',
    'adj_setoffset',
    'clock_caps',
    'get_time',
    'set_time',
    'ptp4l',
    'sfptpd',
    'sys_offset',
    'sys_offset_extended',
    'sys_offset_precise',
]

foreach test : tests
    test_exe = test
    test_c = test + '.c'
    package_tests_c += [ test_c ]
    executable(test_exe, test_c, install: true, install_dir: package_dir,
               dependencies: test_deps)
endforeach

tests_info_xml = custom_target(package_dir.underscorify() + 'tests-info-xml',
                               install: true, install_dir: package_dir,
                               input: package_tests_c,
                               output: 'tests-info.xml', capture: true,
                               command: [ te_tests_info_sh,
                               meson.current_source_dir() ])

install_data([ 'package.dox',
               'package.xml',
               'sfptpd_ptp_master.cfg',
               'sfptpd_ptp_slave.cfg' ],
             install_dir: package_dir)
