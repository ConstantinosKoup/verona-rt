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
long double ACCESS_STANDARD_DEVIATION;
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
  MEMORY_TARGET_MB = opt.is<size_t>("--MEMORY_TARGET_MB", 6000);
  
  COWN_NUMBER = opt.is<size_t>("--COWN_NUMBER", 50000);
  COWN_DATA_SIZE = opt.is<size_t>("--COWN_DATA_SIZE", 200000);
  
  COWNS_PER_BEHAVIOUR = opt.is<size_t>("--COWNS_PER_BEHAVIOUR", 1);

  THREAD_NUMBER = opt.is<size_t>("--THREAD_NUMBER", 8);
  BEHAVIOUR_RUNTIME_MS = opt.is<size_t>("--BEHAVIOUR_RUNTIME_MS", 1);

  size_t throughput = opt.is<size_t>("--THROUGHPUT", 5000);
  INTER_ARRIVAL_MICROSECS = 1000000 / throughput;
  
  INTER_ARRIVAL_STANDARD_DEVIATION = (long double) INTER_ARRIVAL_MICROSECS / opt.is<size_t>("--INTER_ARRIVAL_SD_DIVISOR", 10);

  ACCESS_STANDARD_DEVIATION = (long double) COWN_NUMBER / opt.is<double>("--ACCESS_SD_DIVISOR", 6);

  MONITOR_SLEEP_MICROSECS = opt.is<size_t>("--MONITOR_SLEEP_MICROSECS", 100);
  TOTAL_BEHAVIOURS = opt.is<size_t>("--TOTAL_BEHAVIOURS", 100000);
  WRITE_TO_FILE = !opt.has("--DONT_SAVE");


  std::cout << "Starting run with "
          << "SWAP_ALGO: " << (uninstrumented ? "Uninstrumented" : CownMemoryThread::algo_to_string(SWAP_ALGO)) << ", "
          << "COWN_NUMBER: " << COWN_NUMBER << ", "
          << "COWN_DATA_SIZE: " << COWN_DATA_SIZE << ", "
          << "COWNS_PER_BEHAVIOUR: " << COWNS_PER_BEHAVIOUR << ", "
          << "BEHAVIOUR_RUNTIME_MS: " << BEHAVIOUR_RUNTIME_MS << ", "
          << "MEMORY_TARGET_MB: " << MEMORY_TARGET_MB << ", "
          << "MONITOR_SLEEP_MICROSECS: " << MONITOR_SLEEP_MICROSECS << ", "
          << "ACCESS_STANDARD_DEVIATION: " << ACCESS_STANDARD_DEVIATION << ", "
          << "THREAD_NUMBER: " << THREAD_NUMBER << ", "
          << "TOTAL_BEHAVIOURS: " << TOTAL_BEHAVIOURS << ", "
          << "INTER_ARRIVAL_MICROSECS: " << INTER_ARRIVAL_MICROSECS << ", "
          << "INTER_ARRIVAL_STANDARD_DEVIATION: " << INTER_ARRIVAL_STANDARD_DEVIATION << std::endl;
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
    std::normal_distribution<double> distribution(COWN_NUMBER / 2, ACCESS_STANDARD_DEVIATION);

    int index;
    do {
        index = static_cast<int>(distribution(generator));
    } while (index < 0 || index >= COWN_NUMBER);  // Ensure index is within bounds

    return index;
}


void behaviour_spawning_thread(cown_ptr<Body*> *bodies,
                                std::vector<uint64_t>& latencies,
                                uint64_t& memory_usage_average,
                                std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>>& global_start,
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
  
  for (size_t i = 0; i < TOTAL_BEHAVIOURS; ++i)
  {
    using namespace std::chrono;
    auto loop_start = high_resolution_clock::now();
    cown_ptr<Body *> carray[COWNS_PER_BEHAVIOUR];
    for (size_t j = 0; j < COWNS_PER_BEHAVIOUR; ++j)
      carray[j] = bodies[get_normal_index()];

    cown_array<Body *> ca{carray, COWNS_PER_BEHAVIOUR};

    auto spawn_time = std::chrono::high_resolution_clock::now();
    when(ca) << [spawn_time, &latencies, &global_start, i, &global_end](auto s)
    {
      auto start_time = high_resolution_clock::now();
      auto expected = high_resolution_clock::time_point::min();
      global_start.compare_exchange_strong(expected, start_time);

      volatile size_t dummy;
      while (duration_cast<milliseconds>(high_resolution_clock::now() - start_time).count() < BEHAVIOUR_RUNTIME_MS)
      { ++dummy; }

      auto end_time = high_resolution_clock::now();
      latencies[i] = duration_cast<microseconds>(end_time - spawn_time).count();
      global_end.store(end_time, std::memory_order_acq_rel);
    };

    static std::default_random_engine generator(static_cast<long unsigned int>(time(0)));
    std::normal_distribution<double> distribution(INTER_ARRIVAL_MICROSECS / 10, INTER_ARRIVAL_MICROSECS / 30);
    
    volatile size_t duration = (INTER_ARRIVAL_MICROSECS - INTER_ARRIVAL_MICROSECS / 10) + distribution(generator);
    while (duration_cast<microseconds>(high_resolution_clock::now() - loop_start).count() < duration)
    { }
  }
  std::cout << "Finished spawning behaviours" << std::endl;

  when() << [&memory_usage_average]()
  {
    Scheduler::remove_external_event_source();
    if (!uninstrumented)
      memory_usage_average = CownMemoryThread::stop_monitoring();
  };
}

void test_body(std::vector<uint64_t>& latencies,
                uint64_t& memory_usage_average,
                std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>>& global_start,
                std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>>& global_end)
{
  cown_ptr<Body*> *bodies = new cown_ptr<Body*>[COWN_NUMBER];

  Scheduler& sched = Scheduler::get();
  sched.init(THREAD_NUMBER);

  if (!uninstrumented)
    CownMemoryThread::create(MEMORY_TARGET_MB, MONITOR_SLEEP_MICROSECS, SWAP_ALGO);

  init_bodies(bodies);

  std::thread bs(behaviour_spawning_thread, bodies, std::ref(latencies), std::ref(memory_usage_average), std::ref(global_start), std::ref(global_end));
  sched.run();
  bs.join();

  delete[] bodies;
}

void print_results(long double total_runtime, uint64_t memory_usage_average, long double latency_99th_perc, long double throughput)
{
    struct group_thousands : std::numpunct<char>
  { std::string do_grouping() const override { return "\3"; } };
  std::cout.imbue(std::locale(std::cout.getloc(), new group_thousands));

  std::cout << "Benchmark runtime: " << std::fixed << std::setprecision(3)
            << total_runtime << " miliseconds" << std::endl;
  std::cout << "Average memory usage: " << memory_usage_average  << " MB" << std::endl;
  std::cout << "Latency 99th percentile: "
              << latency_99th_perc  << " μs" << std::endl;
  std::cout << "Throughput: " << std::fixed << std::setprecision(3)
              << throughput << " behaviours per second" << std::endl;
}

void write_to_file(long double total_runtime, uint64_t memory_usage_average, long double latency_99th_perc, long double throughput)
{
  if (WRITE_TO_FILE)
  {
    if (!std::filesystem::exists("results.csv"))
    {
      std::ofstream csv("results.csv");
      std::stringstream csv_out;
      csv_out << "Swap Algo" << ','
              << "Cown Number" << ','
              << "Cown Data Size" << ','
              << "Cowns Per Behaviour" << ','
              << "Behaviour Runtime ms" << ','
              << "Memory Target MB" << ','
              << "Monitor Sleep μs" << ','
              << "Access Standard Deviation" << ','
              << "Thread Number" << ','
              << "Total Behaviours" << ','
              << "Inter Arrival μs" << ','
              << "Inter Arrival Standard Deviation" << ','
              << "Total Runtime" << ','
              << "Average Memory Usage MB" << ','
              << "Latency 99th Percentile" << ','
              << "Throughput" << ',' << std::endl;

      csv << csv_out.str();
      csv.close();
    }

    std::ofstream csv("results.csv", std::ios::app);
    std::stringstream csv_out;
    if (uninstrumented)
      csv_out << "Uninstrumented" << ','
              << COWN_NUMBER << ','
              << COWN_DATA_SIZE << ','
              << COWNS_PER_BEHAVIOUR << ','
              << BEHAVIOUR_RUNTIME_MS << ','
              << "N/A" << ','
              << "N/A" << ','
              << ACCESS_STANDARD_DEVIATION << ','
              << THREAD_NUMBER << ','
              << TOTAL_BEHAVIOURS << ','
              << INTER_ARRIVAL_MICROSECS << ','
              << INTER_ARRIVAL_STANDARD_DEVIATION << ','
              << total_runtime << ','
              << memory_usage_average << ','
              << latency_99th_perc << ','
              << throughput << ',' << std::endl;
    else
      csv_out << CownMemoryThread::algo_to_string(SWAP_ALGO) << ','
              << COWN_NUMBER << ','
              << COWN_DATA_SIZE << ','
              << COWNS_PER_BEHAVIOUR << ','
              << BEHAVIOUR_RUNTIME_MS << ','
              << MEMORY_TARGET_MB << ','
              << MONITOR_SLEEP_MICROSECS << ','
              << ACCESS_STANDARD_DEVIATION << ','
              << THREAD_NUMBER << ','
              << TOTAL_BEHAVIOURS << ','
              << INTER_ARRIVAL_MICROSECS << ','
              << INTER_ARRIVAL_STANDARD_DEVIATION << ','
              << total_runtime << ','
              << memory_usage_average << ','
              << latency_99th_perc << ','
              << throughput << ',' << std::endl;

    csv << csv_out.str();
    csv.close();
  }
}

int main(int argc, char *argv[])
{
  using namespace std::chrono;
  std::srand(std::chrono::system_clock::now().time_since_epoch().count());
  std::atomic<high_resolution_clock::time_point> global_start{high_resolution_clock::time_point::min()};
  std::atomic<high_resolution_clock::time_point> global_end;
  uint64_t memory_usage_average;
  read_input(argc, argv);

  std::vector<uint64_t> latencies(TOTAL_BEHAVIOURS);

  test_body(latencies, memory_usage_average, global_start, global_end);


  uint64_t total_runtime = 
    std::chrono::duration_cast<std::chrono::milliseconds>(global_end.load() - global_start.load()).count();
  long double throughput = (long double) (TOTAL_BEHAVIOURS * 1000) / total_runtime;

  std::sort(latencies.begin(), latencies.end());
  uint64_t latency_99th_perc = latencies[TOTAL_BEHAVIOURS * 99 / 100];

  print_results(total_runtime, memory_usage_average, latency_99th_perc, throughput);

  write_to_file(total_runtime, memory_usage_average, latency_99th_perc, throughput);

  return 0;
}
