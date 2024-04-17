#pragma once

#include "cown.h"
#include "work.h"

#include <type_traits>
#include <fstream>
#include <filesystem>
#include <sstream>
#include <optional>

namespace verona::rt
{
    template<class T, class = void>
    struct has_serialize
    : std::false_type
    {};

    template<class T>
    struct has_serialize<T, std::enable_if_t<std::is_same_v<void(char *, size_t&, SerializeMode), decltype(T::serialize)>>>
    : std::true_type
    {};

    class Behaviour;

    class CownSwapper {
    private:

    public:
        // Should use concepts if we move to C++ 20.
        template<typename T>
        static constexpr bool is_swappable()
        {
            if(!std::is_pointer_v<T>)
                return false;

            using BaseT = std::remove_pointer_t<T>;

            return has_serialize<BaseT>::value;
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

        static auto get_fetch_lambda(Cown *cown, bool& should_fetch)
        {
            auto fetch_lambda = [cown]()
            {
                Logging::cout() << "Fetching cown " << cown << Logging::endl;

                // using BaseT = std::remove_pointer_t<T>;

                std::fstream data;
                size_t data_size;

                cown->serialize(data, data_size);

                auto cown_dir = get_cown_dir();
                std::stringstream filename;
                // filename << cown << ".cown";
                // std::ifstream ifs(cown_dir / filename.str(), std::ios::in | std::ios::binary);
                // cown->value = BaseT::load(ifs);
                // ifs.close();
            };
            // auto foo = Behaviour::make<decltype(fetch_lambda)>(1, fetch_lambda);

            should_fetch = false;
            auto expected = ON_DISK;
            if (cown->swapped.compare_exchange_strong(expected, SwapStatus::IN_MEMORY, std::memory_order_acquire))
            {
                Logging::cout() << "Scheduling fetch for cown " << cown << Logging::endl;
                should_fetch = true;
            }
            return fetch_lambda;
        }

        static bool swap_to_disk(Cown *cown)
        {
            // if constexpr (is_swappable<typeof(cown->value)>())
            // {
                auto expected = SwapStatus::IN_MEMORY;
                return cown->swapped.compare_exchange_strong(expected, SwapStatus::ON_DISK, std::memory_order_acq_rel);
            // }
        }

    };

} // namespace verona::rt
