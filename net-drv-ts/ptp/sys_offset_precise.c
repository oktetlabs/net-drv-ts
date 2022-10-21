/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-sys_offset_precise Check PTP_SYS_OFFSET_PRECISE ioctl
 * @ingroup ptp
 * @{
 *
 * @objective Check that @c PTP_SYS_OFFSET_PRECISE ioctl can be used to
 *            obtain PTP device time and system time at the same moment.
 *
 * @param env             Testing environment:
 *                        - @ref env-peer2peer
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "ptp/sys_offset_precise"

#include "net_drv_test.h"
#include "tapi_mem.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    int fd = -1;
    double offs;

    tarpc_ptp_sys_offset_precise req;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd, "");

    TEST_STEP("Call @b ioctl(@c PTP_SYS_OFFSET_PRECISE) on the PTP device "
              "FD to get timestamps for PTP device, @c CLOCK_REALTIME "
              "and @c CLOCK_MONOTONIC_RAW at the same moment.");

    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_ioctl(iut_rpcs, fd, RPC_PTP_SYS_OFFSET_PRECISE, &req);
    if (rc < 0)
    {
        if (RPC_ERRNO(iut_rpcs) == TE_RC(TE_TA_UNIX, TE_ENOIOCTLCMD) ||
            RPC_ERRNO(iut_rpcs) == RPC_EOPNOTSUPP)
        {
            TEST_SKIP("PTP_SYS_OFFSET_PRECISE request is not supported");
        }

        TEST_VERDICT("ioctl(PTP_SYS_OFFSET_PRECISE) unexpectedly failed "
                     "with error " RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }

    TEST_STEP("Estimate difference between PTP clock and @c CLOCK_REALTIME "
              "as @b device - @b sys_realtime in obtained response. Check "
              "that two consecutive @b clock_gettime() calls on PTP device "
              "and @c CLOCK_REALTIME show similar difference between "
              "timestamps.");
    offs = net_drv_ptp_clock_time_diff(&req.device, &req.sys_realtime);
    net_drv_ptp_offs_check_dev_gettime(iut_rpcs, fd, RPC_CLOCK_REALTIME,
                                       offs);

    TEST_STEP("Estimate difference between PTP clock and "
              "@c CLOCK_MONOTONIC_RAW as @b device - @b sys_monoraw in "
              "obtained response. Check that two consecutive "
              "@b clock_gettime() calls on PTP device and "
              "@c CLOCK_MONOTONIC_RAW show similar difference between "
              "timestamps.");
    offs = net_drv_ptp_clock_time_diff(&req.device, &req.sys_monoraw);
    net_drv_ptp_offs_check_dev_gettime(iut_rpcs, fd,
                                       RPC_CLOCK_MONOTONIC_RAW, offs);

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, fd);

    TEST_END;
}
