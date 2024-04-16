// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT
#pragma once

#include "cown.h"
#include "cown_array.h"

#include <verona.h>

namespace verona::cpp
{
  using namespace verona::rt;
  /**
   * Used to track the type of access request by embedding const into
   * the type T, or not having const.
   */
  template<typename T>
  class Access
  {
    using Type = T;
    ActualCown<std::remove_const_t<T>>* t;
    bool is_move;

  public:
    Access(const cown_ptr<T>& c) : t(c.allocated_cown), is_move(false)
    {
      assert(c.allocated_cown != nullptr);
    }

    Access(cown_ptr<T>&& c) : t(c.allocated_cown), is_move(true)
    {
      assert(c.allocated_cown != nullptr);
      c.allocated_cown = nullptr;
    }

    template<typename F, typename... Args>
    friend class When;
    friend class CownSwapper;
  };

  /**
   * Used to track the type of access request in the case of cown_array
   * Ownership is handled the same for all cown_ptr in the span.
   * If is_move is true, all cown_ptrs will be moved.
   */
  template<typename T>
  class AccessBatch
  {
    using Type = T;
    ActualCown<std::remove_const_t<T>>** act_array;
    acquired_cown<T>* acq_array;
    size_t arr_len;
    bool is_move;

    void constr_helper(const cown_array<T>& ptr_span)
    {
      // Allocate the actual_cown and the acquired_cown array
      // The acquired_cown array is after the actual_cown one
      size_t act_size =
        ptr_span.length * sizeof(ActualCown<std::remove_const_t<T>>*);
      size_t acq_size =
        ptr_span.length * sizeof(acquired_cown<std::remove_const_t<T>>);
      act_array = reinterpret_cast<ActualCown<std::remove_const_t<T>>**>(
        snmalloc::ThreadAlloc::get().alloc(act_size + acq_size));

      for (size_t i = 0; i < ptr_span.length; i++)
      {
        act_array[i] = ptr_span.array[i].allocated_cown;
      }
      arr_len = ptr_span.length;

      acq_array =
        reinterpret_cast<acquired_cown<T>*>((char*)(act_array) + act_size);

      for (size_t i = 0; i < ptr_span.length; i++)
      {
        new (&acq_array[i]) acquired_cown<T>(*ptr_span.array[i].allocated_cown);
      }
    }

  public:
    AccessBatch(const cown_array<T>& ptr_span) : is_move(false)
    {
      constr_helper(ptr_span);
    }

    AccessBatch(cown_array<T>&& ptr_span) : is_move(true)
    {
      constr_helper(ptr_span);

      ptr_span.length = 0;
      ptr_span.arary = nullptr;
    }

    AccessBatch(AccessBatch&& old)
    {
      act_array = old.act_array;
      acq_array = old.acq_array;
      arr_len = old.arr_len;
      is_move = old.is_move;

      old.acq_array = nullptr;
      old.act_array = nullptr;
      old.arr_len = 0;
    }

    ~AccessBatch()
    {
      if (act_array)
      {
        snmalloc::ThreadAlloc::get().dealloc(act_array);
      }
    }

    AccessBatch& operator=(AccessBatch&&) = delete;
    AccessBatch(const AccessBatch&) = delete;
    AccessBatch& operator=(const AccessBatch&) = delete;

    template<typename F, typename... Args>
    friend class When;
    friend class CownSwapper;
  };

   /**
   * Template deduction guide for Access.
   */
  template<typename T>
  Access(const cown_ptr<T>&) -> Access<T>;

   /**
   * Template deduction guide for Access.
   */
  template<typename T>
  AccessBatch(const cown_array<T>&) -> AccessBatch<T>;

} // namespace verona::cpp
