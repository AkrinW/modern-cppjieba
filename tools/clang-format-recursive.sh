#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)

read_clang_tidy_field() {
    local field=$1
    local config="${REPO_ROOT}/.clang-tidy"
    [[ -f "${config}" ]] || return 1
    sed -n "s/^${field}:[[:space:]]*//p" "${config}" \
        | sed "s/^['\"]//; s/['\"][[:space:]]*$//"
}

parse_config() {
    SCAN_ROOTS=()
    EXCLUDE_REGEX=""

    local regex
    if regex=$(read_clang_tidy_field "HeaderFilterRegex") && [[ -n "${regex}" ]]; then
        local inner="${regex#'^'}"
        inner="${inner%'/'}"
        inner="${inner#'('}"
        inner="${inner%')'}"
        IFS='|' read -ra SCAN_ROOTS <<< "${inner}"
    fi
    if [[ ${#SCAN_ROOTS[@]} -eq 0 ]]; then
        SCAN_ROOTS=("include/neo" "test")
    fi

    if regex=$(read_clang_tidy_field "ExcludeHeaderFilterRegex") && [[ -n "${regex}" ]]; then
        EXCLUDE_REGEX="${regex}"
    fi
}

resolve_clang_format() {
    if [[ -n "${CLANG_FORMAT:-}" ]]; then
        printf '%s\n' "${CLANG_FORMAT}"
        return 0
    fi

    local candidate
    for candidate in \
        "/home/cwang/opt/llvm/bin/clang-format" \
        "/usr/bin/clang-format" \
        "/bin/clang-format"
    do
        if [[ -x "${candidate}" ]]; then
            printf '%s\n' "${candidate}"
            return 0
        fi
    done

    if command -v clang-format >/dev/null 2>&1; then
        command -v clang-format
        return 0
    fi

    echo "clang-format not found" >&2
    return 1
}

collect_files() {
    local roots=("$@")
    (
        cd "${REPO_ROOT}"
        while IFS= read -r -d '' file; do
            if [[ -n "${EXCLUDE_REGEX}" ]] && printf '%s' "${file}" | grep -qE "${EXCLUDE_REGEX}"; then
                echo "  [skip] ${file}" >&2
                continue
            fi
            printf '%s\0' "${file}"
        done < <(find "${roots[@]}" -type f \
            \( \
                -name '*.c' -o -name '*.cc' -o -name '*.cpp' -o -name '*.cxx' -o \
                -name '*.h' -o -name '*.hh' -o -name '*.hpp' -o -name '*.ipp' -o -name '*.h.in' \
            \) \
            -print0)
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

main() {
    local clang_format
    clang_format=$(resolve_clang_format)

    parse_config

    local roots=("${SCAN_ROOTS[@]}")
    if [[ $# -gt 0 ]]; then
        roots=("$@")
    fi

    mapfile -d '' -t files < <(collect_files "${roots[@]}")
    if [[ ${#files[@]} -eq 0 ]]; then
        echo "No files matched for clang-format."
        return 0
    fi

    local pass=1
    while true; do
        local before after
        before=$(snapshot_hash "${files[@]}")

        (
            cd "${REPO_ROOT}"
            printf '%s\0' "${files[@]}" | xargs -0 "${clang_format}" -i --style=file
        )

        if (
            cd "${REPO_ROOT}"
            printf '%s\0' "${files[@]}" | xargs -0 "${clang_format}" --dry-run --Werror --style=file >/dev/null
        ); then
            echo "clang-format is clean after pass ${pass}."
            return 0
        fi

        after=$(snapshot_hash "${files[@]}")
        if [[ "${before}" == "${after}" ]]; then
            echo "clang-format still reports issues but produced no further edits." >&2
            return 1
        fi

        pass=$((pass + 1))
    done
}

main "$@"
