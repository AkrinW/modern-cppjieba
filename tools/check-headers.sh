#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd -- "${SCRIPT_DIR}/.." && pwd)

# Each header must compile alone, and a dependent call in a template must find a declaration through the header's
# own includes; otherwise it resolves only by ADL at instantiation, through whatever its includer included first.
# CLANG_CXX may hold a Clang command with flags, for example CLANG_CXX="clang++ -stdlib=libc++".

usage() {
    echo "usage: $0 [header...]   (headers relative to include/, default: every header under include/neo)" >&2
}

collect_headers() {
    (
        cd "${REPO_ROOT}/include"
        find neo -name '*.hpp' -not -path 'neo/third_party/*' | sort
    )
}

# encoding/ needs the ICU development headers.
has_icu() {
    printf '#include <unicode/ucnv.h>\n' | "${CLANG_COMMAND[@]}" -fsyntax-only -x c++ - >/dev/null 2>&1
}

# Print one report per problem in a header; NDEBUG selects different code in some headers, so check both modes.
check_one() {
    local header=$1
    local status=0
    local mode
    for mode in debug ndebug; do
        local -a command=("${CLANG_COMMAND[@]}" -std=c++23 -fsyntax-only -I"${REPO_ROOT}/include")
        if [[ "${mode}" == "ndebug" ]]; then
            command+=(-DNDEBUG)
        fi

        local output
        if ! output=$(printf '#include "%s"\n#include "%s"\n' "${header}" "${header}" |
            "${command[@]}" -x c++ - 2>&1); then
            printf '%s (%s): does not compile on its own\n%s\n' "${header}" "${mode}" "${output}"
            status=1
            continue
        fi

        local dump
        if ! dump=$(printf '#include "%s"\n' "${header}" |
            "${command[@]}" -Xclang -ast-dump -Xclang -ast-dump-filter=neo_cppjieba -fno-color-diagnostics \
                -x c++ - 2>&1); then
            printf '%s (%s): Clang could not dump the AST\n%s\n' "${header}" "${mode}" "${dump}"
            status=1
            continue
        fi
        local unresolved
        unresolved=$(grep -E "UnresolvedLookupExpr.* = '[^']+' empty" <<< "${dump}" |
            sed -E "s/.* = '([^']+)' empty.*/\1/" | sort -u | tr '\n' ' ' || true)
        if [[ -n "${unresolved}" ]]; then
            printf '%s (%s): dependent calls without a declaration in its own includes: %s\n' \
                "${header}" "${mode}" "${unresolved}"
            status=1
        fi
    done
    return "${status}"
}

main() {
    read -ra CLANG_COMMAND <<< "${CLANG_CXX:-clang++}"

    if [[ "${1:-}" == "--one" ]]; then
        check_one "$2"
        return
    fi
    if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
        usage
        return 0
    fi

    # The dependent-lookup check reads Clang's AST; other compilers would make it pass vacuously.
    local version
    version=$("${CLANG_COMMAND[@]}" --version | head -n 1)
    echo "${version}"
    if [[ "${version}" != *clang* ]]; then
        echo "CLANG_CXX must name a Clang compiler." >&2
        return 2
    fi

    local -a headers=("$@")
    if [[ ${#headers[@]} -eq 0 ]]; then
        mapfile -t headers < <(collect_headers)
        if ! has_icu; then
            echo "ICU headers not found; skipping neo/encoding/." >&2
            mapfile -t headers < <(printf '%s\n' "${headers[@]}" | grep -v '^neo/encoding/')
        fi
    fi

    local jobs=${JOBS:-$(nproc)}
    if printf '%s\0' "${headers[@]}" | xargs -0 -P "${jobs}" -I{} "${BASH_SOURCE[0]}" --one {}; then
        echo "${#headers[@]} header(s) are self-contained."
        return 0
    fi
    echo "Some headers depend on their includers; include what each header uses (see above)." >&2
    return 1
}

main "$@"
