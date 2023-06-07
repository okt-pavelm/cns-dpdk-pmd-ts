/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup filters-flow_rule_reflect Reflect Rx traffic back to the physical port
 * @ingroup filters
 * @{
 *
 * @objective Make sure that RTE flow API action engine can reflect Rx traffic
 *
 * @param flow_rule_pattern Flow rule pattern
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "filters/flow_rule_reflect"

#include "dpdk_pmd_test.h"
#include "te_ethernet.h"

int
main(int argc, char *argv[])
{
    asn_value                 *flow_pattern_ndn;

    rcf_rpc_server            *iut_rpcs = NULL;
    tapi_env_host             *tst_host;
    const struct if_nameindex *iut_port = NULL;
    const struct if_nameindex *tst_if = NULL;

    rpc_rte_flow_attr_p         flow_attributes = RPC_NULL;
    rpc_rte_flow_item_p         flow_pattern = RPC_NULL;
    rpc_rte_flow_action_p       flow_actions = RPC_NULL;
    asn_value                 **tmpls_mismatch = NULL;
    unsigned int                nb_tmpls_mismatch;
    asn_value                  *flow_actions_ndn;
    rpc_rte_flow_p              flow = RPC_NULL;
    struct test_transceiver    *tr_tst = NULL;
    struct test_transceiver    *tr_iut = NULL;
    uint32_t                    match_fields;
    struct test_ethdev_config   test_ethdev;
    asn_value                   *tmpl_match;
    tarpc_rte_flow_error        flow_error;
    unsigned int                i;
    bool                        drop = TRUE;

    const uint8_t             zero_mac[ETHER_ADDR_LEN] = {0};
    struct sockaddr           zero_ip = {0};
    struct test_pkt_addresses zero_addrs = {
                                  .dst_mac = zero_mac,
                                  .src_mac = zero_mac,
                                  .dst_ip = &zero_ip,
                                  .src_ip = &zero_ip };

    TEST_START;

    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_pattern_ndn);
    TEST_GET_HOST(tst_host);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_STEP("Prepare state TEST_ETHDEV_STARTED");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port, &test_ethdev,
                                         TEST_ETHDEV_STARTED));

    TEST_STEP("Deploy TST transceiver");
    tr_tst = test_transceiver_net_init(tst_host->ta, tst_if->if_name);

    TEST_STEP("Deploy IUT transceiver");
    tr_iut = test_transceiver_dpdk_init(iut_rpcs, iut_port->if_index,
                                        test_ethdev.mp);

    TEST_STEP("Handle the flow rule pattern and matching traffic template");
    CHECK_RC(tapi_ndn_subst_env(flow_pattern_ndn, &test_params, &env));
    test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs, flow_pattern_ndn,
                                                  &flow_pattern, &tmpl_match,
                                                  &match_fields);
    /*
     * It's lower-level matching, and it's promiscuous mode, so one
     * need never care about addresses in the packet. Fill in zeros.
     */
    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_match, &zero_addrs, &zero_addrs));

    TEST_STEP("Prepare, validate and create the flow rule");
    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, FALSE, FALSE, TRUE,
                            &flow_attributes);
    CHECK_NOT_NULL(flow_actions_ndn = asn_init_value(ndn_rte_flow_actions));

    tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_REPRESENTED_PORT,
                                      iut_port->if_index, flow_actions_ndn,
                                      -1);
    if (drop)
    {
        TEST_STEP("Make flow rule action \"drop\"");
        tapi_rte_flow_add_ndn_action_drop(flow_actions_ndn, -1);
    }
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_actions_ndn, NULL, NULL,
                                    &flow_actions);

    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  flow_attributes, flow_pattern,
                                                  flow_actions);

    TEST_STEP("Enable promiscuous mode on IUT");
    test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                    TEST_OP_REQUIRED);

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Transmit the matching packet and watch it come back");
    test_transciever_simple_exchange_commit(tmpl_match,
                                            tr_tst, 1, 0, tr_tst, /*drop ? 0 : */1, 0, NULL,
                                            "Matching packet wasn't reflected");

    TEST_STEP("Retransmit the matching packet and ensure it's unseen on IUT");
    test_transciever_simple_exchange_commit(tmpl_match,
                                            tr_tst, 1, 0, tr_iut, 0, 0, NULL,
                                            "Matching packet is seen on IUT");

    if (drop)
        TEST_SUCCESS;

    TEST_STEP("Prepare non-matching templates");
    CHECK_RC(test_mk_tmpls_with_change_one_field(tmpl_match, match_fields,
                                                 &zero_addrs, &zero_addrs,
                                                 &tmpls_mismatch,
                                                 &nb_tmpls_mismatch));

    TEST_STEP("Transmit non-matching packets and ensure they're seen on IUT");
    for (i = 0; i < nb_tmpls_mismatch; ++i)
    {
        test_transciever_simple_exchange_commit(tmpls_mismatch[i], tr_tst, 1, 0,
                                                tr_iut, 1, 0, NULL, NULL);
    }

    TEST_SUCCESS;

cleanup:
    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &flow_error);

    rpc_rte_free_flow_rule(iut_rpcs, flow_attributes, flow_pattern,
                           flow_actions);

    TEST_END;
}
/** @} */
