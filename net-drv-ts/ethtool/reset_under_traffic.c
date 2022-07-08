/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-reset_under_traffic Reset NIC when data flows
 * @ingroup ethtool
 * @{
 *
 * @objective Check that NIC can be reset when data is transmitted over it
 *            and that data transmission resumes after NIC is reset.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param flags          Ethtool reset flags to check.
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ET021.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "ethtool/reset_under_traffic"

#include "net_drv_test.h"
#include "tapi_cfg_sys.h"

/* How long to send data, in seconds */
#define SEND_DURATION 10
/* How long to wait before calling ETHTOOL_RESET, in seconds */
#define WAIT_BEFORE_RESET 1

int
main(int argc, char **argv)
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    net_drv_conn conn = NET_DRV_CONN_INIT;
    net_drv_flow tx_flow = NET_DRV_FLOW_INIT;
    net_drv_flow rx_flow = NET_DRV_FLOW_INIT;
    int flags;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_IF(iut_if);
    TEST_GET_BIT_MASK_PARAM(flags, NET_DRV_RESET_FLAGS);

    if (iut_addr->sa_family == AF_INET6)
    {
        TEST_STEP("If IPv6 is checked, enable @b keep_addr_on_down "
                  "setting on the IUT interface to avoid network address "
                  "disappearance when interface is reset.");
        CHECK_RC(tapi_cfg_sys_set_int(
                            iut_rpcs->ta, 1, NULL,
                            "net/ipv6/conf:%s/keep_addr_on_down",
                            iut_if->if_name));
    }

    TEST_STEP("Create a pair of connected UDP sockets on IUT and Tester.");
    conn.rpcs1 = iut_rpcs;
    conn.rpcs2 = tst_rpcs;
    conn.s1_addr = iut_addr;
    conn.s2_addr = tst_addr;
    conn.sock_type = RPC_SOCK_DGRAM;
    net_drv_conn_create(&conn);

    tx_flow.duration = SEND_DURATION;
    tx_flow.new_processes = TRUE;
    tx_flow.conn = &conn;
    tx_flow.rpcs1 = iut_rpcs;
    tx_flow.rpcs2 = tst_rpcs;
    tx_flow.tx = TRUE;
    /* Send errors may happen during reset */
    tx_flow.ignore_send_err = TRUE;
    tx_flow.flow_id = 1;

    memcpy(&rx_flow, &tx_flow, sizeof(tx_flow));
    rx_flow.flow_id = 2;
    rx_flow.tx = FALSE;

    TEST_STEP("Fork new RPC servers on IUT and Tester for sending and "
              "receiving data.");
    net_drv_flow_prepare(&tx_flow);
    net_drv_flow_prepare(&rx_flow);

    TEST_STEP("Start sending UDP packets from IUT with help of "
              "@b rpc_pattern_sender() and receiving them on Tester "
              "with help of @b rpc_drain_fd().");
    net_drv_flow_start(&tx_flow);

    TEST_STEP("Start sending UDP packets from Tester with help of "
              "@b rpc_pattern_sender() and receiving them on IUT "
              "with help of @b rpc_drain_fd().");
    net_drv_flow_start(&rx_flow);

    TEST_STEP("Wait for a while.");
    te_motivated_sleep(WAIT_BEFORE_RESET,
                       "let data to flow for some time");

    TEST_STEP("Call @b ioctl(@c SIOCETHTOOL / @c ETHTOOL_RESET) with reset "
              "flags set according to @p flags on the IUT interface.");
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = net_drv_ethtool_reset(iut_rpcs, conn.s1, iut_if->if_name,
                               flags, NULL);
    if (rc < 0)
    {
        if (RPC_ERRNO(iut_rpcs) == RPC_EOPNOTSUPP)
        {
            TEST_SKIP("ETHTOOL_RESET is not supported");
        }
        else
        {
            TEST_VERDICT("ETHTOOL_RESET command failed with errno %r",
                         RPC_ERRNO(iut_rpcs));
        }
    }

    TEST_STEP("Wait until @b rpc_pattern_sender() on IUT and "
              "@b rpc_drain_fd() on Tester terminate successfully.");
    net_drv_flow_finish(&tx_flow);
    if (!tx_flow.success)
        TEST_VERDICT("Failure occurred during sending data from IUT");

    TEST_STEP("Wait until @b rpc_pattern_sender() on Tester and "
              "@b rpc_drain_fd() on IUT terminate successfully.");
    net_drv_flow_finish(&rx_flow);
    if (!rx_flow.success)
        TEST_VERDICT("Failure occurred during sending data from Tester");

    TEST_STEP("Check that data can be sent and received in both directions "
              "over the pair of UDP sockets.");
    net_drv_conn_check(iut_rpcs, conn.s1, "IUT",
                       tst_rpcs, conn.s2, "Tester",
                       "Checking after reset");

    TEST_SUCCESS;

cleanup:

    CLEANUP_CHECK_RC(net_drv_flow_destroy(&tx_flow));
    CLEANUP_CHECK_RC(net_drv_flow_destroy(&rx_flow));
    CLEANUP_CHECK_RC(net_drv_conn_destroy(&conn));

    TEST_END;
}
