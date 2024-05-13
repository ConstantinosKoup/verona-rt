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
    // temporary
    template <typename T> 
class TSQueue { 
private: 
    // Underlying queue 
    std::queue<T> m_queue; 
  
    // mutex for thread synchronization 
    std::mutex m_mutex; 
  
    // Condition variable for signaling 
    std::condition_variable m_cond; 
  
public: 
    size_t size()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        return m_queue.size();
    }

    bool empty()
    {
        std::unique_lock<std::mutex> lock(m_mutex);

        return m_queue.empty();
    }

    // Pushes an element to the queue 
    void push(T item) 
    { 
  
        // Acquire lock 
        std::unique_lock<std::mutex> lock(m_mutex); 
  
        // Add item 
        m_queue.push(item); 
  
        // Notify one thread that 
        // is waiting 
        m_cond.notify_one(); 
    } 
  
    // Pops an element off the queue 
    T pop() 
    { 
  
        // acquire lock 
        std::unique_lock<std::mutex> lock(m_mutex); 
  
        // wait until queue is not empty 
        m_cond.wait(lock, 
                    [this]() { return !m_queue.empty(); }); 
  
        // retrieve item 
        T item = m_queue.front(); 
        m_queue.pop(); 
  
        // return item 
        return item; 
    } 
}; 
  
    using namespace verona::rt;

    class CownMemoryThread
    {
    private:
        TSQueue<Cown*> cowns;
        std::thread monitoring_thread;
        size_t memory_limit_MB;
        std::atomic_bool keep_monitoring;
        std::atomic_bool running{false};

#ifdef USE_SYSTEMATIC_TESTING
        std::atomic_bool registered{false};
        size_t nothing_loop_count{0};
#endif

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
                CownSwapper::unregister_cown(cowns.pop());
            }
        }
           

        void monitorMemoryUsage() {
            size_t count = 0;

            while (keep_monitoring.load(std::memory_order_relaxed))
            {
                // Get memory usage
                long memory_usage_MB = getMemoryUsage() / 1024;

                // Print memory usage

#ifdef USE_SYSTEMATIC_TESTING
                Logging::cout() << "Memory Usage: " << memory_usage_MB << " MB" << Logging::endl;
#else
                if (count++ % 20 == 0)
                    std::cout << "Memory Usage: " << memory_usage_MB << " MB" << std::endl;
#endif

                yield();

                if (memory_limit_MB == 0)
                    unregister_cowns();
                else if (memory_usage_MB >= memory_limit_MB * 9 / 10)
                {
                    auto count = 0;
                    for (int i = 0; i < cowns.size() && count < 4; ++i)
                    {
                        auto cown = cowns.pop();
                        if (ActualCownSwapper::schedule_swap(cown, [this](Cown *cown) 
                                                                        { cowns.push(cown); }))
                            CownSwapper::unregister_cown(cown);
                        ++count;
                    }
                }
#ifdef USE_SYSTEMATIC_TESTING
                else if (registered.load(std::memory_order_relaxed) && nothing_loop_count++ > 20)
                    break;

                yield();
#else
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
#endif
            }

            Logging::cout() << "Monitoring thread terminated" << Logging::endl;
            unregister_cowns();
        }
        
        void reset(size_t memory_limit_MB, bool debug = false)
        {

            this->keep_monitoring = true;
            this->memory_limit_MB = memory_limit_MB;
            this->running = true;

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
        static void create(size_t memory_limit_MB = 0)
        {
            auto& ref = get_ref();
            if (ref.running.load(std::memory_order_relaxed))
            {
                Logging::cout() << "Error, cannot create new monitoring thread while old one is running" << Logging::endl;
                abort();
            }
            ref.reset(memory_limit_MB);
        }

        static auto create_debug(size_t memory_limit_MB = 0)
        {
            auto& ref = get_ref();
            if (ref.running.load(std::memory_order_relaxed))
            {
                Logging::cout() << "Error, cannot create new monitoring thread while old one is running" << Logging::endl;
                abort();
            }

            ref.reset(memory_limit_MB, true);
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

                ref.cowns.push(cown);
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
