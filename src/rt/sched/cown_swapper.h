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
    class BehaviourCore;

    class CownSwapper {
    private:
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
        static bool is_in_memory(Cown *cown)
        {
            return cown->swap_satus.load(std::memory_order_relaxed) == SwapStatus::IN_MEMORY;
        }

        static auto get_swap_lambda(Cown* cown)
        {
            auto swap_lambda = [cown]()
            {
                Logging::cout() << "Swapping cown " << cown << Logging::endl;
                auto cown_dir = get_cown_dir();
                std::stringstream filename;
                filename << cown << ".cown";
                std::fstream ofs(cown_dir / filename.str(), std::ios::out | std::ios::binary);
                
                cown->serialize(ofs);

                ofs.close();
            };
            
            return swap_lambda;
        }

        static auto get_fetch_lambda(Cown *cown)
        {
            return [cown]()
            {
                Logging::cout() << "Fetching cown " << cown << Logging::endl;
                auto cown_dir = get_cown_dir();
                std::stringstream filename;
                filename << cown << ".cown";
                std::fstream ifs(cown_dir / filename.str(), std::ios::in | std::ios::binary);

                cown->serialize(ifs);
                
                ifs.close();
            };
        }

        static bool register_cown(Cown *cown)
        {
            auto expected = SwapStatus::UNREGISTERED;
            return cown->swap_satus.compare_exchange_strong(expected, SwapStatus::IN_MEMORY, std::memory_order_acq_rel);
        }

        static bool set_swapped(Cown *cown)
        {
            auto expected = SwapStatus::IN_MEMORY;
            return cown->swap_satus.compare_exchange_strong(expected, SwapStatus::ON_DISK, std::memory_order_acq_rel);
        }

        static bool set_in_memory(Cown *cown)
        {
            auto expected = ON_DISK;
            if (cown->swap_satus.compare_exchange_strong(expected, SwapStatus::IN_MEMORY, std::memory_order_acquire))
            {
                Logging::cout() << "Scheduling fetch for cown " << cown << Logging::endl;
                return true;
            }

            return false;
        }


        static void free_cowns(std::deque<Cown*>& cowns)
        {
            while (!cowns.empty())
            {
                auto cown = cowns.front();
                cown->queue_collect(ThreadAlloc::get());
                cowns.pop_front();
            }
        }

        static bool free_cown(Cown *cown)
        {
            if (cown->swap_satus.load(std::memory_order_relaxed) == SwapStatus::FREED)
            {
                cown->queue_collect(ThreadAlloc::get());
                return true;
            }

            return false;
        }

        static void set_fetch_behaviour(Cown *cown, BehaviourCore *fetch_behaviour, void (*fetch_deallocator)(BehaviourCore *))
        {
            cown->fetch_behaviour = fetch_behaviour;
            cown->fetch_deallocator = fetch_deallocator;
        }

    };

} // namespace verona::rt
