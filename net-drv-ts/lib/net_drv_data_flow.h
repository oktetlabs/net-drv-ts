/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Test API for checking data flows
 *
 * Declarations of TAPI for checking data flows.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#ifndef __TS_NET_DRV_DATA_FLOW_H__
#define __TS_NET_DRV_DATA_FLOW_H__

#include "te_config.h"

#include "te_defs.h"
#include "rcf_rpc.h"
#include "te_rpc_sys_socket.h"
#include "tapi_rpc_misc.h"

/** A pair of connected sockets */
typedef struct net_drv_conn {
    /* These fields should be set before using net_drv_conn_create() */
    rcf_rpc_server *rpcs1; /**< RPC server for the first socket */
    rcf_rpc_server *rpcs2; /**< RPC server for the second socket */
    rpc_socket_type sock_type; /**< @c RPC_SOCK_DGRAM or
                                    @c RPC_SOCK_STREAM */
    const struct sockaddr *s1_addr; /**< Address for the first socket */
    const struct sockaddr *s2_addr; /**< Address for the second socket */
    te_bool new_ports; /**< Whether new ports should be allocated */

    /* Internal fields */
    int s1; /**< The first socket */
    int s2; /**< The second socket */
    struct sockaddr_storage s1_addr_st; /**< Auxiliary storage for the
                                             first address */
    struct sockaddr_storage s2_addr_st; /**< Auxiliary storage for the
                                             second address */
} net_drv_conn;

/** Initializer for net_drv_conn structure */
#define NET_DRV_CONN_INIT { .s1 = -1, .s2 = -1 }

/**
 * Create a pair of connected sockets.
 *
 * @param conn          Where to get addresses/ports and etc, where to save
 *                      created sockets.
 */
extern void net_drv_conn_create(net_drv_conn *conn);

/**
 * Destroy a pair of connected sockets.
 *
 * @param conn      Structure with sockets to close.
 */
extern te_errno net_drv_conn_destroy(net_drv_conn *conn);

/** Structure describing data flow between a pair of sockets */
typedef struct net_drv_flow {
    /*
     * These fields are input parameters for net_drv_flow* functions
     */
    rcf_rpc_server *rpcs1; /**< RPC server for the first socket */
    rcf_rpc_server *rpcs2; /**< RPC server for the second socket */
    te_bool tx; /**< If @c TRUE, data is sent from the first
                     socket to the second one, otherwise - in
                     the opposite direction */
    int flow_id; /**< Unique data flow ID */
    te_bool new_processes; /**< Whether to create new processes */
    net_drv_conn *conn; /**< Connected sockets to use */
    int duration; /**< How log to send, in seconds */
    te_bool ignore_send_err; /**< Whether to ignore send() errors */
    int min_size; /**< Minimum data size passed to send() */
    int max_size; /**< Maximum data dise passed to send() */

    /* Internal data */
    rcf_rpc_server *sender_rpcs; /**< Sender RPC server */
    rcf_rpc_server *receiver_rpcs; /**< Receiver RPC server */
    int sender_s; /**< Sender socket */
    int receiver_s; /**< Receiver socket */
    tapi_pat_sender sender_ctx; /**< Context for rpc_pattern_sender() */
    te_bool in_progress; /**< Set to @c TRUE while sending is in progress */

    /* Output */
    te_bool success; /**< After net_drv_flow_finish() this field shows
                          whether data sending/receiving was
                          successful */
} net_drv_flow;

/** Initializer for net_drv_flow structure */
#define NET_DRV_FLOW_INIT { .min_size = 1, .max_size = 1024, \
                            .sender_s = -1, .receiver_s = -1 }

/**
 * Make preparations before starting data flow (create RPC
 * servers if necessary, initialize internal fields, etc).
 *
 * @param flow    Pointer to net_drv_flow structure to prepare.
 */
extern void net_drv_flow_prepare(net_drv_flow *flow);

/**
 * Start sending/receiving data.
 *
 * @param flow    Pointer to net_drv_flow structure describing
 *                the flow.
 */
extern void net_drv_flow_start(net_drv_flow *flow);

/**
 * Wait until sending/receiving data is finished, check results
 * of RPC calls.
 *
 * @param flow    Pointer to net_drv_flow structure describing
 *                the flow.
 */
extern void net_drv_flow_finish(net_drv_flow *flow);

/**
 * Release resources allocated for sending and receiving data.
 *
 * @param flow    Pointer to net_drv_flow structure describing
 *                the flow.
 */
extern te_errno net_drv_flow_destroy(net_drv_flow *flow);

#endif /* !__TS_NET_DRV_DATA_FLOW_H__ */
