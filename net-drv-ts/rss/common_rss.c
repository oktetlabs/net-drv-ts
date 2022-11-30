/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/** @file
 * @brief Test API for RSS tests
 *
 * Implementation of TAPI for RSS tests.
 */

#include "common_rss.h"
#include "tapi_bpf.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_cfg_rx_rule.h"

/* Minimum number of packets to send when testing RSS */
#define RSS_TEST_MIN_PKTS_NUM 3

/* Maximum number of packets to send when testing RSS */
#define RSS_TEST_MAX_PKTS_NUM 30

/*
 * Common code for configuring XDP hook and sending/receiving
 * some packets.
 */
static te_errno
send_check_common(rcf_rpc_server *sender_rpcs, int sender_s,
                  const struct sockaddr *sender_addr,
                  rcf_rpc_server *receiver_rpcs, int receiver_s,
                  const struct sockaddr *receiver_addr,
                  rpc_socket_type sock_type, unsigned int bpf_id,
                  unsigned int *sent_pkts, const char *vpref)
{
    unsigned int i;
    unsigned int pkts_num;
    te_errno rc;

    if (sender_addr != NULL || receiver_addr != NULL)
    {
        int af = (sender_addr != NULL ? sender_addr->sa_family :
                                        receiver_addr->sa_family);

        rc = tapi_bpf_rxq_stats_reset(receiver_rpcs->ta, bpf_id);
        if (rc != 0)
            return rc;

        rc = tapi_bpf_rxq_stats_set_params(
                 receiver_rpcs->ta, bpf_id, af,
                 sender_addr, receiver_addr,
                 sock_type == RPC_SOCK_DGRAM ? IPPROTO_UDP : IPPROTO_TCP,
                 TRUE);
        if (rc != 0)
            return rc;
    }
    else
    {

        rc = tapi_bpf_rxq_stats_clear(receiver_rpcs->ta, bpf_id);
        if (rc != 0)
            return rc;
    }

    pkts_num = rand_range(RSS_TEST_MIN_PKTS_NUM, RSS_TEST_MAX_PKTS_NUM);

    for (i = 0; i < pkts_num; i++)
    {
        net_drv_send_recv_check(sender_rpcs, sender_s,
                                receiver_rpcs, receiver_s, vpref);
    }

    *sent_pkts = pkts_num;
    return 0;
}

/* See description in common_rss.h */
te_errno
net_drv_rss_send_check_stats(rcf_rpc_server *sender_rpcs, int sender_s,
                             const struct sockaddr *sender_addr,
                             rcf_rpc_server *receiver_rpcs, int receiver_s,
                             const struct sockaddr *receiver_addr,
                             rpc_socket_type sock_type, unsigned int exp_queue,
                             unsigned int bpf_id, const char *vpref)
{
    te_errno rc;
    unsigned int pkts_num;

    rc = send_check_common(sender_rpcs, sender_s, sender_addr,
                           receiver_rpcs, receiver_s, receiver_addr,
                           sock_type, bpf_id, &pkts_num, vpref);
    if (rc != 0)
        return rc;

    return tapi_bpf_rxq_stats_check_single(receiver_rpcs->ta, bpf_id,
                                           exp_queue, pkts_num,
                                           sock_type, vpref);
}

/* See description in common_rss.h */
te_errno
net_drv_rss_send_get_stats(rcf_rpc_server *sender_rpcs, int sender_s,
                           const struct sockaddr *sender_addr,
                           rcf_rpc_server *receiver_rpcs, int receiver_s,
                           const struct sockaddr *receiver_addr,
                           rpc_socket_type sock_type, unsigned int bpf_id,
                           tapi_bpf_rxq_stats **stats,
                           unsigned int *stats_count,
                           const char *vpref)
{
    te_errno rc;
    unsigned int i;
    uint64_t got_pkts = 0;
    unsigned int pkts_num;

    rc = send_check_common(sender_rpcs, sender_s, sender_addr,
                           receiver_rpcs, receiver_s, receiver_addr,
                           sock_type, bpf_id, &pkts_num, vpref);
    if (rc != 0)
        return rc;

    rc = tapi_bpf_rxq_stats_read(receiver_rpcs->ta, bpf_id, stats,
                                 stats_count);
    if (rc != 0)
        return rc;

    tapi_bpf_rxq_stats_print(NULL, *stats, *stats_count);

    for (i = 0; i < *stats_count; i++)
        got_pkts += (*stats)[i].pkts;

    if (got_pkts == 0)
        TEST_VERDICT("%s: no packets was detected for any Rx queue", vpref);
    else if (got_pkts < pkts_num)
        TEST_VERDICT("%s: too few packets were detected", vpref);

    return 0;
}

/* See description in common_rss.h */
te_errno
net_drv_rss_check_set_hfunc(const char *ta,
                            const char *if_name,
                            unsigned int rss_context,
                            const char *func_name)
{
    te_errno rc;
    tapi_cfg_if_rss_hfunc *hfuncs = NULL;
    unsigned int hfuncs_count;
    unsigned int i;
    te_bool found = FALSE;
    te_bool need_change = FALSE;

    rc = tapi_cfg_if_rss_hfuncs_get(ta, if_name, rss_context,
                                    &hfuncs, &hfuncs_count);
    if (rc != 0)
        return rc;

    for (i = 0; i < hfuncs_count; i++)
    {
        if (strcmp(hfuncs[i].name, func_name) == 0)
        {
            found = TRUE;
            if (!hfuncs[i].enabled)
                need_change = TRUE;
        }
        else if (hfuncs[i].enabled)
        {
            need_change = TRUE;
        }
    }

    free(hfuncs);

    if (!found)
        return TE_RC(TE_TAPI, TE_ENOENT);

    if (!need_change)
        return 0;

    rc = tapi_cfg_if_rss_hfunc_set_single_local(
                              ta, if_name, rss_context, func_name);
    if (rc != 0)
        return rc;

    rc = tapi_cfg_if_rss_hash_indir_commit(ta, if_name, rss_context);
    if (TE_RC_GET_ERROR(rc) == TE_EOPNOTSUPP)
        return TE_RC(TE_TAPI, TE_EOPNOTSUPP);

    return rc;
}

/* See description in common_rss.h */
void
net_drv_rss_ctx_prepare(net_drv_rss_ctx *ctx, const char *ta,
                        const char *if_name, unsigned int rss_ctx)
{
    te_errno rc = 0;
    int hash_var;
    int rx_queues;

    ctx->ta = ta;
    ctx->if_name = if_name;
    ctx->rss_ctx = rss_ctx;

    net_drv_rss_get_check_hkey(ta, if_name, rss_ctx, &ctx->hash_key,
                               &ctx->key_len);

    rc = tapi_cfg_if_rss_rx_queues_get(ta, if_name, &rx_queues);
    if (rc != 0)
    {
        ERROR("%s(): failed to get number of Rx queues, rc=%r",
              __FUNCTION__, rc);
        goto cleanup;
    }

    ctx->rx_queues = rx_queues;

    rc = tapi_cfg_if_rss_indir_table_size(ta, if_name, rss_ctx,
                                          &ctx->indir_table_size);
    if (rc != 0)
    {
        ERROR("%s(): failed to get indirection table size, rc=%r",
              __FUNCTION__, rc);
        goto cleanup;
    }

    ctx->cache = te_toeplitz_cache_init_size(ctx->hash_key, ctx->key_len);
    if (ctx->cache == NULL)
    {
        ERROR("%s(): failed to allocate Toeplitz hash cache", __FUNCTION__);
        rc = TE_ENOMEM;
        goto cleanup;
    }

    rc = cfg_get_instance_int_fmt(&hash_var, "%s",
                                  "/local:/iut_toeplitz_variant:");
    if (rc != 0)
    {
        ERROR("%s(): failed to get Toeplitz hash variant", __FUNCTION__);
        rc = TE_EINVAL;
        goto cleanup;
    }

    ctx->hash_variant = hash_var;

cleanup:

    if (rc != 0)
    {
        net_drv_rss_ctx_release(ctx);
        TEST_STOP;
    }
}

/* See description in common_rss.h */
void
net_drv_rss_ctx_release(net_drv_rss_ctx *ctx)
{
    free(ctx->hash_key);
    te_toeplitz_hash_fini(ctx->cache);
    ctx->hash_key = NULL;
    ctx->cache = NULL;
}

/* See description in common_rss.h */
te_errno
net_drv_rss_ctx_change_key(net_drv_rss_ctx *ctx, uint8_t *hash_key,
                           unsigned int key_len)
{
    uint8_t *key_dup = NULL;
    te_toeplitz_hash_cache *new_cache = NULL;

    key_dup = TE_ALLOC(key_len);
    if (key_dup == NULL)
    {
        ERROR("%s(): cannot allocate memory for hash key", __FUNCTION__);
        return TE_ENOMEM;
    }
    memcpy(key_dup, hash_key, key_len);

    new_cache = te_toeplitz_cache_init_size(hash_key, key_len);
    if (new_cache == NULL)
    {
        ERROR("%s(): failed to allocate Toeplitz hash cache", __FUNCTION__);
        free(key_dup);
        return TE_ENOMEM;
    }

    free(ctx->hash_key);
    ctx->hash_key = key_dup;
    ctx->key_len = key_len;

    te_toeplitz_hash_fini(ctx->cache);
    ctx->cache = new_cache;

    return 0;
}

/* See description in common_rss.h */
te_errno
net_drv_rss_predict(net_drv_rss_ctx *ctx,
                    const struct sockaddr *src_addr,
                    const struct sockaddr *dst_addr,
                    unsigned int *hash_out,
                    unsigned int *idx_out,
                    unsigned int *queue_out)
{
    unsigned int idx;
    int queue;
    uint32_t hash;
    te_errno rc;

    rc = te_toeplitz_hash_sa(ctx->cache, src_addr, dst_addr,
                             ctx->hash_variant, &hash);
    if (rc != 0)
        return rc;

    if (hash_out != NULL)
        *hash_out = hash;

    if (idx_out == NULL && queue_out == NULL)
        return 0;

    idx = hash % ctx->indir_table_size;
    if (idx_out != NULL)
        *idx_out = idx;

    if (queue_out == NULL)
        return 0;

    rc = tapi_cfg_if_rss_indir_get(ctx->ta, ctx->if_name, ctx->rss_ctx,
                                   idx, &queue);
    if (rc != 0)
        return rc;

    *queue_out = queue;
    return 0;
}

/* See description in common_rss.h */
void
net_drv_rx_rules_check_table_size(const char *ta, const char *if_name,
                                  uint32_t *table_size)
{
    te_errno rc;
    uint32_t size;

    rc = tapi_cfg_rx_rule_table_size_get(ta, if_name, &size);
    if (rc != 0)
    {
        if (rc == TE_RC(TE_CS, TE_ENOENT))
            TEST_SKIP("Rx rules are not supported");
        else
            TEST_VERDICT("Failed to get size of Rx rules table: %r", rc);
    }
    else if (size == 0)
    {
        TEST_SKIP("Rx rules table has zero size");
    }

    if (table_size != NULL)
        *table_size = size;
}

/* See description in common_rss.h */
void
net_drv_rx_rules_check_spec_loc(const char *ta, const char *if_name)
{
    te_errno rc;
    te_bool spec_loc;

    rc = tapi_cfg_rx_rule_spec_loc_get(ta, if_name, &spec_loc);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to check support of special insert "
                     "locations: %r", rc);
    }

    if (!spec_loc)
    {
        TEST_SKIP("Special insert locations are not supported for "
                  "Rx rules");
    }
}

/* See description in common_rss.h */
te_errno
net_drv_rx_rules_find_loc(const char *ta, const char *if_name,
                          int64_t *location)
{
    te_errno rc;
    te_bool spec_loc;

    rc = tapi_cfg_rx_rule_spec_loc_get(ta, if_name, &spec_loc);
    if (rc != 0)
        return rc;

    if (spec_loc)
    {
        *location = TAPI_CFG_RX_RULE_ANY;
        return 0;
    }

    return tapi_cfg_rx_rule_find_location(ta, if_name, 0, 0, location);
}
