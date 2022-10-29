/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-multicast Sending and receiving multicast packets
 * @ingroup basic
 * @{
 *
 * @objective Check that multicast packets may be sent and received
 *            over the tested interface.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer_mcast
 *                      - @ref env-peer2peer_mcast_ipv6
 * @param tx            If @c TRUE, send from IUT to Tester,
 *                      else - the other way around.
 * @param sends_num     How many packets to send:
 *                      - @c 100
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-SYS05.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME  "basic/multicast"

#include "net_drv_test.h"

/*
 * Allowed number of lost packets to pass with verdict.
 *
 * Running on Intel X710 (intel/i40e driver) requires up to 1 second
 * for multicast traffic to work after join.
 */
#define TEST_ALLOWED_PKT_LOSS   TE_DIV_ROUND_UP(1000, TAPI_WAIT_NETWORK_DELAY)

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;

    const struct if_nameindex *iut_if = NULL;
    const struct if_nameindex *tst_if = NULL;

    const struct sockaddr *mcast_addr = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;

    rcf_rpc_server *sender_rpcs = NULL;
    rcf_rpc_server *receiver_rpcs = NULL;
    const struct sockaddr *sender_addr = NULL;
    struct sockaddr_storage mcast_addr_port;
    int sender_s = -1;
    int receiver_s = -1;
    int sender_if_index = -1;
    int receiver_if_index = -1;

    rpc_socket_domain domain;
    int i;

    te_bool tx;
    int sends_num;
    size_t len;
    size_t total_len = 0;
    unsigned int lost = 0;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_ADDR_NO_PORT(mcast_addr);
    TEST_GET_BOOL_PARAM(tx);
    TEST_GET_INT_PARAM(sends_num);

    TEST_STEP("If @p tx is @c TRUE, IUT is sender and Tester is "
              "receiver, otherwise - the other way around.");
    if (tx)
    {
        sender_rpcs = iut_rpcs;
        sender_addr = iut_addr;
        sender_if_index = iut_if->if_index;
        receiver_rpcs = tst_rpcs;
        receiver_if_index = tst_if->if_index;
    }
    else
    {
        sender_rpcs = tst_rpcs;
        sender_addr = tst_addr;
        sender_if_index = tst_if->if_index;
        receiver_rpcs = iut_rpcs;
        receiver_if_index = iut_if->if_index;
    }

    domain = rpc_socket_domain_by_addr(iut_addr);

    TEST_STEP("Create UDP socket on sender, bind it to a unicast address.");
    sender_s = rpc_socket(sender_rpcs, domain, RPC_SOCK_DGRAM,
                          RPC_PROTO_DEF);
    rpc_bind(sender_rpcs, sender_s, sender_addr);

    TEST_STEP("Create UDP socket on receiver, bind it to the multicast "
              "address @p mcast_addr.");
    receiver_s = rpc_socket(receiver_rpcs, domain, RPC_SOCK_DGRAM,
                            RPC_PROTO_DEF);
    CHECK_RC(tapi_sockaddr_clone(receiver_rpcs, mcast_addr,
                                 &mcast_addr_port));
    mcast_addr = SA(&mcast_addr_port);
    rpc_bind(receiver_rpcs, receiver_s, mcast_addr);

    TEST_STEP("With help of @c IP_MULTICAST_IF or @c IPV6_MULTICAST_IF "
              "set an outgoing interface for multicast packets for "
              "the sender socket.");
    if (mcast_addr->sa_family == AF_INET)
    {
        struct tarpc_mreqn mreq;

        memset(&mreq, 0, sizeof(mreq));
        mreq.type = OPT_MREQN;
        mreq.ifindex = sender_if_index;

        rpc_setsockopt(sender_rpcs, sender_s, RPC_IP_MULTICAST_IF, &mreq);
    }
    else
    {
        rpc_setsockopt_int(sender_rpcs, sender_s, RPC_IPV6_MULTICAST_IF,
                           sender_if_index);
    }

    TEST_STEP("Join the receiver socket to the multicast group of "
              "@p mcast_addr.");
    rpc_mcast_join(receiver_rpcs, receiver_s, mcast_addr,
                   receiver_if_index, TARPC_MCAST_JOIN_LEAVE);

    TEST_STEP("For @p sends_num times, send a packet from the sender "
              "socket to @p mcast_addr, receive and check it on "
              "the receiver.");
    for (total_len = 0, lost = 0, i = 0; i < sends_num; i++, total_len += len)
    {
        len = net_drv_sendto_recv_check_may_loss(sender_rpcs, sender_s,
                                                 mcast_addr,
                                                 receiver_rpcs, receiver_s,
                                                 "Checking multicast sending");
        if (len == 0)
        {
            lost++;
            /* Do not allow loss after successfully send/received packet */
            if (total_len != 0)
                TEST_VERDICT("Sent multicast packet is not received");
        }
    }
    if (lost > 0)
    {
        if (total_len > 0 && lost <= TEST_ALLOWED_PKT_LOSS)
            WARN("Few leading multicast packets are not received");
        else
            TEST_VERDICT("Too many leading multicast packets are not received");
    }

    TEST_SUCCESS;

cleanup:

    if (receiver_rpcs != NULL)
        CLEANUP_RPC_CLOSE(receiver_rpcs, receiver_s);
    if (sender_rpcs != NULL)
        CLEANUP_RPC_CLOSE(sender_rpcs, sender_s);

    TEST_END;
}
