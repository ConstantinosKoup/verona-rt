#pragma once

#include "debug/logging.h"
#include "cpp/cown_swapper.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <queue>
#include <cmath>

#include <malloc.h>
#include <unordered_map>
#include <snmalloc/override/malloc-extensions.cc>

#ifdef _WIN32 // Windows-specific headers
#include <Windows.h>
#include <psapi.h>
#elif __linux__ // Linux-specific headers
#include <unistd.h>
#include <sys/resource.h>
#endif

namespace verona::cpp
{
    using namespace verona::rt;
    using cown_pair = std::pair<Cown *, size_t>;

    class CownMemoryThread
    {
    public:
        enum class SwappingAlgo
        {
            LRU,
            LFU,
            RANDOM,
            ROUND_ROBIN,
            SECOND_CHANCE
        };

    private:
        const size_t memory_limit_bytes;
        const size_t sleep_time;
        const bool debug;
        const SwappingAlgo swapping_algo;

        std::atomic_bool keep_average{false};
        uint64_t sum{0};
        size_t measure_count{0};

        std::vector<cown_pair> cowns;
        std::unordered_map<Cown*, bool> cown_flags{};

        uint64_t cowns_size_bytes = 0;

        size_t next_cown{0};
        std::mutex cowns_mutex;
        std::thread monitoring_thread;
        std::atomic_bool keep_monitoring;
        std::atomic_bool running{false};
        std::condition_variable cv;

        std::atomic_uint64_t to_be_swapped{0};

#ifdef USE_SYSTEMATIC_TESTING
        std::atomic_bool registered{false};
        size_t nothing_loop_count{0};
#endif


    private:

        int32_t get_next_cown()
        {
            size_t ret;

            do
            {
                std::lock_guard<std::mutex> lock(cowns_mutex);
                switch (swapping_algo)
                {
                    case SwappingAlgo::LFU:
                        for (size_t i = 0; i < cowns.size(); ++i)
                        {
                            if (cown_flags[cowns[i].first])
                            {
                                next_cown = CownSwapper::get_acceses(cowns[i].first) 
                                            <= CownSwapper::get_acceses(cowns[next_cown].first) ? i : next_cown;
                            }
                        }

                        break;

                    case SwappingAlgo::LRU:
                        for (size_t i = 0; i < cowns.size(); ++i)
                        {
                            if (cown_flags[cowns[i].first])
                            {
                                next_cown = CownSwapper::get_acceses_time(cowns[i].first) 
                                            <= CownSwapper::get_acceses_time(cowns[next_cown].first) ? i : next_cown;
                            }
                        }

                        break;

                    case SwappingAlgo::RANDOM:
                        next_cown = std::rand() % cowns.size();
                        break;

                    case SwappingAlgo::ROUND_ROBIN:
                        break;

                    case SwappingAlgo::SECOND_CHANCE:
                        while (keep_monitoring && CownSwapper::was_accessed(cowns[next_cown++].first))
                        { 
                            if (next_cown == cowns.size())
                                next_cown = 0;
                        }

                        --next_cown;

                        break;
                }

                ret = next_cown;
                if (++next_cown == cowns.size())
                    next_cown = 0;
            }
            while (!cown_flags[cowns[ret].first] && keep_monitoring);

            cown_flags[cowns[ret].first] = false;

            if (!keep_monitoring)
                return -1;

            return ret;
        }

        CownMemoryThread(size_t memory_limit_MB, size_t sleep_time, SwappingAlgo swapping_algo, bool debug) 
        : memory_limit_bytes(memory_limit_MB * 1024 * 1024), sleep_time(sleep_time), debug(debug), swapping_algo(swapping_algo)
        {
            this->keep_monitoring = true;

#ifdef USE_SYSTEMATIC_TESTING
            this->registered = false;
            this->nothing_loop_count = 0;
#endif

            if (! debug)
                monitoring_thread = std::thread(&CownMemoryThread::monitorMemoryUsage, this);

            Logging::cout() << "Monitoring thread created" << Logging::endl;
        }

#ifdef _WIN32
        // Function to get memory usage for Windows
        static SIZE_T getMemoryUsage() {
            PROCESS_MEMORY_COUNTERS_EX pmc;
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
                return pmc.PrivateUsage;
            } else {
                return 0;
            }
        }
#elif __linux__
        static int parseLine(char* line){
            // This assumes that a digit will be found and the line ends in " Kb".
            int i = strlen(line);
            const char* p = line;
            while (*p <'0' || *p > '9') p++;
            line[i-3] = '\0';
            i = atoi(p);
            return i;
        }

        static long getMemoryUsage(){ //Note: this value is in KB!
            FILE* file = fopen("/proc/self/status", "r");
            int result = -1;
            char line[128];

            while (fgets(line, 128, file) != NULL){
                if (strncmp(line, "VmRSS:", 6) == 0){
                    result = parseLine(line);
                    break;
                }
            }
            fclose(file);
            return result;
        }
#endif

        void unregister_cowns()
        {
            std::unique_lock<std::mutex> lock(cowns_mutex);
            while (!cowns.empty())
            {
                CownSwapper::unregister_cown(cowns.back().first);
                cowns.pop_back();
            }
            cowns_size_bytes = 0;
        }

        void unregister_cowns(std::vector<cown_pair>& vec)
        {
            std::unique_lock<std::mutex> lock(cowns_mutex);
            while (!vec.empty())
            {
                auto cown = vec.back();
                cowns_size_bytes -= cown.second;
                cowns.erase(std::find(cowns.begin(), cowns.end(), cown));
                CownSwapper::unregister_cown(vec.back().first);
                vec.pop_back();
            }
        }

        void monitorMemoryUsage() {
            auto prev_t = std::chrono::system_clock::now();
            auto prev_swap_time = std::chrono::system_clock::now();

            schedule_lambda([](){ Scheduler::add_external_event_source(); });

            std::vector<cown_pair> cowns_to_swap;
            int multiplier = 60;
            int64_t actual_swap_size = 0;
            size_t time = 1;
            uint64_t prev_usage = 0;
            std::this_thread::sleep_for(std::chrono::seconds(5));

            while (keep_monitoring.load(std::memory_order_relaxed))
            {
                // Get memory usage
                uint64_t system_usage_bytes = getMemoryUsage() * 1024;
                struct mallinfo2 info = mallinfo2();
                struct malloc_info_v1 malloc_info;
                get_malloc_info_v1(&malloc_info);

                uint64_t memory_usage = cowns_size_bytes + malloc_info.current_memory_usage;
                if (std::chrono::system_clock::now() > prev_t + std::chrono::seconds(1))
                {

                    // std::cout << "Time: " << time++ << std::endl;
                    // std::cout << "System Memory Usage: " << system_usage_bytes / 1024 / 1024 << " MB"<< std::endl;
                    // std::cout << "Mallinfo Memory Usage: " << info.uordblks / 1024 / 1024 << " MB"<< std::endl;
                    // std::cout << "snmalloc Memory Usage: " << malloc_info.current_memory_usage / 1024 / 1024 << " MB" << std::endl;
                    // std::cout << "Cown Memory Usage: " << cowns_size_bytes / 1024 / 1024 << " MB" << std::endl;
                    // std::cout << "======================= Cowns size: " << cowns.size() << ", Multiplier: " << multiplier << std::endl;
                    prev_t = std::chrono::system_clock::now();
                    if (keep_average)
                    {
                        sum += memory_usage / 1024 / 1024;
                        ++measure_count;
                    }
                }


                yield();             
                if (memory_limit_bytes > 0 && memory_usage > memory_limit_bytes * 90 / 100)
                {
                    const size_t SIZE_LIMIT = cowns.size();
                    const size_t SWAP_COUNT_MAX = 1;
                    int64_t mem_to_swap_bytes = (memory_usage - (memory_limit_bytes * multiplier / 100));
                    while (!cowns.empty() && cowns_to_swap.size() < SIZE_LIMIT && mem_to_swap_bytes > actual_swap_size)
                    {
                        auto cown_i = get_next_cown();
                        if (cown_i < 0)
                            break;

                        auto cown = cowns[cown_i];
                        cowns_to_swap.push_back(cown);
                        actual_swap_size += cown.second;
                    }

                    if (to_be_swapped.load(std::memory_order_acquire) < SWAP_COUNT_MAX)
                    {
                        prev_usage = memory_usage;
                        prev_swap_time = std::chrono::high_resolution_clock::now();
                        cowns_size_bytes -= actual_swap_size;
                        to_be_swapped.fetch_add(1, std::memory_order_acquire);
                        auto cowns_to_be_freed = 
                            ActualCownSwapper::schedule_swap(cowns_to_swap.size(), cowns_to_swap.data(), to_be_swapped, 
                                                                actual_swap_size, [this](cown_pair cown) 
                                                                    {
                                                                        cown_flags[cown.first] = true;
                                                                        cowns_size_bytes += cown.second;
                                                                    });

                        unregister_cowns(cowns_to_be_freed);
                        actual_swap_size = 0;
                        cowns_to_swap.clear();
                    }
                }
#ifdef USE_SYSTEMATIC_TESTING
                else if (registered.load(std::memory_order_relaxed) && nothing_loop_count++ > 20)
                    break;
#endif
                else
                    cv.notify_all();

                yield();
            }

            Logging::cout() << "Monitoring thread terminated" << Logging::endl;
            unregister_cowns();

            schedule_lambda([](){ Scheduler::remove_external_event_source(); });
        }

        // Delete copy constructor and assignment operator to prevent duplication
        CownMemoryThread(const CownMemoryThread&) = delete;
        void operator=(const CownMemoryThread&) = delete;
        
        static CownMemoryThread& get_ref(size_t memory_limit_MB = 0, size_t sleep_time = 0,
                                            SwappingAlgo swapping_algo = SwappingAlgo::LRU, bool debug = false)
        {
            static CownMemoryThread ref = CownMemoryThread(memory_limit_MB, sleep_time, swapping_algo, debug);            
            return ref;
        }

    public:
        static void start_keep_average()
        {
            get_ref().keep_average = true;
        }

        static uint64_t get_memory_usage_MB()
        {
            return getMemoryUsage() / 1024;
        }

        static const std::string algo_to_string(SwappingAlgo algo)
        {
            std::unordered_map<SwappingAlgo, std::string> table =
            {
                {SwappingAlgo::LRU, "LRU"},
                {SwappingAlgo::LFU, "LFU"},
                {SwappingAlgo::RANDOM, "Random"},
                {SwappingAlgo::ROUND_ROBIN, "Round Robin"},
                {SwappingAlgo::SECOND_CHANCE, "Second Chance"},
            };

            return table[algo];
        }

        static void create(size_t memory_limit_MB, size_t sleep_time, SwappingAlgo swapping_algo)
        {
            auto& ref = get_ref(memory_limit_MB, sleep_time, swapping_algo);
            if (ref.running.exchange(true, std::memory_order_relaxed))
            {
                Logging::cout() << "Error, cannot create new monitoring thread while old one is running" << Logging::endl;
                abort();
            }
        }

        static void wait(std::unique_lock<std::mutex> lock)
        {
            get_ref().cv.wait(lock);
        }

        static auto create_debug(size_t memory_limit_MB, size_t sleep_time, SwappingAlgo swapping_algo)
        {
            auto& ref = get_ref(memory_limit_MB, sleep_time, swapping_algo, /*debug =*/ true);
            if (ref.running.exchange(true, std::memory_order_relaxed))
            {
                Logging::cout() << "Error, cannot create new monitoring thread while old one is running" << Logging::endl;
                abort();
            }

            return [&ref](){ ref.monitorMemoryUsage(); };
        }

        static uint64_t stop_monitoring()
        {
            auto& ref = get_ref();
            if (!ref.running.load(std::memory_order_relaxed))
            {
                Logging::cout() << "Memory thread is not running" << Logging::endl;
                return 0;
            }

            std::cout << "Stop called" << std::endl;
            ref.running.store(false, std::memory_order_acq_rel);
            ref.keep_monitoring.store(false, std::memory_order_acq_rel);

            if (!ref.debug)
                ref.monitoring_thread.join();

            return ref.measure_count == 0 ? 0 : ref.sum / ref.measure_count;
        }

        template<typename T>
        static bool register_cowns(size_t count, cown_ptr<T>* cown_ptrs)
        {
            auto& ref = get_ref();
            if (!ref.running.load(std::memory_order_relaxed))
            {
                Logging::cout() << "Memory thread is not running" << Logging::endl;
                return false;
            }

            for (size_t i = 0; i < count; ++i)
            {
                auto cown_pair = ActualCownSwapper::register_cown(cown_ptrs[i]);
                if (cown_pair.first == nullptr)
                    return false;

                std::unique_lock<std::mutex> lock(ref.cowns_mutex);
                ref.cown_flags[cown_pair.first] = true;
                ref.cowns.push_back(cown_pair);
                ref.cowns_size_bytes += cown_pair.second;
            }

        // In systematic testing, stop monitoring never gets called because it waits until all threads terminate before
        // shceduler.run() ends
#ifdef USE_SYSTEMATIC_TESTING
            get_ref().registered.store(true, std::memory_order_relaxed);
#endif
            return true;
        }
    };
} // namespace verona::cpp
