// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/when.h>
#include <fstream>
#include <debug/harness.h>
#include <cpp/cown.h>

#include "cown_swapping/swapping_thread.h"

using namespace verona::cpp;

class Body
{
private:
  char *data;
  size_t id;
  size_t data_size;
public:
  Body(size_t id, size_t data_size, char *data) : id(id), data_size(data_size), data(data) {}

  const size_t get_id() const
  {
    return id;
  }

  static Body *serialize(Body* body, std::iostream& archive)
  {
    if (body == nullptr)
    {
      size_t id;
      size_t data_size;
      archive.read((char *)&id, sizeof(id));
      archive.read((char *)&data_size, sizeof(data_size));

      char *data = new char[data_size];
      yield();
      archive.read(data, data_size);
      yield();
      return new Body(id, data_size, data);
    }

    archive.write((char*)&body->id, sizeof(body->id));
    archive.write((char*)&body->data_size, sizeof(body->data_size));
    yield();
    archive.write(body->data, body->data_size);
    yield();
    return nullptr;
  }

  ~Body()
  {
    delete data;
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
        size_t data_size = 30000000;
        char *data = new char[data_size]();
        bodies[i] = make_cown<Body*>(new Body(i, data_size, data));
        yield();
      }

      
      CownMemoryThread::register_cowns(size, bodies);
    }

    ~Store()
    {
      delete[] bodies;
    }
    
};


void test_body(SystematicTestHarness *harness)
{
  harness->external_thread(CownMemoryThread::create_debug(300));
  size_t seed = harness->current_seed();
  Logging::cout() << "test_body()" << Logging::endl;

  auto store_cown = make_cown<Store>(50);
  std::srand(seed);

  when(store_cown) << [=](auto s) 
  {
    
    Store& store = s.get_ref();
    for (int i = 0; i < 100000 * store.size; ++i)
    {
      yield();
      size_t j = std::rand() % (store.size / 5);
      when(store.bodies[j]) << [=](auto b)
      {
        auto body = b.get_ref();
        yield();
        Logging::cout() << "Reading id: " << body->get_id() << Logging::endl;
      };
    }
  };


}

int main(int argc, char** argv)
{

  SystematicTestHarness harness(argc, argv);

  harness.run(test_body, &harness);

  return 0;
}
