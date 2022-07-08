/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Test API for checking PTP
 *
 * Implementation of TAPI for PTP tests.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#include "net_drv_ts.h"
#include "net_drv_ptp.h"

/* See description in net_drv_ptp.h */
void
net_drv_open_ptp_fd(rcf_rpc_server *rpcs, const char *if_name, int *fd)
{
    struct ifreq ifreq_var;
    struct tarpc_ethtool_ts_info ts_info;

    char path[1024];
    int s = -1;
    int rc;

    s = rpc_socket(rpcs, RPC_PF_INET, RPC_SOCK_DGRAM, RPC_PROTO_DEF);

    memset(&ifreq_var, 0, sizeof(ifreq_var));
    strncpy(ifreq_var.ifr_name, if_name,
            sizeof(ifreq_var.ifr_name));

    memset(&ts_info, 0, sizeof(ts_info));
    ifreq_var.ifr_data = (char *)&ts_info;
    ts_info.cmd = RPC_ETHTOOL_GET_TS_INFO;

    RPC_AWAIT_ERROR(rpcs);
    rc = rpc_ioctl(rpcs, s, RPC_SIOCETHTOOL, &ifreq_var);
    if (rc < 0)
    {
        ERROR_VERDICT("ioctl(SIOCETHTOOL/ETHTOOL_GET_TS_INFO) failed with "
                      "error %r", RPC_ERRNO(rpcs));
        RPC_CLOSE(rpcs, s);
        TEST_STOP;
    }
    RPC_CLOSE(rpcs, s);

    if (ts_info.phc_index < 0)
        TEST_SKIP("PTP device index is not known");

    TE_SPRINTF(path, "/dev/ptp%d", (int)(ts_info.phc_index));
    RPC_AWAIT_ERROR(rpcs);
    *fd = rpc_open(rpcs, path, RPC_O_RDWR, 0);
    if (*fd < 0)
    {
        TEST_VERDICT("Failed to open PTP device file, errno=%r",
                     RPC_ERRNO(rpcs));
    }
}

/* See description in net_drv_ptp.h */
double
net_drv_timespec_diff(tarpc_timespec *tsa, tarpc_timespec *tsb)
{
    double seconds;
    double nseconds;

    seconds = (int64_t)(tsa->tv_sec) - tsb->tv_sec;
    nseconds = (int64_t)(tsa->tv_nsec) - tsb->tv_nsec;

    return seconds + nseconds / 1000000000.0;
}
