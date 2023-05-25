/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-set_if_down Setting interface DOWN
 * @ingroup basic
 * @{
 *
 * @objective Check that network interface can be set DOWN.
 *
 * @param env                 Testing environment:
 *                            - @ref env-peer2peer
 * @param check_socket        Whether to check that data cannot be
 *                            sent and received via a socket.
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ST06.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "basic/set_if_down"

#include "net_drv_test.h"
#include "tapi_cfg_base.h"

/** Size of data to send */
#define PKT_SIZE 1024

/**
 * Check that data cannot be sent or received over an interface
 * which is set DOWN.
 *
 * @param sender_rpcs       Sender RPC server.
 * @param sender_s          Sender socket.
 * @param receiver_rpcs     Receiver RPC server.
 * @param receiver_s        Receiver socket.
 * @param receiver_name     Name of the receiver to print in verdicts.
 */
static void
check_send_receive_fail(rcf_rpc_server *sender_rpcs,
                        int sender_s,
                        rcf_rpc_server *receiver_rpcs,
                        int receiver_s,
                        const char *receiver_name)
{
    char buf[PKT_SIZE];
    te_bool readable;
    int rc;

    te_fill_buf(buf, PKT_SIZE);

    /*
     * send() may fail or succeed here, depending on Linux version,
     * it is not important here.
     */
    RPC_AWAIT_ERROR(sender_rpcs);
    rc = rpc_send(sender_rpcs, sender_s, buf, sizeof(buf), 0);

    if (rc > 0)
    {
        RPC_GET_READABILITY(readable, receiver_rpcs, receiver_s,
                            TAPI_WAIT_NETWORK_DELAY);
        if (readable)
        {
            TEST_VERDICT("%s receives data sent after IUT interface "
                         "was set DOWN", receiver_name);
        }
    }
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;

    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    int iut_s = -1;
    int tst_s = -1;

    int if_status;
    cfg_val_type val_type = CVT_INTEGER;

    te_bool check_socket;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_BOOL_PARAM(check_socket);

    if (check_socket)
    {
        TEST_STEP("If @p check_socket is @c TRUE, create UDP socket on "
                  "IUT and its peer on Tester. Check that data can be "
                  "sent and received over the IUT socket.");

        GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                       iut_addr, tst_addr, &iut_s, &tst_s);

        net_drv_conn_check(iut_rpcs, iut_s, "IUT",
                           tst_rpcs, tst_s, "Tester",
                           "Checking connection before setting "
                           "interface down");
    }

    TEST_STEP("Set the IUT network interface DOWN.");
    CHECK_RC(tapi_cfg_base_if_down(iut_rpcs->ta, iut_if->if_name));

    TEST_STEP("Wait for a while and check that interface is reported as "
              "being DOWN in configuration tree.");
    CFG_WAIT_CHANGES;
    CHECK_RC(cfg_synchronize_fmt(TRUE, "/agent:%s/interface:%s",
                                 iut_rpcs->ta, iut_if->if_name));
    CHECK_RC(cfg_get_instance_fmt(&val_type, &if_status,
                                  "/agent:%s/interface:%s/status:",
                                  iut_rpcs->ta, iut_if->if_name));

    if (if_status != 0)
        TEST_VERDICT("Interface is still UP after bringing it DOWN");

    if (check_socket)
    {
        TEST_STEP("If @p check_socket is @c TRUE, check that new data can "
                  "now be neither sent nor received via the IUT socket.");

        check_send_receive_fail(iut_rpcs, iut_s,
                                tst_rpcs, tst_s, "Tester");
        check_send_receive_fail(tst_rpcs, tst_s,
                                iut_rpcs, iut_s, "IUT");
    }

    TEST_SUCCESS;

cleanup:

    NET_DRV_CLEANUP_SET_UP_WAIT(iut_rpcs->ta, iut_if->if_name);
    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    TEST_END;
}
