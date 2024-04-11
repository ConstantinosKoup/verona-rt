#pragma once

#include "cown.h"
#include "access.h"

#include <verona.h>
#include <type_traits>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace verona::cpp
{
    using namespace verona::rt;

    template<class T, class = void>
    struct has_save
    : std::false_type
    {};

    template<class T>
    struct has_save<T, std::enable_if_t<std::is_same_v<void(std::ofstream&, T*), decltype(T::save)>>>
    : std::true_type
    {};

    template<class T, class = void>
    struct has_load
    : std::false_type
    {};

    template<class T>
    struct has_load<T, std::enable_if_t<std::is_same_v<T*(std::ifstream&), decltype(T::load)>>>
    : std::true_type
    {};

    class CownSwapper {
    private:
        // Should use concepts if we move to C++ 20.
        template<typename T>
        static constexpr bool is_swappable()
        {
            if(!std::is_pointer_v<T>)
                return false;

            using BaseT = std::remove_pointer_t<T>;

            return has_save<BaseT>::value && has_load<BaseT>::value;
        }

        static std::filesystem::path get_cown_dir()
        {
            namespace fs = std::filesystem;
            fs::path cown_dir = fs::temp_directory_path() / "verona-rt" / "cowns";
            
            // should be called once at the start of the runtime
            fs::create_directories(cown_dir);
            fs::permissions(cown_dir, fs::perms::owner_all);

            return cown_dir;
        }
        
        template<typename T>
        static void fetch_actual_cown(ActualCown<T> *cown)
        {
            auto expected = SwapStatus::IN_MEMORY;
            if (cown->swapped.compare_exchange_strong(expected, SwapStatus::DO_NOT_SWAP, std::memory_order_acquire))
                return;
            expected = SwapStatus::SWAPPING;
            if (cown->swapped.compare_exchange_strong(expected, SwapStatus::DO_NOT_SWAP, std::memory_order_acquire))
                return;

            expected = ON_DISK;
            cown->swapped.compare_exchange_strong(expected, SwapStatus::FETCHING, std::memory_order_acquire);

            Logging::cout() << "Scheduling fetch for cown " << cown << Logging::endl;
            schedule_lambda(cown, [cown]()
            {
                Logging::cout() << "Fetching cown " << cown << Logging::endl;

                using BaseT = std::remove_pointer_t<T>;

                auto cown_dir = get_cown_dir();
                std::stringstream filename;
                filename << cown << ".cown";
                std::ifstream ifs(cown_dir / filename.str(), std::ios::in | std::ios::binary);
                cown->value = BaseT::load(ifs);
                ifs.close();

                cown->swapped.store(SwapStatus::DO_NOT_SWAP, std::memory_order_release);
            });
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
        
        /////

        template<typename T>
        static void allow_actual_cown(ActualCown<T> *cown)
        {
            schedule_lambda(cown, [cown]()
            {
                auto expected = SwapStatus::DO_NOT_SWAP;
                if (cown->swapped.compare_exchange_strong(expected, SwapStatus::IN_MEMORY, std::memory_order_release))
                    Logging::cout() << "Allowing swappuing for cown " << cown << Logging::endl;
            });
        }

        template<typename T>
        static void allow_access_cowns(Access<T>& access)
        {
            if constexpr (is_swappable<T>())
            {
                allow_actual_cown<T>(access.t);
            }
        }

        template<typename T>
        static void allow_access_cowns(AccessBatch<T>& access_batch)
        {
            if constexpr (is_swappable<T>())
            {
                for (size_t i = 0; i < access_batch.arr_len; i++)
                {
                    allow_actual_cown<T>(access_batch.act_array[i]);
                }
            }
        }

        template<size_t index = 0, typename... Ts>
        static void allow_tuple_cowns(std::tuple<Ts...>& cown_tuple)
        {
            if constexpr (index >= sizeof...(Ts))
            {
                return;
            }
            else
            {
                allow_access_cowns(std::get<index>(cown_tuple));
                allow_tuple_cowns<index + 1>(cown_tuple);
            }
        }
    
    public:
        template<typename... Ts>
        static void fetch_cowns_from_disk(std::tuple<Ts...>& cown_tuple)
        {
            fetch_tuple_cowns(cown_tuple);
        }

        template<typename... Ts>
        static void allow_cowns_to_swap(std::tuple<Ts...>& cown_tuple)
        {
            allow_tuple_cowns(cown_tuple);
        }

        template<typename T>
        static void swap_to_disk(cown_ptr<T>& cown_ptr)
        {
            if constexpr (is_swappable<T>())
            {
                ActualCown<T>* cown = cown_ptr.allocated_cown;

                auto expected = SwapStatus::IN_MEMORY;
                if (cown->swapped.compare_exchange_strong(expected, SwapStatus::SWAPPING, std::memory_order_acq_rel))
                {
                    schedule_lambda(cown, [cown]()
                    {
                        if (cown->swapped.load(std::memory_order_acquire) == SwapStatus::SWAPPING)
                        {
                            Logging::cout() << "Swapping cown " << cown << Logging::endl;

                            using BaseT = std::remove_pointer_t<T>;

                            auto cown_dir = get_cown_dir();
                            std::stringstream filename;
                            filename << cown << ".cown";
                            std::ofstream ofs(cown_dir / filename.str(), std::ios::out | std::ios::binary);
                            BaseT::save(ofs, cown->value);
                            ofs.close();

                            T value_to_delete = cown->value;
                            auto expected = SwapStatus::SWAPPING;
                            if (cown->swapped.compare_exchange_strong(expected, SwapStatus::ON_DISK, std::memory_order_release))
                            {
                                delete value_to_delete;
                            }
                        }
                    });
                }
            }
        }

    };

} // namespace verona::rt
