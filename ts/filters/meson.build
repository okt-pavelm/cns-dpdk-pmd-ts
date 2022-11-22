# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.

tests = [
    'flow_rule_counters',
    'flow_rule_dec_ttl',
    'flow_rule_decap_on_ingress',
    'flow_rule_drop',
    'flow_rule_encap_on_egress',
    'flow_rule_in2q',
    'flow_rule_mark_and_flag',
    'flow_rule_multi_count',
    'flow_rule_reflect',
    'flow_rule_rss',
    'flow_rule_vlan',
    'flow_rule_vlan_push',
    'flow_tunnel',
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

install_data([ 'package.dox', 'package.xml' ],
             install_dir: package_dir)
