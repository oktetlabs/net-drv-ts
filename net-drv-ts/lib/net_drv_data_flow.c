/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Test API for checking data flows
 *
 * Implementation of TAPI for checking data flows.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#include "net_drv_data_flow.h"
#include "tapi_sockaddr.h"
#include "tapi_test.h"
#include "tapi_rpc_client_server.h"
#include "tapi_rpc_unistd.h"
#include "te_str.h"

/* See description in net_drv_data_flow.h */
void
net_drv_conn_create(net_drv_conn *conn)
{
    if (conn->new_ports)
    {
        CHECK_RC(tapi_sockaddr_clone(conn->rpcs1, conn->s1_addr,
                                     &conn->s1_addr_st));
        CHECK_RC(tapi_sockaddr_clone(conn->rpcs2, conn->s2_addr,
                                     &conn->s2_addr_st));
        conn->s1_addr = SA(&conn->s1_addr_st);
        conn->s2_addr = SA(&conn->s2_addr_st);
    }

    GEN_CONNECTION(conn->rpcs1, conn->rpcs2, conn->sock_type, RPC_PROTO_DEF,
                   conn->s1_addr, conn->s2_addr, &conn->s1, &conn->s2);
}

/* See description in net_drv_data_flow.h */
te_errno
net_drv_conn_destroy(net_drv_conn *conn)
{
    int rc;
    te_errno res = 0;

    if (conn->rpcs1 != NULL && conn->s1 >= 0)
    {
        RPC_AWAIT_ERROR(conn->rpcs1);
        rc = rpc_close(conn->rpcs1, conn->s1);
        if (rc < 0)
            res = RPC_ERRNO(conn->rpcs1);
    }

    if (conn->rpcs2 != NULL && conn->s2 >= 0)
    {
        RPC_AWAIT_ERROR(conn->rpcs2);
        rc = rpc_close(conn->rpcs2, conn->s2);
        if (rc < 0)
            res = RPC_ERRNO(conn->rpcs2);
    }

    return res;
}

/**
 * Create new RPC server for a data flow.
 *
 * @param rpcs      RPC server to fork.
 * @param flow_id   Flow identifier.
 * @param rpcs_out  Where to save pointer to the created RPC server.
 */
static void
create_flow_rpc(rcf_rpc_server *rpcs, int flow_id,
                rcf_rpc_server **rpcs_out)
{
    char rpc_name[RCF_MAX_NAME];

    CHECK_RC(te_snprintf(rpc_name, sizeof(rpc_name),
                         "%s_flow%d", rpcs->name, flow_id));
    CHECK_RC(rcf_rpc_server_fork(rpcs, rpc_name, rpcs_out));
}

/* See description in net_drv_data_flow.h */
void
net_drv_flow_prepare(net_drv_flow *flow)
{
    tarpc_pat_gen_arg *pat_arg;
    tapi_pat_sender *sender = &flow->sender_ctx;

    if (flow->new_processes)
    {
        create_flow_rpc(flow->rpcs1, flow->flow_id, &flow->rpcs1);
        create_flow_rpc(flow->rpcs2, flow->flow_id, &flow->rpcs2);
    }

    if (flow->tx)
    {
        flow->sender_rpcs = flow->rpcs1;
        flow->receiver_rpcs = flow->rpcs2;
        flow->sender_s = flow->conn->s1;
        flow->receiver_s = flow->conn->s2;
    }
    else
    {
        flow->sender_rpcs = flow->rpcs2;
        flow->receiver_rpcs = flow->rpcs1;
        flow->sender_s = flow->conn->s2;
        flow->receiver_s = flow->conn->s1;
    }

    tapi_pat_sender_init(sender);

    sender->gen_func = RPC_PATTERN_GEN_LCG;
    pat_arg = &sender->gen_arg;
    pat_arg->offset = 0;
    pat_arg->coef1 = rand_range(0, RAND_MAX);
    pat_arg->coef2 = rand_range(1, RAND_MAX);
    pat_arg->coef3 = rand_range(0, RAND_MAX);

    sender->duration_sec = flow->duration;

    sender->ignore_err = flow->ignore_send_err;
    sender->iomux = FUNC_NO_IOMUX;

    tapi_rand_gen_set(&sender->size, flow->min_size, flow->max_size,
                      FALSE);
}

/* See description in net_drv_data_flow.h */
void
net_drv_flow_start(net_drv_flow *flow)
{
    flow->receiver_rpcs->op = RCF_RPC_CALL;
    rpc_drain_fd_duration(flow->receiver_rpcs, flow->receiver_s,
                          flow->max_size, -1,
                          flow->sender_ctx.duration_sec + 1, NULL);

    flow->in_progress = TRUE;

    flow->sender_rpcs->op = RCF_RPC_CALL;
    rpc_pattern_sender(flow->sender_rpcs, flow->sender_s,
                       &flow->sender_ctx);
}

/* See description in net_drv_data_flow.h */
void
net_drv_flow_finish(net_drv_flow *flow)
{
    int rc;
    te_bool success = TRUE;
    uint64_t read;

    rcf_rpc_server *sender_rpcs = flow->sender_rpcs;
    int sender_s = flow->sender_s;
    tapi_pat_sender *sender_ctx = &flow->sender_ctx;
    rcf_rpc_server *receiver_rpcs = flow->receiver_rpcs;
    int receiver_s = flow->receiver_s;

    RPC_AWAIT_ERROR(sender_rpcs);
    sender_rpcs->timeout = TE_SEC2MS(sender_ctx->duration_sec + 1);
    rc = rpc_pattern_sender(sender_rpcs, sender_s, sender_ctx);
    if (rc < 0)
    {
        ERROR("rpc_pattern_sender() failed on %s with error "
              RPC_ERROR_FMT, sender_rpcs->name,
              RPC_ERROR_ARGS(sender_rpcs));
        success = FALSE;
    }

    RPC_AWAIT_ERROR(receiver_rpcs);
    rc = rpc_drain_fd_duration(receiver_rpcs, receiver_s, flow->max_size,
                               -1, sender_ctx->duration_sec + 1, &read);
    if (rc < 0 && RPC_ERRNO(receiver_rpcs) != RPC_EAGAIN)
    {
        ERROR("rpc_drain_fd_duration() failed on %s with "
              "unexpected error " RPC_ERROR_FMT,
              receiver_rpcs->name,
              RPC_ERROR_ARGS(receiver_rpcs));
        success = FALSE;
    }
    else if (read == 0)
    {
        ERROR("rpc_drain_fd_duration() read no data on %s",
              receiver_rpcs->name);
        success = FALSE;
    }
    else if (read > sender_ctx->sent)
    {
        ERROR("rpc_drain_fd_duration() read too much data on %s",
              receiver_rpcs->name);
        success = FALSE;
    }

    flow->in_progress = FALSE;
    flow->success = success;
}

/**
 * Close socket and destroy RPC server used by a data flow.
 *
 * @param rpcs          RPC server.
 * @param s             Socket.
 * @param in_progress   Whether sending or receiving RPC call is in
 *                      progress.
 */
static te_errno
close_sock_kill_rpcs(rcf_rpc_server *rpcs, int s, te_bool in_progress)
{
    te_errno res = 0;
    te_errno te_rc;
    int rc;

    if (rpcs == NULL)
        return 0;

    if (s >= 0 && !in_progress)
    {
        RPC_AWAIT_ERROR(rpcs);
        rc = rpc_close(rpcs, s);
        if (rc < 0)
            res = RPC_ERRNO(rpcs);
    }

    te_rc = rcf_rpc_server_destroy(rpcs);
    if (te_rc != 0)
        res = te_rc;

    return res;
}

/* See description in net_drv_data_flow.h */
te_errno
net_drv_flow_destroy(net_drv_flow *flow)
{
    te_errno res = 0;
    te_errno rc;

    if (flow->new_processes)
    {
        rc = close_sock_kill_rpcs(flow->rpcs1, flow->conn->s1,
                                  flow->in_progress);
        if (rc != 0)
            res = rc;

        rc = close_sock_kill_rpcs(flow->rpcs2, flow->conn->s2,
                                  flow->in_progress);
        if (rc != 0)
            res = rc;
    }

    return res;
}
