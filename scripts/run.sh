#! /bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.
#
# Helper script to run Test Environment for the Test Suite

set -e
source "$(dirname "$(which "$0")")"/guess.sh
source ${SF_TS_CONFDIR}/scripts/lib.run

run_fail() {
    echo "$*" >&2
    exit 1
}

usage() {
    cat <<EOF
USAGE: run.sh [run.sh options] [dispatcher.sh options]
Options:
  --no-item                 Do not try to reserve requested
                            configuration with item
  --steal-cfg               Steal the configuration even if it is owned by
                            someone else.
  --release-cfg             Release configuration at the end of the tests.
                            It is intended for use in night testing, when
                            the configuration is taken and released
                            automatically.
  --cfg=<CFG>               Configuration to be used.
  --reuse-pco               Do not restart RPC servers in each test
                            (it makes testing significantly faster)
  --net-driver-ndebug       Build net drivers with NDEBUG=1 option

EOF
    "${TE_BASE}"/dispatcher.sh --help
    exit 1
}

function process_cfg() {
    local cfg="$1" ; shift
    local run_conf
    local -a mod_opts

    if $do_item; then
        local force=
        ! ${STEAL_CFG} || force="--force"
        # -mlx means "use the same host but Melanox NIC on it"
        take_items "${cfg/%-mlx/}" "${force}"
    fi

    # Support <cfg>-p0 etc modifiers to use only one port
    if test "${cfg}" != "${cfg%-p[0-9]}" ; then
        run_conf="${cfg%-p[0-9]}"
        # VF modifiers must be applied after port modifiers
        mod_opts=(--script=env/"${cfg#${run_conf}-}" "${mod_opts[@]}")
        cfg="${run_conf}"
    fi
    # Modifier must be applied after base configuration
    RUN_OPTS+=(--opts=run/"${cfg}" "${mod_opts[@]}")
}

# In ts-conf/env/sfc do not try to autodetect interface names for hosts -
# it is not possible here because sfc driver is loaded only by prologue.
# Instead specify interfaces in terms of PCI.
export TE_SFC_IFS_BY_PCI=yes

declare CFG=
declare -a RUN_OPTS
declare -a GEN_OPTS

do_item=true
STEAL_CFG=false
RELEASE_CFG=false

while test -n "$1" ; do
    case "$1" in
        --help) usage ;;

        --no-item) do_item=false ;;

        --steal-cfg) STEAL_CFG=true ;;

        --release-cfg) RELEASE_CFG=true ;;

        --cfg=*)
            test -z "${CFG}" ||
                run_fail "Configuration is specified twice: ${CFG} vs ${1#--cfg=}"

            CFG="${1#--cfg=}"
            ;;

        --reuse-pco)
            export TE_ENV_REUSE_PCO=yes
            ;;

        --net-driver-ndebug)
            NET_DRIVER_MAKE_ARGS+=" NDEBUG=1"
            export NET_DRIVER_MAKE_ARGS
            ;;

        *)  RUN_OPTS+=("$1") ;;
    esac
    shift 1
done

if test -n "${CFG}" ; then
    process_cfg ${CFG}
fi

RUN_OPTS+=(--opts=opts.ts)

GEN_OPTS+=(--conf-dirs="${TE_TS_CONFDIR}:${SF_TS_CONFDIR}")

GEN_OPTS+=(--trc-db="${TE_TS_TRC_DB}")
GEN_OPTS+=(--trc-comparison=normalised)
GEN_OPTS+=(--trc-html=trc-brief.html)
GEN_OPTS+=(--trc-no-expected)
GEN_OPTS+=(--trc-no-total --trc-no-unspec)
GEN_OPTS+=(--tester-only-req-logues)

"${TE_BASE}"/dispatcher.sh "${GEN_OPTS[@]}" "${RUN_OPTS[@]}"
RESULT=$?

if test ${RESULT} -ne 0 ; then
    echo FAIL
    echo ""
fi

if $RELEASE_CFG; then
    free_items "$CFG"
fi

echo -ne "\a"
exit ${RESULT}
