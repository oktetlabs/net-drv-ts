/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * PTP tests
 */

/**
 * @defgroup ptp-clock_caps Get PTP clock capabilities
 * @ingroup ptp
 * @{
 *
 * @objective Check that clock cababilities can be obtained for PTP
 *            device associated with the tested network interface.
 *
 * @param env              Testing environment:
 *                         - @ref env-peer2peer
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-PTP007.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#define TE_TEST_NAME "ptp/clock_caps"

#include "net_drv_test.h"

/* Expected maximum frequency adjustment, parts per billion */
#define EXP_MAX_ADJ 100000000
#define EXP_MAX_ADJ_X3 49999999

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;

    int fd = -1;
    tarpc_ptp_clock_caps caps;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);

    TEST_STEP("Find out PTP device associated with the IUT interface, "
              "call @b open() to get its FD.");
    net_drv_open_ptp_fd(iut_rpcs, iut_if->if_name, &fd);

    TEST_STEP("Call @b ioctl(@c PTP_CLOCK_GETCAPS) on the PTP device FD.");
    RPC_AWAIT_ERROR(iut_rpcs);
    rc = rpc_ioctl(iut_rpcs, fd, RPC_PTP_CLOCK_GETCAPS, &caps);
    if (rc < 0)
    {
        if (RPC_ERRNO(iut_rpcs) == TE_RC(TE_TA_UNIX, TE_ENOIOCTLCMD))
            TEST_SKIP("PTP_CLOCK_GETCAPS request is not supported");

        TEST_VERDICT("ioctl(PTP_CLOCK_GETCAPS) unexpectedly failed with "
                     "error " RPC_ERROR_FMT, RPC_ERROR_ARGS(iut_rpcs));
    }

    TEST_STEP("Check reported capabilities. Frequency adjustment up to "
              "100_000_000 ppb and Pulse Per Second should be supported; "
              "there should be at least one external timestamp channel.");

    if (caps.max_adj == 0)
        WARN_VERDICT("No frequency adjustment is supported");
    else if (caps.max_adj < EXP_MAX_ADJ_X3)
        WARN_VERDICT("Frequency adjustment range is significantly less "
                     "than expected");
    else if (caps.max_adj < EXP_MAX_ADJ)
        WARN_VERDICT("Frequency adjustment range is less than expected");

    if (caps.n_ext_ts == 0)
        WARN_VERDICT("No external timestamp channels");

    if (caps.pps == 0)
        WARN_VERDICT("Pulse Per Second is not supported");

    TEST_SUCCESS;

cleanup:

    CLEANUP_RPC_CLOSE(iut_rpcs, fd);

    TEST_END;
}
