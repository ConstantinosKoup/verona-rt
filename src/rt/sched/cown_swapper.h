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
    using cown_pair = std::pair<Cown *, size_t>;

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
        /// @param count Number of cowns being swapped.
        /// @param cowns Array of cowns to be swapped.
        /// @param to_be_swapped Atomic variable indicating the number of swap behaviours that can concurrently exist.
        /// @return The function used by the swap behaviour.
        static auto get_swap_lambda(size_t count, Cown** cowns, std::atomic_uint64_t& to_be_swapped)
        {
            auto swap_lambda = [=, &to_be_swapped]()
            {
                auto cown_dir = get_cown_dir();
                for (size_t i = 0; i < count; ++i)
                {
                    std::stringstream filename;
                    filename << cowns[i] << ".cown";
                    std::fstream ofs(cown_dir / filename.str(), std::ios::out | std::ios::binary);

                    cowns[i]->serialize(ofs);

                    ofs.flush();
                    ofs.close();
                }

                to_be_swapped.fetch_sub(1);
                auto& alloc = ThreadAlloc::get();
                alloc.dealloc(cowns);
            };
            
            return swap_lambda;
        }

        /// @param cown Cown to be fetched.
        /// @param register_to_thread The callback function to inform the swapping thread that the cown is back in
        /// memory.
        /// @return The function used by the fetch behaviour. 
        static auto get_fetch_lambda(cown_pair cown, std::function<void(cown_pair)> register_to_thread)
        {
            return [cown, register_to_thread]()
            {
                auto cown_dir = get_cown_dir();
                std::stringstream filename;
                filename << cown.first << ".cown";
                std::fstream ifs(cown_dir / filename.str(), std::ios::in | std::ios::binary);

                cown.first->serialize(ifs);
                
                ifs.close();

                register_to_thread(cown);
            };
        }

        /// @brief Perform a weak acquire on the cown to prevent it from being freed while the swapping thread holds it.
        static void register_cown(Cown *cown)
        {
            cown->weak_acquire();
        }

        /// @brief Perform weak release on a cown to allow it to be deallocated. 
        static void unregister_cown(Cown *cown)
        {
            cown->weak_release(ThreadAlloc::get());
        }

        static bool set_swapped(Cown *cown)
        {
            if (!cown->swapped)
            {
                cown->swapped = true;
                return true;
            }

            return false;
        }
 
        static bool was_accessed(Cown *cown)
        {
            auto prev = cown->num_accesses.exchange(0);

            return prev > 0;
        }

        static bool set_in_memory(Cown *cown)
        {
            ++cown->num_accesses;
            cown->last_access = std::chrono::steady_clock::now();
            if (cown->swapped)
            {
                ++cown->num_fetches;
                cown->swapped = false;
                return true;
            }

            return false;
        }

        static uint64_t get_num_accesses(Cown *cown)
        {
            return cown->num_accesses;
        }

        static std::chrono::steady_clock::time_point get_last_access(Cown *cown)
        {
            return cown->last_access;
        }

        static uint64_t debug_get_fetches(Cown *cown)
        {
            return cown->num_fetches;
        }

        static bool acquire_strong(Cown *cown)
        {
            return cown->acquire_strong_from_weak();
        }

        /// @brief Set the fetch behaviour and it's deallocator as function pointers in the cown so they can be called
        /// from within the scheduler and the release method of shared.
        static void set_fetch_behaviour(Cown *cown, BehaviourCore *fetch_behaviour, void (*fetch_deallocator)(BehaviourCore *))
        {
            cown->fetch_behaviour = fetch_behaviour;
            cown->fetch_deallocator = fetch_deallocator;
        }

    };

} // namespace verona::rt
