/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2023 OKTET Labs Ltd. All rights reserved. */
/** @file
 * @brief net_drv_talib Test Agent Library for net-drv-ts
 *
 * Implementation of RPC routines specific to net-drv-ts.
 */

#define TE_LGR_USER "RPC NET-DRV"

#include "te_config.h"
#include "config.h"

#include <linux/sockios.h>
#include <byteswap.h>
#include <poll.h>

#include "logger_api.h"
#include "rpc_server.h"
#include "tarpc.h"
#include "te_errno.h"
#include "te_alloc.h"
#include "te_sockaddr.h"
#include "te_str.h"
#include "te_vector.h"
#include "te_ethtool.h"
#include "te_sleep.h"
#include "te_time.h"

/*
 * Create a lot of Rx classification rules until trying to add the next
 * rule fails. Then remove all the added rules.
 */
static int
too_many_rx_rules(tarpc_net_drv_too_many_rx_rules_in *in,
                  tarpc_net_drv_too_many_rx_rules_out *out)
{
#ifdef ETHTOOL_SRXCLSRLINS
    uint32_t src_port;
    uint32_t dst_port;
    uint32_t queue;

    struct ifreq ifr;
    struct ethtool_rxnfc rule;
    unsigned int rules_count = 0;
    int64_t location = 0;
    uint32_t real_loc;
    uint32_t *loc_ptr;

    te_errno err;
    int rc = 0;
    int result = 0;

    struct ethtool_tcpip4_spec *ip4_spec = NULL;
    struct ethtool_tcpip6_spec *ip6_spec = NULL;

    te_vec added_rules = TE_VEC_INIT(uint32_t);

    struct sockaddr_storage src_addr_st;
    struct sockaddr_storage dst_addr_st;
    struct sockaddr *src_addr = SA(&src_addr_st);
    struct sockaddr *dst_addr = SA(&dst_addr_st);

    err = sockaddr_rpc2h(&in->src_addr, src_addr, sizeof(src_addr_st),
                         NULL, NULL);
    if (err != 0)
    {
        te_rpc_error_set(TE_RC(TE_TA_UNIX, err),
                         "Failed to convert source address");
        return -1;
    }

    err = sockaddr_rpc2h(&in->dst_addr, dst_addr, sizeof(dst_addr_st),
                         NULL, NULL);
    if (err != 0)
    {
        te_rpc_error_set(TE_RC(TE_TA_UNIX, err),
                         "Failed to convert destination address");
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    TE_STRLCPY(ifr.ifr_name, in->if_name, sizeof(ifr.ifr_name));
    ifr.ifr_data = (caddr_t)&rule;

    memset(&rule, 0, sizeof(rule));
    rule.cmd = ETHTOOL_SRXCLSRLINS;

    if (in->any_location)
        location = RX_CLS_LOC_ANY | RX_CLS_LOC_SPECIAL;

    switch (in->sock_type)
    {
        case RPC_SOCK_DGRAM:
            if (src_addr->sa_family == AF_INET)
                rule.fs.flow_type = UDP_V4_FLOW;
            else
                rule.fs.flow_type = UDP_V6_FLOW;

            break;

        case RPC_SOCK_STREAM:
            if (src_addr->sa_family == AF_INET)
                rule.fs.flow_type = TCP_V4_FLOW;
            else
                rule.fs.flow_type = TCP_V6_FLOW;

            break;

        default:
            te_rpc_error_set(TE_RC(TE_TA_UNIX, TE_EPFNOSUPPORT),
                             "Not supported socket type");
            return -1;
    }

    if (src_addr->sa_family == AF_INET)
    {
        ip4_spec = (struct ethtool_tcpip4_spec *)&rule.fs.m_u;
        ip4_spec->ip4src = 0xffffffff;
        ip4_spec->ip4dst = 0xffffffff;
        ip4_spec->psrc = 0xffff;
        ip4_spec->pdst = 0xffff;

        ip4_spec = (struct ethtool_tcpip4_spec *)&rule.fs.h_u;
        memcpy(&ip4_spec->ip4src, te_sockaddr_get_netaddr(src_addr),
               sizeof(ip4_spec->ip4src));
        memcpy(&ip4_spec->ip4dst, te_sockaddr_get_netaddr(dst_addr),
               sizeof(ip4_spec->ip4dst));
    }
    else
    {
        ip6_spec = (struct ethtool_tcpip6_spec *)&rule.fs.m_u;
        memset(&ip6_spec->ip6src, 0xff, sizeof(ip6_spec->ip6src));
        memset(&ip6_spec->ip6dst, 0xff, sizeof(ip6_spec->ip6dst));
        ip6_spec->psrc = 0xffff;
        ip6_spec->pdst = 0xffff;

        ip6_spec = (struct ethtool_tcpip6_spec *)&rule.fs.h_u;
        memcpy(&ip6_spec->ip6src, te_sockaddr_get_netaddr(src_addr),
               sizeof(ip6_spec->ip6src));
        memcpy(&ip6_spec->ip6dst, te_sockaddr_get_netaddr(dst_addr),
               sizeof(ip6_spec->ip6dst));
    }

    for (src_port = 1; src_port <= UINT16_MAX; src_port++)
    {
        for (dst_port = 1; dst_port <= UINT16_MAX; dst_port++)
        {
            queue = rand_range(0, in->queues_num - 1);

            rule.fs.location = location;
            rule.fs.ring_cookie = queue;

            if (src_addr->sa_family == AF_INET)
            {
                ip4_spec->psrc = htons(src_port);
                ip4_spec->pdst = htons(dst_port);
            }
            else
            {
                ip6_spec->psrc = htons(src_port);
                ip6_spec->pdst = htons(dst_port);
            }

            rc = ioctl(in->fd, SIOCETHTOOL, &ifr);
            if (rc < 0)
            {
                out->add_errno = te_rc_os2te(errno);
                goto finish;
            }

            real_loc = rule.fs.location;
            err = TE_VEC_APPEND(&added_rules, real_loc);
            if (err != 0)
            {
                TE_FATAL_ERROR("failed to add rule location "
                               "to vector");
            }

            rules_count++;
            if (!in->any_location)
                location = rules_count;
        }
    }

finish:

    out->rules_count = rules_count;

    memset(&rule.fs.h_u, 0, sizeof(rule.fs.h_u));
    memset(&rule.fs.m_u, 0, sizeof(rule.fs.m_u));
    rule.cmd = ETHTOOL_SRXCLSRLDEL;

    TE_VEC_FOREACH(&added_rules, loc_ptr)
    {
        rule.fs.location = *loc_ptr;
        rc = ioctl(in->fd, SIOCETHTOOL, &ifr);
        if (rc < 0)
        {
            err = te_rc_os2te(errno);
            te_rpc_error_set(TE_RC(TE_TA_UNIX, err),
                             "Failed to remove Rx rule");
            ERROR("Cannot remove previously added rule at location %u, "
                  "errno=%r", *loc_ptr, err);
            result = -1;
        }
    }

    te_vec_free(&added_rules);
    return result;
#else
    te_rpc_error_set(TE_RC(TE_TA_UNIX, TE_EOPNOTSUPP),
                     "ETHTOOL_SRXCLSRLINS is not supported");
    return -1;
#endif
}

TARPC_FUNC_STANDALONE(net_drv_too_many_rx_rules, {},
{
    MAKE_CALL(out->retval = too_many_rx_rules(in, out));
})

/**
 * Payload of a packet sent by rpc_net_drv_send_pkts_exact_delay()
 * and received by rpc_net_drv_recv_pkts_exact_delay().
 */
typedef struct net_drv_exact_delay_payload {
    /** Packet Id (ordinal number) */
    uint64_t id;
    /**
     * Last byte of payload - set to nonzero value, is useful when
     * detecting Ethernet padding added at the end of short frames.
     */
    uint8_t last_byte;
} __attribute__((packed)) net_drv_exact_delay_payload;

static int64_t
send_pkts_exact_delay(tarpc_net_drv_send_pkts_exact_delay_in *in)
{
    uint64_t id = 0;
    te_bool swap_required = (htonl(1) != 1);
    net_drv_exact_delay_payload pld = { 0, };
    te_errno rc;
    int os_rc;

    struct timeval tv_start;
    struct timeval tv_now;
    long int time_diff = 0;
    long int exp_diff = 0;

    rc = te_gettimeofday(&tv_start, NULL);
    if (rc != 0)
        TE_FATAL_ERROR("gettimeofday() failed: %r", rc);

    pld.last_byte = 0xff;

    while (TRUE)
    {
        if (swap_required)
            pld.id = bswap_64(id);
        else
            pld.id = id;

        /*
         * Try to send a packet at moments d, 2d, 3d, 4d, 5d ...
         * where d is requested delay. If we are already past
         * some sending moment, skip it to avoid enqueueing too many
         * packets and causing bursts of packets without delays.
         */
        if (in->delay > 0)
            exp_diff = (time_diff / in->delay + 1) * in->delay;

        /*
         * Unfortunately functions like usleep() appear to be
         * very imprecise for short delays (like 10 us). Because
         * of this busy loop is used here.
         */
        while (TRUE)
        {
            rc = te_gettimeofday(&tv_now, NULL);
            if (rc != 0)
                TE_FATAL_ERROR("gettimeofday() failed: %r", rc);

            time_diff = TIMEVAL_SUB(tv_now, tv_start);
            if (time_diff > exp_diff)
                break;
        }

        if (TE_US2MS(time_diff) > in->time2run)
            break;

        os_rc = send(in->s, &pld, sizeof(pld), 0);
        if (os_rc < 0)
        {
            te_rpc_error_set(TE_OS_RC(TE_TA_UNIX, errno),
                             "send() failed");
            return -1;
        }
        else if (os_rc != sizeof(pld))
        {
            te_rpc_error_set(TE_RC(TE_TA_UNIX, TE_EMSGSIZE),
                             "send() returned incorrect value");
            return -1;
        }

        id++;
    }

    return id;
}

TARPC_FUNC_STANDALONE(net_drv_send_pkts_exact_delay, {},
{
    MAKE_CALL(out->retval = send_pkts_exact_delay(in));
})

static int64_t
recv_pkts_exact_delay(tarpc_net_drv_recv_pkts_exact_delay_in *in)
{
    uint64_t id = 0;
    int64_t last_id = -1;
    uint64_t pkts_count = 0;
    te_bool swap_required = (htonl(1) != 1);
    net_drv_exact_delay_payload pld;
    int os_rc;
    struct pollfd pfd;

    memset(&pfd, 0, sizeof(pfd));

    pfd.fd = in->s;
    pfd.events = POLLIN;

    while (TRUE)
    {
        os_rc = poll(&pfd, 1, in->time2wait);
        if (os_rc < 0)
        {
            te_rpc_error_set(TE_OS_RC(TE_TA_UNIX, errno),
                             "poll() failed");
            return -1;
        }
        else if (os_rc == 0)
        {
            break;
        }
        else if (pfd.revents != POLLIN)
        {
            te_rpc_error_set(TE_RC(TE_TA_UNIX, TE_EFAIL),
                             "poll() returned unexpected events");
            return -1;
        }

        os_rc = recv(in->s, &pld, sizeof(pld), 0);
        if (os_rc < 0)
        {
            te_rpc_error_set(TE_OS_RC(TE_TA_UNIX, errno),
                             "recv() failed");
            return -1;
        }
        else if (os_rc != sizeof(pld))
        {
            te_rpc_error_set(TE_RC(TE_TA_UNIX, TE_EMSGSIZE),
                             "recv() returned incorrect value");
            return -1;
        }

        if (swap_required)
            id = bswap_64(pld.id);
        else
            id = pld.id;

        if ((int64_t)id < last_id)
        {
            ERROR("Packet %ju was received after packet %ju",
                  (uintmax_t)id, (uintmax_t)last_id);
            te_rpc_error_set(TE_RC(TE_TA_UNIX, TE_EILSEQ),
                             "received packet is out of order");
            return -1;
        }
        else if (id == last_id)
        {
            te_rpc_error_set(TE_RC(TE_TA_UNIX, TE_EILSEQ),
                             "received packet is a duplicate");
            return -1;
        }

        last_id = id;
        pkts_count++;
    }

    return pkts_count;
}

TARPC_FUNC_STANDALONE(net_drv_recv_pkts_exact_delay, {},
{
    MAKE_CALL(out->retval = recv_pkts_exact_delay(in));
})
