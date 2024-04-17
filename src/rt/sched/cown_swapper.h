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
    class CownSwapper {
    private:

    public:
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
                auto cown_dir = get_cown_dir();
                std::stringstream filename;
                filename << cown << ".cown";
                std::fstream ifs(cown_dir / filename.str(), std::ios::in | std::ios::binary);

                cown->serialize(ifs);
                
                ifs.close();
            };

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
