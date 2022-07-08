/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-set_time Set clock time
 * @ingroup ptp
 * @{
 *
 * @objective Check that time can be set for PTP device associated with
 *            the tested network interface.
 *
 * @param env              Testing environment:
 *                         - @ref env-peer2peer
 * @param ts               Timestamp to set, in seconds
 *                         (negative value means current time)
 * @param min_sleep        Minimum time to sleep between clock_gettime()
 *                         calls, seconds
 * @param max_sleep        Maximum time to sleep between clock_gettime()
 *                         calls, seconds
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-PTP001.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "ptp/set_time"

#include "net_drv_test.h"
#include "te_time.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    double ts;
    int min_sleep;
    int max_sleep;

    int fd = -1;
    int sleep_time;
    double ts_diff;

    struct timeval tv;
    tarpc_timespec ts_init;
    tarpc_timespec ts_set;
    tarpc_timespec ts_got;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_DOUBLE_PARAM(ts);
    TEST_GET_INT_PARAM(min_sleep);
    TEST_GET_INT_PARAM(max_sleep);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd);

    if (ts < 0)
    {
        CHECK_RC(te_gettimeofday(&tv, NULL));
        ts_set.tv_sec = tv.tv_sec;
        ts_set.tv_nsec = tv.tv_usec * 1000LL;
    }
    else
    {
        ts_set.tv_sec = ts;
        ts_set.tv_nsec = (ts - ts_set.tv_sec) * 1000000000LL;
    }

    TEST_STEP("Get initial timestamp from the PTP device.");
    rpc_clock_gettime(iut_rpcs, TARPC_CLOCK_ID_FD, fd, &ts_init);

    TEST_STEP("Call @b clock_settime() to set current time for the PTP "
              "device according to @p ts.");
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_clock_settime(iut_rpcs, TARPC_CLOCK_ID_FD, fd, &ts_set);
    if (rc < 0)
    {
        TEST_VERDICT("clock_settime() call failed with error "
                     RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }

    TEST_STEP("Call @b clock_gettime() to get current time from the PTP "
              "device, check that difference from the previously set "
              "value is not big.");
    rpc_clock_gettime(iut_rpcs, TARPC_CLOCK_ID_FD, fd, &ts_got);
    ts_diff = net_drv_timespec_diff(&ts_got, &ts_set);
    if (fabs(ts_diff) > NET_DRV_DEF_PTP_INACC)
    {
        if (fabs(net_drv_timespec_diff(&ts_got, &ts_init)) <
                                                NET_DRV_DEF_PTP_INACC)
        {
            TEST_VERDICT("It seems clock_settime() had no effect");
        }
        else
        {
            TEST_VERDICT("Timestamp obtained immediately after setting "
                         "differs too much from the set one");
        }
    }

    TEST_STEP("Sleep for a while (between @p min_sleep and @p max_sleep "
              "seconds).");
    sleep_time = rand_range(min_sleep, max_sleep);
    te_motivated_sleep(sleep_time, "wait for a while to check whether "
                       "hardware clock ticks");

    TEST_STEP("Call @b clock_gettime() to get another timestamp from "
              "the PTP device. Check that the timestamp advanced according "
              "to the delay between calls.");
    rpc_clock_gettime(iut_rpcs, TARPC_CLOCK_ID_FD, fd, &ts_got);
    ts_diff = net_drv_timespec_diff(&ts_got, &ts_set);
    if (fabs(ts_diff - sleep_time) > NET_DRV_DEF_PTP_INACC)
    {
        TEST_VERDICT("Timestamp obtained after delay differs too much from "
                     "the expected one");
    }

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, fd);

    TEST_END;
}
