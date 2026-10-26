#include "support.hpp"
#include "hackos/syscall_numbers.h"

// Compatibility with existing ELF programs, independent of catalogue order.
static_assert(HACKOS_SYS_exit == 0 && HACKOS_SYS_kprintf == 1 &&
              HACKOS_SYS_kprintf_backspace == 2 && HACKOS_SYS_fopen == 3 &&
              HACKOS_SYS_fread == 4 && HACKOS_SYS_fwrite == 5 &&
              HACKOS_SYS_fclose == 6 && HACKOS_SYS_kmalloc_int == 7 &&
              HACKOS_SYS_kfree == 8 && HACKOS_SYS_kread == 9 &&
              HACKOS_SYS_create_window == 10 && HACKOS_SYS_destroy_window == 11 &&
              HACKOS_SYS_get_window_buffer == 12 && HACKOS_SYS_set_window_title == 13 &&
              HACKOS_SYS_user_write == 14 && HACKOS_SYSCALL_COUNT == 15, "Existing syscall ABI changed");
extern "C" uint64_t ExecuteSyscall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
extern "C" void syscall_test_target();
TEST(syscall_bridge_alignment_and_five_arguments)
{
    CHECK(ExecuteSyscall(11, 22, 33, 44, 55, (uint64_t)syscall_test_target) == 123);
    return 0;
}
