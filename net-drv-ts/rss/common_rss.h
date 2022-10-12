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

#endif /* !__TS_NET_DRV_COMMON_RSS_H__ */
