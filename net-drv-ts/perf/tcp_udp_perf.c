/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2016 - 2022 Xilinx, Inc. All rights reserved. */
/* (c) Copyright 2024 OKTET Labs Ltd. All rights reserved. */
/*
 * Net Driver Test Suite
 * Performance testing
 */

/** @defgroup perf-tcp_udp_perf TCP UPD performance test
 * @ingroup perf
 * @{
 *
 * @objective Report TCP or UDP performance
 *
 * @param perf_bench        Performance benchmark type
 * @param dual_mode         Run benchmark in bidirectional mode
 * @param protocol          Use TCP or UDP protocol
 * @param n_perf_insts      Number of performance apps to run
 * @param n_streams         Number of parallel streams to run
 * @param bandwidth         Target bandwidth in Mbps, negative means
 *                          some internal tool value for UDP,
 *                          unlimited for TCP
 * @param rx_csum           Enable, disable Rx checksum offload or
 *                          preserve default
 * @param rx_gro            Enable, disable Rx GRO offload or
 *                          preserve default
 * @param rx_vlan_strip     Enable, disable Rx VLAN stripping offload or
 *                          preserve default
 * @param tx_csum           Enable, disable Tx checksum offload or
 *                          preserve default
 * @param tx_gso            Enable, disable Tx GSO offload or
 *                          preserve default
 *
 * @type performance
 *
 * @author Igor Romanov <Igor.Romanov@oktetlabs.ru>
 *
 * Get the performance statistics of TCP UDP traffic and report it.
 *
 * @par Scenario:
 */

#define TE_TEST_NAME  "perf/tcp_udp_perf"

#include "net_drv_test.h"
#include "tapi_performance.h"
#include "te_units.h"
#include "tapi_sockaddr.h"
#include "tapi_rpc_params.h"
#include "tapi_job_factory_rpc.h"
#include "tapi_cfg_cpu.h"

#define TEST_BENCH_DURATION_SEC 6
#define MAX_PERF_INSTS 32

/**
 * The list of values allowed for parameter of type 'bool_with_default'
 */
#define BOOL_WITH_DEFAULT_MAPPING_LIST  \
    { "DEFAULT", TE_BOOL3_UNKNOWN },    \
    { "FALSE",   TE_BOOL3_FALSE },      \
    { "TRUE",    TE_BOOL3_TRUE }

/**
 * Get the value of parameter of type 'bool_with_default'
 *
 * @param var_name_  Name of the variable used to get the value of
 *                   "var_name_" parameter of type 'bool_with_default' (OUT)
 */
#define TEST_GET_BOOL_WITH_DEFAULT(var_name_) \
    TEST_GET_ENUM_PARAM(var_name_, BOOL_WITH_DEFAULT_MAPPING_LIST)

static void
test_set_if_feature(const char *ta, const char *if_name,
                    const char *feature_name, te_bool3 value)
{
    if (value != TE_BOOL3_UNKNOWN)
        net_drv_set_if_feature(ta, if_name, feature_name,
                               value == TE_BOOL3_FALSE ? 0 : 1);
}

static void
init_perf_insts(tapi_perf_server **servers, tapi_perf_client **clients)
{
    unsigned int i;

    for (i = 0; i < MAX_PERF_INSTS; i++)
    {
        servers[i] = NULL;
        clients[i] = NULL;
    }
}

static void
destroy_perf_insts(tapi_perf_server **perf_server,
                   tapi_perf_client **perf_client, unsigned int n_perf_insts)
{
    unsigned int i;

    for (i = 0; i < n_perf_insts; i++)
    {
        tapi_perf_server_destroy(perf_server[i]);
        tapi_perf_client_destroy(perf_client[i]);
    }
}

static void
perf_summary_throughput_mi_log(const double server_throughput,
                               const double client_throughput)
{
    te_mi_logger *logger;

    CHECK_RC(te_mi_logger_meas_create("summary throughput", &logger));

    te_mi_logger_add_meas_vec(logger, NULL, TE_MI_MEAS_V(
            TE_MI_MEAS(THROUGHPUT,
                       "Server", SINGLE,
                       server_throughput,
                       PLAIN),
            TE_MI_MEAS(THROUGHPUT,
                       "Client", SINGLE,
                       client_throughput,
                       PLAIN)));

    te_mi_logger_destroy(logger);
}

int
main(int argc, char *argv[])
{
    rcf_rpc_server                         *iut_rpcs = NULL;
    const struct if_nameindex              *iut_if = NULL;
    te_bool3                                rx_csum;
    te_bool3                                rx_gro;
    te_bool3                                rx_vlan_strip;
    te_bool3                                tx_csum;
    te_bool3                                tx_gso;

    rcf_rpc_server                         *server_rpcs = NULL;
    rcf_rpc_server                         *client_rpcs = NULL;
    const struct if_nameindex              *server_if = NULL;
    const struct if_nameindex              *client_if = NULL;
    const struct sockaddr                  *server_addr = NULL;
    const struct sockaddr                  *client_addr = NULL;
    uint16_t                                server_ports[MAX_PERF_INSTS];
    char                                   *server_addr_str = NULL;
    char                                   *client_addr_str = NULL;

    tapi_perf_server                       *perf_servers[MAX_PERF_INSTS];
    tapi_perf_client                       *perf_clients[MAX_PERF_INSTS];
    tapi_perf_opts                          perf_opts;
    tapi_perf_bench                         perf_bench;
    te_bool                                 dual_mode;
    rpc_socket_proto                        protocol;
    unsigned int                            n_streams;
    int64_t                                 bandwidth;
    unsigned int                            n_perf_insts;
    tapi_perf_report                        perf_servers_report[MAX_PERF_INSTS];
    tapi_perf_report                        perf_clients_report[MAX_PERF_INSTS];
    double                                  bits_per_second_server = 0;
    double                                  bits_per_second_client = 0;

    tapi_job_factory_t                     *client_factory = NULL;
    tapi_job_factory_t                     *server_factory = NULL;
    unsigned int                            i;
    tapi_cpu_index_t                        cpu_id;

    tapi_job_sched_affinity_param sched_affinity_param = { .cpu_ids_len = 1 };
    tapi_job_exec_param exec_param[] = {
        { .type = TAPI_JOB_EXEC_AFFINITY,
          .data = (void *)&sched_affinity_param },
        { .type = TAPI_JOB_EXEC_END,
          .data = NULL }
    };
    int cpu_id_val;

    init_perf_insts(perf_servers, perf_clients);

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_BOOL_WITH_DEFAULT(rx_csum);
    TEST_GET_BOOL_WITH_DEFAULT(rx_gro);
    TEST_GET_BOOL_WITH_DEFAULT(rx_vlan_strip);
    TEST_GET_BOOL_WITH_DEFAULT(tx_csum);
    TEST_GET_BOOL_WITH_DEFAULT(tx_gso);
    TEST_GET_PCO(server_rpcs);
    TEST_GET_PCO(client_rpcs);
    TEST_GET_IF(server_if);
    TEST_GET_IF(client_if);
    TEST_GET_ADDR(server_rpcs, server_addr);
    TEST_GET_ADDR_NO_PORT(client_addr);
    TEST_GET_PERF_BENCH(perf_bench);
    TEST_GET_BOOL_PARAM(dual_mode);
    TEST_GET_UINT_PARAM(n_perf_insts);
    TEST_GET_UINT_PARAM(n_streams);
    TEST_GET_INT64_PARAM(bandwidth);
    TEST_GET_PROTOCOL(protocol);

    CHECK_RC(n_perf_insts < MAX_PERF_INSTS ? 0 : TE_EINVAL);

    TEST_STEP("Configure Rx checksum offload on IUT interface if specified");
    test_set_if_feature(iut_rpcs->ta, iut_if->if_name, "rx-checksum", rx_csum);

    TEST_STEP("Configure GRO on IUT interface if specified");
    test_set_if_feature(iut_rpcs->ta, iut_if->if_name, "rx-gro", rx_gro);

    TEST_STEP("Configure HW VLAN stripping on IUT interface if specified");
    test_set_if_feature(iut_rpcs->ta, iut_if->if_name, "rx-vlan-hw-parse",
                        rx_vlan_strip);

    TEST_STEP("Configure Tx checksum offload on IUT interface if specified");
    test_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                        "tx-checksum-ip-generic", tx_csum);

    TEST_STEP("Configure GSO offload on IUT interface if specified");
    test_set_if_feature(iut_rpcs->ta, iut_if->if_name,
                        "tx-gso-partial", tx_gso);

    TEST_STEP("If @p rx_vlan_strip is not default, create VLANs, "
              "assign addresses and use it for traffic checks below.");
    if (rx_vlan_strip != TE_BOOL3_UNKNOWN)
    {
        struct sockaddr *client_addr2 = NULL;
        struct sockaddr *server_addr2 = NULL;

        net_drv_ts_add_vlan(client_rpcs->ta, server_rpcs->ta,
                            client_if->if_name, server_if->if_name,
                            client_addr->sa_family, NULL,
                            &client_addr2, &server_addr2);

        te_sockaddr_set_port(client_addr2, te_sockaddr_get_port(client_addr));
        client_addr = client_addr2;
        te_sockaddr_set_port(server_addr2, te_sockaddr_get_port(server_addr));
        server_addr = server_addr2;

        CFG_WAIT_CHANGES;
    }
    CHECK_NOT_NULL(server_addr_str = te_ip2str(server_addr));
    CHECK_NOT_NULL(client_addr_str = te_ip2str(client_addr));

    TEST_STEP("Set default perf options");
    tapi_perf_opts_init(&perf_opts);

    TEST_STEP("Set test specific perf options");
    perf_opts.ipversion =
        (server_addr->sa_family == AF_INET) ? RPC_IPPROTO_IP : RPC_IPPROTO_IPV6;
    perf_opts.host = server_addr_str;
    perf_opts.src_host = client_addr_str;
    perf_opts.protocol = protocol;
    perf_opts.streams = n_streams;
    perf_opts.bandwidth_bits = bandwidth < 0 ? -1 :
                  TE_UNITS_DEC_M2U(bandwidth) / (perf_opts.streams * n_perf_insts);
    perf_opts.duration_sec = TEST_BENCH_DURATION_SEC;
    perf_opts.dual = dual_mode;
    /*
     * To force server to print a report at the end of test even if it lost
     * connection with client (iperf tool issue, Bug 9714).
     */
    perf_opts.interval_sec = perf_opts.duration_sec;

    TEST_STEP("Allocate server ports for perf applications");
    CHECK_RC(tapi_allocate_port_range(server_rpcs, server_ports, n_perf_insts));

    TEST_STEP("Start server and client perf applications");
    CHECK_RC(tapi_job_factory_rpc_create(server_rpcs, &server_factory));
    CHECK_RC(tapi_job_factory_rpc_create(client_rpcs, &client_factory));

    for (i = 0; i < n_perf_insts; i++)
    {
        rc = tapi_cfg_cpu_grab_by_prop(server_rpcs->ta, NULL, &cpu_id);
        if (rc != 0 && rc == TE_RC(TE_TAPI, TE_ENOENT))
            TEST_SKIP("%d/%d CPUs are available for servers", i, n_perf_insts);
        CHECK_RC(rc);

        perf_opts.port = server_ports[i];
        perf_servers[i] = tapi_perf_server_create(perf_bench, &perf_opts,
                                                  server_factory);

        cpu_id_val = cpu_id.package_id;
        sched_affinity_param.cpu_ids = &cpu_id_val;
        CHECK_RC(tapi_job_add_exec_param(perf_servers[i]->app.job,
                                         exec_param));

        CHECK_RC(tapi_perf_server_start_unreliable(perf_servers[i]));
    }

    VSLEEP(1, "ensure all perf servers has started");

    for (i = 0; i < n_perf_insts; i++)
    {
        rc = tapi_cfg_cpu_grab_by_prop(client_rpcs->ta, NULL, &cpu_id);
        if (rc != 0 && rc == TE_RC(TE_TAPI, TE_ENOENT))
            TEST_SKIP("%d/%d CPUs are available for clients", i, n_perf_insts);
        CHECK_RC(rc);

        perf_opts.port = server_ports[i];
        perf_clients[i] = tapi_perf_client_create(perf_bench, &perf_opts,
                                                  client_factory);

        cpu_id_val = cpu_id.package_id;
        sched_affinity_param.cpu_ids = &cpu_id_val;
        CHECK_RC(tapi_job_add_exec_param(perf_clients[i]->app.job,
                                         exec_param));

        CHECK_RC(tapi_perf_client_start(perf_clients[i]));
    }

    TEST_STEP("Wait for perf report to be ready");
    for (i = 0; i < n_perf_insts; i++) {
        CHECK_RC(tapi_perf_client_wait(perf_clients[i],
                                       TAPI_PERF_TIMEOUT_DEFAULT));
    }

    /*
     * Time is relative and goes differently on different hosts.
     * Sometimes we need to wait for a few moments until report is ready.
     */
    VSLEEP(1, "ensure perf server has printed its report");

    for (i = 0; i < n_perf_insts; i++)
    {
        CHECK_RC(tapi_perf_server_get_dump_check_report(perf_servers[i],
                                            "server", &perf_servers_report[i]));
        CHECK_RC(tapi_perf_client_get_dump_check_report(perf_clients[i],
                                            "client", &perf_clients_report[i]));

        CHECK_RC(tapi_perf_server_report_mi_log(perf_servers[i],
                                                &perf_servers_report[i]));
        CHECK_RC(tapi_perf_client_report_mi_log(perf_clients[i],
                                                &perf_clients_report[i]));

        bits_per_second_server += perf_servers_report[i].bits_per_second;
        bits_per_second_client += perf_clients_report[i].bits_per_second;
    }

    TEST_ARTIFACT("Server throughput: %.2f Mbps",
                  TE_UNITS_DEC_U2M(bits_per_second_server));

    TEST_ARTIFACT("Client throughput: %.2f Mbps",
                  TE_UNITS_DEC_U2M(bits_per_second_client));

    perf_summary_throughput_mi_log(bits_per_second_server,
                                   bits_per_second_client);

    TEST_SUCCESS;

cleanup:
    destroy_perf_insts(perf_servers, perf_clients, n_perf_insts);
    free(server_addr_str);
    free(client_addr_str);
    tapi_job_factory_destroy(client_factory);
    tapi_job_factory_destroy(server_factory);

    TEST_END;
}
