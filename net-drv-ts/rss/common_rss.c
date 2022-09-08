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

/* Minimum number of packets to send when testing RSS */
#define RSS_TEST_MIN_PKTS_NUM 3

/* Maximum number of packets to send when testing RSS */
#define RSS_TEST_MAX_PKTS_NUM 30

/* See description in common_rss.h */
void
net_drv_rss_send_check_stats(rcf_rpc_server *sender_rpcs, int sender_s,
                             rcf_rpc_server *receiver_rpcs, int receiver_s,
                             rpc_socket_type sock_type, unsigned int exp_queue,
                             unsigned int bpf_id, const char *vpref)
{
    unsigned int i;
    unsigned int pkts_num;

    CHECK_RC(tapi_bpf_rxq_stats_clear(receiver_rpcs->ta, bpf_id));

    pkts_num = rand_range(RSS_TEST_MIN_PKTS_NUM, RSS_TEST_MAX_PKTS_NUM);

    for (i = 0; i < pkts_num; i++)
    {
        net_drv_send_recv_check(sender_rpcs, sender_s,
                                receiver_rpcs, receiver_s, vpref);
    }

    CHECK_RC(tapi_bpf_rxq_stats_check_single(receiver_rpcs->ta, bpf_id,
                                             exp_queue, pkts_num,
                                             sock_type, vpref));
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
