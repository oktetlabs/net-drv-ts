/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * RX path tests
 */

/**
 * @defgroup rx_path-rx_fcs FCS forwarding
 * @ingroup rx_path
 * @{
 *
 * @objective Check that FCS forwarding can be enabled or disabled.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 *                      - @ref env-peer2peer_ipv6
 * @param sock_type     Socket type:
 *                      - @c SOCK_STREAM
 *                      - @c SOCK_DGRAM
 * @param rx_fcs        If @c TRUE, rx-fcs feature is enabled, else -
 *                      disabled
 * @param small_frame   If @c TRUE, send so small Ethernet frame that
 *                      padding will be inserted in it.
 *
 * @par Scenario:
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "rx_path/rx_fcs"

#include "net_drv_test.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_if.h"
#include "tapi_cfg_sys.h"
#include "tapi_eth.h"
#include "te_vector.h"
#include "tad_common.h"

/* This is needed for crc32() function */
#include <zlib.h>

/** Length of Ethernet address */
#define ETH_ADDR_LEN 6
/** Offset of destination address in Ethernet frame */
#define ETH_DST_ADDR_OFF 0
/** Offset of source address in Ethernet frame */
#define ETH_SRC_ADDR_OFF 6
/** Offset of length/type in Ethernet frame */
#define ETH_LEN_TYPE_OFF 12
/** Offset of payload in Ethernet frame */
#define ETH_DATA_OFF 14
/** Length of Frame Check Sequence in bytes */
#define ETH_FCS_LEN 4
/** Minimum length of payload in Ethernet frame */
#define MIN_ETH_PAYLOAD_LEN 46

/** Maximum length of TCP or UDP payload */
#define MAX_PKT_LEN 1024

/** Data related to incoming packet */
typedef struct pkt_in_data {
    int payload_len;        /**< Payload length */
    uint32_t computed_fcs;  /**< Computed FCS */
    uint32_t pkt_fcs;       /**< Last four bytes of payload */
} pkt_in_data;

/** Structure passed to CSAP callback */
typedef struct pkts_info {
    int pkts_num;   /**< Number of captured packets */

    te_bool expect_fcs; /**< If TRUE, FCS should be present */

    te_vec *pkts;   /**< Where to save information about
                         captured packets */

    te_bool failed; /**< Will be set to TRUE if packets processing
                         failed */
} pkts_info;

/** CSAP callback for processing outgoing packets on Tester */
static void
process_out(asn_value *pkt, void *arg)
{
    te_errno rc;
    pkts_info *info = (pkts_info *)arg;
    int pld_len;

    pld_len = asn_get_length(pkt, "payload.#bytes");
    if (pld_len < 0)
    {
        ERROR("%s(): failed to get length of payload.#bytes",
              __FUNCTION__);
        info->failed = TRUE;
        goto cleanup;
    }

    rc = TE_VEC_APPEND(info->pkts, pld_len);
    if (rc != 0)
    {
        ERROR("%s(): failed to add payload length to the array",
              __FUNCTION__);
        info->failed = TRUE;
        goto cleanup;
    }

    RING("Captured outgoing packet with payload of %d bytes", pld_len);

    info->pkts_num++;

cleanup:

    asn_free_value(pkt);
}

/** CSAP callback for processing incoming packets on IUT */
static void
process_in(asn_value *pkt, void *arg)
{
    te_errno rc;
    int pld_len;
    int pkt_len;
    size_t field_len;
    uint32_t length_type;
    uint32_t pkt_fcs = 0;
    uint32_t computed_fcs = 0;
    uint8_t *buf = NULL;
    pkt_in_data data;

    pkts_info *info = (pkts_info *)arg;

    pld_len = asn_get_length(pkt, "payload.#bytes");
    if (pld_len < 0)
    {
        ERROR("%s(): failed to get length of payload.#bytes",
              __FUNCTION__);
        info->failed = TRUE;
        goto cleanup;
    }

    RING("Captured incoming packet with payload of %d bytes", pld_len);

    if (info->expect_fcs)
    {
        pkt_len = pld_len - ETH_FCS_LEN + ETH_DATA_OFF;

        buf = TE_ALLOC(pld_len + ETH_DATA_OFF);
        if (buf == NULL)
        {
            ERROR("%s(): out of memory", __FUNCTION__);
            info->failed = TRUE;
            goto cleanup;
        }

        field_len = pld_len;
        rc = asn_read_value_field(pkt, buf + ETH_DATA_OFF, &field_len,
                                  "payload.#bytes");
        if (rc != 0)
        {
            ERROR("%s(): failed to read payload.#bytes, rc=%r",
                  __FUNCTION__, rc);
            info->failed = TRUE;
            goto cleanup;
        }

        field_len = ETH_ADDR_LEN;
        rc = asn_read_value_field(pkt, buf + ETH_DST_ADDR_OFF, &field_len,
                                  "pdus.0.#eth.dst-addr.plain");
        if (rc != 0)
        {
            ERROR("%s(): failed to read dst-addr, rc=%r",
                  __FUNCTION__, rc);
            info->failed = TRUE;
            goto cleanup;
        }

        field_len = ETH_ADDR_LEN;
        rc = asn_read_value_field(pkt, buf + ETH_SRC_ADDR_OFF, &field_len,
                                  "pdus.0.#eth.src-addr.plain");
        if (rc != 0)
        {
            ERROR("%s(): failed to read src-addr, rc=%r",
                  __FUNCTION__, rc);
            info->failed = TRUE;
            goto cleanup;
        }

        rc = asn_read_uint32(pkt, &length_type,
                             "pdus.0.#eth.length-type.#plain");
        if (rc != 0)
        {
            ERROR("%s(): failed to read length-type, rc=%r",
                  __FUNCTION__, rc);
            info->failed = TRUE;
            goto cleanup;
        }

        *(uint16_t *)(buf + ETH_LEN_TYPE_OFF) = htons(length_type);

        pkt_fcs = *(uint32_t *)(buf + pkt_len);

        computed_fcs = crc32(0L, buf, pkt_len);

        RING("Obtained FCS 0x%x, computed FCS 0x%x", pkt_fcs, computed_fcs);
    }

    data.payload_len = pld_len;
    data.computed_fcs = computed_fcs;
    data.pkt_fcs = pkt_fcs;

    rc = TE_VEC_APPEND(info->pkts, data);
    if (rc != 0)
    {
        ERROR("%s(): failed to append packet data to the array, rc=%r",
              __FUNCTION__, rc);
        info->failed = TRUE;
        goto cleanup;
    }

    info->pkts_num++;

cleanup:

    free(buf);
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

    te_vec out_pkts = TE_VEC_INIT(int);
    te_vec in_pkts = TE_VEC_INIT(pkt_in_data);
    int out_pkts_num;
    int in_pkts_num;
    int i;

    csap_handle_t csap_rx = CSAP_INVALID_HANDLE;
    csap_handle_t csap_tx = CSAP_INVALID_HANDLE;
    tapi_tad_trrecv_cb_data csap_cb_data;
    pkts_info pkts_data;

    int pkt_out_len;
    pkt_in_data *pkt_in;
    int padded_pkt_len;

    rpc_socket_type sock_type;
    te_bool rx_fcs;
    te_bool small_frame;

    int hdrs_len;
    int min_len;
    int max_len;
    int send_len;
    char send_buf[MAX_PKT_LEN];
    char recv_buf[MAX_PKT_LEN];

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_IF(tst_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_LINK_ADDR(iut_lladdr);
    TEST_GET_LINK_ADDR(tst_lladdr);
    TEST_GET_SOCK_TYPE(sock_type);
    TEST_GET_BOOL_PARAM(rx_fcs);
    TEST_GET_BOOL_PARAM(small_frame);

    TEST_STEP("Enable or disable @b rx-fcs feature on the IUT interface "
              "according to @p rx_fcs.");
    net_drv_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                           "rx-fcs", rx_fcs ? 1 : 0);

    if (iut_addr->sa_family == AF_INET)
        hdrs_len = TAD_IP4_HDR_LEN;
    else
        hdrs_len = TAD_IP6_HDR_LEN;

    if (sock_type == RPC_SOCK_STREAM)
        hdrs_len += TAD_TCP_HDR_LEN;
    else
        hdrs_len += TAD_UDP_HDR_LEN;

    if (small_frame)
    {
        if (iut_addr->sa_family == AF_INET6)
        {
            /*
             * IPv6 header alone takes 40 bytes, and Ethernet payload
             * should be less than 46 bytes here.
             */
            TEST_FAIL("Impossible to send so small IPv6 TCP or UDP packet");
        }

        min_len = 1;
        max_len = MIN_ETH_PAYLOAD_LEN - hdrs_len - 1;
    }
    else
    {
        min_len = MIN_ETH_PAYLOAD_LEN - hdrs_len;
        if (min_len <= 0)
            min_len = 1;
        max_len = MAX_PKT_LEN;
    }
    if (max_len <= min_len)
    {
        TEST_FAIL("Cannot construct Ethernet frame with payload of "
                  "requested length");
    }

    if (small_frame)
    {
        TEST_STEP("If @p small_frame is @c TRUE, disable TCP "
                  "timestamps option on Tester so that packets "
                  "will be as small as possible.");
        CHECK_RC(tapi_cfg_sys_ns_set_int(tst_rpcs->ta, 0, NULL,
                                         "net/ipv4/tcp_timestamps"));
    }

    TEST_STEP("Establish connection between a pair of sockets of type "
              "@p sock_type on IUT and Tester.");
    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    /*
     * No need to disable rx-fcs on Tester for this test - this
     * feature influences only incoming packets captured by CSAP,
     * not outgoing ones.
     */

    TEST_STEP("Create a CSAP on IUT to capture incoming packets.");

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

    TEST_STEP("Create a CSAP on Tester to capture outgoing packets.");

    CHECK_RC(tapi_eth_csap_create(
        tst_rpcs->ta, 0,
        tst_if->if_name,
        TAD_ETH_RECV_OUT |
        TAD_ETH_RECV_NO_PROMISC,
        (uint8_t *)(tst_lladdr->sa_data),
        (uint8_t *)(iut_lladdr->sa_data), NULL,
        &csap_tx));

    CHECK_RC(tapi_tad_trrecv_start(tst_rpcs->ta, 0, csap_tx,
                                   NULL, TAD_TIMEOUT_INF, 0,
                                   RCF_TRRECV_PACKETS));

    TEST_STEP("Send some data from Tester to IUT.");

    send_len = rand_range(min_len, max_len);
    te_fill_buf(send_buf, send_len);
    RPC_SEND(rc, tst_rpcs, tst_s, send_buf, send_len, 0);
    rc = rpc_recv(iut_rpcs, iut_s, recv_buf, sizeof(recv_buf), 0);
    if (rc != send_len || memcmp(send_buf, recv_buf, send_len) != 0)
    {
        TEST_VERDICT("Data received on IUT does not match data sent "
                     "from Tester");
    }

    TEST_STEP("Obtain and process packets captured by CSAPs on IUT and "
              "Tester.");

    memset(&csap_cb_data, 0, sizeof(csap_cb_data));
    memset(&pkts_data, 0, sizeof(pkts_data));

    csap_cb_data.user_data = &pkts_data;

    csap_cb_data.callback = &process_out;
    pkts_data.pkts = &out_pkts;
    CHECK_RC(tapi_tad_trrecv_stop(tst_rpcs->ta, 0, csap_tx,
                                  &csap_cb_data, NULL));

    if (pkts_data.failed)
        TEST_FAIL("Failed to process some packets on Tester");
    else if (pkts_data.pkts_num == 0)
        TEST_FAIL("Failed to capture any packets on Tester");

    out_pkts_num = pkts_data.pkts_num;

    csap_cb_data.callback = &process_in;
    pkts_data.pkts = &in_pkts;
    pkts_data.pkts_num = 0;
    pkts_data.expect_fcs = rx_fcs;
    CHECK_RC(tapi_tad_trrecv_stop(iut_rpcs->ta, 0, csap_rx,
                                  &csap_cb_data, NULL));

    if (pkts_data.failed)
        TEST_FAIL("Failed to process some packets on IUT");
    else if (pkts_data.pkts_num == 0)
        TEST_FAIL("Failed to capture any packets on IUT");

    in_pkts_num = pkts_data.pkts_num;

    if (in_pkts_num != out_pkts_num)
    {
        TEST_FAIL("Numbers of packets captured by two CSAPs "
                  "are different");
    }

    TEST_SUBSTEP("For packets captured by both CSAPs, check that "
                 "every packet captured on IUT has four extra bytes "
                 "in payload containing correct FCS if and only if "
                 "@p rx_fcs is @c TRUE.");

    RING("%d packet(s) were captured by both CSAPs", in_pkts_num);

    for (i = 0; i < in_pkts_num; i++)
    {
        pkt_out_len = TE_VEC_GET(int, &out_pkts, i);

        pkt_in = te_vec_get_safe(&in_pkts, i, sizeof(pkt_in_data));

        /* Padding may be not reported for outgoing packets */
        padded_pkt_len = MAX(pkt_out_len, MIN_ETH_PAYLOAD_LEN);

        RING("Packet %d: outgoing payload %d bytes, incoming payload "
             "%d bytes", i, pkt_out_len, pkt_in->payload_len);

        /*
         * Without FCS, sometimes padding is reported, sometimes it is not.
         */
        if (pkt_out_len == pkt_in->payload_len ||
            padded_pkt_len == pkt_in->payload_len)
        {
            if (rx_fcs)
            {
                TEST_VERDICT("No FCS was reported for a captured "
                             "packet on IUT");
            }
        }
        else if (padded_pkt_len == pkt_in->payload_len - ETH_FCS_LEN)
        {
            if (!rx_fcs)
            {
                TEST_VERDICT("rx-fcs is disabled but incoming packet "
                             "has four extra bytes");
            }
            else if (pkt_in->computed_fcs != pkt_in->pkt_fcs)
            {
                ERROR("Computed FCS is 0x%x while the last four bytes "
                      "contain 0x%x", pkt_in->computed_fcs,
                      pkt_in->pkt_fcs);

                TEST_VERDICT("Computed FCS does not match the last four "
                             "bytes of the incoming packet");
            }
        }
        else
        {
            TEST_VERDICT("Length of outgoing packet on Tester does not "
                         "match length of incoming packet on IUT");
        }
    }

    TEST_SUCCESS;

cleanup:

    if (csap_tx != CSAP_INVALID_HANDLE)
    {
        CLEANUP_CHECK_RC(tapi_tad_csap_destroy(tst_rpcs->ta, 0,
                                               csap_tx));
    }

    if (csap_rx != CSAP_INVALID_HANDLE)
    {
        CLEANUP_CHECK_RC(tapi_tad_csap_destroy(iut_rpcs->ta, 0,
                                               csap_rx));
    }

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    te_vec_free(&in_pkts);
    te_vec_free(&out_pkts);

    TEST_END;
}
