/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-mac_change_rx Changing MAC address and receiving
 * @ingroup basic
 * @{
 *
 * @objective Check that MAC address can be changed and after that
 *            packets sent to the new address can be received.
 *
 * @param env               Testing environment:
 *                          - @ref env-peer2peer
 *                          - @ref env-peer2peer_ipv6
 * @param promisc           If @c TRUE, enable promiscuous mode on the
 *                          IUT interface, otherwise disable it.
 *
 *
 * @par Scenario:
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "basic/mac_change_rx"

#include "net_drv_test.h"
#include "te_ethernet.h"
#include "tapi_udp.h"
#include "tapi_cfg.h"
#include "ndn.h"
#include "tapi_cfg_base.h"

#define MAX_PKT_LEN 1024

static rcf_rpc_server *iut_rpcs = NULL;
static rcf_rpc_server *tst_rpcs = NULL;
static te_bool promisc;
static csap_handle_t csap_tst = CSAP_INVALID_HANDLE;
static csap_handle_t csap_iut = CSAP_INVALID_HANDLE;
static asn_value *pkt_templ = NULL;
static int iut_s = -1;

/**
 * Send UDP packet from Tester to the specified destination MAC
 * address, while specifying IP address and port of the IUT socket.
 * Check whether packet is received by socket or not as expected,
 * check whether it is captured by CSAP.
 */
static void
send_recv_pkt(const struct sockaddr *dst_mac,
              te_bool exp_receive,
              const char *stage)
{
    char send_buf[MAX_PKT_LEN];
    char recv_buf[MAX_PKT_LEN];
    int send_len;
    te_bool readable;
    int rc;

    unsigned int pkts_num;

    send_len = rand_range(1, sizeof(send_buf));
    te_fill_buf(send_buf, send_len);

    CHECK_RC(asn_write_value_field(pkt_templ, send_buf, send_len,
                                   "payload.#bytes"));
    CHECK_RC(asn_write_value_field(pkt_templ, dst_mac->sa_data,
                                   ETHER_ADDR_LEN,
                                   "pdus.2.#eth.dst-addr.#plain"));

    CHECK_RC(tapi_tad_trsend_start(tst_rpcs->ta, 0, csap_tst,
                                   pkt_templ, RCF_MODE_BLOCKING));

    RPC_GET_READABILITY(readable, iut_rpcs, iut_s,
                        TAPI_WAIT_NETWORK_DELAY);
    if (readable != exp_receive)
    {
        TEST_VERDICT("%s: socket is %sreadable unexpectedly",
                     stage, readable ? "" : "not ");
    }

    if (readable)
    {
        RPC_AWAIT_ERROR(iut_rpcs);
        rc = rpc_recv(iut_rpcs, iut_s, recv_buf, sizeof(recv_buf), 0);

        if (rc < 0)
        {
            TEST_VERDICT("%s: recv() failed unexpectedly with error %r",
                         stage, RPC_ERRNO(iut_rpcs));
        }

        if (rc != send_len || memcmp(recv_buf, send_buf, send_len) != 0)
            TEST_VERDICT("%s: recv() returned unexpected data", stage);
    }

    CHECK_RC(tapi_tad_trrecv_get(iut_rpcs->ta, 0, csap_iut, NULL,
                                 &pkts_num));
    if (pkts_num > 1)
    {
        TEST_VERDICT("%s: CSAP on IUT captured more than one packet",
                     stage);
    }

    if (exp_receive || promisc)
    {
        if (pkts_num == 0)
        {
            TEST_VERDICT("%s: CSAP on IUT did not capture the packet",
                         stage);
        }
    }
    else
    {
        if (pkts_num != 0)
            TEST_VERDICT("%s: CSAP on IUT captured the packet", stage);
    }
}

int
main(int argc, char *argv[])
{
    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;

    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    const struct sockaddr *tst_lladdr = NULL;
    const struct sockaddr *iut_lladdr = NULL;
    const struct sockaddr *iut_alien_mac = NULL;

    int num;
    char buf_tmpl[1024];

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_LINK_ADDR(iut_lladdr);
    TEST_GET_LINK_ADDR(iut_alien_mac);
    TEST_GET_BOOL_PARAM(promisc);

    TEST_STEP("Enable or disable promiscuous mode on the IUT interface "
              "according to @p promisc parameter.");
    CHECK_RC(tapi_cfg_base_if_set_promisc(iut_rpcs->ta, iut_if->if_name,
                                          promisc));
    CFG_WAIT_CHANGES;

    TEST_STEP("Create UDP socket on IUT, bind it to @p iut_addr.");
    iut_s = rpc_socket(iut_rpcs, rpc_socket_domain_by_addr(iut_addr),
                       RPC_SOCK_DGRAM, RPC_PROTO_DEF);
    rpc_bind(iut_rpcs, iut_s, iut_addr);

    TEST_STEP("Create a CSAP on IUT to capture UDP packets sent "
              "to @p iut_addr.");

    CHECK_RC(tapi_udp_ip_eth_csap_create(iut_rpcs->ta, 0, iut_if->if_name,
                                         TAD_ETH_RECV_DEF |
                                         TAD_ETH_RECV_NO_PROMISC,
                                         NULL,
                                         (uint8_t *)(tst_lladdr->sa_data),
                                         iut_addr->sa_family,
                                         TAD_SA2ARGS(iut_addr, tst_addr),
                                         &csap_iut));

    CHECK_RC(tapi_tad_trrecv_start(iut_rpcs->ta, 0, csap_iut, NULL,
                                   TAD_TIMEOUT_INF, 0, RCF_TRRECV_COUNT));

    TEST_STEP("Create a CSAP on Tester to send packets to @p iut_addr.");
    CHECK_RC(tapi_udp_ip_eth_csap_create(tst_rpcs->ta, 0, tst_if->if_name,
                                         TAD_ETH_RECV_NO,
                                         (uint8_t *)(tst_lladdr->sa_data),
                                         NULL,
                                         iut_addr->sa_family,
                                         TAD_SA2ARGS(tst_addr, iut_addr),
                                         &csap_tst));

    CHECK_RC(te_snprintf(buf_tmpl, sizeof(buf_tmpl),
                         "{ pdus { udp: {}, ip%d:{}, eth:{} } }",
                         (iut_addr->sa_family == AF_INET ? 4 : 6)));

    CHECK_RC(asn_parse_value_text(buf_tmpl, ndn_traffic_template,
                                  &pkt_templ, &num));

    TEST_STEP("Send a packet to @p iut_lladdr MAC address (currently set "
              "on the IUT interface) from the Tester CSAP. Check that "
              "IUT socket receives data and IUT CSAP captures the packet.");
    send_recv_pkt(iut_lladdr, TRUE,
                  "Sending to the old address before address change");

    TEST_STEP("Send a packet to @p iut_alien_mac MAC address from the "
              "Tester CSAP. Check that IUT socket does not receive data. "
              "Check that IUT CSAP captures the packet only when "
              "@p promisc is @c TRUE.");
    send_recv_pkt(iut_alien_mac, FALSE,
                  "Sending to the new address before address change");

    TEST_STEP("Change MAC address to @p iut_alien_mac on the IUT "
              "interface. Check that the new address is reported "
              "for the interface after that.");

    CHECK_RC(net_drv_set_check_mac(iut_rpcs->ta, iut_if->if_name,
                                   iut_alien_mac->sa_data));

    TEST_STEP("Send a packet to @p iut_lladdr MAC address from the "
              "Tester CSAP. Check that IUT socket does not receive data. "
              "Check that IUT CSAP captures the packet only when "
              "@p promisc is @c TRUE.");
    send_recv_pkt(iut_lladdr, FALSE,
                  "Sending to the old address after address change");

    TEST_STEP("Send a packet to @p iut_alien_mac MAC address (currently "
              "set on the IUT interface) from the Tester CSAP. Check that "
              "IUT socket receives data and IUT CSAP captures the packet.");
    send_recv_pkt(iut_alien_mac, TRUE,
                  "Sending to the new address after address change");

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(iut_rpcs->ta, 0, csap_iut));
    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0, csap_tst));

    TEST_END;
}
