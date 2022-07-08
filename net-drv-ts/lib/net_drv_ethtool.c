/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Common test API
 *
 * Implementation of TAPI for checking ethtool.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#include "tapi_test.h"
#include "tapi_rpc_unistd.h"
#include "tarpc.h"
#include "net_drv_ethtool.h"

#if HAVE_NET_IF_H
#include <net/if.h>
#endif

/* See net_drv_ethtool.h */
int
net_drv_ethtool_reset(rcf_rpc_server *rpcs, int s, const char *if_name,
                      unsigned int flags, unsigned int *ret_flags)
{
    struct ifreq ifreq_var;
    struct tarpc_ethtool_value ethtool_val;
    int rc;

    memset(&ifreq_var, 0, sizeof(ifreq_var));
    CHECK_RC(te_snprintf(ifreq_var.ifr_name, sizeof(ifreq_var.ifr_name),
                         "%s", if_name));

    memset(&ethtool_val, 0, sizeof(ethtool_val));
    ifreq_var.ifr_data = (char *)&ethtool_val;
    ethtool_val.cmd = RPC_ETHTOOL_RESET;
    ethtool_val.data = flags;

    rc = rpc_ioctl(rpcs, s, RPC_SIOCETHTOOL, &ifreq_var);
    if (ret_flags != NULL)
        *ret_flags = ethtool_val.data;

    return rc;
}
