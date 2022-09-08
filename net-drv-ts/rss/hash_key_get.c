/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 *
 * Copyright (C) 2022 OKTET Labs. All rights reserved.
 */

/**
 * @defgroup rss-hash_key_get Getting RSS hash key.
 * @ingroup rss
 * @{
 *
 * @objective Get RSS hash key, check that packets are directed to
 *            receive queues according to its value.
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

#define TE_TEST_NAME "rss/hash_key_get"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "tapi_bpf_rxq_stats.h"
#include "common_rss.h"

/*
 * Minimum port number to use (to avoid conflicts with standard
 * services).
 */
#define MIN_PORT 20000

/* How many times to try to choose ports numbers */
#define MAX_CHANGE_PORT_ATTEMPTS 100000

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *iut_addr = NULL;
    const struct sockaddr *tst_addr = NULL;
    rpc_socket_type sock_type;

    int iut_s = -1;
    int tst_s = -1;

    uint8_t *hash_key = NULL;
    size_t key_len;
    te_toeplitz_hash_cache *cache = NULL;
    unsigned int hash;

    unsigned int table_size;
    unsigned int idx;
    int indir;
    unsigned int i;
    unsigned int j;

    struct sockaddr_storage new_iut_addr_st;
    struct sockaddr_storage new_tst_addr_st;
    struct sockaddr *new_iut_addr = NULL;
    struct sockaddr *new_tst_addr = NULL;
    uint16_t iut_port;
    uint16_t tst_port;
    size_t addr_size;
    unsigned int bpf_id = 0;

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

    TEST_STEP("Make sure that Toeplitz hash function is used on "
              "the IUT interface.");
    NET_DRV_RSS_CHECK_SET_HFUNC(iut_rpcs->ta, iut_if->if_name, 0,
                                "toeplitz", TEST_SUCCESS);

    CHECK_NOT_NULL(cache = te_toeplitz_cache_init_size(hash_key, key_len));

    CHECK_RC(tapi_cfg_if_rss_indir_table_size(iut_rpcs->ta,
                                              iut_if->if_name, 0,
                                              &table_size));

    CHECK_RC(tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                       &bpf_id));

    tapi_sockaddr_clone_exact(iut_addr, &new_iut_addr_st);
    tapi_sockaddr_clone_exact(tst_addr, &new_tst_addr_st);
    new_iut_addr = SA(&new_iut_addr_st);
    new_tst_addr = SA(&new_tst_addr_st);

    TEST_STEP("For every entry in RSS hash indirection table: ");

    for (j = 0; j < table_size; j++)
    {
        TEST_SUBSTEP("Find such address/port pairs on IUT and Tester that "
                     "Toeplitz hash produces value mapped to the table "
                     "entry.");
        for (i = 0; i < MAX_CHANGE_PORT_ATTEMPTS; i++)
        {
            iut_port = rand_range(MIN_PORT, 0xffff);
            tst_port = rand_range(MIN_PORT, 0xffff);
            te_sockaddr_set_port(new_iut_addr, htons(iut_port));
            te_sockaddr_set_port(new_tst_addr, htons(tst_port));

            hash = te_toeplitz_hash(
                        cache, addr_size,
                        te_sockaddr_get_netaddr(new_tst_addr),
                        htons(tst_port),
                        te_sockaddr_get_netaddr(new_iut_addr),
                        htons(iut_port));

            idx = hash % table_size;
            if (idx == j)
            {
                if (rpc_check_port_is_free(iut_rpcs, iut_port) &&
                    rpc_check_port_is_free(tst_rpcs, tst_port))
                {
                    break;
                }
            }
        }

        if (idx != j)
        {
            TEST_FAIL("Could not choose free ports on IUT and Tester "
                      "such that RSS hash points to indirection "
                      "table entry %u", j);
        }

        CHECK_RC(tapi_cfg_if_rss_indir_get(iut_rpcs->ta,
                                           iut_if->if_name, 0, idx,
                                           &indir));

        TEST_SUBSTEP("Create a pair of connected sockets on IUT and Tester "
                     "bound to these address/port pairs.");
        GEN_CONNECTION(iut_rpcs, tst_rpcs, sock_type, RPC_PROTO_DEF,
                       new_iut_addr, new_tst_addr, &iut_s, &tst_s);

        TEST_SUBSTEP("Configure XDP hook to count only packets going "
                     "from Tester to IUT via the created connection.");
        CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));
        CHECK_RC(
            tapi_bpf_rxq_stats_set_params(
                 iut_rpcs->ta, bpf_id, iut_addr->sa_family,
                 new_tst_addr, new_iut_addr,
                 sock_type == RPC_SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP,
                 TRUE));

        TEST_SUBSTEP("Send a few packets from Tester to IUT. "
                     "Check that XDP hook reports that all the packets were "
                     "processed by the Rx queue specified by the current "
                     "indirection table entry.");

        RING("Predicted hash: %u, indirection table entry: %u, Rx queue: %u",
             hash, idx, indir);

        net_drv_rss_send_check_stats(tst_rpcs, tst_s, iut_rpcs, iut_s,
                                     sock_type, indir, bpf_id, "");

        RPC_CLOSE(iut_rpcs, iut_s);
        RPC_CLOSE(tst_rpcs, tst_s);
    }

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);
    CLEANUP_RPC_CLOSE(tst_rpcs, tst_s);

    CLEANUP_CHECK_RC(tapi_bpf_rxq_stats_reset(iut_rpcs->ta, bpf_id));

    te_toeplitz_hash_fini(cache);
    free(hash_key);

    TEST_END;
}
