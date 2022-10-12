/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/** @file
 * @brief Test Suite prologue
 *
 * Prologue for RSS tests.
 */

#ifndef DOXYGEN_TEST_SPEC

/** Logging subsystem entity name */
#define TE_TEST_NAME    "rss/prologue"

#include "net_drv_test.h"
#include "tapi_bpf.h"
#include "tapi_cfg_if_rss.h"
#include "tapi_bpf_rxq_stats.h"

int
main(int argc, char **argv)
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    unsigned int bpf_id;
    int rx_queues;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    rc = tapi_cfg_if_rss_rx_queues_get(iut_rpcs->ta, iut_if->if_name,
                                       &rx_queues);
    if (rc != 0)
    {
        if (rc == TE_RC(TE_CS, TE_ENOENT))
            TEST_SKIP("RSS is not supported for IUT interface");
        else
            TEST_STOP;
    }

    /* Link XDP hook used to get per-queue statistics */

    CHECK_RC(tapi_bpf_rxq_stats_init(iut_rpcs->ta, iut_if->if_name,
                                     "rss_bpf", &bpf_id));

    CHECK_RC(cfg_synchronize("/:", TRUE));

    TEST_SUCCESS;

cleanup:

    TEST_END;
}

#endif
