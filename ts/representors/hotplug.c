/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * DPDK Port Representors Test Suite
 * Port representors use cases
 */

/** @defgroup representors-hotplug Port representors hotplug
 * @ingroup representors
 * @{
 *
 * @objective Test port representors hotplug add and remove
 *
 * @param tmpl              Packet template
 * @param remove_port       If @c TRUE, remove PF before hotplugging it with
 *                          representors; otherwise don't do that
 *
 * @type representor
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * Test port representors hotplug add and remove. Test that traffic flow
 * corresponds to the default representors flow rules.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "representors/hotplug"

#include "dpdk_pmd_test.h"

int
main(int argc, char *argv[])
{
    tapi_env_host                          *tst_host;
    const struct if_nameindex              *tst_if = NULL;
    rcf_rpc_server                         *iut_rpcs = NULL;
    const tapi_env_if                      *iut_port = NULL;
    struct test_ethdev_config               ethdev_config;
    struct test_transceiver                *trsc_net = NULL;
    asn_value                              *tmpl;
    asn_value                              *tmpl_rep;
    asn_value                              *tmpl_vf;
    rpc_rte_mempool_p                       mp;

    const struct sockaddr                  *iut_alien_mac = NULL;
    unsigned int                            n_vfs = 0;
    char                                  **vf_addrs;
    unsigned int                           *vf_ids;
    char                                   *dev_args = NULL;
    unsigned int                            vf_id;
    te_bool                                 remove_port;
    struct tarpc_rte_eth_representor_info   rep_info = {};

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_ENV_IF(iut_port);
    TEST_GET_HOST(tst_host);
    TEST_GET_IF(tst_if);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_NDN_TRAFFIC_TEMPLATE(tmpl);
    TEST_GET_BOOL_PARAM(remove_port);

    trsc_net = test_transceiver_net_init(tst_host->ta, tst_if->if_name);

    test_prepare_config_def_mk(&env, iut_rpcs, &iut_port->if_info,
                               &ethdev_config);
    CHECK_RC(tapi_ndn_subst_env(tmpl, &test_params, &env));
    tmpl_rep = asn_copy_value(tmpl);
    tmpl_vf = asn_copy_value(tmpl);

    TEST_STEP("Create a big enough mempool for PF, VF and representor ports");
    mp = test_rte_pktmbuf_pool_create(iut_rpcs, TEST_PKTS_MEMPOOL_NAME,
                                     TEST_RTE_MEMPOOL_DEF_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_CACHE,
                                     TEST_RTE_MEMPOOL_DEF_PRIV_SIZE,
                                     TEST_RTE_MEMPOOL_DEF_DATA_ROOM,
                                     rpc_rte_eth_dev_socket_id(iut_rpcs,
                                            iut_port->if_info.if_index));

    /* Device is assumed to be a PCI function, so its name is a PCI address */
    CHECK_RC(tapi_rte_get_dev_args_by_pci_addr(iut_rpcs->ta,
                                               ethdev_config.dev_name,
                                               &dev_args));

    TEST_STEP("Get DPDK controller info");
    rpc_rte_eth_representor_info_get(iut_rpcs, ethdev_config.port_id,
                                     &rep_info);

    TEST_STEP("Hotplug all virtual functions");
    test_get_vf_pci_addrs_by_node(tapi_env_get_if_net_node(iut_port),
                                  &n_vfs, &vf_addrs, &vf_ids);


    for (vf_id = 0; vf_id < n_vfs; vf_id++)
    {
        struct test_ethdev_config           ethdev_config_rep;
        struct test_ethdev_config          *ethdev_config_vf;
        struct test_transceiver            *trsc_dpdk = NULL;
        struct test_transceiver            *trsc_rep = NULL;
        struct test_transceiver            *trsc_vf = NULL;
        uint16_t                           *rep_port_ids;
        static char                         rep_name[RPC_RTE_ETH_NAME_MAX_LEN];
        char                               *dev_args_rep = NULL;
        unsigned int                        n_rep;

        if (remove_port)
        {
            TEST_STEP("Remove the PF port");
            if (vf_id != 0)
            {
                CHECK_RC(test_prepare_ethdev(&ethdev_config,
                                             TEST_ETHDEV_CLOSED));
            }
            CHECK_RC(tapi_rte_eal_hotplug_remove(iut_rpcs,
                         ethdev_config.bus_name, ethdev_config.dev_name));
        }

        TEST_STEP("Hotplug the PF port with representor device argument");
        te_asprintf(&dev_args_rep, "%s%srepresentor=c%upf%uvf%u",
                    dev_args == NULL ? "" : dev_args,
                    dev_args == NULL ? "" : ",", rep_info.controller,
                    rep_info.pf, vf_ids[vf_id]);
        CHECK_NOT_NULL(dev_args_rep);
        CHECK_RC(tapi_rte_eal_hotplug_add(iut_rpcs, ethdev_config.bus_name,
                                          ethdev_config.dev_name,
                                          dev_args_rep));

        if (remove_port)
        {
            uint16_t pf_port_id;
            char *dev_name_tmp = tapi_strdup(ethdev_config.dev_name);

            rpc_rte_eth_dev_get_port_by_name(iut_rpcs, dev_name_tmp,
                                             &pf_port_id);
            test_prepare_config_mk(&env, iut_rpcs, dev_name_tmp, pf_port_id,
                                   &ethdev_config);
            free(dev_name_tmp);
        }

        TEST_STEP("Hotplug the VF");
        test_hotplug_vfs_by_ids(iut_rpcs, &env,
                                tapi_env_get_if_net_node(iut_port),
                                1, &vf_ids[vf_id], &ethdev_config_vf);

        TEST_STEP("Find represetor port");
        rpc_dpdk_find_representors(iut_rpcs, &n_rep, &rep_port_ids);
        if (n_rep != 1)
        {
            TEST_VERDICT("Expected representors spawned: %u, actual: %u", 1u,
                      n_rep);
        }

        rpc_rte_eth_dev_get_name_by_port(iut_rpcs, rep_port_ids[0], rep_name);
        test_prepare_config_mk(&env, iut_rpcs, rep_name, rep_port_ids[0],
                               &ethdev_config_rep);

        TEST_STEP("Start PF, representor and the first VF with shared mempool");
        ethdev_config.mp = ethdev_config_rep.mp = ethdev_config_vf->mp = mp;
        CHECK_RC(test_prepare_ethdev(&ethdev_config, TEST_ETHDEV_STARTED));
        CHECK_RC(test_prepare_ethdev(&ethdev_config_rep, TEST_ETHDEV_STARTED));
        CHECK_RC(test_prepare_ethdev(ethdev_config_vf, TEST_ETHDEV_STARTED));


        uint16_t                                proxy_port;
        ERROR("varlogen >> call rpc_rte_flow_pick_transfer_proxy");
        rpc_rte_flow_pick_transfer_proxy(iut_rpcs, ethdev_config_rep.port_id,
                                         &proxy_port, NULL);
        ERROR("varlogen >> pick port ethdev_config_rep %d, proxy_port = %d", ethdev_config_rep.port_id, proxy_port);

        ERROR("varlogen >> call rpc_rte_flow_pick_transfer_proxy");
        rpc_rte_flow_pick_transfer_proxy(iut_rpcs, ethdev_config_vf->port_id,
                                         &proxy_port, NULL);
        ERROR("varlogen >> pick port ethdev_config_vf %d, proxy_port = %d", ethdev_config_vf->port_id, proxy_port);


        TEST_SUCCESS;


        TEST_STEP("Init packet transmission between TST and IUT PF / VF / representor");
        trsc_dpdk = test_transceiver_dpdk_init(iut_rpcs, ethdev_config.port_id,
                                               ethdev_config.mp);
        trsc_rep = test_transceiver_dpdk_init(iut_rpcs,
                                              ethdev_config_rep.port_id,
                                              ethdev_config_rep.mp);
        trsc_vf = test_transceiver_dpdk_init(iut_rpcs,
                                             ethdev_config_vf->port_id,
                                             ethdev_config_vf->mp);

        TEST_STEP("Set MAC addresses of corresponding ports in templates");
        {
            struct tarpc_ether_addr mac;
            unsigned int i;
            struct tmpl_map_t {
                uint16_t port_id;
                asn_value *tmpl;
                te_bool once; /* Set MAC address in the template only once */
            } tmpl_map[] = {
                { ethdev_config.port_id, tmpl, TRUE },
                { ethdev_config_rep.port_id, tmpl_rep, FALSE },
                { ethdev_config_vf->port_id, tmpl_vf, FALSE },
            };

            for (i = 0; i < TE_ARRAY_LEN(tmpl_map); i++)
            {
                if (vf_id != 0 && tmpl_map[i].once)
                    continue;

                rpc_rte_eth_macaddr_get(iut_rpcs, tmpl_map[i].port_id, &mac);
                test_pdus_set_eth_src_dst(tmpl_map[i].tmpl, "pdus",
                    (const uint8_t *)iut_alien_mac->sa_data, mac.addr_bytes);
            }
        }

        TEST_STEP("Check TST-IUT, VF-representor and representor-VF packet transmissions");
        test_transciever_simple_exchange_commit(tmpl, trsc_net, 1, 0,
                                                trsc_dpdk, 1, 0, NULL, NULL);
        test_transciever_simple_exchange_commit(tmpl_rep, trsc_vf, 1, 0,
                                                trsc_rep, 1, 0, NULL, NULL);
        test_transciever_simple_exchange_commit(tmpl_vf, trsc_rep, 1, 0,
                                                trsc_vf, 1, 0, NULL, NULL);

        test_transceiver_free(trsc_dpdk);
        test_transceiver_free(trsc_rep);
        test_transceiver_free(trsc_vf);

        TEST_STEP("Remove representor port");
        CHECK_RC(test_prepare_ethdev(&ethdev_config_rep, TEST_ETHDEV_CLOSED));
    }

    TEST_SUCCESS;

cleanup:
    test_transceiver_free(trsc_net);

    TEST_END;
}
/** @} */
