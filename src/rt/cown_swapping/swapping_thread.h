#pragma once

#include "debug/logging.h"
#include "cpp/cown_swapper.h"

#include <iostream>
#include <thread>
#include <chrono>
#include <queue>

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

    class CownMemoryThread
    {
    private:
        std::deque<Cown*> cowns;
        std::thread monitoring_thread;
        size_t memory_limit_MB;
        std::atomic_bool keep_monitoring{true};



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
        int parseLine(char* line){
            // This assumes that a digit will be found and the line ends in " Kb".
            int i = strlen(line);
            const char* p = line;
            while (*p <'0' || *p > '9') p++;
            line[i-3] = '\0';
            i = atoi(p);
            return i;
        }

        int getMemoryUsage(){ //Note: this value is in KB!
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

        void monitorMemoryUsage() {
            while (keep_monitoring.load(std::memory_order_relaxed) || !cowns.empty())
            {
                // Get memory usage
                long memory_usage_MB = getMemoryUsage() / 1024;

                // Print memory usage
                Logging::cout() << "Memory Usage: " << memory_usage_MB << " MB" << Logging::endl;

                yield();

                if (memory_limit_MB > 0 && memory_usage_MB >= memory_limit_MB * 9 / 10)
                {
                    auto count = 0;
                    for (int i = 0; i < cowns.size() && count < 4; ++i)
                    {
                        auto cown = cowns.front();
                        cowns.pop_front();
                        if (CownSwapper::free_cown(cown))
                            continue;

                        cowns.push_back(cown);
                        if (CownSwapper::is_in_memory(cown))
                        {
                            ActualCownSwapper::schedule_swap(cown);
                            ++count;
                        }
                    }
                }
#ifdef USE_SYSTEMATIC_TESTING
                    yield();
#else
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif
            }

            Logging::cout() << "Monitoring thread terminated" << Logging::endl;
            CownSwapper::free_cowns(cowns);
        }
        
        CownMemoryThread(bool debug = false)
        {
            if (! debug)
                monitoring_thread = std::thread(&CownMemoryThread::monitorMemoryUsage, this);

            Logging::cout() << "Monitoring thread created" << Logging::endl;
        }

        // Delete copy constructor and assignment operator to prevent duplication
        CownMemoryThread(const CownMemoryThread&) = delete;
        void operator=(const CownMemoryThread&) = delete;
        
        static CownMemoryThread& get_ref(bool debug = false)
        {
            static CownMemoryThread ref = CownMemoryThread(debug);            
            return ref;
        }

    public:
        static void create(size_t memory_limit_MB = 0)
        {
            get_ref().memory_limit_MB = memory_limit_MB;
        }

        static auto create_debug(size_t memory_limit_MB = 0)
        {
            auto& ref = get_ref(true);

            ref.memory_limit_MB = memory_limit_MB;
            return [&ref](){ ref.monitorMemoryUsage(); };
        }

        static void stop_monitoring()
        {
            auto& ref = get_ref();
            ref.keep_monitoring.store(false, std::memory_order_acq_rel);
        }

        template<typename T>
        static bool register_cown(cown_ptr<T>& cown_ptr)
        {
            auto cown = ActualCownSwapper::register_cown(cown_ptr);
            if (cown == nullptr)
                return false;

            get_ref().cowns.push_back(cown);

#ifdef USE_SYSTEMATIC_TESTING
            get_ref().keep_monitoring.store(false, std::memory_order_relaxed);
#endif
            return true;
        }

    
    };
} // namespace verona::cpp
