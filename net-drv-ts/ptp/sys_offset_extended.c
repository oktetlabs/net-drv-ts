/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-sys_offset_extended Check PTP_SYS_OFFSET_EXTENDED ioctl
 * @ingroup ptp
 * @{
 *
 * @objective Check that @c PTP_SYS_OFFSET_EXTENDED ioctl can be used to
 *            obtain samples of PTP device time and system time at the same
 *            moment.
 *
 * @param env             Testing environment:
 *                        - @ref env-peer2peer
 * @param n_samples       How many samples to request from
 *                        PTP_SYS_OFFSET_EXTENDED
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "ptp/sys_offset_extended"

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

    tarpc_ptp_sys_offset_extended samples;
    double avg_diff;
    double *diffs = NULL;
    unsigned int i;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_UINT_PARAM(n_samples);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd);

    TEST_STEP("Call @b ioctl(@c PTP_SYS_OFFSET_EXTENDED) on the PTP device "
              "FD asking for @p n_samples samples.");

    samples.n_samples = n_samples;

    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_ioctl(iut_rpcs, fd, RPC_PTP_SYS_OFFSET_EXTENDED, &samples);
    if (rc < 0)
    {
        if (RPC_ERRNO(iut_rpcs) == TE_RC(TE_TA_UNIX, TE_ENOIOCTLCMD) ||
            RPC_ERRNO(iut_rpcs) == RPC_EOPNOTSUPP)
        {
            TEST_SKIP("PTP_SYS_OFFSET_EXTENDED request is not supported");
        }

        TEST_VERDICT("ioctl(PTP_SYS_OFFSET_EXTENDED) unexpectedly failed "
                     "with error " RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }

    if (samples.n_samples != n_samples)
    {
        TEST_VERDICT("Returned number of samples does not match "
                     "requested one");
    }

    TEST_STEP("Compute average difference between timestamps from "
              "PTP and system clocks over the array of retrieved samples. "
              "Check that difference in every sample is close to that "
              "average.");

    diffs = tapi_calloc(n_samples, sizeof(*diffs));
    avg_diff = 0;
    for (i = 0; i < n_samples; i++)
    {
        diffs[i] = (net_drv_ptp_clock_time_diff(&samples.ts[i].phc,
                                                &samples.ts[i].sys1) +
                    net_drv_ptp_clock_time_diff(&samples.ts[i].phc,
                                                &samples.ts[i].sys2)) / 2.0;
        avg_diff += diffs[i];
    }

    avg_diff /= n_samples;

    RING("Average difference between clocks is %f sec", avg_diff);

    net_drv_ptp_offs_check_dev_avg(diffs, n_samples, avg_diff);

    TEST_STEP("Estimate offset between PTP and system clocks with help "
              "of two consecutive @b clock_gettime() calls. Check that "
              "this estimate is close to the average difference between "
              "clocks obtained with @c PTP_SYS_OFFSET_EXTENDED.");
    net_drv_ptp_offs_check_dev_gettime(iut_rpcs, fd, RPC_CLOCK_REALTIME,
                                       avg_diff);

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, fd);
    free(diffs);

    TEST_END;
}
