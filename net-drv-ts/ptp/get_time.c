/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-get_time Get current time
 * @ingroup ptp
 * @{
 *
 * @objective Check that current time can be obtained from PTP device
 *            associated with the tested network interface.
 *
 * @param env              Testing environment:
 *                         - @ref env-peer2peer
 * @param min_sleep        Minimum time to sleep between clock_gettime()
 *                         calls, seconds
 * @param max_sleep        Maximum time to sleep between clock_gettime()
 *                         calls, seconds
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-PTP002.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "ptp/get_time"

#include "net_drv_test.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    int min_sleep;
    int max_sleep;

    int fd = -1;
    tarpc_timespec ts1;
    tarpc_timespec ts2;
    int sleep_time;
    double ts_diff;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_INT_PARAM(min_sleep);
    TEST_GET_INT_PARAM(max_sleep);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd);

    TEST_STEP("Call @b clock_gettime() with PTP device FD to get the "
              "first timestamp.");
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_clock_gettime(iut_rpcs, TARPC_CLOCK_ID_FD, fd, &ts1);
    if (rc < 0)
    {
        TEST_VERDICT("The first clock_gettime() call failed with error "
                     RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }

    TEST_STEP("Sleep for a while.");
    sleep_time = rand_range(min_sleep, max_sleep);
    te_motivated_sleep(sleep_time, "wait for a while to check whether "
                       "hardware clock ticks");

    TEST_STEP("Call @b clock_gettime() with PTP device FD to get the "
              "second timestamp.");
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_clock_gettime(iut_rpcs, TARPC_CLOCK_ID_FD, fd, &ts2);
    if (rc < 0)
    {
        TEST_VERDICT("The second clock_gettime() call failed with error "
                     RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }

    TEST_STEP("Check that difference between the obtained timestamps "
              "corresponds to the delay between @b clock_gettime() calls.");

    ts_diff = net_drv_timespec_diff(&ts2, &ts1);

    RING("Hardware clock thinks that %.3f seconds passed", ts_diff);

    if (fabs(ts_diff - sleep_time) > NET_DRV_DEF_PTP_INACC)
    {
        TEST_VERDICT("Duration measured with hardware clock differs too "
                     "much from delay between clock_gettime() calls");
    }

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, fd);

    TEST_END;
}
