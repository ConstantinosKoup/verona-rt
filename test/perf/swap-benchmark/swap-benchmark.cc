// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/when.h>
#include <stack>

#include "cown_swapping/swapping_thread.h"

using namespace verona::rt;
using namespace verona::cpp;

struct Item
{
  Item(std::string name, size_t descrition_size = 1000) : name{name}, descrition{new char[descrition_size]}
  {}

  ~Item()
  {
    delete[] descrition;
  }


  const std::string name;
  const char *descrition;
};

struct Category
{
  cown_ptr<std::stack<Item> items;
};

using Categories = std::unordered_map<std::string, Category>;

int main()
{
  return 0;
}
