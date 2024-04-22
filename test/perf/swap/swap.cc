// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/cown_swapper.h>
#include <cpp/when.h>
#include <fstream>
#include <debug/harness.h>
#include <cpp/cown.h>

#include "cown_swapping/swapping_thread.h"

using namespace verona::cpp;

class Body
{
private:
  std::string message;
public:
  Body(std::string message) : message{message} {}

  const char *get_message() const
  {
    return message.c_str();
  }

  static Body *serialize(Body* body, std::iostream& archive)
  {
    if (body == nullptr)
    {
      std::string data((std::istreambuf_iterator<char>(archive)), std::istreambuf_iterator<char>());
      return new Body(data);
    }

    size_t archive_size = body->message.size() + 1;
    archive.write(body->get_message(), archive_size);
    return nullptr;
  }

  ~Body()
  {
    Logging::cout() << "Body destroyed" << Logging::endl;
  }
};

class Store
{
  public:
    cown_ptr<Body*> *bodies;
    const size_t size;

    Store(size_t size) : size(size)
    {
      bodies = new cown_ptr<Body*>[size];
      for (size_t i = 0; i < size; ++i)
      {
        std::stringstream ss;
        ss << "Message for cown: " << i;
        bodies[i] = make_cown<Body*>(new Body(ss.str()));
      }
    }

    ~Store()
    {
      delete[] bodies;
    }
    
};


void test_body(SystematicTestHarness *harness)
{
  size_t seed = harness->current_seed();
  Logging::cout() << "test_body()" << Logging::endl;

  auto store_cown = make_cown<Store>(1000000);
  std::srand(seed);

  when(store_cown) << [=](auto s) 
  { 
    Store& store = s.get_ref();
    for (int i = 0; i < 2 * store.size; ++i)
    {
      when(store.bodies[std::rand() / store.size]) << [=](auto b)
      {
        auto body = b.get_ref();
        Logging::cout() << "Reading message: " << body->get_message() << Logging::endl;
      };
    }
  };

}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);

  CownMemoryThread::get_ref();

  harness.run(test_body, &harness);

  CownMemoryThread::stop_monitoring();

  return 0;
}
