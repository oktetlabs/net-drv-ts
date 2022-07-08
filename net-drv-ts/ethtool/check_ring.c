/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-check_ring get and check ring size with ethtool
 * @ingroup ethtool
 * @{
 *
 * @objective Check ring size with ethtool
 *
 * @param env            Testing environment:
 *                       - @ref env-iut_only
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ET014, X3-ET015.
 *
 * @author Trempolskii Demid <demidtr@oktetlabs.ru>
 */

#define TE_TEST_NAME "ethtool/check_ring"

#include "net_drv_test.h"
#include "tapi_ethtool.h"
#include "tapi_job_factory_rpc.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    tapi_job_factory_t *factory = NULL;
    tapi_ethtool_opt opts = tapi_ethtool_default_opt;
    tapi_ethtool_report report = tapi_ethtool_default_report;


    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs, &factory));

    TEST_STEP("Run ethtool \\--show-ring iut_if");
    opts.cmd = TAPI_ETHTOOL_CMD_SHOW_RING;
    opts.if_name = iut_if->if_name;

    TEST_STEP("Check if it is terminated successfully");
    rc = tapi_ethtool(factory, &opts, &report);
    if (rc != 0)
        TEST_VERDICT("Failed to process ethtool command");

    TEST_STEP("Check if output can be parsed and doesn't"
			  "print anything to strderr");
    if (report.err_out)
        ERROR_VERDICT("ethtool printed something to stderr");

    TEST_STEP("Log obtained ring size");
    RING_VERDICT("Current ring sizes RX: %i, TX: %i",
                 report.data.ring.rx, report.data.ring.tx);
    RING_VERDICT("Preset maximums RX: %i, TX: %i",
                 report.data.ring.rx_max, report.data.ring.tx_max);
    TEST_SUCCESS;

cleanup:

    tapi_job_factory_destroy(factory);
    tapi_ethtool_destroy_report(&report);

    TEST_END;
}
