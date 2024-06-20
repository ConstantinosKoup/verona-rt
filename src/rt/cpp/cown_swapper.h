#pragma once

#include "cown.h"
#include "../sched/behaviour.h"
#include "../sched/cown_swapper.h"

#include <verona.h>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace verona::cpp
{
    using namespace verona::rt;

    class ActualCownSwapper {
    private:
        /// @brief Called to deallocate cown's fetch behaviour when it is being deallocated.
        template<typename Be>
        static void dealloc_fetch(BehaviourCore* behaviour)
        {
            Logging::cout() << "Fetch Behaviour " << behaviour << " dealloc" << Logging::endl;
            Be* body = behaviour->get_body<Be>();
            auto work = behaviour->as_work();
            
            // Dealloc behaviour
            body->~Be();
            work->dealloc();
        }

        static void set_fetch_behaviour(cown_pair cown, std::function<void(cown_pair)> register_to_thread)
        {
            auto fetch_lambda = CownSwapper::get_fetch_lambda(cown, register_to_thread);
            Request requests[] = {Request::write(cown.first)};
            BehaviourCore *fetch_behaviour = Behaviour::prepare_to_schedule<decltype(fetch_lambda)>
                                            (1, requests, std::forward<decltype(fetch_lambda)>(fetch_lambda));
            CownSwapper::set_fetch_behaviour(cown.first, fetch_behaviour, dealloc_fetch<decltype(fetch_lambda)>);
        }

        /// @brief Schedules swap for an array of cowns.
        /// @return A vector of unused cowns that should be removed from swapping thread. 
        static std::vector<cown_pair> schedule_swap(size_t count, cown_pair *cowns, std::atomic_uint64_t& to_be_swapped, 
            std::function<void(cown_pair)> register_to_thread)
        {
            if (count == 0)
            {
                to_be_swapped.fetch_sub(1, std::memory_order_release);
                return {};
            }

            size_t new_size = 0;
            auto& alloc = ThreadAlloc::get();
            auto** new_cowns = (Cown**)alloc.alloc(count * sizeof(Cown*));
            std::vector<cown_pair> cowns_to_be_freed;
            for (size_t i = 0; i < count; ++i)
            {
                // Check if the cown has any strong references left to check if it is still in use by the main program.
                if (CownSwapper::acquire_strong(cowns[i].first))
                {
                    set_fetch_behaviour(cowns[i], register_to_thread);
                    new_cowns[new_size++] = cowns[i].first;
                }
                else
                    cowns_to_be_freed.push_back(cowns[i]);
            }

            auto swap_lambda = CownSwapper::get_swap_lambda(new_size, new_cowns, to_be_swapped);
            Behaviour::schedule<YesTransfer>(new_size, new_cowns, std::forward<decltype(swap_lambda)>(swap_lambda), true);

            return cowns_to_be_freed;
        }

        template<typename T>
        static size_t sizeof_cown(ActualCown<T> *cown)
        {
            return std::remove_pointer_t<T>::size(cown->value);
        }

        friend class CownMemoryThread;
        
    public:
        template<typename T>
        static void debug_access_cown(cown_ptr<T>& cown_ptr)
        {
            CownSwapper::set_in_memory(cown_ptr.allocated_cown);
        }

        template<typename T>
        static size_t debug_get_accesses_cown(cown_ptr<T>& cown_ptr)
        {
            return CownSwapper::get_num_accesses(cown_ptr.allocated_cown);
        }

        
        template<typename T>
        static size_t debug_get_fetches_cown(cown_ptr<T>& cown_ptr)
        {
            return CownSwapper::debug_get_fetches(cown_ptr.allocated_cown);
        }

        /// @brief Perform a weak acquire on the cown to prevent it from being freed while the swapping thread holds it.
        /// @return A cown pair if the cown is swappable, otherwise null.
        template<typename T>
        static std::pair<Cown *, size_t> register_cown(cown_ptr<T>& cown_ptr)
        {
            if constexpr (! ActualCown<T>::is_serializable::value)
                return {nullptr, 0};

            ActualCown<T>* cown = cown_ptr.allocated_cown;
            size_t size = sizeof_cown(cown);
            CownSwapper::register_cown(cown);
            
            return {cown, size};
        }

        /// @brief Perform weak release on a cown to allow it to be deallocated. 
        template<typename T>
        static void unregister_cown(cown_ptr<T>& cown_ptr)
        {
            if constexpr (! ActualCown<T>::is_serializable::value)
                return;

            ActualCown<T>* cown = cown_ptr.allocated_cown;
            CownSwapper::unregister_cown(cown);
        }

        /// @return Underlying cown pointer if the cown is swappable, otherwise null.
        template<typename T>
        static ActualCown<T> *get_cown_if_swappable(cown_ptr<T>& cown_ptr)
        {
            ActualCown<T>* cown = cown_ptr.allocated_cown;
            if constexpr (! ActualCown<T>::is_serializable::value)
                return nullptr;

            return cown;
        }
        

        /// @brief Schedule a swap behaviour.
        template<typename T>
        static void schedule_swap(cown_ptr<T>& cown_ptr)
        {
            ActualCown<T> *cown = get_cown_if_swappable(cown_ptr);
            if (cown == nullptr)
            {
                Logging::cout() << "Cannot swap cown " << cown << " as its value is not serializable" << Logging::endl;
                return;
            }

            cown_pair pair = {cown, sizeof_cown(cown)};
            std::atomic_uint64_t to_be_swapped{0};
            
            schedule_swap(1, &pair, to_be_swapped, [](cown_pair p){});
        }
    };

} // namespace verona::cpp
