/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Devlink tests
 */

/**
 * @defgroup devlink-dist_layout dist_layout device parameter
 * @ingroup devlink
 * @{
 *
 * @objective Check that dist_layout device parameter can be retrieved
 *            and changed via devlink API.
 *
 * @param env           Testing environment:
 *                      - @ref env-peer2peer
 * @param if_down       If @c TRUE, set IUT interface down before
 *                      checking the parameter
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-DVL004, X3-DVL006.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "devlink/dist_layout"

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
    te_bool present;

    TEST_START;
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_IF(iut_if);
    TEST_GET_BOOL_PARAM(if_down);

    rc = tapi_cfg_pci_oid_by_net_if(iut_rpcs->ta, iut_if->if_name,
                                    &pci_oid);
    if (rc != 0)
        TEST_SKIP("Cannot find PCI device for the tested interface");

    CHECK_RC(tapi_cfg_pci_param_is_present(pci_oid, "dist_layout",
                                           &present));
    if (!present)
        TEST_SKIP("dist_layout parameter is not present");

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

    TEST_STEP("Check that dist_layout parameter value can be retrieved "
              "for the IUT interface.");
    rc = tapi_cfg_pci_get_param_uint(pci_oid, "dist_layout",
                                     TAPI_CFG_PCI_PARAM_RUNTIME,
                                     &param_val);
    if (rc == TE_RC(TE_CS, TE_ENOENT))
    {
        TEST_SKIP("dist_layout parameter is absent");
    }
    else if (rc != 0)
    {
        TEST_VERDICT("Failed to retrieve dist_layout parameter, rc = %r",
                     rc);
    }

    RING("Current value of dist_layout parameter is %" TE_PRINTF_64 "u",
         param_val);

    TEST_STEP("Check that dist_layout parameter can be set to @c 1 "
              "for the IUT interface.");
    net_drv_set_pci_param_uint(pci_oid, "dist_layout",
                               TAPI_CFG_PCI_PARAM_RUNTIME,
                               1, "Checking value 1");

    TEST_STEP("Check that dist_layout parameter can be set to @c 0 "
              "for the IUT interface.");
    net_drv_set_pci_param_uint(pci_oid, "dist_layout",
                               TAPI_CFG_PCI_PARAM_RUNTIME,
                               0, "Checking value 0");

    TEST_SUCCESS;

cleanup:

    free(pci_oid);

    TEST_END;
}
