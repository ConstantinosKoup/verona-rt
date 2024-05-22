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
        // std::vector<Cown*> cowns;
        std::deque<Cown*> cowns;
        std::mutex cowns_mutex;
        std::thread monitoring_thread;
        size_t memory_limit_MB;
        size_t sleep_time;
        std::atomic_bool keep_monitoring;
        std::atomic_bool running{false};
        bool debug{false};
        std::condition_variable cv;

#ifdef USE_SYSTEMATIC_TESTING
        std::atomic_bool registered{false};
        size_t nothing_loop_count{0};
#endif

    private:
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
            std::unique_lock<std::mutex> lock(cowns_mutex);
            while (!cowns.empty())
            {
                // CownSwapper::unregister_cown(cowns.back());
                // cowns.pop_back();
                CownSwapper::unregister_cown(cowns.front());
                cowns.pop_front();
            }
        }

        void monitorMemoryUsage() {
            auto prev_t = std::chrono::system_clock::now();

            schedule_lambda([](){ Scheduler::add_external_event_source(); });

            while (keep_monitoring.load(std::memory_order_relaxed))
            {
                // Get memory usage
                long memory_usage_MB = getMemoryUsage() / 1024;

                // Print memory usage
#ifdef USE_SYSTEMATIC_TESTING
                Logging::cout() << "Memory Usage: " << memory_usage_MB << " MB" << Logging::endl;
#else
                if (std::chrono::system_clock::now() > prev_t + std::chrono::seconds(1))
                {
                    std::cout << "Memory Usage: " << memory_usage_MB << " MB" << std::endl;
                    prev_t = std::chrono::system_clock::now();
                }
#endif

                yield();
                if (memory_limit_MB > 0 && memory_usage_MB >= memory_limit_MB * 9 / 10)
                {
                    std::unique_lock<std::mutex> lock(cowns_mutex);
                    if (!cowns.empty())
                    {
                        // auto min_cown_it = std::min_element(cowns.begin(), cowns.end(), CownSwapper::num_accesses_comparator);
                        // auto cown = *min_cown_it;
                        // cowns.erase(min_cown_it);
                        auto cown = cowns.front();
                        cowns.pop_front();
                        if (ActualCownSwapper::schedule_swap(cown, [this](Cown *cown) 
                                                                         { 
                                                                            std::unique_lock<std::mutex> lock(cowns_mutex);
                                                                            cowns.push_back(cown); 
                                                                        }))
                            CownSwapper::unregister_cown(cown);
                    }
                }
#ifdef USE_SYSTEMATIC_TESTING
                else if (registered.load(std::memory_order_relaxed) && nothing_loop_count++ > 20)
                    break;
#endif
                else
                    cv.notify_all();

#ifdef USE_SYSTEMATIC_TESTING
                yield();
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
#endif
            }

            Logging::cout() << "Monitoring thread terminated" << Logging::endl;
            unregister_cowns();

            schedule_lambda([](){ Scheduler::remove_external_event_source(); });
        }
        
        void reset(size_t memory_limit_MB, size_t sleep_time, bool debug = false)
        {

            this->keep_monitoring = true;
            this->memory_limit_MB = memory_limit_MB;
            this->sleep_time = sleep_time;
            this->running = true;
            this->debug = debug;

#ifdef USE_SYSTEMATIC_TESTING
            this->registered = false;
            this->nothing_loop_count = 0;
#endif

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
        static void create(size_t memory_limit_MB = 0, size_t sleep_time = 2500)
        {
            auto& ref = get_ref();
            if (ref.running.load(std::memory_order_relaxed))
            {
                Logging::cout() << "Error, cannot create new monitoring thread while old one is running" << Logging::endl;
                abort();
            }
            ref.reset(memory_limit_MB, sleep_time);
        }

        static void wait(std::unique_lock<std::mutex> lock)
        {
            get_ref().cv.wait(lock);
        }

        static auto create_debug(size_t memory_limit_MB = 0, size_t sleep_time = 2500)
        {
            auto& ref = get_ref();
            if (ref.running.load(std::memory_order_relaxed))
            {
                Logging::cout() << "Error, cannot create new monitoring thread while old one is running" << Logging::endl;
                abort();
            }

            ref.reset(memory_limit_MB, sleep_time, true);
            return [&ref](){ ref.monitorMemoryUsage(); };
        }

        static void stop_monitoring()
        {
            auto& ref = get_ref();
            if (!ref.running.load(std::memory_order_relaxed))
            {
                Logging::cout() << "Memory thread is not running" << Logging::endl;
                return;
            }

            ref.running.store(false, std::memory_order_acq_rel);
            ref.keep_monitoring.store(false, std::memory_order_acq_rel);

            if (!ref.debug)
                ref.monitoring_thread.join();
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
                auto cown = ActualCownSwapper::register_cown(cown_ptrs[i]);
                if (cown == nullptr)
                    return false;

                std::unique_lock<std::mutex> lock(ref.cowns_mutex);
                // ref.cowns.push_back(cown);
                ref.cowns.push_front(cown);
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
