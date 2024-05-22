// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/when.h>
#include <stack>
#include <vector>
#include <random>

#include "cown_swapping/swapping_thread.h"

using namespace verona::rt;
using namespace verona::cpp;

const std::string BENCHMARK_NAME = "random";
constexpr size_t COWN_NUMBER = 10000;
constexpr size_t COWN_DATA_SIZE = 1000000;
constexpr size_t COWNS_PER_BEHAVIOUR = 2;
constexpr size_t BEHAVIOUR_RUNTIME_MS = 5;
constexpr size_t MEMORY_LIMIT_MB = 5000;
constexpr double STANDARD_DEVIATION = COWN_NUMBER / 6.0;
constexpr size_t THREAD_NUMBER = 16;
constexpr size_t TOTAL_BEHAVIOURS = 100000;
constexpr size_t INTER_ARRIVAL_MICROSECS = 500;
constexpr bool WRITE_TO_FILE = true;

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

void init_bodies(cown_ptr<Body*> *bodies)
{
  for (size_t i = 0; i < COWN_NUMBER; ++i)
    bodies[i] = make_cown<Body*>(new Body(i, COWN_DATA_SIZE));

  CownMemoryThread::register_cowns(COWN_NUMBER, bodies);
}

size_t get_normal_index() {
    static std::default_random_engine generator(static_cast<long unsigned int>(time(0)));
    std::normal_distribution<double> distribution(COWN_NUMBER / 2, STANDARD_DEVIATION);

    int index;
    do {
        index = static_cast<int>(distribution(generator));
    } while (index < 0 || index >= COWN_NUMBER);  // Ensure index is within bounds

    return index;
}


void behaviour_spawning_thread(cown_ptr<Body*> *bodies,
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
    auto spawn_time = std::chrono::high_resolution_clock::now();
      
    cown_ptr<Body *> carray[COWNS_PER_BEHAVIOUR];
    for (size_t j = 0; j < COWNS_PER_BEHAVIOUR; ++j)
      carray[j] = bodies[get_normal_index()];

    cown_array<Body *> ca{carray, COWNS_PER_BEHAVIOUR};

    when(ca) << [&](...)
    {

      std::this_thread::sleep_for(std::chrono::milliseconds(BEHAVIOUR_RUNTIME_MS));

      auto end = std::chrono::high_resolution_clock::now();
      latency.fetch_add(std::chrono::duration_cast<std::chrono::microseconds>(end - spawn_time).count());
      global_end.store(end, std::memory_order_acq_rel);
    };

    std::this_thread::sleep_for(std::chrono::microseconds(INTER_ARRIVAL_MICROSECS));
  }

  when() << []()
  {
    Scheduler::remove_external_event_source();
    CownMemoryThread::stop_monitoring();
  };
}

int main()
{
  std::srand(std::chrono::system_clock::now().time_since_epoch().count());
  std::atomic_int64_t latency{0};
  std::chrono::time_point<std::chrono::high_resolution_clock> global_start;
  std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>> global_end;

  cown_ptr<Body*> *bodies = new cown_ptr<Body*>[COWN_NUMBER];

  Scheduler& sched = Scheduler::get();
  sched.init(THREAD_NUMBER);

  CownMemoryThread::create(MEMORY_LIMIT_MB);
  init_bodies(bodies);

  std::thread bs(behaviour_spawning_thread, bodies, std::ref(latency), std::ref(global_start), std::ref(global_end));
  sched.run();
  bs.join();

  struct group_thousands : std::numpunct<char> 
  { std::string do_grouping() const override { return "\3"; } };
  std::cout.imbue(std::locale(std::cout.getloc(), new group_thousands));

  long double total_runtime = 
    std::chrono::duration_cast<std::chrono::seconds>(global_end.load() - global_start).count();
  long double throughput = (long double) TOTAL_BEHAVIOURS / total_runtime;
  long double average_latency_s = (long double) latency.load() / TOTAL_BEHAVIOURS;

  if constexpr (WRITE_TO_FILE)
  {
    if (!std::filesystem::exists("results.csv"))
    {
      std::ofstream csv("results.csv");
      std::stringstream csv_out;
      csv_out << "BENCHMARK_NAME" << ','
              << "COWN_NUMBER" << ','
              << "COWN_DATA_SIZE" << ','
              << "COWNS_PER_BEHAVIOUR" << ','
              << "BEHAVIOUR_RUNTIME_MS" << ','
              << "MEMORY_LIMIT_MB" << ','
              << "STANDARD_DEVIATION" << ','
              << "THREAD_NUMBER" << ','
              << "TOTAL_BEHAVIOURS" << ','
              << "INTER_ARRIVAL_MICROSECS" << ','
              << "total_runtime" << ','
              << "average_latency_s" << ','
              << "throughput" << ',' << std::endl;

      csv << csv_out.str();
      csv.close();
    }

    std::ofstream csv("results.csv", std::ios::app);
    std::stringstream csv_out;
    csv_out << BENCHMARK_NAME << ','
            << COWN_NUMBER << ','
            << COWN_DATA_SIZE << ','
            << COWNS_PER_BEHAVIOUR << ','
            << BEHAVIOUR_RUNTIME_MS << ','
            << MEMORY_LIMIT_MB << ','
            << STANDARD_DEVIATION << ','
            << THREAD_NUMBER << ','
            << TOTAL_BEHAVIOURS << ','
            << INTER_ARRIVAL_MICROSECS << ','
            << total_runtime << ','
            << average_latency_s << ','
            << throughput << ',' << std::endl;

    csv << csv_out.str();
    csv.close();
  }

  std::cout << "Benchmark runtime: " << std::fixed << std::setprecision(3)
            << total_runtime << " seconds" << std::endl;
  std::cout << "Average latency: " << std::fixed << std::setprecision(3)
              << average_latency_s  << " microseconds" << std::endl;
  std::cout << "Throughput: " << std::fixed << std::setprecision(3)
              << throughput << " behaviours per second" << std::endl;
  delete[] bodies;

  return 0;
}
