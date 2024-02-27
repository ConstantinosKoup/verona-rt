// Copyright Microsoft and Project Verona Contributors.
// SPDX-License-Identifier: MIT

#include <cpp/when.h>
#include <debug/harness.h>

class Body
{
private:
  size_t _num;
  std::string message = "log";
public:
  Body(int num) {
    _num = num;
  }

  const char *get_message() {
    return message.append(std::to_string(_num)).c_str();
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

  auto log1 = make_cown<Body>(1);
  auto log2 = make_cown<Body>(2);
  auto log3 = make_cown<Body>(3);
  auto log4 = make_cown<Body>(4);

  log3.debug_write_to_disk();
  log4.debug_write_to_disk();

  when(log1, log2) <<
    [=](auto b1, auto b2) { Logging::cout() << b1.get_ref().get_message() << ", " << b2.get_ref().get_message() << Logging::endl; };
  when(log3, log4) <<
    [=](auto b3, auto b4) { Logging::cout() << b3.get_ref().get_message() << ", " << b4.get_ref().get_message() << Logging::endl; };
  when(log2, log3) <<
    [=](auto b2, auto b3) { Logging::cout() << b2.get_ref().get_message() << ", " << b3.get_ref().get_message() << Logging::endl; };
  when(log4, log1) <<
    [=](auto b4, auto b1) { Logging::cout() << b4.get_ref().get_message() << ", " << b1.get_ref().get_message() << Logging::endl; };
}

int main(int argc, char** argv)
{
  SystematicTestHarness harness(argc, argv);

  harness.run(test_body);

  return 0;
}
