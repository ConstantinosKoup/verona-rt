// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/when.h>
#include <stack>
#include <vector>

#include "cown_swapping/swapping_thread.h"
#include "cereal/archives/binary.hpp"
#include "cereal/types/stack.hpp"
#include "cereal/types/vector.hpp"
#include "cereal/types/string.hpp"

using namespace verona::rt;
using namespace verona::cpp;

struct Item
{
  std::string name;
  std::vector<char> descrition;
      
  template<class Archive>
  void serialize(Archive & archive)
  {
    archive( name, descrition ); 
  }
};

struct Category
{
  std::stack<Item> items;

  void add_item(std::string name, size_t descritption_size)
  {
    items.push({name, std::vector<char>(descritption_size, 'e')});
  }

  static Category *serialize(Category *category, std::iostream& stream)
  {
    if (category == nullptr)
    {
      cereal::BinaryInputArchive archive(stream);
      category = new Category();

      archive( category->items );

      return category;
    }
      
    cereal::BinaryOutputArchive archive(stream);
    archive( category->items );

    return nullptr;
  }
};

using Categories = std::unordered_map<size_t, cown_ptr<Category *>>;

void test_body()
{
  Categories categories;
  for (size_t i = 0; i < 10; ++i)
  {
    categories[i] = make_cown<Category*>(new Category());
    CownMemoryThread::register_cowns(1, &categories[i]);
  }


  for (size_t i = 0; i < 1000000; ++i)
    when(categories[i % (categories.size())]) << [](auto c)
    {
      auto cat = c.get_ref();

      cat->add_item("foo", 100000);

    };

}

int main()
{
  Scheduler& sched = Scheduler::get();
  sched.init(10);

  CownMemoryThread::create(300);

  test_body();

  sched.run();

  CownMemoryThread::stop_monitoring();
  CownMemoryThread::join();

  return 0;
}
