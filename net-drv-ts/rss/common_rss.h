/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/** @file
 * @brief Test API for RSS tests
 *
 * Declarations of TAPI for RSS tests.
 */

#ifndef __TS_NET_DRV_COMMON_RSS_H__
#define __TS_NET_DRV_COMMON_RSS_H__

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "tapi_bpf.h"
#include "te_toeplitz.h"

/**
 * Send a few packets and check whether expected Rx queue received them.
 * This function clears XDP hook statistics before sending.
 *
 * @param sender_rpcs     Sender RPC server
 * @param sender_s        Sender socket
 * @param receiver_rpcs   Receiver RPC server
 * @param receiver_s      Receiver socket
 * @param sock_type       Socket type (@c RPC_SOCK_STREAM,
 *                        @c RPC_SOCK_DGRAM)
 * @param exp_queue       Expected RSS queue ID
 * @param vpref           Prefix to use in verdicts
 *
 * @return Status code.
 */
extern te_errno net_drv_rss_send_check_stats(rcf_rpc_server *sender_rpcs,
                                             int sender_s,
                                             rcf_rpc_server *receiver_rpcs,
                                             int receiver_s,
                                             rpc_socket_type sock_type,
                                             unsigned int exp_queue,
                                             unsigned int bpf_id,
                                             const char *vpref);

/**
 * Check whether expected RSS hash function is enabled; if not,
 * try to change configuration.
 *
 * @param ta            Test Agent name
 * @param if_name       Network interface name
 * @param rss_context   RSS context
 * @param func_name     Function name
 *
 * @return Status code.
 */
extern te_errno net_drv_rss_check_set_hfunc(const char *ta,
                                            const char *if_name,
                                            unsigned int rss_context,
                                            const char *func_name);

/**
 * Make sure that specific RSS hash function is used.
 *
 * @param _ta           Test Agent name
 * @param _if_name      Interface name
 * @param _rss_ctx      RSS context
 * @param _func_name    Function name
 * @param _term_macro   Macro used to terminate the test if requested
 *                      function is not known or not supported
 *                      (TEST_SUCCESS, TEST_STOP, etc)
 */
#define NET_DRV_RSS_CHECK_SET_HFUNC(_ta, _if_name, _rss_ctx, _func_name, \
                                    _term_macro) \
    do {                                                            \
        te_errno _rc;                                               \
                                                                    \
        _rc = net_drv_rss_check_set_hfunc(_ta, _if_name, _rss_ctx,  \
                                          _func_name);              \
        if (_rc != 0)                                               \
        {                                                           \
            if (_rc == TE_RC(TE_TAPI, TE_ENOENT))                   \
            {                                                       \
                RING_VERDICT("Hash function %s is not known",       \
                             _func_name);                           \
                _term_macro;                                        \
            }                                                       \
            else if (_rc == TE_RC(TE_TAPI, TE_EOPNOTSUPP))          \
            {                                                       \
                RING_VERDICT("Hash function %s is not supported",   \
                             _func_name);                           \
                _term_macro;                                        \
            }                                                       \
            else                                                    \
            {                                                       \
                TEST_STOP;                                          \
            }                                                       \
        }                                                           \
    } while (0)

/**
 * Get RSS hash key.
 *
 * @param ta            Test Agent name
 * @param if_name       Network interface name
 * @param rss_context   RSS context
 * @param key           Where to save pointer to hash key
 *                      (should be released by caller)
 * @param len           Where to save key length
 */
static inline void
net_drv_rss_get_check_hkey(const char *ta, const char *if_name,
                           unsigned int rss_context, uint8_t **key,
                           size_t *len)
{
    te_errno rc;

    rc = tapi_cfg_if_rss_hash_key_get(ta, if_name, rss_context,
                                      key, len);
    if (rc != 0)
    {
        if (rc == TE_RC(TE_CS, TE_EOPNOTSUPP))
            TEST_SKIP("Getting RSS hash key is not supported");
        else
            TEST_VERDICT("Failed to get RSS hash key, rc=%r", rc);
    }

    RING("RSS hash key: %Tm", *key, *len);
}

/** Helper structure for RSS tests */
typedef struct net_drv_rss_ctx {
    const char *ta; /**< Test Agent name */
    const char *if_name; /**< Interface name */
    unsigned int rss_ctx; /**< RSS context id */
    unsigned int indir_table_size; /**< Size of indirection table */
    unsigned int rx_queues; /**< Number of Rx queues */
    uint8_t *hash_key; /**< Hash key */
    size_t key_len; /**< Length of hash key */
    te_toeplitz_hash_cache *cache; /**< Cache used for computing Toeplitz
                                        hash */
    te_toeplitz_hash_variant hash_variant; /**< Variant of Toeplitz
                                                algorithm to use */
} net_drv_rss_ctx;

/** Initializer for net_drv_rss_ctx */
#define NET_DRV_RSS_CTX_INIT \
    {                                                                     \
        .ta = "", .if_name = "", .rss_ctx = 0, .indir_table_size = 0,     \
        .rx_queues = 0, .hash_key = NULL, .key_len = 0, .cache = NULL,    \
        .hash_variant = TE_TOEPLITZ_HASH_STANDARD                         \
    }

/**
 * Fill fields of RSS test context.
 *
 * @note This function can print verdict and jump to cleanup.
 *
 * @param ctx       Context structure to initialize
 * @param ta        Test agent name
 * @param if_name   Interface name
 * @param rss_ctx   RSS context id
 */
extern void net_drv_rss_ctx_prepare(net_drv_rss_ctx *ctx,
                                    const char *ta,
                                    const char *if_name,
                                    unsigned int rss_ctx);

/**
 * Release resources allocated for RSS test context.
 *
 * @param ctx     Pointer to context structure
 */
extern void net_drv_rss_ctx_release(net_drv_rss_ctx *ctx);

/**
 * Change hash key used for Toeplitz hash computation.
 *
 * @param ctx       Pointer to context structure
 * @param hash_key  New hash key
 * @param key_len   Key length
 *
 * @return Status code.
 */
extern te_errno net_drv_rss_ctx_change_key(net_drv_rss_ctx *ctx,
                                           uint8_t *hash_key,
                                           unsigned int key_len);

/**
 * Predict Toeplitz hash value, indirection table index and
 * Rx queue id for given pairs of addresses/ports.
 *
 * @param ctx         RSS test context
 * @param src_addr    Source address/port
 * @param dst_addr    Destination address/port
 * @param hash_out    Where to save hash value (may be @c NULL)
 * @param idx_out     Where to save indirection table index (may be @c NULL)
 * @param queue_out   Where to save Rx queue id (may be @c NULL)
 *
 * @return Status code.
 */
extern te_errno net_drv_rss_predict(net_drv_rss_ctx *ctx,
                                    const struct sockaddr *src_addr,
                                    const struct sockaddr *dst_addr,
                                    unsigned int *hash_out,
                                    unsigned int *idx_out,
                                    unsigned int *queue_out);

#endif /* !__TS_NET_DRV_COMMON_RSS_H__ */
