#! /bin/bash
# SPDX-License-Identifier: Apache-2.0
# (c) Copyright 2021 - 2022 Xilinx, Inc. All rights reserved.
#
# Helper script to run Test Environment for the Test Suite

set -e
source "$(dirname "$(which "$0")")"/guess.sh

source "${TE_BASE}/scripts/lib"
source "${TE_BASE}/scripts/lib.grab_cfg"

if [[ -e "${TE_TS_RIGSDIR}/scripts/lib/grab_cfg_handlers" ]] ; then
    source "${TE_TS_RIGSDIR}/scripts/lib/grab_cfg_handlers"
fi

CFG=

run_fail() {
    echo "$*" >&2
    exit 1
}

cleanup() {
    call_if_defined grab_cfg_release
}
trap "cleanup" EXIT

usage() {
    cat <<EOF
USAGE: run.sh [run.sh options] [dispatcher.sh options]
Options:
  --cfg=<CFG>               Configuration to be used.
EOF

    call_if_defined grab_cfg_print_help

    cat <<EOF
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

    call_if_defined grab_cfg_process "${cfg}"

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

declare -a RUN_OPTS
declare -a GEN_OPTS

while test -n "$1" ; do

    if call_if_defined grab_cfg_check_opt "$1" ; then
        shift 1
        continue
    fi

    case "$1" in
        --help) usage ;;

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

conf_dirs="${TE_TS_CONFDIR}"
if [[ -n "${TE_TS_RIGSDIR}" ]] ; then
    conf_dirs="${conf_dirs}:${TE_TS_RIGSDIR}"
fi
conf_dirs="${conf_dirs}:${SF_TS_CONFDIR}"
GEN_OPTS+=(--conf-dirs="${conf_dirs}")

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

echo -ne "\a"
exit ${RESULT}
