// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/when.h>
#include <atomic>
#include <stack>
#include <vector>
#include <random>
#include <test/opt.h>
#include <sstream>
#include <algorithm>

#include "cown_swapping/swapping_thread.h"
#include "zipfian_distribution.h"

using namespace verona::rt;
using namespace verona::cpp;

CownMemoryThread::SwappingAlgo SWAP_ALGO{CownMemoryThread::SwappingAlgo::LRU};
bool uninstrumented{false};
size_t COWN_NUMBER;
size_t COWN_DATA_SIZE;
size_t COWNS_PER_BEHAVIOUR;
size_t BEHAVIOUR_RUNTIME_MICROSECONDS;
size_t MEMORY_TARGET_MB;
float ACCESS_STANDARD_DEVIATION;
size_t MULTIPLIER;
size_t THREAD_NUMBER;
size_t TOTAL_BEHAVIOURS;
size_t TOTAL_TIME_SECS;
size_t INTER_ARRIVAL_NANOSECONDS;
size_t INTER_ARRIVAL_STANDARD_DEVIATION;
bool WRITE_TO_FILE;
std::atomic_char32_t behaviours_ran{0};
char32_t final_behaviours_ran{0};
uint64_t num_fetches{0};
uint64_t num_cowns_fetched{0};

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
  MEMORY_TARGET_MB = opt.is<size_t>("--MEMORY_TARGET_MB", 7000);
  
  COWN_NUMBER = opt.is<size_t>("--COWN_NUMBER", 50000);
  COWN_DATA_SIZE = opt.is<size_t>("--COWN_DATA_SIZE", 200000);
  
  COWNS_PER_BEHAVIOUR = opt.is<size_t>("--COWNS_PER_BEHAVIOUR", 1);

  THREAD_NUMBER = opt.is<size_t>("--THREAD_NUMBER", 8);
  BEHAVIOUR_RUNTIME_MICROSECONDS = opt.is<size_t>("--BEHAVIOUR_RUNTIME_MICROSECONDS", 80);

  size_t throughput = opt.is<size_t>("--THROUGHPUT", 150000);
  INTER_ARRIVAL_NANOSECONDS = 1000000000 / throughput;
  
  INTER_ARRIVAL_STANDARD_DEVIATION = 0;

  // ACCESS_STANDARD_DEVIATION = (long double) COWN_NUMBER / opt.is<double>("--ACCESS_SD_DIVISOR", 6);
  ACCESS_STANDARD_DEVIATION = (float) opt.is<int>("--ACCESS_SD", 90) / 100;

  MULTIPLIER = opt.is<size_t>("--MULTIPLIER", 10000);
  TOTAL_BEHAVIOURS = opt.is<size_t>("--TOTAL_BEHAVIOURS", 1000000);
  TOTAL_TIME_SECS = opt.is<size_t>("--TOTAL_TIME_SECS", 60);
  WRITE_TO_FILE = !opt.has("--DONT_SAVE");


  std::cout << "Starting run with "
          << "SWAP_ALGO: " << (uninstrumented ? "Uninstrumented" : CownMemoryThread::algo_to_string(SWAP_ALGO)) << ", "
          << "COWN_NUMBER: " << COWN_NUMBER << ", "
          << "COWN_DATA_SIZE: " << COWN_DATA_SIZE << ", "
          << "COWNS_PER_BEHAVIOUR: " << COWNS_PER_BEHAVIOUR << ", "
          << "BEHAVIOUR_RUNTIME_MICROSECONDS: " << BEHAVIOUR_RUNTIME_MICROSECONDS << ", "
          << "MEMORY_TARGET_MB: " << MEMORY_TARGET_MB << ", "
          << "MULTIPLIER: " << MULTIPLIER << ", "
          << "ACCESS_STANDARD_DEVIATION: " << ACCESS_STANDARD_DEVIATION << ", "
          << "THREAD_NUMBER: " << THREAD_NUMBER << ", "
          << "TOTAL_BEHAVIOURS: " << TOTAL_BEHAVIOURS << ", "
          << "INTER_ARRIVAL_NANOSECONDS: " << INTER_ARRIVAL_NANOSECONDS << ", "
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

  Body(size_t id, size_t data_size) : Body(id, data_size, new char[data_size])
  {
    std::memset(this->data, 0, data_size);
  }

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

      assert(data_size == COWN_DATA_SIZE);

      char *data = new char[data_size];
      archive.read(data, data_size);
      return new Body(id, data_size, data);
    }

    archive.write((char*)&body->id, sizeof(body->id));
    archive.write((char*)&body->data_size, sizeof(body->data_size));
    archive.write(body->data, body->data_size);
    
    delete body;
    
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

size_t get_normal_index(std::vector<size_t>& indices) {
    static std::mt19937 generator(static_cast<long unsigned int>(time(0)));
    auto zipf = zipf_distribution<size_t, double>(COWN_NUMBER, ACCESS_STANDARD_DEVIATION);
    size_t index = zipf(generator) - 1;
    
    return indices[index];
}

void init_bodies(cown_ptr<Body*> *bodies, std::vector<size_t>& indices)
{
  std::iota(indices.begin(), indices.end(), 0);
  std::shuffle(indices.begin(), indices.end(), std::mt19937{std::random_device{}()});

  for (size_t i = 0; i < COWN_NUMBER; ++i)
    bodies[i] = make_cown<Body*>(new Body(i, COWN_DATA_SIZE));

  for (size_t i = 0; i < TOTAL_BEHAVIOURS / 100; ++i)
    ActualCownSwapper::debug_access_cown(bodies[get_normal_index(indices)]);

  if (!uninstrumented)
    CownMemoryThread::register_cowns(COWN_NUMBER, bodies);

}

void behaviour_spawning_thread(cown_ptr<Body*> *bodies,
                                std::vector<uint64_t>& latencies,
                                uint64_t& memory_usage_average,
                                std::chrono::time_point<std::chrono::high_resolution_clock>& global_start,
                                std::chrono::time_point<std::chrono::high_resolution_clock>& global_end,
                                std::vector<size_t>& indices)
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

  static std::default_random_engine generator(static_cast<long unsigned int>(time(0)));
  std::normal_distribution<double> distribution(INTER_ARRIVAL_NANOSECONDS / 10, INTER_ARRIVAL_STANDARD_DEVIATION);

  // Fill up thread queues
  for (size_t i = 0; i < THREAD_NUMBER * 2; ++i)
  {
    using namespace std::chrono;
    auto loop_start = high_resolution_clock::now();

    cown_ptr<Body *> carray[COWNS_PER_BEHAVIOUR];
    for (size_t j = 0; j < COWNS_PER_BEHAVIOUR; ++j)
      carray[j] = bodies[get_normal_index(indices)];

    cown_array<Body *> ca{carray, COWNS_PER_BEHAVIOUR};

    when(ca) << [](auto s)
    {
      auto start_time = high_resolution_clock::now();

      volatile size_t dummy;
      while (duration_cast<microseconds>(high_resolution_clock::now() - start_time).count() < BEHAVIOUR_RUNTIME_MICROSECONDS)
      { ++dummy; }
    };

    volatile size_t duration = (INTER_ARRIVAL_NANOSECONDS - INTER_ARRIVAL_NANOSECONDS / 10) + distribution(generator);
    while (duration_cast<nanoseconds>(high_resolution_clock::now() - loop_start).count() < duration)
    { }
  }
  
  global_start = std::chrono::high_resolution_clock::now();
  for (size_t i = 0; i < TOTAL_BEHAVIOURS ; ++i)
  {
    using namespace std::chrono;
    auto loop_start = high_resolution_clock::now();

    cown_ptr<Body *> carray[COWNS_PER_BEHAVIOUR];
    for (size_t j = 0; j < COWNS_PER_BEHAVIOUR; ++j)
      carray[j] = bodies[get_normal_index(indices)];

    cown_array<Body *> ca{carray, COWNS_PER_BEHAVIOUR};

    auto spawn_time = std::chrono::high_resolution_clock::now();
    when(ca) << [spawn_time, &latencies, i](auto s)
    {
      auto start_time = high_resolution_clock::now();
      behaviours_ran.fetch_add(1, std::memory_order_acq_rel);

      volatile size_t dummy;
      while (duration_cast<microseconds>(high_resolution_clock::now() - start_time).count() < BEHAVIOUR_RUNTIME_MICROSECONDS)
      { ++dummy; }
      
      auto end_time = high_resolution_clock::now();
      latencies[i] = duration_cast<microseconds>(end_time - spawn_time).count();
    };


    if (duration_cast<seconds>(high_resolution_clock::now() - global_start).count() > TOTAL_TIME_SECS)
    {
      break;
    }
    
    volatile size_t duration = (INTER_ARRIVAL_NANOSECONDS - INTER_ARRIVAL_NANOSECONDS / 10) + distribution(generator);
    while (duration_cast<nanoseconds>(high_resolution_clock::now() - loop_start).count() < duration)
    { }
  }
  global_end = std::chrono::high_resolution_clock::now();
  final_behaviours_ran = behaviours_ran.load(std::memory_order_acquire);
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
                std::chrono::high_resolution_clock::time_point& global_start,
                std::chrono::high_resolution_clock::time_point& global_end)
{
  cown_ptr<Body*> *bodies = new cown_ptr<Body*>[COWN_NUMBER];

  Scheduler& sched = Scheduler::get();
  sched.init(THREAD_NUMBER);

  if (!uninstrumented)
    CownMemoryThread::create(MEMORY_TARGET_MB, MULTIPLIER, SWAP_ALGO);
  
  std::vector<size_t> indices(COWN_NUMBER);

  init_bodies(bodies, indices);

  std::thread bs(behaviour_spawning_thread, bodies, std::ref(latencies), std::ref(memory_usage_average), 
                std::ref(global_start), std::ref(global_end), std::ref(indices));
  sched.run();
  bs.join();

  std::ofstream cs("cowns_accesses.txt");
  for (size_t i = 0; i < COWN_NUMBER; ++i)
  {
    cs << ActualCownSwapper::debug_get_accesses_cown(bodies[i]) << std::endl;
    auto nf = ActualCownSwapper::debug_get_fetches_cown(bodies[i]);

    if (nf > 0)
    {
      ++num_cowns_fetched;
      num_fetches += nf;
    }
  }
  cs.close();

  delete[] bodies;
}

void print_results(long double total_runtime, uint64_t memory_usage_average, std::vector<uint64_t>& latencies, uint64_t throughput)
{
    struct group_thousands : std::numpunct<char>
  { std::string do_grouping() const override { return "\3"; } };
  std::cout.imbue(std::locale(std::cout.getloc(), new group_thousands));

  std::cout << "Benchmark runtime: " << std::fixed << std::setprecision(3)
            << total_runtime << " miliseconds" << std::endl;
  std::cout << "Average memory usage: " << memory_usage_average  << " MB" << std::endl;
  std::cout << "Latency 90th percentile: "
            << latencies[final_behaviours_ran * 10 / 100]  << " μs" << std::endl;
  std::cout << "Latency 95th percentile: "
              << latencies[final_behaviours_ran * 50 / 100]  << " μs" << std::endl;
  std::cout << "Latency 99th percentile: "
              << latencies[final_behaviours_ran * 15 / 100]  << " μs" << std::endl;
  std::cout << "Throughput: " << throughput << " behaviours per second" << std::endl;
}

void write_to_file(long double total_runtime, uint64_t memory_usage_average, std::vector<uint64_t>& latencies, uint64_t throughput)
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
              << "Multiplier" << ','
              << "Access Standard Deviation" << ','
              << "Thread Number" << ','
              << "Behaviours ran" << ','
              << "Inter Arrival ns" << ','
              << "Inter Arrival Standard Deviation" << ','
              << "Total Runtime" << ','
              << "Average Memory Usage MB" << ','
              << "Latency 90th Percentile" << ','
              << "Latency 95th Percentile" << ','
              << "Latency 99th Percentile" << ','
              << "Total Fetches" << ','
              << "Fetches %" << ','
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
              << BEHAVIOUR_RUNTIME_MICROSECONDS << ','
              << "N/A" << ','
              << "N/A" << ','
              << ACCESS_STANDARD_DEVIATION << ','
              << THREAD_NUMBER << ','
              << final_behaviours_ran << ','
              << INTER_ARRIVAL_NANOSECONDS << ','
              << INTER_ARRIVAL_STANDARD_DEVIATION << ','
              << total_runtime << ','
              << memory_usage_average << ','
              << latencies[final_behaviours_ran * 10 / 100] << ','
              << latencies[final_behaviours_ran * 5 / 100] << ','
              << latencies[final_behaviours_ran * 1 / 100] << ','
              << "N/A" << ','
              << "N/A" << ','
              << throughput << ',' << std::endl;
    else
      csv_out << CownMemoryThread::algo_to_string(SWAP_ALGO) << ','
              << COWN_NUMBER << ','
              << COWN_DATA_SIZE << ','
              << COWNS_PER_BEHAVIOUR << ','
              << BEHAVIOUR_RUNTIME_MICROSECONDS << ','
              << MEMORY_TARGET_MB << ','
              << MULTIPLIER << ','
              << ACCESS_STANDARD_DEVIATION << ','
              << THREAD_NUMBER << ','
              << final_behaviours_ran << ','
              << INTER_ARRIVAL_NANOSECONDS << ','
              << INTER_ARRIVAL_STANDARD_DEVIATION << ','
              << total_runtime << ','
              << memory_usage_average << ','
              << latencies[final_behaviours_ran * 10 / 100] << ','
              << latencies[final_behaviours_ran * 5 / 100] << ','
              << latencies[final_behaviours_ran * 1 / 100] << ','
              << num_fetches << ','
              << (long double) num_cowns_fetched / COWN_NUMBER << ','
              << throughput << ',' << std::endl;

    csv << csv_out.str();
    csv.close();
  }
}

int main(int argc, char *argv[])
{
  using namespace std::chrono;
  std::srand(std::chrono::system_clock::now().time_since_epoch().count());
  high_resolution_clock::time_point global_start{high_resolution_clock::time_point::min()};
  high_resolution_clock::time_point global_end;
  uint64_t memory_usage_average;
  read_input(argc, argv);

  std::vector<uint64_t> latencies(TOTAL_BEHAVIOURS);

  test_body(latencies, memory_usage_average, global_start, global_end);

  uint64_t total_runtime =
    std::chrono::duration_cast<std::chrono::microseconds>(global_end - global_start).count();
  uint64_t throughput = ((uint64_t) final_behaviours_ran) / (total_runtime / 1000000);

  std::sort(latencies.begin(), latencies.end(), std::greater<uint64_t>());

  print_results(total_runtime, memory_usage_average, latencies, throughput);

  write_to_file(total_runtime, memory_usage_average, latencies, throughput);

  return 0;
}
