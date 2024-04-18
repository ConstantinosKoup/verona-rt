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
            BehaviourCore *b = 
                Behaviour::prepare_to_schedule<T>(1, requests, std::forward<T>(f), /* is_swap */ true);
            
            BehaviourCore *arr[] = {b};
            BehaviourCore::schedule_many(arr, 1);
        }
    public:
        template<typename T>
        static void schedule_swap(cown_ptr<T>& cown_ptr)
        {
            ActualCown<T>* cown = cown_ptr.allocated_cown;
            if constexpr (! ActualCown<T>::is_serializable::value)
            {
                Logging::cout() << "Cannot swap cown " << cown << " as its value is not serializable" << Logging::endl;
                return;
            }

            auto swap_lambda = CownSwapper::get_swap_lambda(cown);
            schedule_swap_lambda(cown, std::forward<decltype(swap_lambda)>(swap_lambda));
        }
    };

} // namespace verona::cpp
