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
        static void clear_cown_dir()
        {
            namespace fs = std::filesystem;
            auto cown_dir = get_cown_dir();

            fs::remove_all(cown_dir);
        }
        static bool is_in_memory(Cown *cown)
        {
            return !cown->swapped;
        }

        static auto get_swap_lambda(Cown* cown)
        {
            auto swap_lambda = [cown]()
            {
                auto cown_dir = get_cown_dir();
                std::stringstream filename;
                filename << cown << ".cown";
                std::fstream ofs(cown_dir / filename.str(), std::ios::out | std::ios::binary);

                cown->serialize(ofs);

                ofs.close();
            };
            
            return swap_lambda;
        }

        static auto get_fetch_lambda(Cown *cown, std::function<void(Cown *)> register_to_thread)
        {
            return [cown, register_to_thread]()
            {
                auto cown_dir = get_cown_dir();
                std::stringstream filename;
                filename << cown << ".cown";
                std::fstream ifs(cown_dir / filename.str(), std::ios::in | std::ios::binary);

                cown->serialize(ifs);
                
                ifs.close();

                register_cown(cown);
                register_to_thread(cown);
            };
        }

        static void register_cown(Cown *cown)
        {
            cown->weak_acquire();
        }

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

        static bool num_accesses_comparator(Cown *a, Cown *b)
        {
            return a->num_accesses <= b->num_accesses;
        }

        
        static bool last_access_comparator(Cown *a, Cown *b)
        {
            return a->last_access <= b->last_access;
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
                cown->swapped = false;
                return true;
            }

            return false;
        }

        static bool acquire_strong(Cown *cown)
        {
            auto succeeded = cown->acquire_strong_from_weak();
            if (!succeeded)
                unregister_cown(cown);

            return succeeded;
        }

        static void release_strong(Cown *cown)
        {
            Shared::release(ThreadAlloc::get(), cown);
        }

        static void set_fetch_behaviour(Cown *cown, BehaviourCore *fetch_behaviour, void (*fetch_deallocator)(BehaviourCore *))
        {
            cown->fetch_behaviour = fetch_behaviour;
            cown->fetch_deallocator = fetch_deallocator;
        }

    };

} // namespace verona::rt
