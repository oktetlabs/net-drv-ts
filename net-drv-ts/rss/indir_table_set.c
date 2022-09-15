/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-indir_table_set Changing RSS hash indirection table.
 * @ingroup rss
 * @{
 *
 * @objective Check that a record of RSS hash indirection table can be
 *            changed to specify another Rx queue and the new queue will
 *            then be used for an existing connection whose hash is mapped
 *            to that table record.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param sock_type      Socket type:
 *                       - @c SOCK_STREAM
 *                       - @c SOCK_DGRAM
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "rss/indir_table_set"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "tapi_bpf_rxq_stats.h"
#include "common_rss.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;
    rpc_socket_type sock_type;

    size_t addr_size;

    int iut_s = -1;
    int tst_s = -1;

    int rx_queues;
    unsigned int table_size;
    unsigned int idx;
    int cur_queue;
    int new_queue;
    unsigned int bpf_id = 0;

    uint8_t *hash_key = NULL;
    size_t key_len;
    te_toeplitz_hash_cache *cache = NULL;
    unsigned int hash;
    te_bool test_failed = FALSE;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_SOCK_TYPE(sock_type);

    addr_size = te_netaddr_get_size(iut_addr->sa_family);

    TEST_STEP("Check that RSS hash key can be obtained for IUT "
              "interface.");

    net_drv_rss_get_check_hkey(iut_rpcs->ta, iut_if->if_name, 0,
                               &hash_key, &key_len);

    TEST_STEP("Check that multiple Rx queues are available.");

    CHECK_RC(tapi_cfg_if_rss_rx_queues_get(
                    iut_rpcs->ta, iut_if->if_name, &rx_queues));
    if (rx_queues <= 1)
        TEST_SKIP("Multiple Rx queues should be available");

    TEST_STEP("Check that at least one record is present in RSS hash "
              "indirection table.");

    CHECK_RC(tapi_cfg_if_rss_indir_table_size(iut_rpcs->ta,
                                              iut_if->if_name, 0,
                                              &table_size));

    if (table_size < 1)
        TEST_SKIP("RSS indirection table does not have any records");

    TEST_STEP("Make sure that Toeplitz hash function is used on "
              "the IUT interface.");
    NET_DRV_RSS_CHECK_SET_HFUNC(
          iut_rpcs->ta, iut_if->if_name, 0,
          "toeplitz", TEST_SKIP("Test cannot be run without enabling "
                                "Toeplitz hash function"));

    CHECK_RC(tapi_cfg_if_rss_print_indir_table(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    TEST_STEP("Create a pair of connected sockets of type @p sock_type "
              "on IUT and Tester.");

    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Compute Toeplitz hash for packets sent from Tester to find "
              "out which indirection table record specifies Rx queue for "
              "those packets.");

    CHECK_NOT_NULL(cache = te_toeplitz_cache_init_size(hash_key, key_len));

    hash = te_toeplitz_hash(
                cache, addr_size,
                te_sockaddr_get_netaddr(tst_addr),
                te_sockaddr_get_port(tst_addr),
                te_sockaddr_get_netaddr(iut_addr),
                te_sockaddr_get_port(iut_addr));

    idx = hash % table_size;

    CHECK_RC(tapi_cfg_if_rss_indir_get(iut_rpcs->ta,
                                       iut_if->if_name, 0, idx,
                                       &cur_queue));

    TEST_STEP("Configure XDP hook to count only packets going "
              "from Tester to IUT via the created connection. It will "
              "tell which Rx queues processed these packets.");

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    CHECK_RC(
        tapi_bpf_rxq_stats_set_params(
             iut_rpcs->ta, bpf_id, iut_addr->sa_family,
             tst_addr, iut_addr,
             sock_type == RPC_SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP,
             TRUE));

    TEST_STEP("Send a few packets from the Tester socket. Check that they "
              "went via the specified Rx queue.");

    RING("With current hash key hash value is expected to be 0x%x "
         "and packets should go via queue %d specified "
         "in indirection table entry %u", hash, cur_queue, idx);

    rc = net_drv_rss_send_check_stats(tst_rpcs, tst_s, iut_rpcs, iut_s,
                                      sock_type, cur_queue, bpf_id,
                                      "Before indirection table change");
    if (rc != 0)
        test_failed = TRUE;

    TEST_STEP("Change Rx queue in the indirection table record.");

    new_queue = (cur_queue + 1) % rx_queues;

    CHECK_RC(tapi_cfg_if_rss_indir_set_local(iut_rpcs->ta, iut_if->if_name,
                                             0, idx, new_queue));
    CHECK_RC(tapi_cfg_if_rss_hash_indir_commit(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    TEST_STEP("Send a few packets from the Tester socket. Check that they "
              "went via the new Rx queue.");

    RING("After indirection table change packets should go via queue %d",
         new_queue);

    CHECK_RC(net_drv_rss_send_check_stats(
                                    tst_rpcs, tst_s, iut_rpcs, iut_s,
                                    sock_type, new_queue, bpf_id,
                                    "After indirection table change"));

    if (test_failed)
        TEST_STOP;
    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));

    te_toeplitz_hash_fini(cache);
    free(hash_key);

    TEST_END;
}
