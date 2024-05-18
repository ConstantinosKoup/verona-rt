// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/when.h>
#include <stack>
#include <vector>
#include <random>

#include "cown_swapping/swapping_thread.h"

using namespace verona::rt;
using namespace verona::cpp;

constexpr size_t COWN_NUMBER = 10000;
constexpr size_t COWN_DATA_SIZE = 1000000;
constexpr size_t COWNS_PER_BEHAVIOUR = 1;
constexpr size_t BEHAVIOUR_RUNTIME_MS = 1;
constexpr size_t LOAD = 1;
constexpr size_t MAX_WEIGHT = 20;
constexpr size_t MEMORY_LIMIT_MB = 1000;
constexpr size_t THREAD_NUMBER = 10;
constexpr size_t TOTAL_BEHAVIOURS = 1000;
constexpr size_t INTER_ARRIVAL_MS = 10;

class Body
{
private:
  char *data;
  size_t id;
  size_t data_size;
public:
  Body(size_t id, size_t data_size, char *data) : id(id), data_size(data_size), data(data) {}

  Body(size_t id, size_t data_size) : Body(id, data_size, new char[data_size]()) {}

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
      archive.read(data, data_size);
      return new Body(id, data_size, data);
    }

    archive.write((char*)&body->id, sizeof(body->id));
    archive.write((char*)&body->data_size, sizeof(body->data_size));
    archive.write(body->data, body->data_size);
    return nullptr;
  }

  ~Body()
  {
    delete[] data;
  }
};

void init_bodies(cown_ptr<Body*> *bodies, std::array<size_t, COWN_NUMBER>& probabilities)
{
  for (size_t i = 0; i < COWN_NUMBER; ++i)
  {
    bodies[i] = make_cown<Body*>(new Body(i, COWN_DATA_SIZE));
    probabilities[i] = std::rand() % MAX_WEIGHT == MAX_WEIGHT - 1 ? MAX_WEIGHT : 1;
    // probabilities[i] = std::rand() % MAX_WEIGHT;
  }

  CownMemoryThread::register_cowns(COWN_NUMBER, bodies);
}

void behaviour_spawning_thread(
                                cown_ptr<Body*> *bodies,
                                std::discrete_distribution<> d,
                                std::atomic_int64_t& latency,
                                std::chrono::time_point<std::chrono::high_resolution_clock>& global_start,
                                std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>>& global_end)
{
  std::random_device rd;
  std::mt19937 gen(rd());
  std::mutex m;

  when() << []()
  {
    Scheduler::add_external_event_source();
  };

  CownMemoryThread::wait(std::unique_lock(m));

  std::cout << "Memory limit reached. Benchmark starting" << std::endl;
  global_start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < TOTAL_BEHAVIOURS; ++i)
  {
    auto start = std::chrono::high_resolution_clock::now();
    when(bodies[d(gen)]) << [&](auto b)
    {
      std::this_thread::sleep_for(std::chrono::milliseconds(BEHAVIOUR_RUNTIME_MS));
      auto end = std::chrono::high_resolution_clock::now();

      latency.fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
      global_end.store(end, std::memory_order_acq_rel);
    };

    std::this_thread::sleep_for(std::chrono::milliseconds(INTER_ARRIVAL_MS));
  }

  when() << []()
  {
    Scheduler::remove_external_event_source();
  };
}

int main()
{
  std::srand(32);
  std::atomic_int64_t latency{0};
  std::chrono::time_point<std::chrono::high_resolution_clock> global_start;
  std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>> global_end;

  cown_ptr<Body*> *bodies = new cown_ptr<Body*>[COWN_NUMBER];
  std::array<size_t, COWN_NUMBER> probabilities;

  Scheduler& sched = Scheduler::get();
  sched.init(THREAD_NUMBER);

  CownMemoryThread::create(MEMORY_LIMIT_MB);
  init_bodies(bodies, probabilities);

  std::discrete_distribution<> d(probabilities.begin(), probabilities.end());
  std::thread bs(behaviour_spawning_thread, bodies, d, std::ref(latency), std::ref(global_start), std::ref(global_end));

  sched.run();

  bs.join();
  CownMemoryThread::stop_monitoring();
  delete[] bodies;

  struct group_thousands : std::numpunct<char> 
  { std::string do_grouping() const override { return "\3"; } };
  std::cout.imbue(std::locale(std::cout.getloc(), new group_thousands));

  long double total_runtime = 
    std::chrono::duration_cast<std::chrono::seconds>(global_end.load() - global_start).count();
  long double throughput = (long double) TOTAL_BEHAVIOURS / total_runtime;
  long double average_latency_s = (long double) latency.load() / TOTAL_BEHAVIOURS / std::pow(10, 6);

  std::cout << "Average latency: " << std::fixed << std::setprecision(3)
              << average_latency_s  << " milliseconds" << std::endl;
  std::cout << "Throughput: " << std::fixed << std::setprecision(3)
              << throughput << " behaviours per second" << std::endl;


  return 0;
}
