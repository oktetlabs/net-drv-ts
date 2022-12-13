/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2023 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * RSS tests
 */

/**
 * @defgroup rss-too_many_rx_rules Adding too many Rx rules
 * @ingroup rss
 * @{
 *
 * @objective Check that if Rx rules table is overfilled, nothing bad
 *            happens.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param sock_type      Socket type (determines whether TCP or
 *                       UDP Rx rules are created):
 *                       - @c SOCK_STREAM
 *                       - @c SOCK_DGRAM
 * @param iters          How many times to fill rules table and
 *                       remove all the added rules
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "rss/too_many_rx_rules"

#include "net_drv_test.h"
#include "tapi_cfg_if_rss.h"
#include "te_toeplitz.h"
#include "tapi_bpf_rxq_stats.h"
#include "tapi_cfg_rx_rule.h"
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
    int iters;

    net_drv_rss_ctx rss_ctx = NET_DRV_RSS_CTX_INIT;
    te_bool spec_loc;
    int iut_s = -1;
    int i;

    unsigned int prev_rules_count = 0;
    unsigned int rules_count = 0;
    te_bool dec_count_verdict = FALSE;

    unsigned int rules_num;
    cfg_handle *rules_handles = NULL;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(iut_rpcs, iut_addr);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_SOCK_TYPE(sock_type);
    TEST_GET_INT_PARAM(iters);

    TEST_STEP("Check that there is no Rx rules on IUT interface.");
    CHECK_RC(cfg_find_pattern_fmt(
                &rules_num, &rules_handles,
                "/agent:%s/interface:%s/rx_rules:/rule:*",
                iut_rpcs->ta, iut_if->if_name));
    free(rules_handles);
    if (rules_num != 0)
        TEST_FAIL("Some Rx rules are already present on IUT");

    TEST_STEP("Create a socket on IUT for adding Rx rules via "
              "ioctl().");
    iut_s = rpc_create_and_bind_socket(iut_rpcs, sock_type,
                                       RPC_PROTO_DEF, FALSE, FALSE,
                                       iut_addr);

    net_drv_rss_ctx_prepare(&rss_ctx, iut_rpcs->ta, iut_if->if_name, 0);

    CHECK_RC(tapi_cfg_rx_rule_spec_loc_get(iut_rpcs->ta,
                                           iut_if->if_name, &spec_loc));

    TEST_STEP("Iterate @p iters times:");
    for (i = 0; i < iters; i++)
    {
        TEST_SUBSTEP("Create a lot of Rx classification rules on IUT "
                     "until adding the next one fails. Then remove all "
                     "the added rules. Check that the count of added rules "
                     "does not decrease in comparison to the previous "
                     "iteration.");

        RPC_AWAIT_ERROR(iut_rpcs);
        rc = rpc_net_drv_too_many_rx_rules(iut_rpcs, iut_s, iut_if->if_name,
                                           tst_addr, iut_addr, sock_type,
                                           spec_loc, rss_ctx.rx_queues,
                                           &rules_count, NULL);
        if (rc < 0)
        {
            TEST_VERDICT("rpc_net_drv_too_many_rx_rules() failed with "
                         "error " RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
        }

        if (rules_count == 0)
            TEST_VERDICT("No Rx rules were added on IUT");

        if (prev_rules_count > 0 && rules_count < prev_rules_count &&
            !dec_count_verdict)
        {
            WARN_VERDICT("After filling and clearing rules table, less "
                         "rules than before can be added to it");
            dec_count_verdict = TRUE;
        }

        prev_rules_count = rules_count;
    }

    TEST_SUCCESS;

cleanup:

    net_drv_rss_ctx_release(&rss_ctx);
    CLEANUP_RPC_CLOSE(iut_rpcs, iut_s);

    TEST_END;
}
