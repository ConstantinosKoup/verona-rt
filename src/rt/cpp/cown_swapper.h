#pragma once

#include "cown.h"
#include "access.h"

#include <verona.h>
#include <type_traits>
namespace verona::cpp
{
    using namespace verona::rt;

    class CownSwapper {
    private:
        template<typename T>
        static constexpr bool is_swappable()
        {
            return std::is_pointer_v<T>;
        }

        template<typename T>
        static void fetch_actual_cown(ActualCown<T> *cown)
        {
            if (cown->value == nullptr)
            {
                //good code
                cown->value = reinterpret_cast<T>(0xdeadbeef);

                schedule_lambda(cown, [cown](){
                    Logging::cout() << "Fetching cown " << cown << Logging::endl;
                });
            }
        }

        template<typename T>
        static void fetch_access_cowns(Access<T>& access)
        {
            if constexpr (is_swappable<T>())
            {
                fetch_actual_cown<T>(access.t);
            }
        }

        template<typename T>
        static void fetch_access_cowns(AccessBatch<T>& access_batch)
        {
            if constexpr (is_swappable<T>())
            {
                for (size_t i = 0; i < access_batch.arr_len; i++)
                {
                    fetch_actual_cown<T>(access_batch.act_array[i]);
                }
            }
        }

        template<size_t index = 0, typename... Ts>
        static void fetch_tuple_cowns(std::tuple<Ts...>& cown_tuple)
        {
            if constexpr (index >= sizeof...(Ts))
            {
                return;
            }
            else
            {
                fetch_access_cowns(std::get<index>(cown_tuple));
                fetch_tuple_cowns<index + 1>(cown_tuple);
            }
        }
    
    public:
        template<typename... Ts>
        static void fetch_cowns_from_disk(std::tuple<Ts...>& cown_tuple)
        {
            fetch_tuple_cowns(cown_tuple);
        }

        template<typename T>
        static void swap_to_disk(cown_ptr<T>& cown_ptr)
        {
            if constexpr (is_swappable<T>())
            {
                ActualCown<T>* cown = cown_ptr.allocated_cown;
                if (cown->value != nullptr)
                {
                    auto value = cown->value;
                    cown->value = nullptr;

                    schedule_lambda(cown, [cown, value](){
                        Logging::cout() << "Swapping cown " << cown << Logging::endl;
                    });
                }
                else 
                {
                    Logging::cout() << "Swapping cown " << cown << " failed, cown already swapped" << Logging::endl;
                    exit(EXIT_FAILURE);
                }
            }
        }

    };

} // namespace verona::rt
