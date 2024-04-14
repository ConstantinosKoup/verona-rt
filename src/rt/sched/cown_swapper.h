#pragma once

#include "cown.h"

#include <type_traits>
#include <fstream>
#include <filesystem>
#include <sstream>

namespace verona::rt
{
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
    
    public:
        static void fetch_cown_from_disk(Cown *cown)
        {
            // if constexpr (is_swappable<typeof(cown->value)>())
            // {
                auto expected = ON_DISK;
                cown->swapped.compare_exchange_strong(expected, SwapStatus::IN_MEMORY, std::memory_order_acquire);

                Logging::cout() << "Scheduling fetch for cown " << cown << Logging::endl;
                schedule_lambda(cown, [cown]()
                {
                    Logging::cout() << "Fetching cown " << cown << Logging::endl;

                    // using BaseT = std::remove_pointer_t<T>;

                    auto cown_dir = get_cown_dir();
                    std::stringstream filename;
                    filename << cown << ".cown";
                    std::ifstream ifs(cown_dir / filename.str(), std::ios::in | std::ios::binary);
                    // cown->value = BaseT::load(ifs);
                    ifs.close();
                });
            // }
        }

        // static void swap_to_disk(Cown *cown)
        // {
        //     // if constexpr (is_swappable<typeof(cown->value)>())
        //     // {
        //         auto expected = SwapStatus::IN_MEMORY;
        //         if (cown->swapped.compare_exchange_strong(expected, SwapStatus::ON_DISK, std::memory_order_acq_rel))
        //         {
        //             auto swap_function = [cown]()
        //             {
        //                 Logging::cout() << "Swapping cown " << cown << Logging::endl;

        //                 // using BaseT = std::remove_pointer_t<T>;

        //                 auto cown_dir = get_cown_dir();
        //                 std::stringstream filename;
        //                 filename << cown << ".cown";
        //                 std::ofstream ofs(cown_dir / filename.str(), std::ios::out | std::ios::binary);
        //                 // BaseT::save(ofs, cown->value);
        //                 ofs.close();

        //                 // T value_to_delete = cown->value;
        //                 // delete value_to_delete;
        //             };
        //         }
        //     // }
        // }

    };

} // namespace verona::rt
