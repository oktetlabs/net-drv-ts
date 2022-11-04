/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-mtu_udp MTU usage for UDP packets
 * @ingroup basic
 * @{
 *
 * @objective Check that UDP packets of full MTU size are sent
 *            and that jumbo frames are processed correctly.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 *                      - @ref env-peer2peer_ipv6
 * @param mtu           MTU to be set:
 *                      - @c 512
 *                      - @c 1280
 *                      - @c 1500
 *                      - @c 1600
 *                      - @c 9000
 * @param tx            If @c TRUE, send from IUT to Tester,
 *                      else - the other way around.
 * @param pkt_size      Data size for @b send() call (in terms of MTUs):
 *                      - @c less
 *                      - @c equal
 *                      - @c more
 * @param sends_num     How many times to send data:
 *                      - @c 100
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-SYS04.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME  "basic/mtu_udp"

#include "net_drv_test.h"
#include "tapi_cfg_base.h"
#include "tapi_mem.h"
#include "tapi_eth.h"
#include "tapi_udp.h"
#include "tad_common.h"

/**
 * Maximum sent data size is computed as maximum payload allowed by MTU
 * multiplied by this number
 */
#define MAX_LEN_MULTIPLIER 1.5

/** Data sizes in terms of MTU */
enum {
    PKT_SIZE_LESS,
    PKT_SIZE_EQUAL,
    PKT_SIZE_MORE,
};

/** Data sizes for TEST_GET_ENUM_PARAM() */
#define PKT_SIZES \
    { "less", PKT_SIZE_LESS }, \
    { "equal", PKT_SIZE_EQUAL }, \
    { "more", PKT_SIZE_MORE }

/** Arguments for CSAP callback */
typedef struct cb_args {
    /* Input arguments */

    te_bool ipv4; /**< Whether this packet is IPv4 or IPv6 */
    int max_pld_size; /**< Maximum payload size */
    int max_frag_size; /**< Maximum IP fragment size
                            (in the case when fragmentation is expected) */

    /* Output arguments */

    int pkts_num; /**< Number of captured packets */
    int first_small_pkt; /**< Number of the first small packet,
                              negative if there is no such packet */

    te_bool too_big_pkt; /**< Set to TRUE if too big packet is captured */
    te_bool failed; /**< Set to TRUE if some error occurred when processing
                         packets */
} cb_args;

/** CSAP callback for UDP packets */
static void
process_eth(asn_value *pkt, void *arg)
{
    cb_args *args = (cb_args *)arg;
    int pld_len;
    int next_hdr;
    size_t field_len;
    te_errno rc;

    if (args->pkts_num == 0)
        args->first_small_pkt = -1;

    args->pkts_num++;

    field_len = sizeof(pld_len);
    if (args->ipv4)
    {
        rc = asn_read_value_field(pkt, &pld_len, &field_len,
                                  "pdus.0.#ip4.total-length");
    }
    else
    {
        rc = asn_read_value_field(pkt, &pld_len, &field_len,
                                  "pdus.0.#ip6.payload-length");
        pld_len += TAD_IP6_HDR_LEN;
    }

    if (rc != 0)
    {
        ERROR("Failed to get length of payload: %r", rc);
        args->failed = TRUE;
        goto cleanup;
    }

    if (!args->ipv4)
    {
        field_len = sizeof(next_hdr);
        rc = asn_read_value_field(pkt, &next_hdr, &field_len,
                                  "pdus.0.#ip6.next-header");
        if (next_hdr == IPPROTO_ICMPV6)
        {
            RING("Skipping ICMPv6 packet");
            args->pkts_num--;
            goto cleanup;
        }
    }

    RING("An Ethernet packet with payload of %d bytes was captured",
         pld_len);
    if (pld_len < args->max_pld_size)
    {
        if (args->max_frag_size > 0 &&
            pld_len == args->max_frag_size)
        {
            /*
             * The captured packet is smaller than MTU allows,
             * however matches maximum intermediary IP fragment length.
             * It's OK, do nothing.
             */
        }
        else
        {
            if (args->first_small_pkt < 0)
                args->first_small_pkt = args->pkts_num;
        }
    }
    else if (pld_len > args->max_pld_size)
    {
        ERROR("The captured packet is bigger than MTU allows");
        args->too_big_pkt = TRUE;
    }

cleanup:

    asn_free_value(pkt);
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
    const struct sockaddr *tst_lladdr = NULL;

    int iut_s = -1;
    int tst_s = -1;

    rcf_rpc_server *sender_rpcs = NULL;
    rcf_rpc_server *receiver_rpcs = NULL;
    int sender_s = -1;
    int receiver_s = -1;

    te_bool tx;
    int mtu;
    int pkt_size;
    int sends_num;

    int mtu_pld_size;
    int l3_hdr_size;
    int min_size;
    int max_size;

    char *send_buf = NULL;
    char *recv_buf = NULL;
    int i;
    int send_len;
    te_bool readable;

    csap_handle_t tst_csap = CSAP_INVALID_HANDLE;
    tapi_tad_trrecv_cb_data csap_cb_data;
    cb_args args;

    int csap_recv_mode = 0;
    const uint8_t *csap_local_addr = NULL;
    const uint8_t *csap_remote_addr = NULL;
    const struct sockaddr *csap_local_ip_addr = NULL;
    const struct sockaddr *csap_remote_ip_addr = NULL;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_LINK_ADDR(iut_lladdr);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_BOOL_PARAM(tx);
    TEST_GET_INT_PARAM(mtu);
    TEST_GET_ENUM_PARAM(pkt_size, PKT_SIZES);
    TEST_GET_INT_PARAM(sends_num);

    TEST_STEP("Set MTU on IUT and Tester interfaces to @p mtu.");
    net_drv_set_mtu(iut_rpcs->ta, iut_if->if_name, mtu, "IUT");
    net_drv_set_mtu(tst_rpcs->ta, tst_if->if_name, mtu, "Tester");
    CFG_WAIT_CHANGES;

    TEST_STEP("Create a pair of connected UDP sockets on IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    if (iut_addr->sa_family == AF_INET6)
        l3_hdr_size = TAD_IP6_HDR_LEN;
    else
        l3_hdr_size = TAD_IP4_HDR_LEN;

    TEST_STEP("Assume that payload size in MTU-sized packet is @p mtu "
              "minus IP/UDP headers size.");

    mtu_pld_size = mtu - l3_hdr_size - TAD_UDP_HDR_LEN;

    if (mtu_pld_size <= 1)
        TEST_FAIL("MTU is too small");

    TEST_STEP("If @p tx is @c TRUE, sender is IUT and receiver is Tester, "
              "otherwise - the other way around.");
    if (tx)
    {
        sender_rpcs = iut_rpcs;
        sender_s = iut_s;
        receiver_rpcs = tst_rpcs;
        receiver_s = tst_s;

        csap_recv_mode = TAD_ETH_RECV_HOST | TAD_ETH_RECV_NO_PROMISC;
        csap_remote_addr = (const uint8_t *)(iut_lladdr->sa_data);
        csap_local_addr = (const uint8_t *)(tst_lladdr->sa_data);
        csap_remote_ip_addr = iut_addr;
        csap_local_ip_addr = tst_addr;
    }
    else
    {
        sender_rpcs = tst_rpcs;
        sender_s = tst_s;
        receiver_rpcs = iut_rpcs;
        receiver_s = iut_s;

        csap_recv_mode = TAD_ETH_RECV_OUT | TAD_ETH_RECV_NO_PROMISC;
        csap_remote_addr = (const uint8_t *)(tst_lladdr->sa_data);
        csap_local_addr = (const uint8_t *)(iut_lladdr->sa_data);
        csap_remote_ip_addr = tst_addr;
        csap_local_ip_addr = iut_addr;
    }

    if (pkt_size == PKT_SIZE_LESS)
    {
        min_size = 0;
        max_size = mtu_pld_size - 1;
    }
    else if (pkt_size == PKT_SIZE_EQUAL)
    {
        min_size = mtu_pld_size;
        max_size = mtu_pld_size;
    }
    else
    {
        min_size = mtu_pld_size + 1;
        max_size = mtu_pld_size * MAX_LEN_MULTIPLIER;
    }
    if (min_size > max_size)
        TEST_FAIL("Failed to choose constraints on data size correctly");

    send_buf = tapi_malloc(max_size);
    recv_buf = tapi_malloc(max_size);

    TEST_STEP("Create a CSAP on Tester to capture packets sent "
              "from (if @p tx is @c TRUE) or to (if @p tx is @c FALSE) "
              "IUT.");

    CHECK_RC(tapi_ip_eth_csap_create(
        tst_rpcs->ta, 0,
        tst_if->if_name,
        csap_recv_mode,
        csap_local_addr,
        csap_remote_addr,
        csap_remote_ip_addr->sa_family,
        te_sockaddr_get_netaddr(csap_local_ip_addr),
        te_sockaddr_get_netaddr(csap_remote_ip_addr),
        -1, &tst_csap));

    CHECK_RC(tapi_tad_trrecv_start(tst_rpcs->ta, 0, tst_csap,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    memset(&csap_cb_data, 0, sizeof(csap_cb_data));
    memset(&args, 0, sizeof(args));

    csap_cb_data.user_data = &args;

    args.max_pld_size = mtu;

    if (iut_addr->sa_family == AF_INET)
        args.ipv4 = TRUE;
    else
        args.ipv4 = FALSE;

    if (pkt_size == PKT_SIZE_MORE)
    {
        /*
         * In case of IP fragmentation, fragment offset field
         * is specified in 8-bytes units, so all the IP
         * fragments except the last one must have payload
         * length divisible by 8. Therefore they may be
         * a bit smaller than MTU allows.
         */
        args.max_frag_size = ((mtu - l3_hdr_size) / 8) * 8
                             + l3_hdr_size;

        RING("Intermediary IP fragment should have %d bytes",
             args.max_frag_size);
    }

    csap_cb_data.callback = &process_eth;

    RING("Expected maximum payload of a captured packet is %d bytes",
         args.max_pld_size);

    TEST_STEP("In a loop for @p sends_num times:");

    for (i = 0; i < sends_num; i++)
    {
        send_len = rand_range(min_size, max_size);
        te_fill_buf(send_buf, send_len);

        TEST_SUBSTEP("Send some data from sender, choosing its size "
                     "randomly but according to @p pkt_size.");

        RPC_AWAIT_ERROR(sender_rpcs);
        rc = rpc_send(sender_rpcs, sender_s, send_buf, send_len, 0);
        if (rc < 0)
        {
            TEST_VERDICT("Failed to send data from %s, errno is %r",
                         sender_rpcs->name, RPC_ERRNO(sender_rpcs));
        }

        TEST_SUBSTEP("Check that receiver socket becomes readable.");

        RPC_GET_READABILITY(readable, receiver_rpcs, receiver_s,
                            TAPI_WAIT_NETWORK_DELAY);
        if (!readable)
            TEST_VERDICT("Receiving socket did not become readable");

        TEST_SUBSTEP("Receive and check data on the receiver.");

        RPC_AWAIT_ERROR(receiver_rpcs);
        rc = rpc_recv(receiver_rpcs, receiver_s, recv_buf,
                      max_size, 0);
        if (rc < 0)
        {
            TEST_VERDICT("Failed to receive a packet on %s: " RPC_ERROR_FMT,
                         receiver_rpcs->name, RPC_ERROR_ARGS(receiver_rpcs));
        }

        if (rc != send_len || memcmp(send_buf, recv_buf, send_len) != 0)
            TEST_VERDICT("Unexpected data was received");

        TEST_SUBSTEP("Obtain packets captured by CSAP, check that sizes "
                     "of all the packets except the last one correspond to "
                     "@p mtu.");

        args.pkts_num = 0;
        CHECK_RC(tapi_tad_trrecv_get(tst_rpcs->ta, 0, tst_csap,
                                     &csap_cb_data, NULL));
        if (args.failed)
            TEST_FAIL("Failed to process captured packets on Tester");
        else if (args.pkts_num == 0)
            TEST_FAIL("Failed to capture any packets on Tester");

        if (args.too_big_pkt)
            TEST_VERDICT("A bigger-than-MTU packet was captured");

        if (args.first_small_pkt >= 0 &&
            args.first_small_pkt != args.pkts_num)
        {
            TEST_VERDICT("The first smaller-than-MTU packet was not "
                         "the last one");
        }
    }

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    free(send_buf);
    free(recv_buf);

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0, tst_csap));

    TEST_END;
}
