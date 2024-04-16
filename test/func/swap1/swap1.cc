// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/cown_swapper.h>
#include <cpp/when.h>
#include <fstream>
#include <debug/harness.h>

class Body
{
private:
  std::string message;
public:
  Body(std::string message) : message{message} {}

  const char *get_message() {
    return message.c_str();
  }

  static void save(std::ofstream& file, Body *body) {
    file << body->message.c_str();
  }

  static Body *load(std::ifstream& file) {
    std::streampos size = file.tellg();
    char *memblock = new char [size];
    file.seekg (0, std::ios::beg);
    file.read (memblock, size);

    std::string msg{memblock, static_cast<size_t>(size)};
    delete[] memblock;

    return new Body(msg);
  }

  ~Body()
  {
    Logging::cout() << "Body destroyed" << Logging::endl;
  }
};

using namespace verona::cpp;

void test_body()
{
  Logging::cout() << "test_body()" << Logging::endl;

  auto log = make_cown<Body*>(new Body("Cown Message"));
  ActualCownSwapper::schedule_swap(log);

  when(log) << [=](auto b) { Logging::cout() << "Printing message: " << b.get_ref()->get_message() << Logging::endl; };
}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);

  harness.run(test_body);

  return 0;
}
