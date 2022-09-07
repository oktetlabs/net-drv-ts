/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-sys_offset Check PTP_SYS_OFFSET ioctl
 * @ingroup ptp
 * @{
 *
 * @objective Check that @c PTP_SYS_OFFSET ioctl can be used to obtain
 *            samples of PTP device time and system time at the same moment.
 *
 * @param env             Testing environment:
 *                        - @ref env-peer2peer
 * @param n_samples       How many samples to request from
 *                        PTP_SYS_OFFSET
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-PTP006.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "ptp/sys_offset"

#include "net_drv_test.h"
#include "math.h"
#include "tapi_mem.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    unsigned int n_samples;

    int fd = -1;

    tarpc_ptp_sys_offset samples;
    unsigned int i;
    double *diffs = NULL;
    unsigned int n_diffs;
    double avg_diff;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_UINT_PARAM(n_samples);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd);

    TEST_STEP("Call @b ioctl(@c PTP_SYS_OFFSET) on the PTP device FD "
              "asking for @p n_samples samples.");

    samples.n_samples = n_samples;

    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_ioctl(iut_rpcs, fd, RPC_PTP_SYS_OFFSET, &samples);
    if (rc < 0)
    {
        if (RPC_ERRNO(iut_rpcs) == TE_RC(TE_TA_UNIX, TE_ENOIOCTLCMD))
            TEST_SKIP("PTP_SYS_OFFSET request is not supported");

        TEST_VERDICT("ioctl(PTP_SYS_OFFSET) unexpectedly failed with "
                     "error " RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }

    if (samples.n_samples != n_samples)
    {
        TEST_VERDICT("Returned number of samples does not match "
                     "requested one");
    }

    TEST_STEP("Compute average difference between consecutive pairs of "
              "timestamps obtained from @c PTP_SYS_OFFSET. Check that "
              "difference between every such pair is very close to that "
              "average.");

    /*
     * PTP_SYS_OFFSET returns 2 * n_samples + 1 timestamps:
     * system ts, PTP ts, system ts, PTP ts, ..., system ts.
     */
    n_diffs = 2 * n_samples;
    diffs = tapi_calloc(n_diffs, sizeof(*diffs));

    avg_diff = 0;
    for (i = 0; i < n_diffs; i++)
    {
        diffs[i] = net_drv_ptp_clock_time_diff(&samples.ts[i],
                                               &samples.ts[i + 1]);

        /*
         * If i % 2 == 0, computed difference is
         * system timestamp - PTP timestamp, so we
         * negate it.
         */
        if (i % 2 == 0)
            diffs[i] = -diffs[i];

        avg_diff += diffs[i];
    }
    avg_diff /= n_diffs;

    net_drv_ptp_offs_check_dev_avg(diffs, n_diffs, avg_diff);

    TEST_STEP("Estimate offset between PTP and system clocks with help "
              "of two consecutive @b clock_gettime() calls. Check that "
              "this estimate is close to the average difference between "
              "timestamps pairs obtained from @c PTP_SYS_OFFSET.");

    net_drv_ptp_offs_check_dev_gettime(iut_rpcs, fd, RPC_CLOCK_REALTIME,
                                       avg_diff);

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, fd);
    free(diffs);

    TEST_END;
}
