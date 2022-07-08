/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Test API for checking PTP
 *
 * Declarations of TAPI for PTP tests.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#ifndef __TS_NET_DRV_PTP_H__
#define __TS_NET_DRV_PTP_H__

#include "te_config.h"
#include "te_defs.h"
#include "rcf_rpc.h"

/**
 * Maximum inaccuracy in seconds when checking what clock_gettime()
 * returns. It is assumed that this inaccuracy is largely due to TE
 * delays (time required to call RPCs, etc).
 */
#define NET_DRV_DEF_PTP_INACC 0.1

/**
 * Open PTP device associated with a given network interface.
 * This function prints verdict and terminates test on failure.
 *
 * @param rpcs      RPC server
 * @param if_name   Interface name
 * @param fd        Where to save opened file descriptor
 */
extern void net_drv_open_ptp_fd(rcf_rpc_server *rpcs, const char *if_name,
                                int *fd);

/**
 * Get difference between two timespec structures in seconds
 * (tsa - tsb).
 *
 * @param tsa     The first timestamp
 * @param tsb     The second timestamp
 *
 * @return Difference in seconds.
 */
extern double net_drv_timespec_diff(tarpc_timespec *tsa,
                                    tarpc_timespec *tsb);

#endif /* !__TS_NET_DRV_PTP_H__ */
