/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Stress tests
 */

/**
 * @defgroup stress-driver_unload_traffic Unloading driver when data flows
 * @ingroup stress
 * @{
 *
 * @objective Check that unloading network driver works fine when data is
 *            received and transmitted over an interface.
 *
 * @param env             Testing environment:
 *                        - @ref env-peer2peer
 *                        - @ref env-peer2peer_ipv6
 * @param send_time       How long to send/receive data, in seconds.
 * @param unload_once     If @c TRUE, unload and reload the driver once,
 *                        if @c FALSE - do it many times in a loop until
 *                        @p send_time expires.
 * @param conns_num       How many connections should send and receive
 *                        data.
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-SYS09, X3-STR13.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "stress/driver_unload_traffic"

#include "net_drv_test.h"
#include "tapi_cfg_modules.h"
#include "tapi_mem.h"
#include "te_time.h"

/** How long to send, in seconds */
#define SEND_TIME 10

/** How long to wait after loading/unloading driver, in seconds */
#define WAIT_AFTER_DRIVER_OP 3

/** Maximum size of UDP packet payload */
#define MAX_SEND_SIZE 1400

int
main(int argc, char **argv)
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    char *cfg_bkp = NULL;
    te_bool reload_in_cleanup = FALSE;
    char *iut_drv_name = NULL;

    struct timeval tv_start;
    struct timeval tv_now;

    int send_time;
    te_bool unload_once;

    int conns_num;
    int flows_num;
    int i;
    int j;
    int flow_id;
    net_drv_conn *conns = NULL;
    net_drv_flow *flows = NULL;
    net_drv_flow *flow = NULL;
    te_bool send_success;

    unsigned int neigh_nodes_num = 0;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_INT_PARAM(send_time);
    TEST_GET_BOOL_PARAM(unload_once);
    TEST_GET_INT_PARAM(conns_num);

    CHECK_NOT_NULL(iut_drv_name = net_drv_driver_name(iut_rpcs->ta));

    if (!net_drv_driver_unloadable(iut_rpcs->ta,
                                   iut_drv_name))
    {
        TEST_SKIP("Network driver on IUT cannot be unloaded");
    }

    CHECK_RC(net_drv_neigh_nodes_count(iut_rpcs->ta,
                                       &neigh_nodes_num));

    conns = tapi_calloc(conns_num, sizeof(*conns));
    flows_num = conns_num * 2;
    flows = tapi_calloc(flows_num, sizeof(*flows));

    TEST_STEP("Create @p conns_num pairs of connected UDP sockets "
              "on IUT and Tester. Fork additional RPC servers to "
              "send data simultaneously in both directions over them.");

    for (i = 0; i < conns_num; i++)
    {
        conns[i].rpcs1 = iut_rpcs;
        conns[i].rpcs2 = tst_rpcs;
        conns[i].sock_type = RPC_SOCK_DGRAM;
        conns[i].s1_addr = iut_addr;
        conns[i].s2_addr = tst_addr;
        conns[i].new_ports = TRUE;

        net_drv_conn_create(&conns[i]);

        for (j = 0; j < 2; j++)
        {
            flow_id = 2 * i + j;
            flow = &flows[flow_id];
            flow->flow_id = flow_id;
            flow->duration = send_time;
            flow->new_processes = TRUE;
            flow->conn = &conns[i];
            flow->rpcs1 = iut_rpcs;
            flow->rpcs2 = tst_rpcs;
            flow->tx = (j == 0);
            flow->ignore_send_err = TRUE;
            flow->min_size = 1;
            flow->max_size = MAX_SEND_SIZE;

            net_drv_flow_prepare(flow);
        }
    }

    TEST_STEP("Save configuration backup.");
    CHECK_RC(cfg_create_backup(&cfg_bkp));

    TEST_STEP("Start sending and receiving data simultaneously in both "
              "directions over each connected socket pair. This "
              "should continue until @p send_time expires.");
    for (j = 0; j < flows_num; j++)
        net_drv_flow_start(&flows[j]);

    CHECK_RC(te_gettimeofday(&tv_start, NULL));

    TEST_STEP("If @p unload_once is @c TRUE, do the following steps once. "
              "Otherwise do them until @p send_time expires.");
    do {
        TEST_SUBSTEP("Unload the tested driver module.");
        rc = net_drv_driver_set_loaded(iut_rpcs->ta, iut_drv_name, FALSE);
        if (rc != 0)
            TEST_VERDICT("Failed to unload the driver");
        reload_in_cleanup = TRUE;

        TEST_SUBSTEP("Wait for a while.");
        SLEEP(WAIT_AFTER_DRIVER_OP);

        TEST_SUBSTEP("Reload the tested driver module.");
        rc = net_drv_driver_set_loaded(iut_rpcs->ta, iut_drv_name, TRUE);
        if (rc != 0)
            TEST_VERDICT("Failed to load the driver");
        reload_in_cleanup = FALSE;
        CHECK_RC(net_drv_wait_neigh_nodes_recover(iut_rpcs->ta,
                                                  neigh_nodes_num));

        TEST_SUBSTEP("Restore configuration from the backup so that "
                     "addresses assigned to the IUT interface are "
                     "restored.");
        CHECK_RC(cfg_synchronize_fmt(TRUE, "/agent:%s", iut_rpcs->ta));
        CHECK_RC(cfg_restore_backup_nohistory(cfg_bkp));

        TEST_SUBSTEP("Wait for a while.");
        SLEEP(WAIT_AFTER_DRIVER_OP);

        CHECK_RC(te_gettimeofday(&tv_now, NULL));
        if (TE_US2SEC(TIMEVAL_SUB(tv_now, tv_start)) >= send_time)
            break;

    } while (!unload_once);

    TEST_STEP("Wait until sending and receiving data over sockets "
              "terminates, check that no unexpected errors occurred.");
    send_success = TRUE;
    for (j = 0; j < flows_num; j++)
    {
        net_drv_flow_finish(&flows[j]);
        if (!flows[j].success)
            send_success = FALSE;
    }

    if (!send_success)
    {
        TEST_VERDICT("Unexpected error occurred during sending or "
                     "receiving performed concurrently with driver "
                     "unloading and reloading");
    }

    TEST_STEP("Check that data can be sent from IUT to Tester and "
              "vice versa and received by peer.");
    net_drv_conn_check(iut_rpcs, conns[0].s1, "IUT",
                       tst_rpcs, conns[0].s2, "Tester",
                       "Final check");

    TEST_SUCCESS;

cleanup:

    if (reload_in_cleanup)
    {
        /* Configurator fails to rollback module unload. */
        CLEANUP_CHECK_RC(tapi_cfg_module_load(
                                      iut_rpcs->ta,
                                      iut_drv_name));

        /*
         * Make sure interfaces are fully recovered so that configuration
         * can be restored automatically.
         */
        CLEANUP_CHECK_RC(net_drv_wait_neigh_nodes_recover(iut_rpcs->ta,
                                                          neigh_nodes_num));
    }

    if (cfg_bkp != NULL)
        CLEANUP_CHECK_RC(cfg_release_backup(&cfg_bkp));

    free(iut_drv_name);

    if (flows != NULL)
    {
        for (j = 0; j < flows_num; j++)
            CLEANUP_CHECK_RC(net_drv_flow_destroy(&flows[j]));
    }

    if (conns != NULL)
    {
        for (i = 0; i < conns_num; i++)
            CLEANUP_CHECK_RC(net_drv_conn_destroy(&conns[i]));
    }

    free(flows);
    free(conns);

    TEST_END;
}
