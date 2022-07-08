/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-mac_change_tx Changing MAC address and sending
 * @ingroup basic
 * @{
 *
 * @objective Check that MAC address can be changed and it affects source
 *            MAC address of sent packets.
 *
 * @param env               Testing environment:
 *                          - @ref env-peer2peer
 *                          - @ref env-peer2peer_ipv6
 * @param if_down           If @c TRUE, set the interface down before
 *                          changing MAC address on it.
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-SYS17, X3-SYS19.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "basic/mac_change_tx"

#include "net_drv_test.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_sys.h"
#include "te_ethernet.h"
#include "tapi_udp.h"

/** Auxiliary structure to pass information to/from CSAP callback */
typedef struct cb_args {
    const void *old_src_mac;
    const void *exp_src_mac;

    unsigned int pkts_num;
    te_bool old_mac;
    te_bool unknown_mac;
    te_bool failed;
} cb_args;

/** Callback to process packets captured by CSAP */
static void
process_pkts(asn_value *pkt, void *arg)
{
    cb_args *args = (cb_args *)arg;
    uint8_t src_mac[ETHER_ADDR_LEN];
    size_t eth_len = ETHER_ADDR_LEN;
    te_errno rc;

    args->pkts_num++;

    rc = asn_read_value_field(pkt, src_mac, &eth_len,
                              "pdus.2.#eth.src-addr.#plain");
    if (rc != 0)
    {
        ERROR("Failed to read Ethernet source address, rc=%r", rc);
        args->failed = TRUE;
        goto cleanup;
    }

    if (memcmp(args->exp_src_mac, src_mac, ETHER_ADDR_LEN) != 0)
    {
        if (memcmp(args->old_src_mac, src_mac, ETHER_ADDR_LEN) == 0)
        {
            ERROR("A packet with old source MAC address was caught");
            args->old_mac = TRUE;
        }
        else
        {
            ERROR("A packet with unknown source MAC address was caught");
            args->unknown_mac = TRUE;
        }
    }

cleanup:
    asn_free_value(pkt);
}

/** Obtain and check packets captured by CSAP */
static void
check_csap(const char *ta, csap_handle_t csap,
           const struct sockaddr *exp_mac,
           const struct sockaddr *old_mac,
           const char *vpref)
{
    tapi_tad_trrecv_cb_data csap_cb_data;
    cb_args pkts_data;

    memset(&csap_cb_data, 0, sizeof(csap_cb_data));
    csap_cb_data.callback = &process_pkts;
    csap_cb_data.user_data = &pkts_data;

    memset(&pkts_data, 0, sizeof(pkts_data));
    pkts_data.old_src_mac = old_mac->sa_data;
    pkts_data.exp_src_mac = exp_mac->sa_data;

    CHECK_RC(tapi_tad_trrecv_get(ta, 0, csap, &csap_cb_data, NULL));

    if (pkts_data.pkts_num == 0)
    {
        TEST_VERDICT("%s: CSAP captured no packets on Tester", vpref);
    }
    else if (pkts_data.failed)
    {
        TEST_VERDICT("%s: failed to process some of the packets captured "
                     "on Tester", vpref);
    }
    else if (pkts_data.unknown_mac)
    {
        TEST_VERDICT("%s: IUT used unknown source MAC address for sending",
                     vpref);
    }
    else if (pkts_data.old_mac)
    {
        TEST_VERDICT("%s: IUT used old source MAC address for sending",
                     vpref);
    }
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;

    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    const struct sockaddr *iut_lladdr = NULL;
    const struct sockaddr *iut_alien_mac = NULL;

    int iut_s = -1;
    int tst_s = -1;

    csap_handle_t csap_tst = CSAP_INVALID_HANDLE;

    te_bool if_down;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_LINK_ADDR(iut_lladdr);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_BOOL_PARAM(if_down);

    if (if_down && iut_addr->sa_family == AF_INET6)
    {
        TEST_STEP("If @p if_down is @c TRUE and @c IPv6 is checked, "
                  "enable @b keep_addr_on_down setting for the IUT "
                  "interface to avoid IPv6 address disappearance "
                  "after brinding it down.");
        CHECK_RC(tapi_cfg_sys_set_int(
                            iut_rpcs->ta, 1, NULL,
                            "net/ipv6/conf:%s/keep_addr_on_down",
                            iut_if->if_name));
    }

    TEST_STEP("Create a CSAP on Tester to capture packets sent "
              "from IUT.");

    CHECK_RC(tapi_udp_ip_eth_csap_create(tst_rpcs->ta, 0, tst_if->if_name,
                                         TAD_ETH_RECV_HOST |
                                         TAD_ETH_RECV_NO_PROMISC,
                                         NULL, NULL,
                                         iut_addr->sa_family,
                                         TAD_SA2ARGS(tst_addr, iut_addr),
                                         &csap_tst));

    CHECK_RC(tapi_tad_trrecv_start(tst_rpcs->ta, 0, csap_tst,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    TEST_STEP("Establish connection between a pair of UDP sockets "
              "on IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Send some data from IUT socket to peer, receive "
              "and check it.");
    net_drv_send_recv_check(iut_rpcs, iut_s, tst_rpcs, tst_s,
                            "Checking before MAC address change");

    TEST_STEP("Check packets captured by CSAP on Tester, their source "
              "MAC address should be @p iut_lladdr.");
    check_csap(tst_rpcs->ta, csap_tst, iut_lladdr, iut_lladdr,
               "Before MAC address change");

    if (if_down)
    {
        TEST_STEP("If @p if_down is @c TRUE, set the IUT interface down.");
        CHECK_RC(tapi_cfg_base_if_down(iut_rpcs->ta, iut_if->if_name));
        CFG_WAIT_CHANGES;
    }

    TEST_STEP("Change MAC address to @p iut_alien_mac on the IUT "
              "interface. Check that the new address is reported "
              "for the interface after that.");

    CHECK_RC(net_drv_set_check_mac(iut_rpcs->ta, iut_if->if_name,
                                   iut_alien_mac->sa_data));

    if (if_down)
    {
        TEST_STEP("If @p if_down is @c TRUE, set the IUT interface up "
                  "again.");
        CHECK_RC(tapi_cfg_base_if_up(iut_rpcs->ta, iut_if->if_name));
        CFG_WAIT_CHANGES;
    }

    TEST_STEP("Send some data from IUT socket to peer, receive "
              "and check it.");
    net_drv_send_recv_check(iut_rpcs, iut_s, tst_rpcs, tst_s,
                            "Checking after MAC address change");

    TEST_STEP("Check packets captured by CSAP on Tester, their source "
              "MAC address should be @p iut_alien_mac.");
    check_csap(tst_rpcs->ta, csap_tst, iut_alien_mac, iut_lladdr,
               "After MAC address change");

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0, csap_tst));

    TEST_END;
}
