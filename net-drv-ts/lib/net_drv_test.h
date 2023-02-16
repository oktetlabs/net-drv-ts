/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Net Driver Test Suite
 *
 * Macros to be used in tests. The header must be included from test
 * sources only. It is allowed to use the macros only from @b main()
 * function of the test.
 *
 * @author Yurij Plotnikov <Yurij.Plotnikov@arknetworks.am>
 */

#ifndef __TS_NET_DRV_TEST_H__
#define __TS_NET_DRV_TEST_H__

#include "te_config.h"

#include "math.h"

#include "tapi_sockaddr.h"
#include "tapi_env.h"
#include "tapi_rpc.h"
#include "tapi_rpc_params.h"
#include "tapi_rpc_client_server.h"

/* Common test variables */
#ifndef TEST_START_VARS
#define TEST_START_VARS TEST_START_ENV_VARS
#endif

/* The first action in any test - process environment */
#define TEST_START_SPECIFIC TEST_START_ENV

/* Perform environment-related cleanup at the end */
#ifndef TEST_END_SPECIFIC
#define TEST_END_SPECIFIC TEST_END_ENV
#endif

#include "net_drv_ts.h"
#include "net_drv_data_flow.h"
#include "net_drv_ethtool.h"
#include "net_drv_ptp.h"
#include "net_drv_rpc.h"

#endif /* !__TS_NET_DRV_TEST_H__ */
