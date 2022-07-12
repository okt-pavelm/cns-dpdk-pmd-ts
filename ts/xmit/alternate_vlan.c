/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK PMD Test Suite
 * Transmit functionality
 */

/** @defgroup xmit-alternate_vlan Prove that VLAN ID alternation is done correctly
 * @ingroup xmit
 * @{
 *
 * @objective Make sure that VLAN ID alternation performed by means of sending
 *            couples of mbufs with different VLAN IDs is carried out properly
 *
 * @param template              Traffic template
 * @param payload_len           Payload length, bytes
 * @param vlan_id_first         VLAN ID (1st packet) or @c -1 (disable)
 * @param vlan_id_second        VLAN ID (2nd packet) or @c -1 (disable)
 *
 * @type conformance
 *
 * @author Ivan Malov <Ivan.Malov@oktetlabs.ru>
 *
 * The test is to verify that PMD is able to fulfill VLAN offload properly
 * under such circumstances like sending couples of packets with different
 * values of 'vlan_tci' field (or without such) in the corresponding mbufs
 *
 * @par Scenario:
 */

#define TE_TEST_NAME                   "xmit/alternate_vlan"

#include "dpdk_pmd_test.h"
#include "tapi_cfg_base.h"

#include "tapi_ndn.h"
#include "tapi_tad.h"

#define TEST_TX_PKTS_NUM    2

int
main(int argc, char *argv[])
{
    rcf_rpc_server                     *iut_rpcs = NULL;
    tapi_env_host                      *tst_host = NULL;
    const struct if_nameindex          *iut_port = NULL;
    const struct if_nameindex          *tst_if = NULL;

    asn_value                          *template;
    unsigned int                        payload_len;
    int                                 vlan_id_first;
    int                                 vlan_id_second;

    struct test_ethdev_config           ethdev_config;
    send_transform                      cond;
    rpc_rte_mbuf_p                     *mbufs_first;
    unsigned int                        n_mbufs_first;
    rpc_rte_mbuf_p                     *mbufs_second;
    unsigned int                        n_mbufs_second;
    rpc_rte_mbuf_p                      burst[TEST_TX_PKTS_NUM] = {};
    asn_value                          *pattern;
    asn_value                          *pattern_second;
    csap_handle_t                       rx_csap = CSAP_INVALID_HANDLE;
    unsigned int                        packets_received;

    TEST_START;

    TEST_GET_PCO(iut_rpcs);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(iut_port);
    TEST_GET_IF(tst_if);

    TEST_GET_NDN_TRAFFIC_TEMPLATE(template);
    TEST_GET_UINT_PARAM(payload_len);
    TEST_GET_VLAN_ID_PARAM(vlan_id_first);
    TEST_GET_VLAN_ID_PARAM(vlan_id_second);

    TEST_STEP("Prepare @c TEST_ETHDEV_STARTED state");
    (void)test_prepare_config_def_mk(&env, iut_rpcs, iut_port, &ethdev_config);
    ethdev_config.min_tx_desc = TEST_TX_PKTS_NUM;
    CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));

    TEST_STEP("Verify the configuration requested");
    if (((vlan_id_first >= 0) || (vlan_id_second >= 0)) &&
        ((ethdev_config.dev_info.tx_offload_capa &
        (1U << TARPC_RTE_DEV_TX_OFFLOAD_VLAN_INSERT_BIT)) == 0))
        TEST_SKIP("TX VLAN insertion is not available");

    TEST_STEP("Obtain the source Ethernet address");
    CHECK_RC(tapi_rpc_add_mac_as_octstring2kvpair(iut_rpcs, iut_port->if_index,
                                                  &test_params, "iut_mac"));

    TEST_STEP("Adjust the traffic template");
    CHECK_RC(tapi_ndn_subst_env(template, &test_params, &env));

    asn_write_value_field(template, &payload_len, sizeof(payload_len),
                          "payload.#length");

    TEST_STEP("Adjust so-called 'send transformations' for the 1st packet");
    memset(&cond, 0, sizeof(cond));

    if (vlan_id_first >= 0)
    {
        cond.hw_flags |= SEND_COND_HW_OFFL_VLAN;
        cond.vlan_tci = (uint16_t)vlan_id_first;
    }

    TEST_STEP("Generate mbuf(s) and the corresponding pattern for the 1st packet");
    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, template, ethdev_config.mp,
                                     &cond, &mbufs_first, &n_mbufs_first,
                                     &pattern);

    TEST_STEP("Adjust so-called 'send transformations' for the 2nd packet");
    memset(&cond, 0, sizeof(cond));

    if (vlan_id_second >= 0)
    {
        cond.hw_flags |= SEND_COND_HW_OFFL_VLAN;
        cond.vlan_tci = (uint16_t)vlan_id_second;
    }

    TEST_STEP("Generate mbuf(s) and the corresponding pattern for the 2nd packet");
    tapi_rte_mk_mbuf_mk_ptrn_by_tmpl(iut_rpcs, template, ethdev_config.mp,
                                     &cond, &mbufs_second, &n_mbufs_second,
                                     &pattern_second);

    TEST_STEP("Construct a burst array from the two packets");
    burst[0] = mbufs_first[0];
    burst[1] = mbufs_second[0];

    TEST_STEP("Concatenate the 1st and 2nd patterns");
    CHECK_RC(tapi_tad_concat_patterns(pattern, pattern_second));

    TEST_STEP("Create an RX CSAP on the TST host according to the template");
    CHECK_RC(tapi_eth_based_csap_create_by_tmpl(tst_host->ta, 0,
                                                tst_if->if_name,
                                                TAD_ETH_RECV_DEF,
                                                template, &rx_csap));

    TEST_STEP("Start to capture traffic with the pattern prepared");
    CHECK_RC(tapi_tad_trrecv_start(tst_host->ta, 0, rx_csap, pattern,
                                   TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS | RCF_TRRECV_SEQ_MATCH |
                                   RCF_TRRECV_MISMATCH));

    TEST_STEP("Validate and send the burst");
    if (test_tx_prepare_and_burst(iut_rpcs, iut_port->if_index, 0,
                                  burst, TEST_TX_PKTS_NUM) != TEST_TX_PKTS_NUM)
        TEST_VERDICT("Cannot send the packets");

    TEST_STEP("Receive packets on peer");
    CHECK_RC(test_rx_await_pkts(tst_host->ta, rx_csap, 2, 0));
    CHECK_RC(tapi_tad_trrecv_stop(tst_host->ta, 0, rx_csap, NULL,
                                  &packets_received));

    TEST_STEP("Verify the number of matching packets received");
    CHECK_MATCHED_PACKETS_NUM(packets_received, 2);

    TEST_SUCCESS;

cleanup:
    TEST_END;
}
/** @} */
