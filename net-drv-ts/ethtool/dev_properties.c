/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Ethtool tests
 */

/**
 * @defgroup ethtool-dev_properties Getting device properties with ethtool
 * @ingroup ethtool
 * @{
 *
 * @objective Check that ethtool shows interface properties.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ET001, X3-ET010.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "ethtool/dev_properties"

#include "net_drv_test.h"
#include "tapi_ethtool.h"
#include "tapi_job_factory_rpc.h"
#include "tapi_cfg_base.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    tapi_job_factory_t *factory = NULL;
    tapi_ethtool_report report = tapi_ethtool_default_report;
    tapi_ethtool_opt opts = tapi_ethtool_default_opt;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs, &factory));

    TEST_STEP("Run 'ethtool @p iut_if' on IUT. Check that it terminated "
              "with zero exit code and did not print anything to "
              "@b stderr.");

    opts.cmd = TAPI_ETHTOOL_CMD_NONE;
    opts.if_name = iut_if->if_name;

    rc = tapi_ethtool(factory, &opts, &report);
    if (rc != 0)
        TEST_VERDICT("Failed to process ethtool command, rc=%r", rc);

    if (report.err_out)
        ERROR_VERDICT("ethtool printed something to stderr");

    TEST_STEP("Check that @b ethtool shows that the link is UP.");
    if (!report.data.if_props.link)
        TEST_VERDICT("ethtool does not report interface as being UP");

    TEST_STEP("Set IUT interface DOWN.");
    CHECK_RC(tapi_cfg_base_if_down(iut_rpcs->ta, iut_if->if_name));

    TEST_STEP("Run 'ethtool @p iut_if' on IUT. Check that it terminated "
              "with zero exit code and did not print anything to "
              "@b stderr.");

    rc = tapi_ethtool(factory, &opts, &report);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to process ethtool command after "
                     "setting interface down, rc=%r", rc);
    }

    if (report.err_out)
    {
        ERROR_VERDICT("ethtool printed something to stderr after "
                      "interface was set down");
    }

    TEST_STEP("Check that @b ethtool shows that the link is DOWN.");
    if (report.data.if_props.link)
        TEST_VERDICT("ethtool does not report interface as being DOWN");

    TEST_SUCCESS;

cleanup:

    tapi_ethtool_destroy_report(&report);
    tapi_job_factory_destroy(factory);

    TEST_END;
}
