/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Rx path tests
 */

/**
 * @defgroup rx_path-rx_coalesce_usecs Setting rx_coalesce_usecs to a specific value
 * @ingroup rx_path
 * @{
 *
 * @objective Check that when @b rx_coalesce_usecs is set to a specific value,
 *            Rx interrupts coalescing works accordingly.
 *
 * @param env             Testing environment:
 *                        - @ref env-peer2peer
 *                        - @ref env-peer2peer_ipv6
 * @param coalesce_usecs  Value to set for @b rx_coalesce_usecs:
 *                        - @c 0 (interrupt moderation is disabled)
 *                        - @c 300
 *                        - @c 800
 *
 * @par Scenario:
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "rx_path/rx_coalesce_usecs"

#include "net_drv_test.h"
#include "tapi_eth.h"
#include "tapi_cfg_if_coalesce.h"

#include <math.h>

/**
 * Minimum delay between packets after which the next packet
 * is considered to be from another packets group, microseconds.
 */
#define MIN_GROUP_DELAY 100

/**
 * Minimum nonzero value of rx_coalesce_usecs which this test can
 * check reliably.
 */
#define MIN_NONZERO_USECS (MIN_GROUP_DELAY * 3)

/**
 * Maximum relative deviation of actual packet distrubition
 * parameter value from expected one.
 */
#define ERR_MARGIN 0.10

/**
 * Maximum share of unexpectedly large delays between packets when
 * rx_coalesce_usecs is zero.
 */
#define MAX_UNEXP_DELAYS 0.005

/**
 * Delay after sending a packet from Tester, in microseconds.
 */
#define PKT_SEND_DELAY 10

/** Maximum packet size */
#define MAX_PKT_SIZE 1024

/** How long to send packets from Tester, in seconds */
#define SEND_TIME 2

/** Data passed to and from CSAP callback */
typedef struct test_cb_args {
    unsigned int coalesce_usecs; /**< Value of rx_coalesce_usecs */

    double first_ts;      /**< Timestamp of the first packet, in us */
    double prev_ts;       /**< Timestamp of the previous packet, in us */
    double cur_group_ts;  /**< Timestamp of the first packet in
                               the current packet group, in us */

    unsigned int n_pkts;      /**< Number of captured packets */
    unsigned int n_groups;    /**< Number of packet groups */
    double squared_dev_sum;   /**< Sum of squared deviations of
                                   actual packet group durations
                                   from rx_coalesce_usecs */

    double avg_group_time;    /**< Average duration of a packet
                                   group */
    double group_time_dev;    /**< Square root of average squared
                                   deviation of packet group
                                   duration from rx_coalesce_usecs */

    te_bool failed;           /**< Will be set to TRUE if packet
                                   processing failed */
} test_cb_args;

/**
 * Finalize the current packet group, update related
 * statistics.
 */
static void
finish_pkt_group(test_cb_args *args, double ts)
{
    double group_duration;
    double duration_deviation;

    args->n_groups++;

    group_duration = ts - args->cur_group_ts;
    duration_deviation = group_duration - args->coalesce_usecs;
    args->squared_dev_sum += duration_deviation * duration_deviation;

    args->cur_group_ts = ts;
}

/** CSAP callback for processing captured packets */
static void
process_pkts(asn_value *pkt, void *user_data)
{
    uint32_t secs = 0;
    uint32_t usecs = 0;
    double ts;
    te_errno rc;

    test_cb_args *args = (test_cb_args *)user_data;

    args->n_pkts++;

    rc = asn_read_uint32(pkt, &secs, "received.seconds");
    if (rc != 0)
    {
        ERROR("%s(): failed to get seconds from CSAP packet: %r",
              __FUNCTION__, rc);
        args->failed = TRUE;
        goto cleanup;
    }

    rc = asn_read_uint32(pkt, &usecs, "received.micro-seconds");
    if (rc != 0)
    {
        ERROR("%s(): failed to get microseconds from CSAP packet: %r",
              __FUNCTION__, rc);
        args->failed = TRUE;
        goto cleanup;
    }

    ts = secs * 1000000.0 + usecs;

    if (args->prev_ts < 0)
    {
        args->prev_ts = ts;
        args->first_ts = ts;
        args->cur_group_ts = ts;
    }
    else
    {
        if (ts - args->prev_ts > MIN_GROUP_DELAY)
            finish_pkt_group(args, ts);

        args->prev_ts = ts;
    }

cleanup:

    asn_free_value(pkt);
}

/**
 * Finalize computation of packet statistics after CSAP captured
 * the last packet.
 */
static void
finalize_stats(test_cb_args *args)
{
    if (args->prev_ts - args->cur_group_ts > MIN_GROUP_DELAY)
        finish_pkt_group(args, args->prev_ts);

    if (args->n_groups == 0)
    {
        TEST_FAIL("Too few packets were captured to compute "
                  "statistics");
    }

    args->avg_group_time =
                  (args->prev_ts - args->first_ts) / args->n_groups;
    args->group_time_dev = sqrt(args->squared_dev_sum / args->n_groups);

    RING("Number of packet groups: %u\n"
         "Average group time: %.3f microseconds\n"
         "Group time deviation: %.3f",
         args->n_groups, args->avg_group_time, args->group_time_dev);
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;

    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;
    const struct sockaddr *iut_lladdr = NULL;
    const struct sockaddr *tst_lladdr = NULL;

    int iut_s = -1;
    int tst_s = -1;

    int coalesce_usecs;

    tapi_pat_sender send_ctx;
    tapi_pat_receiver recv_ctx;
    tarpc_pat_gen_arg *pat_arg = NULL;

    csap_handle_t csap_rx = CSAP_INVALID_HANDLE;
    tapi_tad_trrecv_cb_data csap_cb_data;
    test_cb_args cb_args;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_LINK_ADDR(iut_lladdr);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_INT_PARAM(coalesce_usecs);

    if (coalesce_usecs > 0 &&
        coalesce_usecs < MIN_NONZERO_USECS)
    {
        TEST_FAIL("Too small value of coalesce_usecs is requested for "
                  "this test to work");
    }

    TEST_STEP("Establish connection between a pair of UDP sockets "
              "on IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, RPC_SOCK_DGRAM, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Disable @b use_adaptive_rx_coalesce and set "
              "@b rx_coalesce_usecs to the value of @p coalesce_usecs "
              "for the IUT interface.");

    rc = tapi_cfg_if_coalesce_set(iut_rpcs->ta, iut_if->if_name,
                                  "use_adaptive_rx_coalesce", 0);
    if (rc != 0)
        TEST_VERDICT("Failed to set use_adaptive_rx_coalesce, rc=%r", rc);

    rc = tapi_cfg_if_coalesce_set(iut_rpcs->ta, iut_if->if_name,
                                  "rx_coalesce_usecs",
                                  coalesce_usecs);
    if (TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
        TEST_SKIP("rx_coalesce_usecs=%u is not supported", coalesce_usecs);
    else if (rc != 0)
        TEST_VERDICT("Failed to set rx_coalesce_usecs, rc=%r", rc);

    TEST_STEP("Create a CSAP on IUT to capture packets received from "
              "Tester.");

    CHECK_RC(tapi_eth_csap_create(
        iut_rpcs->ta, 0,
        iut_if->if_name,
        TAD_ETH_RECV_DEF |
        TAD_ETH_RECV_NO_PROMISC,
        (uint8_t *)(tst_lladdr->sa_data),
        (uint8_t *)(iut_lladdr->sa_data), NULL,
        &csap_rx));

    CHECK_RC(tapi_tad_trrecv_start(iut_rpcs->ta, 0, csap_rx,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    tapi_pat_sender_init(&send_ctx);
    send_ctx.time2wait = TAPI_WAIT_NETWORK_DELAY;
    send_ctx.duration_sec = SEND_TIME;

    tapi_rand_gen_set(&send_ctx.size, 1, MAX_PKT_SIZE, FALSE);
    tapi_rand_gen_set(&send_ctx.delay, PKT_SEND_DELAY, PKT_SEND_DELAY,
                      TRUE);

    send_ctx.gen_func = RPC_PATTERN_GEN_LCG;
    pat_arg = &send_ctx.gen_arg;
    pat_arg->offset = 0;
    pat_arg->coef1 = rand_range(0, RAND_MAX);
    pat_arg->coef2 = rand_range(1, RAND_MAX);
    pat_arg->coef3 = rand_range(0, RAND_MAX);

    tapi_pat_receiver_init(&recv_ctx);
    recv_ctx.time2wait = TAPI_WAIT_NETWORK_DELAY;
    recv_ctx.duration_sec = SEND_TIME + 1;
    recv_ctx.gen_func = RPC_PATTERN_GEN_LCG;
    memcpy(&recv_ctx.gen_arg, pat_arg, sizeof(*pat_arg));

    TEST_STEP("For some time send UDP packets from Tester to IUT, "
              "waiting a little after sending every packet. Receive "
              "and check data on IUT.");

    iut_rpcs->op = RCF_RPC_CALL;
    rpc_pattern_receiver(iut_rpcs, iut_s, &recv_ctx);

    RPC_AWAIT_ERROR(tst_rpcs);
    rc = rpc_pattern_sender(tst_rpcs, tst_s, &send_ctx);
    if (rc < 0)
    {
        ERROR_VERDICT("rpc_pattern_sender() failed with unexpected error "
                      RPC_ERROR_FMT, RPC_ERROR_ARGS(tst_rpcs));
        rpc_pattern_receiver(iut_rpcs, iut_s, &recv_ctx);
        TEST_STOP;
    }

    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_pattern_receiver(iut_rpcs, iut_s, &recv_ctx);
    if (rc < 0)
    {
        TEST_VERDICT("rpc_pattern_receiver() failed with unexpected error "
                      RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }
    else if (send_ctx.sent != recv_ctx.received)
    {
        TEST_VERDICT("Number of bytes received does not match "
                     "number of bytes sent");
    }

    rcf_tr_op_log(FALSE);
    memset(&csap_cb_data, 0, sizeof(csap_cb_data));
    csap_cb_data.callback = &process_pkts;
    csap_cb_data.user_data = &cb_args;

    memset(&cb_args, 0, sizeof(cb_args));
    cb_args.prev_ts = -1.0;
    cb_args.coalesce_usecs = coalesce_usecs;

    TEST_STEP("Investigate timestamps of packets captured by CSAP on IUT. "
              "If @p coalesce_usecs is @c 0, there should be only small "
              "delays after every packet. If @p coalesce_usecs is "
              "not zero, packets should be received in groups separated "
              "by larger delays.");

    CHECK_RC(tapi_tad_trrecv_stop(iut_rpcs->ta, 0, csap_rx,
                                  &csap_cb_data, NULL));
    if (cb_args.n_pkts == 0)
        TEST_VERDICT("No packets were captured by IUT CSAP");
    if (cb_args.failed)
        TEST_FAIL("Failed to process some of the captured packets");

    finalize_stats(&cb_args);
    if (coalesce_usecs > 0)
    {
        if (fabs(cb_args.avg_group_time - coalesce_usecs) / coalesce_usecs >
                                                          ERR_MARGIN ||
            fabs(cb_args.group_time_dev / coalesce_usecs) > ERR_MARGIN)
        {
            TEST_VERDICT("Actual timestamps of received packets differ "
                         "too much from the expected distribution");
        }
    }
    else
    {
        if ((double)(cb_args.n_groups) / cb_args.n_pkts > MAX_UNEXP_DELAYS)
            TEST_VERDICT("Too many big delays between packets");
    }

    TEST_SUCCESS;

cleanup:

    if (csap_rx != CSAP_INVALID_HANDLE)
    {
        CLEANUP_CHECK_RC(tapi_tad_csap_destroy(iut_rpcs->ta, 0,
                                               csap_rx));
    }

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    TEST_END;
}
