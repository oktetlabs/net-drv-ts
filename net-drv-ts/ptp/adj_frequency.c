/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-adj_frequency Tune clock frequency
 * @ingroup ptp
 * @{
 *
 * @objective Check that frequency can be tuned for PTP device
 *            associated with the tested network interface.
 *
 * @param env              Testing environment:
 *                         - @ref env-peer2peer
 * @param freq             Frequency offset, in parts per million
 * @param min_sleep        Minimum time to sleep between clock_gettime()
 *                         calls when checking how the clock advances,
 *                         seconds
 * @param max_sleep        Maximum time to sleep between clock_gettime()
 *                         calls when checking how the clock advances,
 *                         seconds
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-PTP004, X3-PTP005.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "ptp/adj_frequency"

#include "net_drv_test.h"
#include "te_time.h"
#include "math.h"

/*
 * Use clock_adjtime() with ADJ_FREQUENCY mode to tune clock frequency.
 * Check that the clock works as expected after that.
 */
static void
adj_check_freq(rcf_rpc_server *rpcs, int clock_fd,
               int min_sleep, int max_sleep, double freq,
               const char *stage)
{
    tarpc_timespec ts1;
    tarpc_timespec ts2;

    tarpc_timex adj_params;
    double ts_diff;
    double exp_diff;
    double no_change_diff;
    int sleep_time;
    int rc;

    rpc_clock_gettime(rpcs, TARPC_CLOCK_ID_FD, clock_fd, &ts1);

    memset(&adj_params, 0, sizeof(adj_params));
    adj_params.modes = RPC_ADJ_FREQUENCY;

    /*
     * "man clock_adjtime" says that freq field in timex structure is
     * specified "with a 16-bit fractional part, which means that a value
     * of 1 <...> actually means 2^-16 ppm, and 2^16=65536 is 1 ppm".
     */
    adj_params.freq = freq * (1 << 16);

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_clock_adjtime(rpcs, TARPC_CLOCK_ID_FD, clock_fd, &adj_params);
    if (rc < 0)
    {
        TEST_VERDICT("%s: clock_adjtime() failed unexpectedly with error "
                     RPC_ERROR_FMT, stage, RPC_ERROR_ARGS(rpcs));
    }

    sleep_time = rand_range(TE_SEC2MS(min_sleep), TE_SEC2MS(max_sleep));
    te_motivated_msleep(sleep_time, "wait for a while to check whether "
                        "hardware clock advances as expected");

    rpc_clock_gettime(rpcs, TARPC_CLOCK_ID_FD, clock_fd, &ts2);
    ts_diff = net_drv_timespec_diff(&ts2, &ts1);
    no_change_diff = sleep_time / 1000.0;
    exp_diff = no_change_diff * (1.0 + freq / 1000000.0);

    RING("Measured difference in timestamps is %.3f seconds, while "
         "expected is %.3f seconds", ts_diff, exp_diff);
    if (fabs(ts_diff - exp_diff) > NET_DRV_DEF_PTP_INACC)
    {
        if (fabs(ts_diff - no_change_diff) < NET_DRV_DEF_PTP_INACC)
        {
            TEST_VERDICT("%s(): frequency offset has no "
                         "significant effect", stage);
        }
        else
        {
            TEST_VERDICT("%s: after a delay timestamp does not match "
                         "expectation", stage);
        }
    }
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    int fd = -1;

    double freq;
    int min_sleep;
    int max_sleep;

    tarpc_timex adj_params;
    te_bool undo_change = FALSE;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_DOUBLE_PARAM(freq);
    TEST_GET_INT_PARAM(min_sleep);
    TEST_GET_INT_PARAM(max_sleep);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd, "");

    TEST_STEP("Call @b clock_adjtime() to check whether current frequency "
              "offset is zero.");
    memset(&adj_params, 0, sizeof(adj_params));
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_clock_adjtime(iut_rpcs, TARPC_CLOCK_ID_FD, fd, &adj_params);
    if (rc < 0)
    {
        TEST_VERDICT("Initial clock_adjtime() call failed unexpectedly "
                     "with error " RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }
    if (adj_params.freq != 0)
        WARN_VERDICT("Initial frequency offset is not zero");

    TEST_STEP("Use @b clock_adjtime() to tune PTP device frequency "
              "according to @p freq. With help of @b clock_gettime() "
              "check whether the clock works as expected after that.");
    undo_change = TRUE;
    adj_check_freq(iut_rpcs, fd, min_sleep, max_sleep, freq,
                   "Setting frequency offset");

    TEST_STEP("Use @b clock_adjtime() to undo the previously made change. "
              "With help of @b clock_gettime() check whether the clock "
              "works as expected after that.");
    adj_check_freq(iut_rpcs, fd, min_sleep, max_sleep, adj_params.freq,
                   "Restoring initial frequency");
    undo_change = FALSE;

    TEST_SUCCESS;

cleanup:

    if (undo_change)
    {
        adj_params.modes = RPC_ADJ_FREQUENCY;
        rpc_clock_adjtime(iut_rpcs, TARPC_CLOCK_ID_FD, fd, &adj_params);
    }
    CLEANUP_RPC_CLOSE(iut_rpcs, fd);

    TEST_END;
}
