# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved.

tests = [
    'all_multicast_mode',
    'deferred_start_rx_queue',
    'deferred_start_tx_queue',
    'dev_conf_rss_adv',
    'dev_info_persistence',
    'dev_reconfigure',
    'flow_ctrl_get',
    'fw_version',
    'get_mtu',
    'io_forward_and_drop',
    'link_up_down',
    'multi_process',
    'promiscuous_mode',
    'rss',
    'rss_hash_conf_get',
    'rss_hash_info',
    'rss_reta_query',
    'rss_reta_update',
    'runtime_rx_queue_setup_with_flow',
    'runtime_rx_queue_setup_with_rss',
    'runtime_tx_queue_setup',
    'rx_burst_simple',
    'rx_desc_nb',
    'rx_descriptor_status',
    'rx_intr',
    'rx_offload_checksum',
    'rx_ptype',
    'rx_scatter',
    'rx_stats',
    'set_default_mac_addr',
    'set_mc_addr_list',
    'set_mtu',
    'test_detach',
    'tunnel_udp_port_config',
    'tx_burst_simple',
    'tx_descriptor_status',
    'tx_desc_nb',
    'tx_multi_burst',
    'tx_stats',
    'update_rss_hash_conf',
    'vlan_strip',
    'xstats_by_id',
    'xstats_dev_state',
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
