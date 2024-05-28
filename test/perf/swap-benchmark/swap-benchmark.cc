// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/when.h>
#include <stack>
#include <vector>
#include <random>
#include <test/opt.h>

#include "cown_swapping/swapping_thread.h"

using namespace verona::rt;
using namespace verona::cpp;

CownMemoryThread::SwappingAlgo SWAP_ALGO{CownMemoryThread::SwappingAlgo::LRU};
bool uninstrumented{false};
size_t COWN_NUMBER;
size_t COWN_DATA_SIZE;
size_t COWNS_PER_BEHAVIOUR;
size_t BEHAVIOUR_RUNTIME_MS;
size_t MEMORY_TARGET_MB;
long double STANDARD_DEVIATION;
size_t MONITOR_SLEEP_MICROSECS;
size_t THREAD_NUMBER;
size_t TOTAL_BEHAVIOURS;
size_t INTER_ARRIVAL_MICROSECS;
size_t INTER_ARRIVAL_STANDARD_DEVIATION;
bool WRITE_TO_FILE;

void read_swap_algo(opt::Opt& opt)
{
  size_t swap_count = 0;
  if (opt.has("--LFU"))
  {
    SWAP_ALGO = CownMemoryThread::SwappingAlgo::LFU;
    ++swap_count;
  }
  if (opt.has("--LRU"))
  {
    SWAP_ALGO = CownMemoryThread::SwappingAlgo::LRU;
    ++swap_count;
  }
  if (opt.has("--Random"))
  {
    SWAP_ALGO = CownMemoryThread::SwappingAlgo::RANDOM;
    ++swap_count;
  }
  if (opt.has("--RoundRobin"))
  {
    SWAP_ALGO = CownMemoryThread::SwappingAlgo::ROUND_ROBIN;
    ++swap_count;
  }
  if (opt.has("--SecondChance"))
  {
    SWAP_ALGO = CownMemoryThread::SwappingAlgo::SECOND_CHANCE;
    ++swap_count;
  }
  if (opt.has("--Uninstrumented"))
  {
    uninstrumented = true;
    ++swap_count;
  }

  if (swap_count > 1)
    error("More than one swapping algorithm selected");
}

void read_input(int argc, char *argv[])
{
  opt::Opt opt(argc, argv);

  read_swap_algo(opt);
  COWN_NUMBER = opt.is<size_t>("--COWN_NUMBER", 50000);
  COWN_DATA_SIZE = opt.is<size_t>("--COWN_DATA_SIZE", 200000);
  COWNS_PER_BEHAVIOUR = opt.is<size_t>("--COWNS_PER_BEHAVIOUR", 2);
  BEHAVIOUR_RUNTIME_MS = opt.is<size_t>("--BEHAVIOUR_RUNTIME_MS", 3);
  MEMORY_TARGET_MB = opt.is<size_t>("--MEMORY_TARGET_MB", 5000);
  STANDARD_DEVIATION = opt.is<double>("--STANDARD_DEVIATION", (long double) COWN_NUMBER / 6.0);
  MONITOR_SLEEP_MICROSECS = opt.is<size_t>("--MONITOR_SLEEP_MICROSECS", 3000);
  THREAD_NUMBER = opt.is<size_t>("--THREAD_NUMBER", 12);
  TOTAL_BEHAVIOURS = opt.is<size_t>("--TOTAL_BEHAVIOURS", 100000);
  INTER_ARRIVAL_MICROSECS = opt.is<size_t>("--INTER_ARRIVAL_MICROSECS", 1500);
  INTER_ARRIVAL_STANDARD_DEVIATION = opt.is<size_t>("--INTER_ARRIVAL_STANDARD_DEVIATION", INTER_ARRIVAL_MICROSECS / 30);
  WRITE_TO_FILE = !opt.has("--DONT_SAVE");
}

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

  static size_t size(Body *body)
  {
    return sizeof(id) + sizeof(data_size) + body->data_size * sizeof(char);
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

  if (!uninstrumented)
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
                                uint64_t& memory_usage_average,
                                std::chrono::time_point<std::chrono::high_resolution_clock>& global_start,
                                std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>>& global_end)
{
  std::mutex m;

  when() << []()
  {
    Scheduler::add_external_event_source();
  };

  if (!uninstrumented)
    CownMemoryThread::wait(std::unique_lock(m));

  std::cout << "Memory limit reached. Benchmark starting" << std::endl;
  if (uninstrumented)
    memory_usage_average = CownMemoryThread::get_memory_usage_MB();
  else
    CownMemoryThread::start_keep_average();
  
  global_start = std::chrono::high_resolution_clock::now();

  for (size_t i = 0; i < TOTAL_BEHAVIOURS; ++i)
  {
    cown_ptr<Body *> carray[COWNS_PER_BEHAVIOUR];
    for (size_t j = 0; j < COWNS_PER_BEHAVIOUR; ++j)
      carray[j] = bodies[get_normal_index()];

    cown_array<Body *> ca{carray, COWNS_PER_BEHAVIOUR};

    auto spawn_time = std::chrono::high_resolution_clock::now();
    when(ca) << [spawn_time, &latency, &global_end](...)
    {
      using namespace std::chrono;
      auto start_time = high_resolution_clock::now();

      volatile size_t dummy;
      while (duration_cast<milliseconds>(high_resolution_clock::now() - start_time).count() < BEHAVIOUR_RUNTIME_MS)
      { ++dummy; }

      auto end_time = high_resolution_clock::now();
      latency.fetch_add(duration_cast<microseconds>(end_time - spawn_time).count());
      global_end.store(end_time, std::memory_order_acq_rel);
    };

    static std::default_random_engine generator(static_cast<long unsigned int>(time(0)));
    std::normal_distribution<double> distribution(INTER_ARRIVAL_MICROSECS / 10, INTER_ARRIVAL_MICROSECS / 30);
    volatile size_t duration = (INTER_ARRIVAL_MICROSECS - INTER_ARRIVAL_MICROSECS / 10) + distribution(generator);

    std::this_thread::sleep_for(std::chrono::microseconds(duration));
  }
  std::cout << "Finished spawning behaviours" << std::endl;

  when() << [&memory_usage_average]()
  {
    Scheduler::remove_external_event_source();
    if (!uninstrumented)
      memory_usage_average = CownMemoryThread::stop_monitoring();
  };
}

void test_body(std::atomic_int64_t& latency,
                uint64_t& memory_usage_average,
                std::chrono::time_point<std::chrono::high_resolution_clock>& global_start,
                std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>>& global_end)
{
  cown_ptr<Body*> *bodies = new cown_ptr<Body*>[COWN_NUMBER];

  Scheduler& sched = Scheduler::get();
  sched.init(THREAD_NUMBER);

  if (!uninstrumented)
    CownMemoryThread::create(MEMORY_TARGET_MB, MONITOR_SLEEP_MICROSECS, SWAP_ALGO);

  init_bodies(bodies);

  std::thread bs(behaviour_spawning_thread, bodies, std::ref(latency), std::ref(memory_usage_average), std::ref(global_start), std::ref(global_end));
  sched.run();
  bs.join();

  delete[] bodies;
}

void print_results(long double total_runtime, uint64_t memory_usage_average, long double average_latency_s, long double throughput)
{
    struct group_thousands : std::numpunct<char>
  { std::string do_grouping() const override { return "\3"; } };
  std::cout.imbue(std::locale(std::cout.getloc(), new group_thousands));

  std::cout << "Benchmark runtime: " << std::fixed << std::setprecision(3)
            << total_runtime << " seconds" << std::endl;
  std::cout << "Average memory usage: " << memory_usage_average  << " MB" << std::endl;
  std::cout << "Average latency: " << std::fixed << std::setprecision(3)
              << average_latency_s  << " microseconds" << std::endl;
  std::cout << "Throughput: " << std::fixed << std::setprecision(3)
              << throughput << " behaviours per second" << std::endl;
}

void write_to_file(long double total_runtime, uint64_t memory_usage_average, long double average_latency_s, long double throughput)
{
  if (WRITE_TO_FILE)
  {
    if (!std::filesystem::exists("results.csv"))
    {
      std::ofstream csv("results.csv");
      std::stringstream csv_out;
      csv_out << "SWAP_ALGO" << ','
              << "COWN_NUMBER" << ','
              << "COWN_DATA_SIZE" << ','
              << "COWNS_PER_BEHAVIOUR" << ','
              << "BEHAVIOUR_RUNTIME_MS" << ','
              << "MEMORY_TARGET_MB" << ','
              << "MEMORY_THREAD_INTERVAL" << ','
              << "STANDARD_DEVIATION" << ','
              << "THREAD_NUMBER" << ','
              << "TOTAL_BEHAVIOURS" << ','
              << "INTER_ARRIVAL_MICROSECS" << ','
              << "INTER_ARRIVAL_STANDARD_DEVIATION" << ','
              << "TOTAL_RUNTIME" << ','
              << "AVERAGE_MEMORY_USAGE_MB" << ','
              << "AVERAGE_LATENCY_S" << ','
              << "THROUGHPUT" << ',' << std::endl;

      csv << csv_out.str();
      csv.close();
    }

    std::ofstream csv("results.csv", std::ios::app);
    std::stringstream csv_out;
    csv_out << (uninstrumented ? "Uninstrumented" : CownMemoryThread::algo_to_string(SWAP_ALGO)) << ','
            << COWN_NUMBER << ','
            << COWN_DATA_SIZE << ','
            << COWNS_PER_BEHAVIOUR << ','
            << BEHAVIOUR_RUNTIME_MS << ','
            << MEMORY_TARGET_MB << ','
            << MONITOR_SLEEP_MICROSECS << ','
            << STANDARD_DEVIATION << ','
            << THREAD_NUMBER << ','
            << TOTAL_BEHAVIOURS << ','
            << INTER_ARRIVAL_MICROSECS << ','
            << INTER_ARRIVAL_STANDARD_DEVIATION << ','
            << total_runtime << ','
            << memory_usage_average << ','
            << average_latency_s << ','
            << throughput << ',' << std::endl;

    csv << csv_out.str();
    csv.close();
  }
}

int main(int argc, char *argv[])
{
  std::srand(std::chrono::system_clock::now().time_since_epoch().count());
  std::atomic_int64_t latency{0};
  std::chrono::time_point<std::chrono::high_resolution_clock> global_start;
  std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>> global_end;
  uint64_t memory_usage_average;

  read_input(argc, argv);

  test_body(latency, memory_usage_average, global_start, global_end);


  long double total_runtime = 
    std::chrono::duration_cast<std::chrono::seconds>(global_end.load() - global_start).count();
  long double throughput = (long double) TOTAL_BEHAVIOURS / total_runtime;
  long double average_latency_s = (long double) latency.load() / TOTAL_BEHAVIOURS;

  print_results(total_runtime, memory_usage_average, average_latency_s, throughput);

  write_to_file(total_runtime, memory_usage_average, average_latency_s, throughput);

  return 0;
}
