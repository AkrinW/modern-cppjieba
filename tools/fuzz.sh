#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)

readonly FUZZERS=(utf_decode main_dict user_dict hmm_model dict_snapshot c_api_cut)
readonly FUZZ_SECONDS_DEFAULT=60

usage() {
    echo "usage: $0 <build-dir> [seconds-per-fuzzer] [fuzzer...]" >&2
    echo "fuzzers: ${FUZZERS[*]}" >&2
}

# Seed each corpus from repository data so mutations start from well-formed inputs.
write_seeds() {
    local corpus=$1
    local name
    for name in "${FUZZERS[@]}"; do
        mkdir -p "${corpus}/${name}"
    done

    cp "${REPO_ROOT}/test/testdata/testlines.utf8" "${corpus}/utf_decode/"
    head -n 64 "${REPO_ROOT}/dict/jieba.dict.utf8" > "${corpus}/main_dict/jieba_head.utf8"
    cp "${REPO_ROOT}"/test/testdata/userdict* "${REPO_ROOT}/dict/user.dict.utf8" "${corpus}/user_dict/"
    # Keep 16 emission entries per state; the full model is too large for quick mutation.
    awk -F, '/^#/ || NF <= 16 { print; next } { line = $1; for (i = 2; i <= 16; ++i) line = line "," $i; print line }' \
        "${REPO_ROOT}/dict/hmm_model.utf8" > "${corpus}/hmm_model/hmm_model_head.utf8"
    cp "${REPO_ROOT}"/test/testdata/snapshot/format_v*.bin "${corpus}/dict_snapshot/"

    # c_api_cut reads its mode from the first byte.
    local line mode index=0
    while IFS= read -r line; do
        for mode in 0 1 2 3 4 5 6; do
            printf "\\$(printf '%03o' "${mode}")%s" "${line}" > "${corpus}/c_api_cut/line${index}_mode${mode}"
        done
        index=$((index + 1))
    done < "${REPO_ROOT}/test/testdata/testlines.utf8"
}

# The library logs every rejected input, so target output is closed; libFuzzer still reports crashes.
# New inputs go to the working corpus; the checked-in corpus is read only.
run_fuzzer() {
    local binary=$1
    local corpus=$2
    local checked_in=$3
    local artifacts=$4
    local seconds=$5

    echo "--- ${binary##*/}: ${seconds}s ---"
    "${binary}" "${corpus}" "${checked_in}" \
        -max_total_time="${seconds}" -timeout=10 -rss_limit_mb=2048 \
        -close_fd_mask=3 -print_final_stats=1 -artifact_prefix="${artifacts}/"
}

main() {
    if [[ $# -lt 1 ]]; then
        usage
        return 2
    fi

    local build_dir=$1
    local seconds=${2:-${FUZZ_SECONDS_DEFAULT}}
    local -a fuzzers=("${@:3}")
    if [[ ${#fuzzers[@]} -eq 0 ]]; then
        fuzzers=("${FUZZERS[@]}")
    fi

    local work="${build_dir}/fuzz-run"
    write_seeds "${work}/corpus"
    mkdir -p "${work}/artifacts"

    local name
    for name in "${fuzzers[@]}"; do
        run_fuzzer "${build_dir}/fuzz/${name}_fuzzer" "${work}/corpus/${name}" "${REPO_ROOT}/test/fuzz/corpus/${name}" \
            "${work}/artifacts" "${seconds}"
    done
}

main "$@"
