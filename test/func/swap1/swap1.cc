// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/cown_swapper.h>
#include <cpp/when.h>
#include <debug/harness.h>

class Body
{
private:
  std::string message = "Cown Message";
public:
  const char *get_message() {
    return message.c_str();
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

  auto log = make_cown<Body*>(new Body());
  CownSwapper::swap_to_disk(log);

  when(log) << [=](auto b) { Logging::cout() << b.get_ref()->get_message() << Logging::endl; };
}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);

  harness.run(test_body);

  return 0;
}
