/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-mtu_tcp MTU usage for TCP packets
 * @ingroup basic
 * @{
 *
 * @objective Check that TCP packets of full MTU size are sent and
 *            processed correctly.
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
 * @param gso_on        Should Generic Segmentation Offload be enabled on IUT?
 * @param tso_on        Should TCP Segmentation Offload be enabled on IUT?
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

#define TE_TEST_NAME  "basic/mtu_tcp"

#include "net_drv_test.h"
#include "tapi_cfg_base.h"
#include "tapi_mem.h"
#include "tapi_eth.h"
#include "tapi_tcp.h"

/** Maximum allowed size of headers with options for TCP */
#define MAX_MAXSEG_DIFF 200

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

    int max_pld_size; /**< Maximum payload size */

    /* Output arguments */

    int pkts_num; /**< Number of captured packets */

    int64_t first_small_pkt_seqn; /**< SEQN of the first small packet,
                                       negative if there is no such
                                       packet */
    int64_t last_seqn; /**< SEQN of the last packet */

    te_bool too_big_pkt; /**< Set to TRUE if too big packet is captured */
    te_bool failed; /**< Set to TRUE if some error occurred when processing
                         packets */
} cb_args;

/** CSAP callback for TCP packets */
static void
process_tcp(asn_value *pkt, void *arg)
{
    cb_args *args = (cb_args *)arg;

    unsigned int pld_len;
    uint32_t seqn;
    te_errno rc;

    if (args->pkts_num == 0)
        args->first_small_pkt_seqn = -1;

    args->pkts_num++;

    rc = tapi_tcp_get_hdrs_payload_len(pkt, NULL, &pld_len);
    if (rc != 0)
    {
        ERROR("Failed to get packet payload length, rc=%r", rc);
        args->failed = TRUE;
        goto cleanup;
    }

    rc = asn_read_uint32(pkt, &seqn, "pdus.0.#tcp.seqn");
    if (rc != 0)
    {
        ERROR("Failed to get SEQN: %r", rc);
        args->failed = TRUE;
        goto cleanup;
    }

    RING("TCP packet with %u bytes of payload with SEQN=%u was captured",
         pld_len, seqn);

    if (args->pkts_num == 1 ||
        tapi_tcp_compare_seqn(args->last_seqn, seqn) < 0)
    {
        args->last_seqn = seqn;
    }

    if ((int)pld_len < args->max_pld_size)
    {
        if (args->first_small_pkt_seqn < 0 ||
            tapi_tcp_compare_seqn(seqn, args->first_small_pkt_seqn) < 0)
        {
            args->first_small_pkt_seqn = seqn;
        }
    }
    else if ((int)pld_len > args->max_pld_size)
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
    te_bool tso_on;
    te_bool gso_on;
    int mtu;
    int pkt_size;
    int sends_num;

    int tcp_maxseg;
    int mtu_pld_size;
    int min_size;
    int max_size;

    char *send_buf = NULL;
    te_dbuf recv_dbuf = TE_DBUF_INIT(0);
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
    TEST_GET_BOOL_PARAM(tso_on);
    TEST_GET_BOOL_PARAM(gso_on);
    TEST_GET_INT_PARAM(mtu);
    TEST_GET_ENUM_PARAM(pkt_size, PKT_SIZES);
    TEST_GET_INT_PARAM(sends_num);

    TEST_STEP("Set MTU on IUT and Tester interfaces to @p mtu.");
    net_drv_set_mtu(iut_rpcs->ta, iut_if->if_name, mtu, "IUT");
    net_drv_set_mtu(tst_rpcs->ta, tst_if->if_name, mtu, "Tester");

    if (tx)
    {
        TEST_STEP("If @p tx is @c TRUE, make sure that receive "
                  "offloads are disabled on Tester interface, "
                  "so that CSAP there will receive IUT packets "
                  "not modified in any way.");
        net_drv_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                               "rx-lro", 0);
        net_drv_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                               "rx-gro", 0);
    }
    else
    {
        TEST_STEP("If @p tx is @c FALSE, make sure that TCP "
                  "segmentation offload is disabled on Tester "
                  "interface, so that CSAP sees packets "
                  "there exactly as they are sent.");
        if (iut_addr->sa_family == AF_INET)
        {
            net_drv_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                                   "tx-tcp-segmentation", 0);
        }
        else
        {
            net_drv_set_if_feature(tst_rpcs->ta, tst_if->if_name,
                                   "tx-tcp6-segmentation", 0);
        }
    }

    TEST_STEP("Enable or disable Generic Segmentation Offload on IUT "
              "according to @p gso_on.");
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           "tx-generic-segmentation",
                           (gso_on ? 1 : 0));

    TEST_STEP("Enable or disable TCP Segmentation Offload on IUT "
              "according to @p tso_on.");
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           (iut_addr->sa_family == AF_INET ?
                            "tx-tcp-segmentation" :
                            "tx-tcp6-segmentation"),
                           (tso_on ? 1 : 0));

    CFG_WAIT_CHANGES;

    TEST_STEP("Create a pair of connected TCP sockets on IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_STREAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Get payload size in MTU-sized packet from @c TCP_MAXSEG "
              "option value on IUT socket.");

    rpc_getsockopt(iut_rpcs, iut_s, RPC_TCP_MAXSEG, &tcp_maxseg);
    if (tcp_maxseg > mtu)
        TEST_VERDICT("Reported TCP_MAXSEG is too big");
    else if (tcp_maxseg < mtu - MAX_MAXSEG_DIFF)
        TEST_VERDICT("Reported TCP_MAXSEG is too small");

    mtu_pld_size = tcp_maxseg;

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

    TEST_STEP("On the sender socket enable @c TCP_NODELAY option so that "
              "packets are sent as soon as possible even if they are "
              "small.");
    rpc_setsockopt_int(sender_rpcs, sender_s, RPC_TCP_NODELAY, 1);

    if (pkt_size == PKT_SIZE_LESS)
    {
        min_size = 1;
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
    CHECK_RC(te_dbuf_expand(&recv_dbuf, max_size));

    TEST_STEP("Create a CSAP on Tester to capture packets sent "
              "from (if @p tx is @c TRUE) or to (if @p tx is @c FALSE) "
              "IUT.");

    CHECK_RC(tapi_tcp_ip_eth_csap_create(
        tst_rpcs->ta, 0,
        tst_if->if_name,
        csap_recv_mode,
        csap_local_addr, csap_remote_addr,
        iut_addr->sa_family,
        TAD_SA2ARGS(csap_local_ip_addr,
                    csap_remote_ip_addr),
        &tst_csap));

    CHECK_RC(tapi_tad_trrecv_start(tst_rpcs->ta, 0, tst_csap,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    memset(&csap_cb_data, 0, sizeof(csap_cb_data));
    memset(&args, 0, sizeof(args));

    csap_cb_data.user_data = &args;
    args.max_pld_size = tcp_maxseg;
    csap_cb_data.callback = &process_tcp;

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
        rc = rpc_read_fd2te_dbuf(receiver_rpcs, receiver_s,
                                 TAPI_WAIT_NETWORK_DELAY,
                                 send_len, &recv_dbuf);
        if (rc < 0)
        {
            TEST_VERDICT("Failed to receive a packet on %s: " RPC_ERROR_FMT,
                         receiver_rpcs->name, RPC_ERROR_ARGS(receiver_rpcs));
        }

        if (recv_dbuf.len != (size_t)send_len ||
            memcmp(send_buf, recv_dbuf.ptr, send_len) != 0)
        {
            TEST_VERDICT("Unexpected data was received");
        }

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

        if (args.first_small_pkt_seqn >= 0 &&
            args.first_small_pkt_seqn != args.last_seqn)
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
    te_dbuf_free(&recv_dbuf);

    CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0, tst_csap));

    TEST_END;
}
