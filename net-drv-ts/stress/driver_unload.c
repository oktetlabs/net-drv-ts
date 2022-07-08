/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Stress tests
 */

/**
 * @defgroup stress-driver_unload Unloading driver
 * @ingroup stress
 * @{
 *
 * @objective Check that unloading network driver works fine.
 *
 * @param env             Testing environment:
 *                        - @ref env-peer2peer
 * @param if_status       Interface status before unloading the driver:
 *                        - @c down
 *                        - @c up
 *                        - @c up_down (first set it UP, then DOWN)
 * @param iters           How many times to unload/load the driver.
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ST02, X3-STR01, X3-STR04, X3-STR05.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "stress/driver_unload"

#include "net_drv_test.h"
#include "tapi_cfg_base.h"
#include "tapi_cfg_modules.h"

enum {
    TEST_IF_DOWN,
    TEST_IF_UP,
    TEST_IF_UP_DOWN
};

#define TEST_IF_STATUS \
    { "down", TEST_IF_DOWN },         \
    { "up", TEST_IF_UP },             \
    { "up_down", TEST_IF_UP_DOWN }

/** Maximum time to sleep between making interface UP and DOWN, in ms */
#define MAX_SLEEP 100

int
main(int argc, char **argv)
{
    tapi_env_host *iut_host = NULL;
    const struct if_nameindex *iut_if = NULL;

    int i;
    te_bool reload_in_cleanup = FALSE;
    char *iut_drv_name = NULL;

    int if_status;
    int iters;

    unsigned int neigh_nodes_num = 0;

    TEST_START;
    TEST_GET_HOST(iut_host);
    TEST_GET_IF(iut_if);
    TEST_GET_INT_PARAM(iters);
    TEST_GET_ENUM_PARAM(if_status, TEST_IF_STATUS);

    CHECK_NOT_NULL(iut_drv_name = net_drv_driver_name(iut_host->ta));

    if (!net_drv_driver_unloadable(iut_host->ta, iut_drv_name))
        TEST_SKIP("Network driver on IUT cannot be unloaded");

    CHECK_RC(net_drv_neigh_nodes_count(iut_host->ta,
                                       &neigh_nodes_num));

    TEST_STEP("Do in a loop @p iters times:");
    for (i = 0; i < iters; i++)
    {
        RING("Iteration %d", i);

        TEST_SUBSTEP("Set IUT interface UP and/or DOWN as requested by "
                     "@p if_status parameter.");

        if (if_status == TEST_IF_UP ||
            if_status == TEST_IF_UP_DOWN)
        {
            CHECK_RC(tapi_cfg_base_if_up(iut_host->ta, iut_if->if_name));

            if (i > 0)
            {
                TEST_SUBSTEP("Sleep for a short random period of time "
                             "after bringing interface UP (and before "
                             "bringing it DOWN again if it is required) "
                             "if this is not the first iteration of the "
                             "loop.");
                te_motivated_msleep(rand_range(0, MAX_SLEEP),
                                    "random delay helps to unload driver "
                                    "in a bit different states");
            }
        }

        if (if_status == TEST_IF_UP_DOWN ||
            (i == 0 && if_status == TEST_IF_DOWN))
        {
            CHECK_RC(tapi_cfg_base_if_down(iut_host->ta, iut_if->if_name));
        }

        TEST_SUBSTEP("Unload the network driver on IUT.");
        TEST_SUBSTEP("Check that it is really unloaded.");
        rc = net_drv_driver_set_loaded(iut_host->ta, iut_drv_name, FALSE);
        if (rc != 0)
            TEST_VERDICT("Failed to unload the driver");

        reload_in_cleanup = TRUE;

        TEST_SUBSTEP("Load the network driver on IUT.");
        TEST_SUBSTEP("Check that it is really loaded.");
        rc = net_drv_driver_set_loaded(iut_host->ta, iut_drv_name, TRUE);
        if (rc != 0)
            TEST_VERDICT("Failed to load the driver");

        reload_in_cleanup = FALSE;

        CHECK_RC(net_drv_wait_neigh_nodes_recover(iut_host->ta,
                                                  neigh_nodes_num));
    }

    TEST_SUCCESS;

cleanup:

    if (reload_in_cleanup)
    {
        /* Configurator fails to rollback module unload. */
        CLEANUP_CHECK_RC(tapi_cfg_module_load(
                                      iut_host->ta,
                                      iut_drv_name));

        /*
         * Make sure interfaces are fully recovered so that configuration
         * can be restored automatically.
         */
        CLEANUP_CHECK_RC(net_drv_wait_neigh_nodes_recover(iut_host->ta,
                                                          neigh_nodes_num));
    }

    free(iut_drv_name);

    TEST_END;
}
