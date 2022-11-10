/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/** @file
 * @brief Test Suite epilogue
 *
 * Epilogue for RSS tests.
 */

#ifndef DOXYGEN_TEST_SPEC

/** Logging subsystem entity name */
#define TE_TEST_NAME    "rss/epilogue"

#include "net_drv_test.h"
#include "tapi_bpf_rxq_stats.h"

int
main(int argc, char **argv)
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    unsigned int bpf_id = 0;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    rc = tapi_bpf_rxq_stats_get_id(iut_rpcs->ta, iut_if->if_name,
                                   &bpf_id);
    if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
        TEST_SUCCESS;
    else if (rc != 0)
        TEST_FAIL("tapi_bpf_rxq_stats_get_id() returned %r", rc);

    CHECK_RC(tapi_bpf_rxq_stats_fini(iut_rpcs->ta, iut_if->if_name,
                                     bpf_id));

    CHECK_RC(cfg_del_instance_fmt(FALSE, "/local:/iut_toeplitz_variant:"));
    CHECK_RC(cfg_unregister_object_str("/local/iut_toeplitz_variant"));

    TEST_SUCCESS;

cleanup:

    TEST_END;
}

#endif
