#pragma once

#include "debug/logging.h"
#include "cpp/cown_swapper.h"

#include <iostream>
#include <thread>
#include <chrono>

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
            while (keep_monitoring.load(std::memory_order_relaxed)) {
                // Get memory usage
                long memoryUsage = getMemoryUsage() / 1024;

                // Print memory usage
                Logging::cout() << "Memory Usage: " << memoryUsage << " MB" << Logging::endl;

                // Sleep for one second
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }

        std::thread monitoring_thread;
        std::atomic_bool keep_monitoring{true};

        CownMemoryThread()
        {
            monitoring_thread = std::thread(&CownMemoryThread::monitorMemoryUsage, this);
            Logging::cout() << "Monitoring thread created" << Logging::endl;
        }

        // Delete copy constructor and assignment operator to prevent duplication
        CownMemoryThread(const CownMemoryThread&) = delete;
        void operator=(const CownMemoryThread&) = delete;

    public:
        static CownMemoryThread& get_ref() {
            static CownMemoryThread ref;            
            return ref;
        }

        static void stop_monitoring() {
            get_ref().keep_monitoring.store(false, std::memory_order_acq_rel);
            get_ref().monitoring_thread.join();
            Logging::cout() << "Monitoring thread terminated" << Logging::endl;
        }

    
    };
} // namespace verona::cpp
