#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)

readonly BUILD_DIR_DEFAULT="${REPO_ROOT}/build"
readonly SOURCE_ROOT_DEFAULT="${REPO_ROOT}/test"
readonly HEADER_ROOT_DEFAULT="${REPO_ROOT}/include/neo"
readonly MAX_PASSES_DEFAULT=10

resolve_clang_tidy() {
    if [[ -n "${CLANG_TIDY:-}" ]]; then
        printf '%s\n' "${CLANG_TIDY}"
        return 0
    fi

    local candidate
    for candidate in \
        "/home/cwang/opt/llvm/bin/clang-tidy" \
        "/usr/bin/clang-tidy" \
        "/bin/clang-tidy"
    do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    if command -v clang-tidy >/dev/null 2>&1; then
        command -v clang-tidy
        return 0
    fi

    echo "clang-tidy not found" >&2
    return 1
}

collect_source_files() {
    (
        cd "${REPO_ROOT}"
        find "${SOURCE_ROOT}" -type f \( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \) -print0
    )
}

collect_header_files() {
    (
        cd "${REPO_ROOT}"
        find "${HEADER_ROOT}" -type f \( -name '*.hpp' -o -name '*.hh' -o -name '*.h' -o -name '*.ipp' \) -print0
    )
}

snapshot_hash() {
    local files=("$@")
    if [[ ${#files[@]} -eq 0 ]]; then
        printf 'empty\n'
        return 0
    fi

    (
        cd "${REPO_ROOT}"
        sha256sum "${files[@]}"
    ) | sha256sum | awk '{print $1}'
}

run_source_tidy() {
    local mode=$1
    shift
    local files=("$@")
    if [[ ${#files[@]} -eq 0 ]]; then
        return 0
    fi

    local args=(
        -p "${BUILD_DIR}"
        --quiet
        --extra-arg-before=--gcc-toolchain="${GCC_TOOLCHAIN}"
    )
    if [[ "${mode}" == "fix" ]]; then
        args+=(--fix --format-style=file)
    fi

    (
        cd "${REPO_ROOT}"
        printf '%s\0' "${files[@]}" | xargs -0 "${CLANG_TIDY_BIN}" "${args[@]}"
    )
}

run_header_tidy() {
    local mode=$1
    shift
    local files=("$@")
    if [[ ${#files[@]} -eq 0 ]]; then
        return 0
    fi

    local file
    for file in "${files[@]}"; do
        local args=(
            "${file}"
            --quiet
        )
        if [[ "${mode}" == "fix" ]]; then
            args+=(--fix --format-style=file)
        fi
        args+=(
            --
            --gcc-toolchain="${GCC_TOOLCHAIN}"
            -x c++-header
            --std=c++26
            -I"${REPO_ROOT}/include"
            -I"${REPO_ROOT}/deps/limonp/include"
        )

        (
            cd "${REPO_ROOT}"
            "${CLANG_TIDY_BIN}" "${args[@]}"
        )
    done
}

warning_count() {
    local log_file=$1
    grep -c ': warning:' "${log_file}" || true
}

error_count() {
    local log_file=$1
    grep -c ': error:' "${log_file}" || true
}

main() {
    CLANG_TIDY_BIN=$(resolve_clang_tidy)
    BUILD_DIR=${BUILD_DIR:-${BUILD_DIR_DEFAULT}}
    SOURCE_ROOT=${SOURCE_ROOT:-${SOURCE_ROOT_DEFAULT}}
    HEADER_ROOT=${HEADER_ROOT:-${HEADER_ROOT_DEFAULT}}
    GCC_TOOLCHAIN=${GCC_TOOLCHAIN:-/home/cwang/dev1/gcc-15.2-o3}
    MAX_PASSES=${MAX_PASSES:-${MAX_PASSES_DEFAULT}}

    if [[ ! -f "${BUILD_DIR}/compile_commands.json" ]]; then
        echo "compile_commands.json not found under ${BUILD_DIR}" >&2
        return 2
    fi
    if [[ ! -d "${GCC_TOOLCHAIN}" ]]; then
        echo "GCC toolchain not found: ${GCC_TOOLCHAIN}" >&2
        return 2
    fi

    local -a source_files=()
    local -a header_files=()
    mapfile -d '' -t source_files < <(collect_source_files)
    mapfile -d '' -t header_files < <(collect_header_files)

    if [[ ${#source_files[@]} -eq 0 && ${#header_files[@]} -eq 0 ]]; then
        echo "No files matched for clang-tidy."
        return 0
    fi

    local -a tracked_files=("${source_files[@]}" "${header_files[@]}")
    local pass
    for ((pass = 1; pass <= MAX_PASSES; ++pass)); do
        local before after
        local fix_log check_log
        before=$(snapshot_hash "${tracked_files[@]}")
        fix_log=$(mktemp)
        check_log=$(mktemp)

        if ! {
            run_source_tidy fix "${source_files[@]}"
            run_header_tidy fix "${header_files[@]}"
        } >"${fix_log}" 2>&1; then
            cat "${fix_log}" >&2
            rm -f "${fix_log}" "${check_log}"
            return 2
        fi

        after=$(snapshot_hash "${tracked_files[@]}")

        if ! {
            run_source_tidy check "${source_files[@]}"
            run_header_tidy check "${header_files[@]}"
        } >"${check_log}" 2>&1; then
            cat "${check_log}" >&2
            rm -f "${fix_log}" "${check_log}"
            return 2
        fi

        local warnings errors
        warnings=$(warning_count "${check_log}")
        errors=$(error_count "${check_log}")

        if [[ "${errors}" != "0" ]]; then
            cat "${check_log}" >&2
            echo "clang-tidy reported compiler errors." >&2
            rm -f "${fix_log}" "${check_log}"
            return 2
        fi

        if [[ "${warnings}" == "0" ]]; then
            echo "clang-tidy is clean after pass ${pass}."
            rm -f "${fix_log}" "${check_log}"
            return 0
        fi

        if [[ "${before}" == "${after}" ]]; then
            cat "${check_log}" >&2
            echo "clang-tidy still reports ${warnings} warning(s), but no further automatic fixes were applied." >&2
            rm -f "${fix_log}" "${check_log}"
            return 1
        fi

        rm -f "${fix_log}" "${check_log}"
        echo "clang-tidy pass ${pass} applied fixes; rerunning to check for remaining diagnostics."
    done

    echo "clang-tidy reached MAX_PASSES=${MAX_PASSES} before converging." >&2
    return 1
}

main "$@"
