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
        size_t data_size = 100000000;
        char *data = new char[data_size]();
        bodies[i] = make_cown<Body*>(new Body(i, data_size, data));
        CownMemoryThread::register_cown(bodies[i]);
        yield();
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

  auto store_cown = make_cown<Store>(50);
  std::srand(seed);

  when(store_cown) << [=](auto s) 
  {
    
    Store& store = s.get_ref();
    for (int i = 0; i < store.size / 5; ++i)
    {
      yield();
      size_t j = std::rand() % store.size;
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
      when(store.bodies[j]) << [=](auto b)
      {
        auto body = b.get_ref();
#ifdef USE_SYSTEMATIC_TESTING
        yield();
        Logging::cout() << "Reading id: " << body->get_id() << Logging::endl;
#else
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        std::cout << "Reading id: " << body->get_id() << std::endl;
#endif
      };
    }
  };


}

int main(int argc, char** argv)
{

  SystematicTestHarness harness(argc, argv);

  harness.external_thread(CownMemoryThread::create_debug(3000));
  harness.run(test_body, &harness);

  return 0;
}
