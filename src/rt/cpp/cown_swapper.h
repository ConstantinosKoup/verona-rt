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
            schedule_swap_lambda(cown, [cown]()
            {
                Logging::cout() << "Swapping cown " << cown << Logging::endl;

                using BaseT = std::remove_pointer_t<T>;

                auto cown_dir = CownSwapper::get_cown_dir();
                std::stringstream filename;
                filename << cown << ".cown";
                std::ofstream ofs(cown_dir / filename.str(), std::ios::out | std::ios::binary);
                // BaseT::save(ofs, cown->value);
                ofs.close();

                // T value_to_delete = cown->value;
                // delete value_to_delete;
            });
        }
    };

} // namespace verona::cpp
