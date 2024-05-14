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
public:
  char *data;
  size_t id;
  size_t data_size;
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
    std::deque<cown_ptr<Body*>> bodies;
    const size_t size;

    Store(size_t size) : size(size)
    {
      for (size_t i = 0; i < size; ++i)
      {
        size_t data_size = 1000000;
        char *data = new char[data_size]();
        bodies.push_back(make_cown<Body *>(new Body(i, data_size, data)));
        CownMemoryThread::register_cowns(1, &bodies.back());
        yield();
      }
      
    }
};

static size_t limit = 1000000;
static std::atomic_bool limit_reached{false};
static std::atomic_bool stop{false};

void access_cowns(verona::cpp::cown_ptr<Store>& store_cown)
{
  when(store_cown) << [&](auto s)
  {
    auto& store = s.get_ref();
    if (store.bodies.empty())
    {
      if (limit_reached.load(std::memory_order_relaxed))
        stop.store(true, std::memory_order_acq_rel); 
      return;
    }

    size_t a = std::rand() % store.size;
    size_t b = std::rand() % store.size;
    when(store.bodies[a], store.bodies[b]) << [&](auto aa, auto bb)
    {
      auto body_a = aa.get_ref();
      auto body_b = bb.get_ref();

      size_t data_size = 10000000;
      char *data1 = new char[data_size];

      for (size_t i = 0; i < data_size; ++i)
      {
        data1[i] = (unsigned char) body_a->data[i] + body_b->data[i];
      }

      auto new_body1 = new Body(body_a->id + body_b->id, data_size, data1);

      when(store_cown) << [&](auto s)
      {
        Store& store2 = s.get_ref();
        if (store2.size > limit)
          limit_reached.store(true);

        store2.bodies.pop_front();
        if (limit_reached.load(std::memory_order_relaxed))
          return;

        store2.bodies.push_back(make_cown<Body*>(new_body1));
      };
    };
    
  };
}


void test_body(SystematicTestHarness *harness, verona::cpp::cown_ptr<Store>&& store_cown)
{
  harness->external_thread(CownMemoryThread::create_debug(300));
  size_t seed = harness->current_seed();
  Logging::cout() << "test_body()" << Logging::endl;

  when() << [&]()
  {
    while (!stop.load(std::memory_order_relaxed))
    {
      access_cowns(store_cown);
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  };
}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);
  auto store_cown = make_cown<Store>(10);

  harness.run(test_body, &harness, std::move(store_cown));

  CownSwapper::clear_cown_dir();

  return 0;
}
