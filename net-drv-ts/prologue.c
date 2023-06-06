/* SPDX-License-Identifier: Apache-2.0 */
/* (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved. */
/** @file
 * @brief Test Suite prologue
 *
 * Net Driver Test Suite prologue.
 *
 * @author Dmitry Izbitsky <Dmitry.Izbitsky@oktetlabs.ru>
 */

#ifndef DOXYGEN_TEST_SPEC

/** Logging subsystem entity name */
#define TE_TEST_NAME    "prologue"

#include "net_drv_test.h"

#include "tapi_cfg_base.h"
#include "tapi_cfg_net.h"
#include "tapi_cfg_modules.h"
#include "tapi_cfg_pci.h"
#include "tapi_sh_env.h"
#include "tapi_cfg_pci.h"
#include "tapi_tags.h"

#define DEF_STR_LEN 1024

static te_errno
load_required_modules(const char *ta, void *cookie)
{
    te_errno rc;
    char *driver = NULL;
    char *net_driver = NULL;
    bool is_net_driver_needed = false;

    UNUSED(cookie);

    rc = tapi_cfg_pci_get_ta_driver(ta, NET_DRIVER_TYPE_NET, &driver);
    if (rc != 0)
        goto cleanup;

    if (driver != NULL)
    {
        net_driver = net_drv_driver_name(ta);
        if (strcmp(driver, net_driver) != 0)
            is_net_driver_needed = true;

        if (strcmp_start("sfc", driver) == 0)
        {
            rc = tapi_cfg_module_add_from_ta_dir_or_fallback(ta, "sfc", TRUE);
        }
        else if (strcmp(NET_DRV_X3_DRIVER_NAME, driver) == 0)
        {
            rc = tapi_cfg_module_add_from_ta_dir(ta, "auxiliary", TRUE);
            if (rc == 0)
                rc = tapi_cfg_module_add_from_ta_dir(ta,
                                                     NET_DRV_X3_DRIVER_NAME,
                                                     TRUE);
        }
        else
        {
            rc = tapi_cfg_module_add(ta, driver, FALSE);
            if (rc != 0)
                goto cleanup;

            if (is_net_driver_needed)
            {
                rc = tapi_cfg_module_add(ta, net_driver, FALSE);
                if (rc != 0)
                    goto cleanup;
            }
        }
    }

cleanup:
    free(driver);
    free(net_driver);
    return rc;
}

/**
 * Get the network interface with the minimum ifindex on the specified PCI device
 *
 * @param[in]  pci_oid      PCI device OID (/agent/hardware/pci/device)
 * @param[in]  agent        Agent name (can be used in case of X3 NIC)
 * @param[out] interface    Network interface name (must not be @c NULL)
 *
 * @return Status code
 */
static te_errno
net_drv_cfg_pci_get_net_if(const char *pci_oid, const char *agent,
                           char **interface)
{
    cfg_val_type type = CVT_STRING;
    te_errno rc;
    char *driver = NULL;
    rcf_rpc_server *rpcs = NULL;

    rc = cfg_get_instance_fmt(NULL, &driver,
                              "%s/driver:", pci_oid);
    if (rc != 0)
    {
        if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
        {
            ERROR("No driver name provided by PCI function '%s'",
                  pci_oid);
        }
        return rc;
    }

    if (strcmp(driver, NET_DRV_X3_DRIVER_NAME) != 0)
    {
        rc = cfg_get_instance_fmt(&type, interface, "%s/net:", pci_oid);
        if (rc != 0)
            ERROR("Failed to get the only interface of a PCI device: %r", rc);
    }
    else
    {
        unsigned int ifindex;
        char *ifname = NULL;
        unsigned int sel_ifindex = UINT_MAX;
        char *sel_ifname = NULL;
        int i;

        /* It is needed to call rpc_if_nametoindex() */
        rc = rcf_rpc_server_create(agent, "bootstrap", &rpcs);
        if (rc != 0)
        {
            ERROR("Failed to start bootstrap rpc_server on %s: %r", agent, rc);
            goto out;
        }

        /* X3 has two interfaces on one pci function  */
        for (i = 0; i < 2; i++)
        {
            rc = cfg_get_instance_fmt(&type, &ifname, "%s/net:%d", pci_oid, i);
            if (rc != 0)
            {
                ERROR("Failed to get the %d interface of a PCI device: %r",
                      i, rc);
                goto out;
            }

            ifindex = rpc_if_nametoindex(rpcs, ifname);
            if (ifindex < sel_ifindex)
            {
                sel_ifindex = ifindex;
                if (sel_ifname)
                    free(sel_ifname);
                sel_ifname = ifname;
            }
            else
            {
                free(ifname);
                ifname = NULL;
            }
        }
        *interface = sel_ifname;
    }

out:
    free(driver);
    rcf_rpc_server_destroy(rpcs);
    return rc;
}

/**
 * Callback to switch network node specified using PCI function to
 * network interface.
 *
 * @note Similar to switch_agent_pci_fn_to_interface()
 */
static te_errno
net_drv_switch_agent_pci_fn_to_interface(cfg_net_t *net, cfg_net_node_t *node,
                                         const char *oid_str, cfg_oid *oid,
                                         void *cookie)
{
    char interface_path[RCF_MAX_PATH];
    enum net_node_type *type = cookie;
    char *interface = NULL;
    const char *agent;
    char *pci_path = NULL;
    te_errno rc = 0;

    UNUSED(net);

    if (*type != NET_NODE_TYPE_INVALID && node->type != *type)
        goto out;

    if (strcmp(cfg_oid_inst_subid(oid, 1), "agent") != 0 ||
        strcmp(cfg_oid_inst_subid(oid, 2), "hardware") != 0)
    {
        INFO("Network node '%s' is not a PCI function", oid_str);
        goto out;
    }

    rc = cfg_get_instance_str(NULL, &pci_path, oid_str);
    if (rc != 0)
    {
        ERROR("Failed to get PCI device path: %r", rc);
        goto out;
    }

    agent = CFG_OID_GET_INST_NAME(oid, 1);
    rc = net_drv_cfg_pci_get_net_if(pci_path, agent, &interface);
    if (rc != 0)
    {
        if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
        {
            ERROR("No network interfaces provided by PCI function '%s'",
                  pci_path);
        }
        goto out;
    }

    rc = tapi_cfg_base_if_add_rsrc(agent, interface);
    if (rc != 0)
    {
        ERROR("Failed to reserve network interface '%s' resource on TA '%s': %r",
              interface, agent, rc);
        goto out;
    }

    rc = te_snprintf(interface_path, sizeof(interface_path),
                     "/agent:%s/interface:%s", agent, interface);
    if (rc != 0)
    {
        ERROR("Failed to make interface OID in provided buffer: %r", rc);
        goto out;
    }

    rc = cfg_set_instance(node->handle, CFG_VAL(STRING, interface_path));
    if (rc != 0)
        ERROR("Failed to assign network node to interface");

out:
    free(interface);
    free(pci_path);

    return TE_RC(TE_TAPI, rc);
}


/**
 * Update network nodes specified in terms of PCI function and bound to
 * network driver to refer to corresponding network interfaces.
 *
 * The interface is reserved as a resource.
 *
 * @param type  Network node type of the nodes to update,
 *              pass @c NET_NODE_TYPE_INVALID to update all network nodes
 *
 * @return Status code.
 *
 * @note Similar to tapi_cfg_net_nodes_update_pci_fn_to_interface
 */
static te_errno
net_drv_cfg_net_nodes_update_pci_fn_to_interface(enum net_node_type type)
{
    te_errno rc;

    rc = tapi_cfg_net_foreach_node(net_drv_switch_agent_pci_fn_to_interface,
                                   &type);
    if (rc != 0)
    {
        ERROR("Failed to configure interfaces mentioned in networks "
              "configuration: %r", rc);
    }

    return rc;
}

#define FUNC_CHECK_RC(_expr) \
    do {                            \
        rc = (_expr);               \
                                    \
        if (rc != 0)                \
            goto finish;            \
    } while (0)

/* Add TRC tags related to tested NIC */
static te_errno
add_nic_tags(const char *ta,
             const struct if_nameindex *intf)
{
    char *pci_oid = NULL;
    unsigned int vendor_id = 0;
    unsigned int device_id = 0;
    unsigned int sub_vendor_id = 0;
    unsigned int sub_device_id = 0;
    te_string str = TE_STRING_INIT_STATIC(DEF_STR_LEN);
    te_errno rc;

    rc = tapi_cfg_pci_oid_by_net_if(ta, intf->if_name, &pci_oid);
    if (TE_RC_GET_ERROR(rc) == TE_ENOENT)
        return 0;
    else if (rc != 0)
        return rc;

    FUNC_CHECK_RC(tapi_cfg_pci_get_vendor_dev_ids(
                        pci_oid, &vendor_id, &device_id,
                        &sub_vendor_id,
                        &sub_device_id));

    FUNC_CHECK_RC(te_string_append(&str, "pci-%04x", vendor_id));
    FUNC_CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));

    FUNC_CHECK_RC(te_string_append(&str, "-%04x", device_id));
    FUNC_CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));

    te_string_reset(&str);
    FUNC_CHECK_RC(te_string_append(&str, "pci-sub-%04x", sub_vendor_id));
    FUNC_CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));

    FUNC_CHECK_RC(te_string_append(&str, "-%04x", sub_device_id));
    FUNC_CHECK_RC(tapi_tags_add_tag(te_string_value(&str), NULL));

finish:

    return rc;
}

/* Add TRC tag named after network interface driver */
static te_errno
add_driver_tag(const char *ta, const char *prefix)
{
    char *drv_name = NULL;
    te_string str = TE_STRING_INIT_STATIC(DEF_STR_LEN);
    te_errno rc;

    rc = tapi_cfg_pci_get_ta_driver(ta, NET_DRIVER_TYPE_NET, &drv_name);
    if (rc != 0)
        return rc;

    rc = te_string_append(&str, "%s%s", prefix, drv_name);
    if (rc == 0)
        rc = tapi_tags_add_tag(te_string_value(&str), NULL);

    free(drv_name);
    return rc;
}

int
main(int argc, char **argv)
{
    const struct if_nameindex *iut_if = NULL;
    rcf_rpc_server *iut_rpcs = NULL;
    rcf_rpc_server *tst_rpcs = NULL;
    te_bool env_init = FALSE;

/* Redefine as empty to avoid environment processing here */
#undef TEST_START_VARS
#define TEST_START_VARS
#undef TEST_START_SPECIFIC
#define TEST_START_SPECIFIC
#undef TEST_END_SPECIFIC
#define TEST_END_SPECIFIC

    TEST_START_ENV_VARS;
    TEST_START;

    UNUSED(env);

    CHECK_RC(tapi_expand_path_all_ta(NULL));

    if ((rc = tapi_cfg_net_remove_empty()) != 0)
        TEST_VERDICT("Failed to remove /net instances with empty interfaces");

    rc = tapi_cfg_net_reserve_all();
    if (rc != 0)
    {
        TEST_VERDICT("Failed to reserve all interfaces mentioned in networks "
                     "configuration: %r", rc);
    }

    CHECK_RC(rcf_foreach_ta(load_required_modules, NULL));

    /*
     * Bind net drivers to IUT and TST explicitly, because we can't
     * rely on the driver to do this automatically.
     */
    rc = tapi_cfg_net_bind_driver_by_node(NET_NODE_TYPE_AGENT,
                                          NET_DRIVER_TYPE_NET);
    if (rc != 0)
        TEST_VERDICT("Failed to bind net driver on agent net node");

    rc = tapi_cfg_net_bind_driver_by_node(NET_NODE_TYPE_NUT,
                                          NET_DRIVER_TYPE_NET);
    if (rc != 0)
        TEST_VERDICT("Failed to bind net driver on nut net node");

    CFG_WAIT_CHANGES;
    CHECK_RC(rc = cfg_synchronize("/:", TRUE));

    CHECK_RC(net_drv_cfg_net_nodes_update_pci_fn_to_interface(
                                              NET_NODE_TYPE_INVALID));

    CHECK_RC(tapi_cfg_net_all_up(FALSE));
    CHECK_RC(tapi_cfg_net_delete_all_ip4_addresses());
    CHECK_RC(tapi_cfg_net_all_assign_ip(AF_INET));
    CHECK_RC(tapi_cfg_net_all_assign_ip(AF_INET6));
    CFG_WAIT_CHANGES;

    CHECK_RC(rc = cfg_synchronize("/:", TRUE));
    CHECK_RC(rc = cfg_tree_print(NULL, TE_LL_RING, "/:"));

    tapi_env_init(&env);
    env_init = TRUE;
    TEST_START_ENV;

    TEST_GET_IF(iut_if);
    TEST_GET_PCO(iut_rpcs);
    TEST_GET_PCO(tst_rpcs);

    CHECK_RC(add_driver_tag(iut_rpcs->ta, ""));
    CHECK_RC(add_driver_tag(tst_rpcs->ta, "peer-"));
    CHECK_RC(add_nic_tags(iut_rpcs->ta, iut_if));

    TEST_SUCCESS;

cleanup:

    if (env_init)
        TEST_END_ENV;

    TEST_END;
}

#endif /* !DOXYGEN_TEST_SPEC */
