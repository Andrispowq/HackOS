#include "support.hpp"
extern "C" uint64_t ExecuteSyscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" void syscall_test_target();
TEST(syscall_bridge_alignment_and_five_arguments)
{
    CHECK(ExecuteSyscall(11, 22, 33, 44, 55, (uint64_t)syscall_test_target) == 123);
    return 0;
}
