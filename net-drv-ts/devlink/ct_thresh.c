/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Devlink tests
 */

/**
 * @defgroup devlink-ct_thresh ct_thresh device parameter
 * @ingroup devlink
 * @{
 *
 * @objective Check that ct_thresh device parameter can be retrieved
 *            and changed via devlink API.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-DVL002, X3-DVL003.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "devlink/ct_thresh"

#include "net_drv_test.h"
#include "tapi_cfg_pci.h"
#include "tapi_cfg_base.h"
#include "tapi_job_factory_rpc.h"
#include "tapi_ping.h"

/* How long to wait for ping termination, in ms */
#define PING_TIMEOUT 120000

/* Minimum value for ct_thresh parameter */
#define MIN_CT_THRESH 64

/* Number of ping packets to send */
#define PING_PKTS 60

/* Check whether ct_thresh parameter value was changed */
static void
check_param_change(const char *pci_oid, uint64_t *prev_val,
                   const char *when)
{
    uint64_t cur_val;

    CHECK_RC(cfg_synchronize(pci_oid, TRUE));

    CHECK_RC(tapi_cfg_pci_get_param_uint(pci_oid, "ct_thresh",
                                         TAPI_CFG_PCI_PARAM_RUNTIME,
                                         &cur_val));

    RING("Now value of ct_thresh parameter is %" TE_PRINTF_64 "u", cur_val);

    if (cur_val != *prev_val)
    {
        WARN_VERDICT("Value of ct_thresh parameter changed %s", when);
        *prev_val = cur_val;
    }
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *tst_addr = NULL;

    tapi_ping_opt ping_opts = tapi_ping_default_opt;
    tapi_job_factory_t *factory = NULL;
    tapi_ping_app *app = NULL;
    tapi_ping_report report;
    char *dst_addr = NULL;

    char *pci_oid = NULL;
    uint64_t prev_val;
    uint64_t new_val;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(tst_rpcs, tst_addr);

    rc = tapi_cfg_pci_oid_by_net_if(iut_rpcs->ta, iut_if->if_name,
                                    &pci_oid);
    if (rc != 0)
        TEST_SKIP("Cannot find PCI device for the tested interface");

    TEST_STEP("Check that ct_thresh parameter value can be retrieved "
              "for the IUT interface.");
    rc = tapi_cfg_pci_get_param_uint(pci_oid, "ct_thresh",
                                     TAPI_CFG_PCI_PARAM_RUNTIME,
                                     &prev_val);
    if (rc == TE_RC(TE_CS, TE_ENOENT))
    {
        TEST_SKIP("ct_thresh parameter is not present");
    }
    else if (rc != 0)
    {
        TEST_VERDICT("Failed to retrieve ct_thresh parameter, rc = %r",
                     rc);
    }

    RING("Current value of ct_thresh parameter is %" TE_PRINTF_64 "u",
         prev_val);

    TEST_STEP("Ping Tester from IUT.");

    CHECK_RC(te_sockaddr_h2str(tst_addr, &dst_addr));
    ping_opts.interface = iut_if->if_name;
    ping_opts.destination = dst_addr;
    ping_opts.packet_count = PING_PKTS;

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs, &factory));
    CHECK_RC(tapi_ping_create(factory, &ping_opts, &app));

    CHECK_RC(tapi_ping_start(app));
    CHECK_RC(tapi_ping_wait(app, PING_TIMEOUT));

    CHECK_RC(tapi_ping_get_report(app, &report));

    RING("%u packets were transmitted, %u packets were received",
         report.transmitted, report.received);

    if (report.transmitted == 0)
        ERROR_VERDICT("No packets were transmitted when pinging");
    if (report.received == 0)
        ERROR_VERDICT("No packets were received when pinging");

    TEST_STEP("Check that value of ct_thresh parameter was not changed.");
    check_param_change(pci_oid, &prev_val, "after running ping");

    TEST_STEP("Set IUT interface down.");
    CHECK_RC(tapi_cfg_base_if_down(iut_rpcs->ta, iut_if->if_name));
    CFG_WAIT_CHANGES;

    TEST_STEP("Check that value of ct_thresh parameter was not changed.");
    check_param_change(pci_oid, &prev_val, "after setting interface down");

    TEST_STEP("Set IUT interface up.");
    CHECK_RC(tapi_cfg_base_if_up(iut_rpcs->ta, iut_if->if_name));
    CFG_WAIT_CHANGES;

    TEST_STEP("Check that value of ct_thresh parameter was not changed.");
    check_param_change(pci_oid, &prev_val, "after setting interface up");

    /* Note: ct_thresh must be a multiple of 64 */
    if (prev_val > MIN_CT_THRESH)
        new_val = prev_val - MIN_CT_THRESH;
    else
        new_val = prev_val + MIN_CT_THRESH;

    TEST_STEP("Check that ct_thresh parameter value can be changed.");
    net_drv_set_pci_param_uint(pci_oid, "ct_thresh",
                               TAPI_CFG_PCI_PARAM_RUNTIME,
                               new_val, "Changing parameter value");

    TEST_SUCCESS;

cleanup:

    free(pci_oid);

    CLEANUP_CHECK_RC(tapi_ping_destroy(app));
    tapi_job_factory_destroy(factory);
    free(dst_addr);

    TEST_END;
}
