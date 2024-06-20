# Swap Benchmark Flags

## Eviction Algorithm Flags
  - --LFU
  - --LRU
  - --Random
  - --RoundRobin
  - --SecondChance

## For running wihtout evictions
- --Uninstrumented

## Configuration Flags
  - --MEMORY_TARGET_MB `[UINT]`
  - --COWN_NUMBER `[UINT]`
  - --COWN_DATA_SIZE `[UINT]`
  - --COWNS_PER_BEHAVIOUR `[UINT]`
  - --THREAD_NUMBER `[UINT]`
  - --BEHAVIOUR_RUNTIME_MICROSECONDS `[UINT]`
  - --THROUGHPUT `[UINT]`
  - --ACCESS_SD `[UINT]`
  - --TOTAL_BEHAVIOURS `[UINT]`
  - --TOTAL_TIME_SECS `[UINT]`

## Output Flags
  - --DONT_SAVE
    - If present the benchmark will not record the output in `out.csv`
  - --PRINT_MEMORY
    - If present the memory usage of the system will be printed every second.