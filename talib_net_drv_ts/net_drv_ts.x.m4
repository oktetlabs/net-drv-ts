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

program net_drv_ts
{
    version ver0
    {
        RPC_DEF(net_drv_too_many_rx_rules)
    } = 1;
} = 2;
