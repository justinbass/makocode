#!/usr/bin/env bash

mako_test_seed_init() {
    local repo_root=$1
    if [[ -z ${MAKO_TEST_SEED:-} ]]; then
        local generated_seed
        generated_seed=$(od -An -N4 -tu4 /dev/urandom)
        if [[ -z $generated_seed ]]; then
            echo "test seed: unable to read /dev/urandom" >&2
            return 1
        fi
        MAKO_TEST_SEED=$((generated_seed & 0x7fffffff))
    fi

    if [[ ! $MAKO_TEST_SEED =~ ^[0-9]+$ ]]; then
        echo "test seed: MAKO_TEST_SEED must be a non-negative decimal integer" >&2
        return 1
    fi
    while [[ ${#MAKO_TEST_SEED} -gt 1 && ${MAKO_TEST_SEED:0:1} == 0 ]]; do
        MAKO_TEST_SEED=${MAKO_TEST_SEED:1}
    done
    if (( MAKO_TEST_SEED > 2147483647 )); then
        echo "test seed: MAKO_TEST_SEED must be between 0 and 2147483647" >&2
        return 1
    fi

    export MAKO_TEST_SEED
    if [[ ${MAKO_TEST_SEED_REPORTED:-0} != 1 ]]; then
        mkdir -p "$repo_root/test"
        printf '%s\n' "$MAKO_TEST_SEED" > "$repo_root/test/test_seed.txt"
        printf 'Test seed: %s\n' "$MAKO_TEST_SEED"
        printf 'Repeat with: MAKO_TEST_SEED=%s make test\n' "$MAKO_TEST_SEED"
        export MAKO_TEST_SEED_REPORTED=1
    fi
}

mako_test_case_seed() {
    local case_key=$1
    local derived_seed
    read -r derived_seed _ < <(printf '%s:%s' "$MAKO_TEST_SEED" "$case_key" | cksum)
    printf '%s\n' "$((derived_seed & 0x7fffffff))"
}
