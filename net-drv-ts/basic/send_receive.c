/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-send_receive Sending and receiving data
 * @ingroup basic
 * @{
 *
 * @objective Check that packets can be sent and received over the
 *            tested interface.
 *
 * @param env            Testing environment:
 *                      - @ref env-peer2peer
 *                      - @ref env-peer2peer_ipv6
 * @param sock_type     Socket type:
 *                      - @c SOCK_STREAM
 *                      - @c SOCK_DGRAM
 * @param tx            If @c TRUE, check sending from IUT, otherwise
 *                      check receiving on IUT.
 * @param pkt_size      Packet size:
 *                      - @c 1000
 *                      - @c -1 (random)
 * @param pkts_num      Number of packets to send or receive:
 *                      - @c 100
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-SYS01, X3-SYS02.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "basic/send_receive"

#include "net_drv_test.h"
#include "tapi_cfg_stats.h"

/** Maximum payload size for a packet */
#define MAX_DATA_SIZE 1500

/** How long to wait until statistics are updated, in seconds */
#define STATS_UPDATE_TIME 1

/**
 * Maximum number of extra packets in statistics which may result from
 * things like ARP resolution.
 */
#define MAX_EXTRA_PKTS 5

/** Return value by which interface statistics field changed */
#define GET_STATS_FIELD_DIFF(_stats_before, _stats_after, _tx, _field) \
    do {                                              \
        if (_tx)                                      \
        {                                             \
            return _stats_after->out_ ## _field -     \
                   _stats_before->out_ ## _field;     \
        }                                             \
        else                                          \
        {                                             \
            return _stats_after->in_ ## _field -      \
                   _stats_before->in_ ## _field;      \
        }                                             \
    } while (0)

/** Get change of errors counter */
static int
get_errors_diff(tapi_cfg_if_stats *stats_before,
                tapi_cfg_if_stats *stats_after,
                te_bool tx)
{
    GET_STATS_FIELD_DIFF(stats_before, stats_after, tx, errors);
}

/** Get change of discards counter */
static int
get_discards_diff(tapi_cfg_if_stats *stats_before,
                  tapi_cfg_if_stats *stats_after,
                  te_bool tx)
{
    GET_STATS_FIELD_DIFF(stats_before, stats_after, tx, discards);
}

/** Get change of ucast_pkts counter */
static int
get_ucast_pkts_diff(tapi_cfg_if_stats *stats_before,
                    tapi_cfg_if_stats *stats_after,
                    te_bool tx)
{
    GET_STATS_FIELD_DIFF(stats_before, stats_after, tx, ucast_pkts);
}

/** Check that statistics field remained constant */
#define CHECK_CONST_COUNTER(_stats_before, _stats_after, _tx, _name) \
    do {                                                    \
        int _diff = get_ ## _name ## _diff(_stats_before,   \
                                           _stats_after,    \
                                           _tx);            \
        if (_diff > 0)                                      \
            ERROR_VERDICT(#_name " counter increased");     \
        else if (_diff < 0)                                 \
            ERROR_VERDICT(#_name " counter decreased");     \
    } while (0)

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

    rcf_rpc_server *sender_rpcs = NULL;
    rcf_rpc_server *receiver_rpcs = NULL;
    int sender_s = -1;
    int receiver_s = -1;

    int i;
    char send_buf[MAX_DATA_SIZE];
    char recv_buf[MAX_DATA_SIZE];
    int send_len;
    int max_size;
    te_bool readable;

    rpc_socket_type sock_type;
    te_bool tx;
    int pkt_size;
    int pkts_num;

    tapi_cfg_if_stats stats_before;
    tapi_cfg_if_stats stats_after;
    int new_packets;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_SOCK_TYPE(sock_type);
    TEST_GET_BOOL_PARAM(tx);
    TEST_GET_INT_PARAM(pkt_size);
    TEST_GET_INT_PARAM(pkts_num);

    TEST_STEP("Establish connection between a pair of sockets of "
              "type @p sock_type on IUT and Tester.");

    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("If @p tx is @c TRUE, sender is IUT and receiver is Tester, "
              "otherwise - the other way around.");
    if (tx)
    {
        sender_rpcs = iut_rpcs;
        sender_s = iut_s;
        receiver_rpcs = tst_rpcs;
        receiver_s = tst_s;
    }
    else
    {
        sender_rpcs = tst_rpcs;
        sender_s = tst_s;
        receiver_rpcs = iut_rpcs;
        receiver_s = iut_s;
    }

    NET_DRV_WAIT_IF_STATS_UPDATE;
    CHECK_RC(cfg_tree_print(NULL, TE_LL_RING,
                            "/agent:%s/interface:%s/stats:",
                            iut_rpcs->ta, iut_if->if_name));
    CHECK_RC(tapi_cfg_stats_if_stats_get(iut_rpcs->ta, iut_if->if_name,
                                         &stats_before));

    if (sock_type == RPC_SOCK_STREAM)
    {
        TEST_STEP("If @c TCP is checked, enable @c TCP_NODELAY on "
                  "sender socket to ensure that a packet is sent as soon "
                  "as possible, even when it is small.");
        rpc_setsockopt_int(sender_rpcs, sender_s, RPC_TCP_NODELAY, 1);

        rpc_getsockopt(sender_rpcs, sender_s, RPC_TCP_MAXSEG, &max_size);
    }
    else
    {
        rpc_getsockopt(sender_rpcs, sender_s, RPC_IP_MTU, &max_size);

        if (iut_addr->sa_family == AF_INET)
            max_size -= TAD_IP4_HDR_LEN;
        else
            max_size -= TAD_IP6_HDR_LEN;

        max_size -= TAD_UDP_HDR_LEN;

        if (max_size < 0)
            TEST_FAIL("Reported MTU is too small");
    }

    max_size = MIN(max_size, MAX_DATA_SIZE);

    TEST_STEP("In a loop, send @p pkts_num packets with sizes chosen "
              "according to @p pkt_size. Receive and check them on peer.");

    for (i = 0; i < pkts_num; i++)
    {
        if (pkt_size > 0)
            send_len = pkt_size;
        else
            send_len = rand_range(1, max_size);

        te_fill_buf(send_buf, send_len);

        RPC_AWAIT_ERROR(sender_rpcs);
        rc = rpc_send(sender_rpcs, sender_s, send_buf, send_len, 0);
        if (rc < 0)
        {
            TEST_VERDICT("Failed to send a packet from %s, errno is %r",
                         sender_rpcs->name, RPC_ERRNO(sender_rpcs));
        }

        RPC_GET_READABILITY(readable, receiver_rpcs, receiver_s,
                            TAPI_WAIT_NETWORK_DELAY);
        if (!readable)
            TEST_VERDICT("Receiving socket did not become readable");

        RPC_AWAIT_ERROR(receiver_rpcs);
        rc = rpc_recv(receiver_rpcs, receiver_s, recv_buf,
                      sizeof(recv_buf), 0);
        if (rc < 0)
        {
            TEST_VERDICT("Failed to receive a packet on %s, errno is %r",
                         receiver_rpcs->name, RPC_ERRNO(receiver_rpcs));
        }

        if (rc != send_len ||
            memcmp(send_buf, recv_buf, send_len) != 0)
        {
            TEST_VERDICT("Unexpected data was received");
        }
    }

    TEST_STEP("Check that interface statistics were updated expectedly, "
              "there were no errors or discarded packets.");

    NET_DRV_WAIT_IF_STATS_UPDATE;
    CHECK_RC(cfg_tree_print(NULL, TE_LL_RING,
                            "/agent:%s/interface:%s/stats:",
                            iut_rpcs->ta, iut_if->if_name));
    CHECK_RC(tapi_cfg_stats_if_stats_get(iut_rpcs->ta, iut_if->if_name,
                                         &stats_after));

    CHECK_CONST_COUNTER(&stats_before, &stats_after, tx, errors);
    CHECK_CONST_COUNTER(&stats_before, &stats_after, tx, discards);

    new_packets = get_ucast_pkts_diff(&stats_before, &stats_after, tx);
    RING("%d packets were sent, and ucast_pkts counter increased "
         "by %d", pkts_num, new_packets);
    if (new_packets < 0)
    {
        ERROR_VERDICT("ucast_pkts counter decreased");
    }
    if (new_packets < pkts_num)
    {
        ERROR_VERDICT("ucast_pkts counter was updated by too "
                      "small number");
    }
    else if (new_packets > pkts_num + MAX_EXTRA_PKTS)
    {
        ERROR_VERDICT("ucast_pkts counter was updated by too "
                      "big number");
    }

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    TEST_END;
}
