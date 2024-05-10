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
        template<typename T>
        static void schedule_swap_lambda(Cown* cown, T&& f)
        {
            Logging::cout() << "Scheduling swap for cown " << cown << Logging::endl;
            Request requests[] = {Request::write(cown)};
            requests[0].mark_move();
            BehaviourCore *b = 
                Behaviour::prepare_to_schedule<T>(1, requests, std::forward<T>(f), /* is_swap */ true);
            
            BehaviourCore *arr[] = {b};
            BehaviourCore::schedule_many(arr, 1);
        }

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

        static void set_fetch_behaviour(Cown *cown)
        {
            auto fetch_lambda = CownSwapper::get_fetch_lambda(cown);
            Request requests[] = {Request::write(cown)};
            BehaviourCore *fetch_behaviour = Behaviour::prepare_to_schedule<decltype(fetch_lambda)>
                                            (1, requests, std::forward<decltype(fetch_lambda)>(fetch_lambda));
            CownSwapper::set_fetch_behaviour(cown, fetch_behaviour, dealloc_fetch<decltype(fetch_lambda)>);
        }

        static bool schedule_swap(Cown *cown)
        {
            if (!CownSwapper::acquire_strong(cown))
                return false;
            set_fetch_behaviour(cown);

            auto swap_lambda = CownSwapper::get_swap_lambda(cown);
            schedule_swap_lambda(cown, std::forward<decltype(swap_lambda)>(swap_lambda));
            return true;
        }


        friend class CownMemoryThread;
        
    public:
        template<typename T>
        static Cown *register_cown(cown_ptr<T>& cown_ptr)
        {
            if constexpr (! ActualCown<T>::is_serializable::value)
                return nullptr;

            ActualCown<T>* cown = cown_ptr.allocated_cown;
            CownSwapper::register_cown(cown);
            
            return cown;
        }

        template<typename T>
        static void unregister_cown(cown_ptr<T>& cown_ptr)
        {
            if constexpr (! ActualCown<T>::is_serializable::value)
                return;

            ActualCown<T>* cown = cown_ptr.allocated_cown;
            CownSwapper::register_cown(cown);
        }

        template<typename T>
        static Cown *get_cown_if_swappable(cown_ptr<T>& cown_ptr)
        {
            ActualCown<T>* cown = cown_ptr.allocated_cown;
            if constexpr (! ActualCown<T>::is_serializable::value)
                return nullptr;

            return cown;
        }
        
        template<typename T>
        static void schedule_swap(cown_ptr<T>& cown_ptr)
        {
            Cown *cown = get_cown_if_swappable(cown_ptr);
            if (cown == nullptr)
            {
                Logging::cout() << "Cannot swap cown " << cown << " as its value is not serializable" << Logging::endl;
                return;
            }
            
            schedule_swap(cown);
        }
    };

} // namespace verona::cpp
