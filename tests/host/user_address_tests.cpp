#include "support.hpp"
#include "proc/user/address_space.h"

extern PageTableManager KernelDirectory;
bool user_nx_enabled = true;
static PageFrameAllocator frame_allocator;
static int requests, fail_request = -1, live_frames;
static void* frames[160];
static void* raw_frames[160];
PageFrameAllocator* PageFrameAllocator::SharedAllocator() { return &frame_allocator; }
void* PageFrameAllocator::TryRequestPage()
{
    if (requests++ == fail_request) return nullptr;
    for (unsigned i = 0; i < 160; ++i) if (!frames[i])
    {
        raw_frames[i] = test_alloc(8192);
        frames[i] = (void*)(((uint64_t)raw_frames[i] + 4095) & ~4095ULL);
        ++live_frames;
        return frames[i];
    }
    test_abort("test frame pool exhausted"); return nullptr;
}
void PageFrameAllocator::FreePages(void* p, uint64_t count)
{
    if (count != 1) test_abort("unexpected free count");
    for (unsigned i = 0; i < 160; ++i) if (frames[i] == p)
    {
        test_free(raw_frames[i]); frames[i] = nullptr; --live_frames; return;
    }
    test_abort("freeing unowned/already freed frame");
}
static void setup_user_space()
{
    static PageTable kernel;
    test_fill(&kernel, 0, sizeof(kernel));
    kernel.entries[0].present = kernel.entries[0].writeable = 1;
    kernel.entries[0].address = 0x123;
    KernelDirectory = PageTableManager(&kernel);
    requests = 0; fail_request = -1;
}
TEST(user_address_private_pages_and_permissions)
{
    setup_user_space();
    {
        UserAddressSpace a, b;
        CHECK(a.Initialise() && b.Initialise());
        CHECK(a.directory.GetPML4() != b.directory.GetPML4());
        auto first = (char*)a.AddPage(USER_BASE, false, true);
        auto second = (char*)b.AddPage(USER_BASE, false, true);
        CHECK(first && second && first != second && !first[0] && !second[0]);
        first[0] = 'a'; second[0] = 'b';
        char output = 0;
        CHECK(a.CopyFromUser(&output, USER_BASE, 1) && output == 'a');
        CHECK(b.CopyFromUser(&output, USER_BASE, 1) && output == 'b');
        CHECK(!a.directory.GetPML4()->entries[0].user_access);
        CHECK(KernelDirectory.GetPML4()->entries[0].address == 0x123);
        CHECK(!a.AddPage(USER_BASE, true, false)); // no replacement/leak
        CHECK(!a.AddPage(USER_BASE + 4096, true, true));
        CHECK(!a.AddPage(USER_BASE - 4096, true, false));
        CHECK(!a.AddPage(USER_END, true, false));
        CHECK(!a.AddPage(USER_BASE + 1, true, false));
    }
    CHECK(live_frames == 0 && user_pages_live == 0);
    return 0;
}
TEST(user_copy_validates_whole_range)
{
    setup_user_space();
    {
        UserAddressSpace space; CHECK(space.Initialise());
        auto first = (char*)space.AddPage(USER_BASE, false, true);
        auto second = (char*)space.AddPage(USER_BASE + 4096, true, false);
        CHECK(first && second);
        first[4095] = 'a'; second[0] = 'b';
        char output[4] = {'!', '!', '!', '!'};
        CHECK(space.CopyFromUser(output, USER_BASE + 4095, 2));
        CHECK(output[0] == 'a' && output[1] == 'b' && output[2] == '!');
        output[0] = '!';
        CHECK(!space.CopyFromUser(output, USER_BASE + 8191, 2));
        CHECK(output[0] == '!'); // no partial copy before discovering a hole
        CHECK(!space.CopyFromUser(output, 0x100000, 1));
        CHECK(!space.CopyFromUser(output, USER_END - 1, 2));
        CHECK(!space.CopyFromUser(output, ~0ULL - 1, 4));
        CHECK(!space.CopyFromUser(output, USER_BASE, ~0ULL));
        CHECK(space.CopyFromUser(output, 0, 0));
    }
    CHECK(live_frames == 0 && user_pages_live == 0);
    return 0;
}
TEST(user_address_failure_and_page_limit_cleanup)
{
    for (int fail = 0; fail < 4; ++fail)
    {
        setup_user_space(); fail_request = fail;
        { UserAddressSpace space; CHECK(!space.Initialise()); }
        CHECK(live_frames == 0 && user_pages_live == 0);
    }
    setup_user_space();
    {
        UserAddressSpace space; CHECK(space.Initialise());
        fail_request = requests;
        CHECK(!space.AddPage(USER_BASE, true, false));
        fail_request = -1;
        for (uint64_t i = 0; i < USER_MAX_PAGES; ++i)
            CHECK(space.AddPage(USER_BASE + i * 4096, true, false));
        CHECK(!space.AddPage(USER_BASE + USER_MAX_PAGES * 4096, true, false));
    }
    CHECK(live_frames == 0 && user_pages_live == 0);
    return 0;
}
