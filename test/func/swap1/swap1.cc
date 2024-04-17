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

  const char *get_message() const
  {
    return message.c_str();
  }

  static Body *serialize(Body* body, std::iostream& archive)
  {
    if (body == nullptr)
    {
      std::string data((std::istreambuf_iterator<char>(archive)), std::istreambuf_iterator<char>());
      return new Body(data);
    }

    size_t archive_size = body->message.size() + 1;
    archive.write(body->get_message(), archive_size);
    return nullptr;
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
