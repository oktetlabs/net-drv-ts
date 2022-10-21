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
net_drv_open_ptp_fd(rcf_rpc_server *rpcs, const char *if_name, int *fd,
                    const char *vpref)
{
    struct ifreq ifreq_var;
    struct tarpc_ethtool_ts_info ts_info;

    char path[1024];
    te_string vpref_str = TE_STRING_INIT_STATIC(256);
    int s = -1;
    int rc;

    if (vpref != NULL && *vpref != '\0')
    {
        te_string_append(&vpref_str, "%s: ", vpref);
        vpref = te_string_value(&vpref_str);
    }
    else
    {
        vpref = "";
    }

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
        ERROR_VERDICT("%sioctl(SIOCETHTOOL/ETHTOOL_GET_TS_INFO) failed "
                      "with error %r", vpref, RPC_ERRNO(rpcs));
        RPC_CLOSE(rpcs, s);
        TEST_STOP;
    }
    RPC_CLOSE(rpcs, s);

    if (ts_info.phc_index < 0)
        TEST_SKIP("%sPTP device index is not known", vpref);

    TE_SPRINTF(path, "/dev/ptp%d", (int)(ts_info.phc_index));
    RPC_AWAIT_ERROR(rpcs);
    *fd = rpc_open(rpcs, path, RPC_O_RDWR, 0);
    if (*fd < 0)
    {
        TEST_VERDICT("%sfailed to open PTP device file, errno=%r",
                     vpref, RPC_ERRNO(rpcs));
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

/* See description in net_drv_ptp.h */
double
net_drv_ptp_clock_time_diff(tarpc_ptp_clock_time *tsa,
                            tarpc_ptp_clock_time *tsb)
{
    double seconds;
    double nseconds;

    seconds = (int64_t)(tsa->sec) - tsb->sec;
    nseconds = (int64_t)(tsa->nsec) - tsb->nsec;

    return seconds + nseconds / 1000000000.0;
}

/* Get maximum deviation from a given target in an array of values */
static double
net_drv_max_dev(double *values, unsigned int number,
                double target)
{
    unsigned int i;
    double max_dev = 0;
    double dev;

    for (i = 0; i < number; i++)
    {
        dev = fabs(values[i] - target);
        if (dev > max_dev)
            max_dev = dev;
    }

    return max_dev;
}

/* See description in net_drv_ptp.h */
void
net_drv_ptp_offs_check_dev_avg(double *values, unsigned int number,
                               double avg_diff)
{
    double max_dev;

    max_dev = net_drv_max_dev(values, number, avg_diff);

    RING("Maximum deviation from average offset is %f sec", max_dev);
    if (max_dev > NET_DRV_PTP_OFFS_MAX_DEV_FROM_AVG)
    {
        WARN_VERDICT("Difference between pairs of timestamps changes "
                     "significantly over array of samples");
    }
}

/* See description in net_drv_ptp.h */
void
net_drv_ptp_offs_check_dev_gettime(rcf_rpc_server *rpcs, int ptp_fd,
                                   rpc_clock_id sys_clock,
                                   double ptp_offs)
{
    tarpc_timespec ts_ptp;
    tarpc_timespec ts_sys;
    double gettime_diff;

    rpc_clock_gettime(rpcs, TARPC_CLOCK_ID_FD, ptp_fd, &ts_ptp);

    rpc_clock_gettime(rpcs, TARPC_CLOCK_ID_NAMED, sys_clock,
                      &ts_sys);

    gettime_diff = net_drv_timespec_diff(&ts_ptp, &ts_sys);
    RING("Difference measured with clock_gettime() calls: %f sec",
         gettime_diff);

    if (fabs(ptp_offs - gettime_diff) > NET_DRV_PTP_OFFS_MAX_DEV_FROM_GETTIME)
    {
        WARN_VERDICT("Offset relative to %s measured with ioctl() request "
                     "differs too much from offset measured with help of "
                     "clock_gettime() calls", clock_id_rpc2str(sys_clock));
    }
}
