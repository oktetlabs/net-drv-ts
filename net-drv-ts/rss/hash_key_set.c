/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-hash_key_set Changing RSS hash key.
 * @ingroup rss
 * @{
 *
 * @objective Check that RSS hash key change can result in changing
 *            of Rx queue which processes packets of a given connection.
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

#define TE_TEST_NAME "rss/hash_key_set"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_mem.h"
#include "common_rss.h"

/* Maximum number of attempts to find a new hash key */
#define MAX_ATTEMPTS 10000

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;
    rpc_socket_type sock_type;

    void *iut_netaddr = NULL;
    void *tst_netaddr = NULL;
    uint16_t iut_port;
    uint16_t tst_port;
    size_t addr_size;

    int iut_s = -1;
    int tst_s = -1;

    uint8_t *hash_key = NULL;
    size_t key_len;
    te_toeplitz_hash_cache *cache = NULL;
    unsigned int hash;
    uint8_t *new_key = NULL;

    int rx_queues;
    unsigned int table_size;
    unsigned int idx;
    unsigned int i;
    unsigned int j;
    int first_entry = -1;
    int entry;
    te_bool fix_indir_table = TRUE;
    unsigned int bpf_id = 0;

    int init_queue;
    int new_queue;
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

    new_key = tapi_calloc(key_len, 1);

    TEST_STEP("Check that multiple Rx queues are available.");

    CHECK_RC(tapi_cfg_if_rss_rx_queues_get(
                    iut_rpcs->ta, iut_if->if_name, &rx_queues));
    if (rx_queues <= 1)
        TEST_SKIP("Multiple Rx queues should be available");

    TEST_STEP("Check that multiple entries are present in RSS hash "
              "indirection table.");

    CHECK_RC(tapi_cfg_if_rss_indir_table_size(iut_rpcs->ta,
                                              iut_if->if_name, 0,
                                              &table_size));
    if (table_size <= 1)
    {
        TEST_SKIP("RSS indirection table should have multiple "
                  "entries");
    }

    TEST_STEP("Make sure that Toeplitz hash function is used on "
              "the IUT interface.");
    NET_DRV_RSS_CHECK_SET_HFUNC(
          iut_rpcs->ta, iut_if->if_name, 0,
          "toeplitz", TEST_SKIP("Test cannot be run without enabling "
                                "Toeplitz hash function"));

    TEST_STEP("Make sure that RSS hash indirection table mentions more "
              "than one Rx queue.");

    CHECK_RC(tapi_cfg_if_rss_indir_get(iut_rpcs->ta,
                                       iut_if->if_name, 0, 0,
                                       &first_entry));
    for (i = 1; i < table_size; i++)
    {
        CHECK_RC(tapi_cfg_if_rss_indir_get(iut_rpcs->ta,
                                           iut_if->if_name, 0, i,
                                           &entry));

        if (first_entry != entry)
        {
            fix_indir_table = FALSE;
            break;
        }
    }

    if (fix_indir_table)
    {
        CHECK_RC(tapi_cfg_if_rss_fill_indir_table(iut_rpcs->ta,
                                                  iut_if->if_name, 0,
                                                  0, rx_queues - 1));
    }

    CHECK_NOT_NULL(cache = te_toeplitz_cache_init_size(hash_key, key_len));

    CHECK_NOT_NULL(iut_netaddr = te_sockaddr_get_netaddr(iut_addr));
    CHECK_NOT_NULL(tst_netaddr = te_sockaddr_get_netaddr(tst_addr));
    iut_port = te_sockaddr_get_port(iut_addr);
    tst_port = te_sockaddr_get_port(tst_addr);

    hash = te_toeplitz_hash(
                cache, addr_size, tst_netaddr, tst_port,
                iut_netaddr, iut_port);
    idx = hash % table_size;

    CHECK_RC(tapi_cfg_if_rss_indir_get(iut_rpcs->ta,
                                       iut_if->if_name, 0, idx,
                                       &init_queue));

    TEST_STEP("Create a pair of connected sockets of type @p sock_type "
              "on IUT and Tester.");

    GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                   iut_addr, tst_addr, &iut_s, &tst_s);

    TEST_STEP("Send a few packets from Tester over the created connection. "
              "Check that XDP hook reports that all packets go through the "
              "Rx queue determined by the current hash key value.");

    RING("With current hash key hash value should be 0x%x and packets "
         "should go via queue %d specified in indirection table entry %u",
         hash, init_queue, idx);

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    CHECK_RC(
            tapi_bpf_rxq_stats_set_params(
                 iut_rpcs->ta, bpf_id, iut_addr->sa_family,
                 tst_addr, iut_addr,
                 sock_type == RPC_SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP,
                 TRUE));

    rc = net_drv_rss_send_check_stats(tst_rpcs, tst_s, iut_rpcs, iut_s,
                                      sock_type, init_queue, bpf_id,
                                      "Before hash key change");
    if (rc != 0)
        test_failed = TRUE;

    for (i = 0; i < MAX_ATTEMPTS; i++)
    {
        for (j = 0; j < key_len; j++)
            new_key[j] = rand_range(0, 0xff);

        te_toeplitz_hash_fini(cache);
        CHECK_NOT_NULL(cache = te_toeplitz_cache_init_size(new_key,
                                                           key_len));

        hash = te_toeplitz_hash(
                    cache, addr_size, tst_netaddr, tst_port,
                    iut_netaddr, iut_port);
        idx = hash % table_size;

        CHECK_RC(tapi_cfg_if_rss_indir_get(iut_rpcs->ta,
                                           iut_if->if_name, 0, idx,
                                           &new_queue));

        if (new_queue != init_queue)
            break;
    }

    if (i == MAX_ATTEMPTS)
        TEST_FAIL("Failed to find a new hash resulting in Rx queue change");

    TEST_STEP("Set RSS hash key on IUT interface to a different value such "
              "that Toeplitz hash for packets sent from Tester now points "
              "to a different RSS indirection table entry which specifies "
              "a different Rx queue.");

    CHECK_RC(tapi_cfg_if_rss_hash_key_set_local(iut_rpcs->ta,
                                                iut_if->if_name, 0,
                                                new_key, key_len));
    CHECK_RC(tapi_cfg_if_rss_hash_indir_commit(iut_rpcs->ta,
                                               iut_if->if_name, 0));

    TEST_STEP("Send again a few packets from Tester. "
              "Check that XDP hook reports that all the packets were "
              "processed by the new Rx queue.");

    RING("With changed hash key hash value should be 0x%x and packets "
         "should go via queue %d specified in indirection table entry %u",
         hash, new_queue, idx);

    CHECK_RC(net_drv_rss_send_check_stats(tst_rpcs, tst_s, iut_rpcs, iut_s,
                                          sock_type, new_queue, bpf_id,
                                          "After hash key change"));

    if (test_failed)
        TEST_STOP;
    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));

    te_toeplitz_hash_fini(cache);
    free(hash_key);
    free(new_key);

    TEST_END;
}
