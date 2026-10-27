#include "support.hpp"
TestCase test_cases[256];
unsigned test_count = 0;

int main(int argc, char** argv)
{
    heap_init = 1; // Route kmalloc through the explicitly substituted host allocator.
    if (argc == 2 && test_name_equal(argv[1], "--list"))
    {
        for (unsigned i = 0; i < test_count; ++i)
        {
            printf("%s\n", test_cases[i].name);
        }

        return 0;
    }

    for (unsigned i = 0; argc == 2 && i < test_count; ++i)
    {
        if (test_name_equal(argv[1], test_cases[i].name))
        {
            return test_cases[i].run();
        }
    }

    printf("usage: host-tests --list | CASE_NAME\n");
    return 2;
}
