/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Stress tests
 */

/**
 * @defgroup stress-if_down_up_loop Setting interface DOWN/UP
 * @ingroup stress
 * @{
 *
 * @objective Check that setting interface DOWN/UP does not break
 *            the driver.
 *
 * @param env                 Testing environment:
 *                            - @ref env-peer2peer
 *                            - @ref env-peer2peer_ipv6
 * @param duration            How long to run the loop setting interface
 *                            DOWN/UP, in seconds:
 *                            - @c 0 (run the loop exactly once)
 *                            - @c 100
 * @param wait_after_if_up    How long to wait after bringing interface UP,
 *                            in milliseconds:
 *                            - @c 0
 *                            - @c 500
 * @param tx_traffic          If @c TRUE, data should be sent continuously
 *                            from IUT to Tester during the testing.
 * @param rx_traffic          If @c TRUE, data should be sent continuously
 *                            from Tester to IUT during the testing.
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-SYS06, X3-STR03, X3-STR12.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "stress/if_down_up_loop"

#include "net_drv_test.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_modules.h"
#include "tapi_cfg_sys.h"
#include "te_sleep.h"
#include "te_time.h"

/** Minimum time to send data, in seconds */
#define MIN_SEND_TIME 5

/** How long to send, in seconds */
#define SEND_DURATION(_duration) (MAX(_duration + 1, MIN_SEND_TIME))

int
main(int argc, char **argv)
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    struct timeval tv_start;
    struct timeval tv_now;

    int duration;
    int wait_after_if_up;
    te_bool tx_traffic;
    te_bool rx_traffic;

    te_bool rx_success = TRUE;
    te_bool tx_success = TRUE;

    net_drv_conn conn = NET_DRV_CONN_INIT;
    net_drv_flow tx_flow = NET_DRV_FLOW_INIT;
    net_drv_flow rx_flow = NET_DRV_FLOW_INIT;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_IF(iut_if);
    TEST_GET_INT_PARAM(duration);
    TEST_GET_INT_PARAM(wait_after_if_up);
    TEST_GET_BOOL_PARAM(tx_traffic);
    TEST_GET_BOOL_PARAM(rx_traffic);

    if (tx_traffic || rx_traffic)
    {
        TEST_STEP("If @p tx_traffic or @p rx_traffic is @c TRUE, create "
                  "a pair of connected UDP sockets on IUT and Tester.");

        conn.rpcs1 = iut_rpcs;
        conn.rpcs2 = tst_rpcs;
        conn.s1_addr = iut_addr;
        conn.s2_addr = tst_addr;
        conn.sock_type = RPC_SOCK_DGRAM;
        conn.new_ports = FALSE;
        net_drv_conn_create(&conn);

        if (iut_addr->sa_family == AF_INET6)
        {
            TEST_SUBSTEP("If @p IPv6 is checked, also enable "
                         "@b keep_addr_on_down setting for the IUT "
                         "interface to avoid IPv6 address disappearance "
                         "after brinding it down.");
            CHECK_RC(tapi_cfg_sys_set_int(
                                iut_rpcs->ta, 1, NULL,
                                "net/ipv6/conf:%s/keep_addr_on_down",
                                iut_if->if_name));
        }
    }

    tx_flow.duration = SEND_DURATION(duration);
    tx_flow.new_processes = TRUE;
    tx_flow.conn = &conn;
    tx_flow.rpcs1 = iut_rpcs;
    tx_flow.rpcs2 = tst_rpcs;
    tx_flow.ignore_send_err = TRUE;
    memcpy(&rx_flow, &tx_flow, sizeof(rx_flow));

    if (tx_traffic)
    {
        tx_flow.tx = TRUE;
        tx_flow.flow_id = 1;

        net_drv_flow_prepare(&tx_flow);
    }

    if (rx_traffic)
    {
        rx_flow.tx = FALSE;
        rx_flow.flow_id = 2;

        net_drv_flow_prepare(&rx_flow);
    }

    if (tx_traffic)
    {
        TEST_STEP("If @p tx_traffic is @c TRUE, call "
                  "@b rpc_pattern_sender() in a child process on IUT and "
                  "@b rpc_drain_fd_duration() in a child process on Tester "
                  "to start data flow from IUT to Tester.");

        net_drv_flow_start(&tx_flow);
    }

    if (rx_traffic)
    {
        TEST_STEP("If @p rx_traffic is @c TRUE, call "
                  "@b rpc_pattern_sender() in a child process on Tester and "
                  "@b rpc_drain_fd_duration() in a child process on IUT "
                  "to start data flow from Tester to IUT.");

        net_drv_flow_start(&rx_flow);
    }

    if (tx_traffic || rx_traffic)
        TAPI_WAIT_NETWORK;

    TEST_STEP("In a loop set IUT interface DOWN and UP until @p duration "
              "seconds passes (but do it at least once). If "
              "@p wait_after_if_up is not zero, wait specified number of "
              "milliseconds at the end of every loop iteration.");

    CHECK_RC(te_gettimeofday(&tv_start, NULL));

    while (TRUE)
    {
        CHECK_RC(tapi_cfg_base_if_down(iut_rpcs->ta, iut_if->if_name));
        CHECK_RC(tapi_cfg_base_if_up(iut_rpcs->ta, iut_if->if_name));

        if (wait_after_if_up > 0)
            MSLEEP(wait_after_if_up);

        CHECK_RC(te_gettimeofday(&tv_now, NULL));
        if (TE_US2SEC(TIMEVAL_SUB(tv_now, tv_start)) >= duration)
            break;
    }

    if (tx_traffic)
    {
        TEST_STEP("If @p tx_traffic is @c TRUE, check that data sending "
                  "from IUT and data receiving on Tester terminated "
                  "successfully.");
        net_drv_flow_finish(&tx_flow);
        tx_success = tx_flow.success;
    }

    if (rx_traffic)
    {
        TEST_STEP("If @p rx_traffic is @c TRUE, check that data sending "
                  "from Tester and data receiving on IUT terminated "
                  "successfully.");
        net_drv_flow_finish(&rx_flow);
        rx_success = rx_flow.success;
    }

    if (!tx_success)
        TEST_VERDICT("Failure occurred during sending");
    if (!rx_success)
        TEST_VERDICT("Failure occurred during receiving");

    TEST_SUCCESS;

cleanup:

    NET_DRV_CLEANUP_SET_UP_WAIT(iut_rpcs->ta, iut_if->if_name);

    if (tx_traffic)
        CLEANUP_CHECK_RC(net_drv_flow_destroy(&tx_flow));
    if (rx_traffic)
        CLEANUP_CHECK_RC(net_drv_flow_destroy(&rx_flow));

    CLEANUP_CHECK_RC(net_drv_conn_destroy(&conn));

    TEST_END;
}
