/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2023 OKTET Labs Ltd. All rights reserved. */

struct tarpc_net_drv_too_many_rx_rules_in {
    struct tarpc_in_arg common;

    tarpc_int fd;
    string if_name<>;
    struct tarpc_sa src_addr;
    struct tarpc_sa dst_addr;
    tarpc_int sock_type;
    tarpc_bool any_location;
    tarpc_uint queues_num;
};

struct tarpc_net_drv_too_many_rx_rules_out {
    struct tarpc_out_arg common;

    tarpc_uint rules_count;
    tarpc_uint add_errno;
    tarpc_int retval;
};

struct tarpc_net_drv_send_pkts_exact_delay_in {
    struct tarpc_in_arg common;

    tarpc_int s;
    uint32_t delay;
    uint32_t time2run;
};

struct tarpc_net_drv_send_pkts_exact_delay_out {
    struct tarpc_out_arg common;

    int64_t retval;
};

struct tarpc_net_drv_recv_pkts_exact_delay_in {
    struct tarpc_in_arg common;

    tarpc_int s;
    uint32_t time2wait;
};

struct tarpc_net_drv_recv_pkts_exact_delay_out {
    struct tarpc_out_arg common;

    int64_t retval;
};

program net_drv_ts
{
    version ver0
    {
        RPC_DEF(net_drv_too_many_rx_rules)
        RPC_DEF(net_drv_send_pkts_exact_delay)
        RPC_DEF(net_drv_recv_pkts_exact_delay)
    } = 1;
} = 2;
