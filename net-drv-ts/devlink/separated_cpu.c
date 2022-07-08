/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Devlink tests
 */

/**
 * @defgroup devlink-separated_cpu separated_cpu device parameter
 * @ingroup devlink
 * @{
 *
 * @objective Check that @b separated_cpu device parameter can be retrieved
 *            and changed via devlink API.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 * @param if_down       If @c TRUE, set IUT interface down before
 *                      checking the parameter
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-DLV005, X3-DLV007.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "devlink/separated_cpu"

#include "net_drv_test.h"
#include "tapi_cfg_pci.h"
#include "tapi_cfg_base.h"

int
main(int argc, char *argv[])
{
    rcf_rpc_server *iut_rpcs = NULL;
    const struct if_nameindex *iut_if = NULL;
    te_bool if_down;

    char *pci_oid = NULL;
    uint64_t param_val;
    uint64_t new_val;
    te_bool present;

    cfg_handle *cpu_handles = NULL;
    unsigned int cpu_num = 0;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_BOOL_PARAM(if_down);

    rc = tapi_cfg_pci_oid_by_net_if(iut_rpcs->ta, iut_if->if_name,
                                    &pci_oid);
    if (rc != 0)
        TEST_SKIP("Cannot find PCI device for the tested interface");

    CHECK_RC(tapi_cfg_pci_param_is_present(pci_oid, "separated_cpu",
                                           &present));
    if (!present)
        TEST_SKIP("separated_cpu parameter is not present");

    if (if_down)
    {
        TEST_STEP("If @p if_down is @c TRUE, set IUT interface down.");
        CHECK_RC(tapi_cfg_base_if_down(iut_rpcs->ta, iut_if->if_name));
        CFG_WAIT_CHANGES;
        /**
         * Normally setting interface down should not influence PCI device
         * in any way, so Configurator will not synchronize it automatically.
         */
        CHECK_RC(cfg_synchronize(pci_oid, TRUE));
    }

    TEST_STEP("Check that @b separated_cpu parameter value can be "
              "retrieved for the IUT interface.");
    rc = tapi_cfg_pci_get_param_uint(pci_oid, "separated_cpu",
                                     TAPI_CFG_PCI_PARAM_RUNTIME,
                                     &param_val);
    if (rc != 0)
    {
        TEST_VERDICT("Failed to retrieve separated_cpu parameter, rc = %r",
                     rc);
    }

    RING("Current value of separated_cpu parameter is %" TE_PRINTF_64 "u",
         param_val);

    CHECK_RC(cfg_find_pattern_fmt(
                        &cpu_num, &cpu_handles,
                        "/agent:%s/hardware:*/node:*/cpu:*/core:*/thread:*",
                        iut_rpcs->ta));
    free(cpu_handles);
    RING("Number of CPUs (CPU threads) on IUT: %u", cpu_num);
    if (cpu_num <= 1)
    {
        TEST_SKIP("Host does not have multiple CPUs, so cannot test "
                  "separated_cpu change");
    }

    TEST_STEP("Set @b dist_layout parameter to @c 1, so that receive queue "
              "has separated layout. Only then @b separated_cpu parameter "
              "is meaningful, it determines to which CPU receive queue "
              "events processing is assigned.");
    net_drv_set_pci_param_uint(pci_oid, "dist_layout",
                               TAPI_CFG_PCI_PARAM_RUNTIME,
                               1, "Setting dist_layout to 1");

    do {
        new_val = rand_range(0, cpu_num - 1);
    } while (new_val == param_val);

    TEST_STEP("Check that @b separated_cpu parameter can be changed.");
    net_drv_set_pci_param_uint(pci_oid, "separated_cpu",
                               TAPI_CFG_PCI_PARAM_RUNTIME,
                               new_val, "Changing separated_cpu");

    TEST_SUCCESS;

cleanup:

    free(pci_oid);

    TEST_END;
}
