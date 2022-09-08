/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (C) 2022 OKTET Labs Ltd. All rights reserved. */

/*
 * Net Driver Test Suite
 * RSS tests
 *
 * Copyright (C) 2022 OKTET Labs. All rights reserved.
 */

/**
 * @defgroup rss-hash_key_get Getting RSS hash key.
 * @ingroup rss
 * @{
 *
 * @objective Get RSS hash key, check that packets are directed to
 *            receive queues according to its value.
 *
 * @param env            Testing environment:
 *                       - @ref env-peer2peer
 *                       - @ref env-peer2peer_ipv6
 * @param sock_type      Socket type:
 *                       - @c SOCK_STREAM
 *                       - @c SOCK_DGRAM
 *
 * @par Scenario:
 */

#define TE_TEST_NAME "rss/hash_key_get"

#include "net_drv_test.h"

int
main(int argc, char *argv[])
{
    TEST_START;

    TEST_SKIP("Not implemented yet");

    TEST_SUCCESS;

cleanup:

    TEST_END;
}
