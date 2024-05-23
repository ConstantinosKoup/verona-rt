#!/bin/bash

# List of branches
branches=(
    "benchmark-random"
    "benchmark-round-robin"
    "benchmark-second-chance"
    "benchmark-uninstrumented"
    "benchmark-LRU"
    "benchmark-least-accessed"
)

run_count=3

# Loop through each branch
for branch in "${branches[@]}"; do
    echo "Checking out branch $branch"
    
    # Checkout the branch
    git checkout $branch
    if [ $? -ne 0 ]; then
        echo "Error: Failed to checkout branch $branch"
        exit 1
    fi

    git cherry-pick df2ebe4fb6cc69cb826a7c52eb6355a36ca960ff

    # # Build the benchmark
    # echo "Building benchmark on branch $branch"
    # ninja
    # # Run the benchmark multiple times
    # for ((i=1; i<=run_count; i++)); do
    #     echo "Running benchmark on branch $branch (Run $i/$run_count)"
    #     ./test/perf-con-swap-benchmark
    #     if [ $? -ne 0 ]; then
    #         echo "Error: Benchmark failed on branch $branch (Run $i/$run_count)"
    #         exit 1
    #     fi
    #     echo "Cleaning cown dir"
    #     rm -rf /tmp/verona-rt/cowns

    # done

    # echo "Completed branch $branch"
done