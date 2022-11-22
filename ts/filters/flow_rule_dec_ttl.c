/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2023 AMD, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Traffic filtering
 */

/** @defgroup filters-flow_rule_dec_ttl Check for decreasing TTL value for DEC_TTL action
 * @ingroup filters
 * @{
 *
 * @objective Check that flow API DEC_TTL action is executed correctly
 *
 * @param flow_rule_pattern     Flow rule pattern
 * @param transfer              Set transfer attribute to flow rule if @c TRUE
 * @param ttl                   Basic TTL value
 *
 * @author Pavel Martynov <Pavel.Martynov@arknetworks.am>
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "filters/flow_rule_dec_ttl"

#include "dpdk_pmd_test.h"

static asn_value *
transform_ptrn(const asn_value *ptrn_tx, void *ptrn_data)
{
    asn_value *result;
    asn_value *pdus;
    asn_value *choice;
    int ttl = (*(int *)ptrn_data - 1);

    CHECK_NOT_NULL(result = asn_copy_value(ptrn_tx));
    CHECK_NOT_NULL(pdus = asn_find_descendant(result, NULL, "0.pdus"));
    CHECK_RC(asn_write_value_field(pdus, &ttl, sizeof(ttl),
                                   "0.#ip4.time-to-live.#plain"));

    CHECK_NOT_NULL(choice = asn_find_descendant(result, NULL, "0"));
    CHECK_RC(tapi_ndn_pkt_demand_correct_ip_cksum(choice, TAPI_NDN_INNER_L3));

    return result;
}

int
main(int argc, char *argv[])
{
    asn_value                              *flow_rule_pattern;

    asn_value                              *tmpl_match;
    rcf_rpc_server                         *iut_rpcs = NULL;
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *iut_port = NULL;
    const struct if_nameindex              *tst_if = NULL;
    struct test_ethdev_config               ethdev_config;
    struct test_transceiver                *trsc_net = NULL;
    struct test_transceiver                *trsc_dpdk = NULL;
    rpc_rte_flow_attr_p                     attr = RPC_NULL;
    rpc_rte_flow_item_p                     pattern = RPC_NULL;
    asn_value                              *ndn_actions;
    rpc_rte_flow_action_p                   actions = RPC_NULL;
    rpc_rte_flow_p                          flow = RPC_NULL;
    struct tarpc_ether_addr                 iut_lladdr;
    const struct sockaddr                  *tst_lladdr = NULL;
    struct test_pkt_addresses               addrs;
    struct test_pkt_addresses               ifrm_addrs;
    struct sockaddr                        *ifrm_src_addr = NULL;
    struct sockaddr                        *ifrm_dst_addr = NULL;
    const struct sockaddr                  *iut_addr = NULL;
    const struct sockaddr                  *tst_addr = NULL;
    const struct sockaddr                  *iut_alien_mac = NULL;
    const struct sockaddr                  *tst_alien_mac = NULL;
    struct test_transceiver_exchange       *exchange = NULL;
    te_bool                                 transfer;
    int                                     ttl;

    TEST_START;

    TEST_GET_NDN_RTE_FLOW_PATTERN(flow_rule_pattern);
    TEST_GET_HOST(tst_host);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_LINK_ADDR(tst_alien_mac);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_ADDR_NO_PORT(iut_addr);
    TEST_GET_ADDR_NO_PORT(tst_addr);
    TEST_GET_BOOL_PARAM(transfer);
    TEST_GET_INT_PARAM(ttl);

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    CHECK_RC(test_default_prepare_ethdev(&env, iut_rpcs, iut_port,
                                         &ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Initiate DPDK and net transceivers");
    trsc_net = test_transceiver_net_init(tst_host->ta, tst_if->if_name);
    trsc_dpdk = test_transceiver_dpdk_init(iut_rpcs, iut_port->if_index,
                                           ethdev_config.mp);

    tapi_rte_flow_make_attr(iut_rpcs, 0, 0, FALSE, FALSE, transfer, &attr);

    TEST_STEP("Make flow rule pattern and prepare matching template");
    CHECK_RC(tapi_ndn_subst_env(flow_rule_pattern, &test_params, &env));
    test_mk_pattern_and_tmpl_by_flow_rule_pattern(iut_rpcs, flow_rule_pattern,
                                                  &pattern, &tmpl_match,
                                                  NULL);

    TEST_STEP("Add DEC_TTL flow rule action");
    CHECK_NOT_NULL(ndn_actions = asn_init_value(ndn_rte_flow_actions));
    tapi_rte_flow_add_ndn_action_dec_ttl(ndn_actions, -1);

    TEST_STEP("Add flow rule fate action, \"PORT_REPRESENTOR\"");
    tapi_rte_flow_add_ndn_action_port(NDN_FLOW_ACTION_TYPE_PORT_REPRESENTOR,
                                      iut_port->if_index, ndn_actions,
                                      -1);

    rpc_rte_mk_flow_rule_components(iut_rpcs, ndn_actions, NULL, NULL,
                                    &actions);

    TEST_STEP("Validate and create the flow rule");
    flow = tapi_rte_flow_validate_and_create_rule(iut_rpcs, iut_port->if_index,
                                                  attr, pattern, actions);

    TEST_STEP("Fill required fields in match template");
    rpc_rte_eth_macaddr_get(iut_rpcs, iut_port->if_index, &iut_lladdr);
    CHECK_NOT_NULL(ifrm_src_addr = test_rand_ip_addr(AF_INET));
    CHECK_NOT_NULL(ifrm_dst_addr = test_rand_ip_addr(AF_INET));
    test_set_pkt_addresses(&ifrm_addrs, (uint8_t *)tst_alien_mac->sa_data,
                           iut_lladdr.addr_bytes, ifrm_src_addr, ifrm_dst_addr);
    test_set_pkt_addresses(&addrs, (uint8_t *)tst_lladdr->sa_data,
                           (uint8_t *)iut_alien_mac->sa_data,
                           tst_addr, iut_addr);

    CHECK_RC(test_fill_in_tmpl_req_fields(tmpl_match, &addrs, &ifrm_addrs));

    /* Set time to live value */
    CHECK_RC(asn_write_int32(tmpl_match, ttl,
                             "pdus.0.#ip4.time-to-live.#plain"));

    test_rte_eth_promiscuous_enable(iut_rpcs, iut_port->if_index,
                                    TEST_OP_REQUIRED);

    TEST_STEP("Ensure that interface is UP on Tester side");
    CHECK_RC(tapi_cfg_base_if_await_link_up(tst_host->ta, tst_if->if_name,
                                            TEST_LINK_UP_MAX_CHECKS,
                                            TEST_LINK_UP_WAIT_MS, 0));

    TEST_STEP("Specify the transformations template and pattern");
    exchange = test_transceiver_exchange_init(tmpl_match, NULL, NULL,
                                              transform_ptrn, (void *)&ttl);

    TEST_STEP("Commit transfer of a packet from one transceiver to another "
              "and match the received packet");
    test_transceiver_exchange_commit(exchange, trsc_net, 1, 0, trsc_dpdk,
                                     ttl <= 1 ? 0 : 1, 0);

    TEST_SUCCESS;

cleanup:

    if (flow != RPC_NULL)
        rpc_rte_flow_destroy(iut_rpcs, iut_port->if_index, flow, NULL);

    rpc_rte_free_flow_rule(iut_rpcs, attr, pattern, actions);
    test_transceiver_free(trsc_net);
    test_transceiver_free(trsc_dpdk);
    test_transceiver_exchange_free(exchange);

    TEST_END;
}
