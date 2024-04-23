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

    class CownMemoryThread {
    private:
#ifdef _WIN32
        // Function to get memory usage for Windows
        SIZE_T getMemoryUsage() {
            PROCESS_MEMORY_COUNTERS_EX pmc;
            if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc))) {
                return pmc.PrivateUsage;
            } else {
                return 0;
            }
        }
#elif __linux__
        // Function to get memory usage for Linux
        long getMemoryUsage() {
            struct rusage usage;
            getrusage(RUSAGE_SELF, &usage);
            return usage.ru_maxrss;
        }
#endif

        void monitorMemoryUsage() {
            auto count = 0;
            while (keep_monitoring.load(std::memory_order_relaxed)) {
                // Get memory usage
                long memory_usage_MB = getMemoryUsage() / 1024;

                // Print memory usage
                Logging::cout() << "Memory Usage: " << memory_usage_MB << " MB" << Logging::endl;

                if (memory_limit_MB > 0 && memory_usage_MB >= memory_limit_MB * 9 / 10)
                {
                    for (int i = 0; i < cowns.size() && count < 100; ++i)
                    {
                        auto cown = cowns.front();
                        cowns.pop();
                        cowns.push(cown);
                        if (CownSwapper::is_in_memory(cown))
                        {
                            ActualCownSwapper::schedule_swap(cown);
                            ++count;
                        }
                    }
                }

                // Sleep for one second
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        std::queue<Cown *> cowns;
        std::thread monitoring_thread;
        size_t memory_limit_MB;
        std::atomic_bool keep_monitoring{true};

        CownMemoryThread()
        {
            monitoring_thread = std::thread(&CownMemoryThread::monitorMemoryUsage, this);
            Logging::cout() << "Monitoring thread created" << Logging::endl;
        }

        // Delete copy constructor and assignment operator to prevent duplication
        CownMemoryThread(const CownMemoryThread&) = delete;
        void operator=(const CownMemoryThread&) = delete;

        static CownMemoryThread& get_ref()
        {
            static CownMemoryThread ref;            
            return ref;
        }
    public:
        static void create(size_t memory_limit_MB = 0)
        {
            get_ref().memory_limit_MB = memory_limit_MB;
        }

        static void stop_monitoring()
        {
            get_ref().keep_monitoring.store(false, std::memory_order_acq_rel);
            Logging::cout() << "Monitoring thread terminated" << Logging::endl;
        }

        template<typename T>
        static bool register_cown(cown_ptr<T>& cown_ptr)
        {
            Cown *cown = ActualCownSwapper::get_cown_if_swappable(cown_ptr);
            if (cown == nullptr)
                return false;

            get_ref().cowns.push(cown);
            return true;
        }

    
    };
} // namespace verona::cpp
