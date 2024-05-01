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
        std::atomic_bool keep_monitoring;

        CownMemoryThread() = default;



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

        void unregister_cowns()
        {
            while (!cowns.empty())
            {
                CownSwapper::unregister_cown(cowns.front());
                cowns.pop_front();
            }
        }
           

        void monitorMemoryUsage() {
            while (keep_monitoring.load(std::memory_order_relaxed) || !cowns.empty())
            {
                // Get memory usage
                long memory_usage_MB = getMemoryUsage() / 1024;

                // Print memory usage
                Logging::cout() << "Memory Usage: " << memory_usage_MB << " MB" << Logging::endl;

                yield();

                if (memory_limit_MB == 0)
                    unregister_cowns();
                else if (memory_usage_MB >= memory_limit_MB * 9 / 10)
                {
                    auto count = 0;
                    for (int i = 0; i < cowns.size() && count < 4; ++i)
                    {
                        auto cown = cowns.front();
                        cowns.pop_front();
                        if (!CownSwapper::acquire_strong(cown))
                            continue;

                        cowns.push_back(cown);
                        if (CownSwapper::is_in_memory(cown))
                        {
                            ActualCownSwapper::schedule_swap(cown);
                            ++count;
                        }

                        CownSwapper::release_strong(cown);
                    }
                }
#ifdef USE_SYSTEMATIC_TESTING
                    yield();
#else
                    // std::this_thread::sleep_for(std::chrono::milliseconds(500));
#endif
            }

            Logging::cout() << "Monitoring thread terminated" << Logging::endl;

            // Remove for bettter performance
            CownSwapper::clear_cown_dir();
        }
        
        void reset(size_t memory_limit_MB, bool debug = false)
        {

            this->keep_monitoring = true;
            this->memory_limit_MB = memory_limit_MB;

            if (! debug)
                monitoring_thread = std::thread(&CownMemoryThread::monitorMemoryUsage, this);

            Logging::cout() << "Monitoring thread created" << Logging::endl;
        }

        // Delete copy constructor and assignment operator to prevent duplication
        CownMemoryThread(const CownMemoryThread&) = delete;
        void operator=(const CownMemoryThread&) = delete;
        
        static CownMemoryThread& get_ref()
        {
            static CownMemoryThread ref = CownMemoryThread();            
            return ref;
        }

    public:
        static void create(size_t memory_limit_MB = 0)
        {
            get_ref().reset(memory_limit_MB);
        }

        static auto create_debug(size_t memory_limit_MB = 0)
        {
            auto& ref = get_ref();

            ref.reset(memory_limit_MB, true);
            return [&ref](){ ref.monitorMemoryUsage(); };
        }

        static void stop_monitoring()
        {
            auto& ref = get_ref();
            ref.keep_monitoring.store(false, std::memory_order_acq_rel);
        }

        template<typename T>
        static bool register_cowns(size_t count, cown_ptr<T>* cown_ptrs)
        {
            for (size_t i = 0; i < count; ++i)
            {
                auto cown = ActualCownSwapper::register_cown(cown_ptrs[i]);
                if (cown == nullptr)
                    return false;

                get_ref().cowns.push_back(cown);
            }

        // In systematic testing, stop monitoring never gets called because it waits until all threads terminate before
        // shceduler.run() ends
#ifdef USE_SYSTEMATIC_TESTING
            get_ref().keep_monitoring.store(false, std::memory_order_relaxed);
#endif
            return true;
        }

    
    };
} // namespace verona::cpp
