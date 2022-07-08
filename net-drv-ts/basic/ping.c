/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-ping ping tool
 * @ingroup basic
 * @{
 *
 * @objective Check that @b ping tool can ping Tester address from IUT.
 *
 * @param env               Testing environment:
 *                          - @ref env-peer2peer
 *                          - @ref env-peer2peer_ipv6
 * @param packets           Number of ping packets to send.
 * @param data_size         Number of payload bytes in a ping packet.
 * @param interval          Number of seconds to wait between sending ping
 *                          packets.
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ST07.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "basic/ping"

#include "net_drv_test.h"
#include "tapi_job_factory_rpc.h"
#include "tapi_ping.h"
#include "te_mi_log.h"

/** How long to wait for ping termination, in ms */
#define PING_TIMEOUT 120000

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    const struct sockaddr *tst_addr = NULL;

    tapi_ping_opt opts = tapi_ping_default_opt;
    char *dst_addr = NULL;
    tapi_job_factory_t *factory = NULL;
    tapi_ping_app *app = NULL;
    tapi_ping_report report;
    te_mi_logger *logger = NULL;

    unsigned int packets;
    int data_size;
    double interval;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_ADDR(tst_rpcs, tst_addr);
    TEST_GET_UINT_PARAM(packets);
    TEST_GET_INT_PARAM(data_size);
    TEST_GET_DOUBLE_PARAM(interval);

    CHECK_RC(te_sockaddr_h2str(tst_addr, &dst_addr));

    CHECK_RC(te_mi_logger_meas_create("ping", &logger));

    opts.packet_count = packets;
    opts.interval = TAPI_JOB_OPT_DOUBLE_VAL(interval);
    if (data_size > 0)
        opts.packet_size = data_size;

    opts.interface = iut_if->if_name;
    opts.destination = dst_addr;

    CHECK_RC(tapi_job_factory_rpc_create(iut_rpcs, &factory));
    CHECK_RC(tapi_ping_create(factory, &opts, &app));

    TEST_STEP("Start @b ping tool on IUT to ping Tester address. "
              "Send @p packets ping packets with @p data_size bytes "
              "of payload, waiting for @p interval seconds between them.");
    CHECK_RC(tapi_ping_start(app));

    TEST_STEP("Wait until the tool terminates.");
    CHECK_RC(tapi_ping_wait(app, PING_TIMEOUT));

    CHECK_RC(tapi_ping_get_report(app, &report));

    RING("%u packets were transmitted, %u packets were received",
         report.transmitted, report.received);

    tapi_ping_report_mi_log(logger, &report);
    CHECK_RC(te_mi_logger_flush(logger));

    TEST_STEP("Check that the number of transmitted packets equals to "
              "the number of received packets in @b ping tool report.");

    if (report.transmitted != report.received)
    {
        TEST_VERDICT("Number of transmitted packets does not match "
                     "number of received packets");
    }

    if (report.transmitted != packets)
        TEST_VERDICT("ping sent unexpected number of packets");

    TEST_SUCCESS;

cleanup:

    free(dst_addr);

    CLEANUP_CHECK_RC(tapi_ping_destroy(app));
    tapi_job_factory_destroy(factory);
    te_mi_logger_destroy(logger);

    TEST_END;
}
