/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2023 Advanced Micro Devices, Inc. */
/*
 * DPDK PMD Test Suite
 * Reliability in normal use
 */

/** @defgroup usecases-io_forward_and_drop IO forward + flow rule check
 * @{
 *
 * @objective Check that IO-forwarded packets do not erroneously bypass the flow engine
 *
 * @param rx_tmpl               Traffic template to be sent from Tester and received on IUT
 * @param tx_tmpl               Traffic template to be sent from IUT and received on Tester
 * @param flow_rule_pattern     Flow rule pattern
 *
 * @author Pavel Martynov <Pavel.Martynov@arknetworks.am>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "usecases/io_forward_and_drop"

#define NB_PKTS       1

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    asn_value                 *flow_rule_pattern;

    rcf_rpc_server            *iut_rpcs = NULL;
    tapi_env_host             *tst_host;
    const struct if_nameindex *iut_port = NULL;
    const struct if_nameindex *tst_if = NULL;
    rpc_rte_flow_item_p        flow_pattern = RPC_NULL;
    rpc_rte_flow_attr_p        attr = RPC_NULL;
    rpc_rte_flow_action_p      actions = RPC_NULL;
    rpc_rte_flow_p             flow = RPC_NULL;
    asn_value                 *ndn_actions;
    struct test_ethdev_config  test_ethdev;
    rpc_rte_mbuf_p             mbufs[BURST_SIZE] = {};
    asn_value                 *rx_ptrn = NULL;
    asn_value                 *tx_ptrn = NULL;
    unsigned int               received = 0;
    tarpc_rte_flow_error       error;
    unsigned int               nb_matched = 0;
    rpc_rte_flow_action_p      count_action = RPC_NULL;
    tarpc_rte_flow_query_data  count_query;
    uint32_t                   match_pkt_size;
    uint16_t                   sent;

    uint64_t                   metadata;
    csap_handle_t              rx_csap = CSAP_INVALID_HANDLE;
    asn_value                 *rx_tmpl = NULL;
    asn_value                 *tx_tmpl = NULL;

    TEST_START;

    TEST_GET_HOST(tst_host);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_rule_pattern);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(rx_tmpl);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tx_tmpl);

    TEST_STEP("Prepare state TEST_ETHDEV_INITIALIZED");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port, &test_ethdev,
                                         TEST_ETHDEV_INITIALIZED));

    /* Mishandling of metadata by the PMD
       can make egress packets bypass the flow engine */
    metadata = (1ULL << TARPC_RTE_ETH_RX_METADATA_USER_MARK_BIT);

    CHECK_RC(rpc_rte_eth_rx_metadata_negotiate(iut_rpcs, test_ethdev.port_id,
                                               &metadata));

    TEST_STEP("Prepare state TEST_ETHDEV_STARTED");
    test_ethdev.min_tx_desc = test_ethdev.dev_info.tx_desc_lim.nb_max;
    CHECK_RC(test_prepare_ethdev(&test_ethdev, TEST_ETHDEV_STARTED));

    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, FALSE, FALSE, TRUE, &attr);

    TEST_STEP("Prepare @p rx_tmpl and @p tx_tmpl for test");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params,
                                                  TEST_IUT_PORT_MAC_NAME));

    CHECK_RC(tapi_ndn_subst_env(rx_tmpl, &test_params, &env));
    CHECK_RC(tapi_ndn_subst_env(tx_tmpl, &test_params, &env));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&rx_tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));
    CHECK_RC(tapi_tad_tmpl_ptrn_set_payload_plain(&tx_tmpl, FALSE, NULL,
                                                  DPMD_TS_PAYLOAD_LEN_DEF));

    TEST_STEP("Enable promiscuous mode on IUT");
    test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                    TEST_OP_REQUIRED);

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Send packet from TST to IUT and recieve it");
    CHECK_RC(tapi_eth_gen_traffic_sniff_pattern(tst_host->ta, 0,
                                                tst_if->if_name,
                                                rx_tmpl, NULL,
                                                &rx_ptrn));
    if (rx_ptrn == NULL)
        TEST_VERDICT("Failed to sniff pattern");

    received = test_rx_burst_with_retries(iut_rpcs, iut_port->if_index, 0,
                                          mbufs, TE_ARRAY_LEN(mbufs), NB_PKTS);

    if (received > 0)
    {
        TEST_STEP("Check received packets");
        rpc_rte_mbuf_match_pattern(iut_rpcs, rx_ptrn, mbufs, received,
                                   NULL, &nb_matched);
    }

    TEST_STEP("Check the number of received and matching packets");
    CHECK_PACKETS_NUM(received, NB_PKTS);
    CHECK_MATCHED_PACKETS_NUM(nb_matched, NB_PKTS);

    TEST_STEP("Create \"drop\" and \"count\" rules");
    CHECK_NOT_NULL(ndn_actions = asn_init_value(ndn_rte_flow_actions));
    tapi_rte_flow_add_ndn_action_drop(ndn_actions, -1);
    test_add_and_mk_rte_flow_action_count(0, 0, iut_rpcs,
                                          ndn_actions, &count_action);

    TEST_SUBSTEP("Set action");
    rpc_rte_mk_flow_rule_components(iut_rpcs, ndn_actions, NULL, NULL,
                                    &actions);

    TEST_SUBSTEP("Set pattern");
    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));
    rpc_rte_mk_flow_rule_components(iut_rpcs, flow_rule_pattern, NULL,
                                    &flow_pattern, NULL);

    TEST_STEP("Validate and create the flow rule");
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, flow_pattern, actions);

    TEST_STEP("Send packet back from IUT to TST and ensure that "
              "the packet is unseen on TST");
    CHECK_NOT_NULL(tx_ptrn = tapi_tad_mk_pattern_from_template(tx_tmpl));
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                tx_tmpl, &rx_csap));

    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, rx_ptrn,
                                   TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS |
                                   RCF_TRRECV_MISMATCH));

    sent = test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index, 0, mbufs,
                                     NB_PKTS);

    CHECK_PACKETS_NUM(sent, NB_PKTS);

    TEST_STEP("Wait packet on TST side, hope not to find it");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, NB_PKTS, 0));

    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL, &received));

    CHECK_PACKETS_NUM(received, 0);

    TEST_STEP("Make sure the hit count reads 1");

    match_pkt_size = test_get_template_packet_length(iut_rpcs, tx_tmpl,
                                                     test_ethdev.mp);
    memset(&count_query, 0, sizeof(count_query));
    /* FIXME: get sleep time from configurator */
    VSLEEP(1, "Wait for counters to update");
    rpc_rte_flow_query(iut_rpcs, iut_port->if_index, flow,
                       count_action, &count_query, &error);
    test_check_flow_query_data(&count_query, TRUE, NB_PKTS, TRUE,
                               match_pkt_size);

    TEST_SUCCESS;

cleanup:
    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, &error);

    rpc_rte_free_flow_rule(iut_rpcs, attr, flow_pattern, actions);
    rpc_rte_free_flow_rule(iut_rpcs, RPC_NULL, RPC_NULL, count_action);

    TEST_END;
}
/** @} */
