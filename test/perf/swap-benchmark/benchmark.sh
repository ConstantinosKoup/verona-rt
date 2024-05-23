#!/bin/bash

# List of branches
branches=(
    "--SecondChance"
    "--Uninstrumented"
    "--LFU"
    "--LRU"
    "--Random"
    "--RoundRobin"
)

run_count=1

# Loop through each branch
for branch in "${branches[@]}"; do
    # Build the benchmark
    echo "Building benchmark on branch $branch"
    ninja
    # Run the benchmark multiple times
    for ((i=1; i<=run_count; i++)); do
        echo "Running benchmark on branch $branch (Run $i/$run_count)"
        ./test/perf-con-swap-benchmark $branch
        if [ $? -ne 0 ]; then
            echo "Error: Benchmark failed on branch $branch (Run $i/$run_count)"
            exit 1
        fi
        echo "Cleaning cown dir"
        rm -rf /tmp/verona-rt/cowns

    done

    echo "Completed branch $branch"
done