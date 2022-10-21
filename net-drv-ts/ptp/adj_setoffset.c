/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-adj_setoffset Add offset to the current time
 * @ingroup ptp
 * @{
 *
 * @objective Check that offset can be added to the current time
 *            of PTP device associated with the tested network interface.
 *
 * @param env              Testing environment:
 *                         - @ref env-peer2peer
 * @param time_offset      Time offset to add, in seconds
 * @param min_sleep        Minimum time to sleep between clock_gettime()
 *                         calls when checking how the clock advances,
 *                         seconds
 * @param max_sleep        Maximum time to sleep between clock_gettime()
 *                         calls when checking how the clock advances,
 *                         seconds
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-PTP003.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "ptp/adj_setoffset"

#include "net_drv_test.h"
#include "te_time.h"
#include "math.h"

/*
 * Fill timeval structure fields according to time_offset.
 */
static void
time_offset_to_timeval(double time_offset, tarpc_timeval *tv)
{
    double abs_offset;
    double a;
    double b;

    abs_offset = fabs(time_offset);
    a = floor(abs_offset);
    b = abs_offset - a;
    if (time_offset < 0)
    {
        if (b > 0)
        {
            /* tv_usec field must always be positive */
            a = -(a + 1.0);
            b = 1.0 - b;
        }
        else
        {
            a = -a;
        }
    }

    tv->tv_sec = a;
    tv->tv_usec = b * 1000000.0;
}

/*
 * Use clock_adjtime() with ADJ_SETOFFSET mode to add offset to the current
 * time. Check that the clock works as expected after that.
 */
static void
adj_check_time(rcf_rpc_server *rpcs, int clock_fd,
               int min_sleep, int max_sleep, double time_offset,
               const char *stage)
{
    tarpc_timespec ts1;
    tarpc_timespec ts2;

    tarpc_timex adj_params;
    double ts_diff;
    double exp_diff;
    int sleep_time;
    int rc;

    rpc_clock_gettime(rpcs, TARPC_CLOCK_ID_FD, clock_fd, &ts1);

    memset(&adj_params, 0, sizeof(adj_params));
    adj_params.modes = RPC_ADJ_SETOFFSET;
    time_offset_to_timeval(time_offset, &adj_params.time);

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_clock_adjtime(rpcs, TARPC_CLOCK_ID_FD, clock_fd, &adj_params);
    if (rc < 0)
    {
        TEST_VERDICT("%s: clock_adjtime() failed unexpectedly with error "
                     RPC_ERROR_FMT, stage, RPC_ERROR_ARGS(rpcs));
    }

    rpc_clock_gettime(rpcs, TARPC_CLOCK_ID_FD, clock_fd, &ts2);

    ts_diff = net_drv_timespec_diff(&ts2, &ts1);
    RING("Measured difference in timestamps is %.3f seconds, while "
         "expected is %.3f seconds", ts_diff, time_offset);
    if (fabs(ts_diff - time_offset) > NET_DRV_DEF_PTP_INACC)
    {

        if (fabs(ts_diff) < NET_DRV_DEF_PTP_INACC)
            TEST_VERDICT("%s: time did not change noticeably", stage);
        else
            TEST_VERDICT("%s: time changed in unexpected way", stage);
    }

    sleep_time = rand_range(min_sleep, max_sleep);
    te_motivated_sleep(sleep_time, "wait for a while to check whether "
                       "hardware clock advances as expected");

    rpc_clock_gettime(rpcs, TARPC_CLOCK_ID_FD, clock_fd, &ts2);
    ts_diff = net_drv_timespec_diff(&ts2, &ts1);
    exp_diff = time_offset + sleep_time;

    RING("Measured difference in timestamps is %.3f seconds, while "
         "expected is %.3f seconds", ts_diff, exp_diff);
    if (fabs(ts_diff - exp_diff) > NET_DRV_DEF_PTP_INACC)
    {
        TEST_VERDICT("%s: after a delay timestamp does not match "
                     "expectation", stage);
    }
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    int fd = -1;

    double time_offset;
    int min_sleep;
    int max_sleep;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_DOUBLE_PARAM(time_offset);
    TEST_GET_INT_PARAM(min_sleep);
    TEST_GET_INT_PARAM(max_sleep);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd, "");

    TEST_STEP("Use @b clock_adjtime() to add @p time_offset seconds "
              "to the current time on the PTP device. With help of "
              "@b clock_gettime() check whether the clock works as "
              "expected after that.");
    adj_check_time(iut_rpcs, fd, min_sleep, max_sleep, time_offset,
                   "Adding time offset");

    TEST_STEP("Use @b clock_adjtime() to subtract @p time_offset seconds "
              "from the current time on the PTP device. With help of "
              "@b clock_gettime() check whether the clock works as "
              "expected after that.");
    adj_check_time(iut_rpcs, fd, min_sleep, max_sleep, -time_offset,
                   "Subtracting time offset");

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, fd);

    TEST_END;
}
