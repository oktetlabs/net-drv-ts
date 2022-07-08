/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/*
 * Net Driver Test Suite
 * Basic tests
 */

/**
 * @defgroup basic-driver_info Getting information about driver.
 * @ingroup basic
 * @{
 *
 * @objective Check information returned about a driver for an interface.
 *
 * @param env            Testing environment.
 *                       - @ref env-iut_only
 *
 * @par Scenario:
 *
 * @note Scenarios: X3-ET002.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#define TE_TEST_NAME "basic/driver_info"

#include "net_drv_test.h"
#include "tapi_cfg_if.h"
#include "tapi_cfg_modules.h"

int
main(int argc, char *argv[])
{
    tapi_env_host *iut_host;
    const struct if_nameindex *iut_if = NULL;

    char *mod_version = NULL;
    char *drv_version = NULL;
    char *drv_name = NULL;
    char *exp_drv_name = NULL;

    TEST_START;
    TEST_GET_HOST(iut_host);
    TEST_GET_IF(iut_if);

    CHECK_NOT_NULL(exp_drv_name = net_drv_driver_name(iut_host->ta));

    TEST_STEP("Check that driver name reported for the IUT interface "
              "is the same as the name of the loaded driver module.");
    CHECK_RC(tapi_cfg_if_deviceinfo_drivername_get(iut_host->ta,
                                                   iut_if->if_name,
                                                   &drv_name));

    RING("Driver name is '%s'", drv_name);

    if (strcmp(drv_name, exp_drv_name) != 0)
    {
        ERROR("Interface driver name is '%s' instead of '%s'",
              drv_name, exp_drv_name);

        TEST_VERDICT("Unexpected driver name");
    }

    TEST_STEP("Check that driver version reported for the IUT interface "
              "matches version of the loaded driver module.");

    rc = tapi_cfg_module_version_get(iut_host->ta, exp_drv_name, &mod_version);
    if (rc == TE_RC(TE_CS, TE_ENOENT))
    {
        WARN_VERDICT("Module %s has no version information", exp_drv_name);
    }
    else if (rc != 0)
    {
        TEST_VERDICT("Failed to get module version, rc=%r", rc);

    }

    CHECK_RC(tapi_cfg_if_deviceinfo_driverversion_get(iut_host->ta,
                                                      iut_if->if_name,
                                                      &drv_version));

    RING("Driver version is '%s'", drv_version);

    if (mod_version != NULL && strcmp(mod_version, drv_version) != 0)
    {
        ERROR("Module version is '%s', however interface driver version "
              "is '%s'", mod_version, drv_version);
        TEST_VERDICT("Driver version reported for the interface does not "
                     "match driver module version");
    }

    TEST_SUCCESS;

cleanup:

    free(mod_version);
    free(drv_version);
    free(drv_name);
    free(exp_drv_name);

    TEST_END;
}

/** @} */
