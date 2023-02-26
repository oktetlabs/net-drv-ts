/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2023 OKTET Labs Ltd. All rights reserved. */
/** @file
 * @brief RPC calls
 *
 * Implementation of test suite speficic RPC calls.
 */

#include "te_config.h"

#include "net_drv_rpc.h"
#include "tapi_rpc_internal.h"

/* See description in net_drv_rpc.h */
int
rpc_net_drv_too_many_rx_rules(rcf_rpc_server *rpcs,
                              int fd,
                              const char *if_name,
                              const struct sockaddr *src_addr,
                              const struct sockaddr *dst_addr,
                              rpc_socket_type sock_type,
                              te_bool any_location,
                              unsigned int queues_num,
                              unsigned int *rules_count,
                              te_errno *add_errno)
{
    struct tarpc_net_drv_too_many_rx_rules_in in;
    struct tarpc_net_drv_too_many_rx_rules_out out;
    char src_addr_str[1000];
    char dst_addr_str[1000];

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    in.fd = fd;
    in.if_name = (char *)if_name;
    in.sock_type = sock_type;
    sockaddr_input_h2rpc(src_addr, &in.src_addr);
    sockaddr_input_h2rpc(dst_addr, &in.dst_addr);
    in.any_location = any_location;
    in.queues_num = queues_num;

    rcf_rpc_call(rpcs, "net_drv_too_many_rx_rules", &in, &out);

    if (RPC_IS_CALL_OK(rpcs) && rpcs->op != RCF_RPC_WAIT)
    {
        if (rules_count != NULL)
            *rules_count = out.rules_count;
        if (add_errno != NULL)
            *add_errno = out.add_errno;
    }

    CHECK_RETVAL_VAR_IS_ZERO_OR_MINUS_ONE(net_drv_too_many_rx_rules,
                                          out.retval);

    SOCKADDR_H2STR_SBUF(src_addr, src_addr_str);
    SOCKADDR_H2STR_SBUF(dst_addr, dst_addr_str);
    TAPI_RPC_LOG(rpcs, net_drv_too_many_rx_rules, "%s, %s, %s, %s, "
                 "any_location=%s, queues_num=%u",
                 "%d rules_count=%u add_errno=%r",
                 if_name, src_addr_str, dst_addr_str,
                 socktype_rpc2str(sock_type),
                 any_location ? "TRUE" : "FALSE",
                 queues_num, out.retval, out.rules_count, out.add_errno);

    RETVAL_INT(net_drv_too_many_rx_rules, out.retval);
}

/* See description in net_drv_rpc.h */
int64_t
rpc_net_drv_send_pkts_exact_delay(rcf_rpc_server *rpcs,
                                  int s,
                                  unsigned int delay,
                                  unsigned int time2run)
{
    struct tarpc_net_drv_send_pkts_exact_delay_in in;
    struct tarpc_net_drv_send_pkts_exact_delay_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    in.s = s;
    in.delay = delay;
    in.time2run = time2run;

    rcf_rpc_call(rpcs, "net_drv_send_pkts_exact_delay", &in, &out);

    CHECK_RETVAL_VAR_IS_GTE_MINUS_ONE(net_drv_send_pkts_exact_delay,
                                      out.retval);

    TAPI_RPC_LOG(rpcs, net_drv_send_pkts_exact_delay,
                 "socket=%d, delay=%u us, time2run=%u ms", "%jd",
                 s, delay, time2run, (intmax_t)out.retval);

    RETVAL_INT64(net_drv_send_pkts_exact_delay, out.retval);
}

/* See description in net_drv_rpc.h */
int64_t
rpc_net_drv_recv_pkts_exact_delay(rcf_rpc_server *rpcs,
                                  int s,
                                  unsigned int time2wait)
{
    struct tarpc_net_drv_recv_pkts_exact_delay_in in;
    struct tarpc_net_drv_recv_pkts_exact_delay_out out;

    memset(&in, 0, sizeof(in));
    memset(&out, 0, sizeof(out));

    in.s = s;
    in.time2wait = time2wait;

    rcf_rpc_call(rpcs, "net_drv_recv_pkts_exact_delay", &in, &out);

    CHECK_RETVAL_VAR_IS_GTE_MINUS_ONE(net_drv_recv_pkts_exact_delay,
                                      out.retval);

    TAPI_RPC_LOG(rpcs, net_drv_recv_pkts_exact_delay,
                 "socket=%d, time2wait=%u ms", "%jd",
                 s, time2wait, (intmax_t)out.retval);

    RETVAL_INT64(net_drv_recv_pkts_exact_delay, out.retval);
}
