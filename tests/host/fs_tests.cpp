#include "fake_fs.hpp"
#include "fs/vfs.h"
#include "fs/fat32/fat32.h"

TEST(vfs_selects_volume_and_translates_path)
{
    FakeFS fs;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    CHECK(fopen("fs0:/FILE", O_READ) != nullptr);
    CHECK(test_name_equal(fs.path, "~/FILE"));
    CHECK(fs.file.fs_index == 0);
    return 0;
}

TEST(vfs_two_digit_index)
{
    FakeFS fs;
    InitialiseFilesystems();
    filesystems[12] = &fs;
    CHECK(fopen("fs12:/FILE", O_READ) != nullptr);
    CHECK(fs.file.fs_index == 12);
    return 0;
}

TEST(vfs_missing_file)
{
    FakeFS fs;
    fs.missing = true;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    CHECK(fopen("fs0:/MISSING", O_READ) == nullptr);
    return 0;
}

TEST(vfs_absent_volume)
{
    InitialiseFilesystems();
    CHECK(fopen("fs9:/FILE", O_READ) == nullptr);
    return 0;
}

TEST(vfs_rejects_out_of_range_volume)
{
    InitialiseFilesystems();
    CHECK(fopen("fs99:/FILE", O_READ) == nullptr);
    return 0;
}

TEST(vfs_create)
{
    FakeFS fs;
    fs.missing = true;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    CHECK(fopen("fs0:/NEW", O_CREAT | O_WRITE) != nullptr);
    CHECK(fs.created);
    CHECK(test_name_equal(fs.path, "~"));
    CHECK(test_name_equal(fs.name, "NEW"));
    return 0;
}

TEST(vfs_io_dispatch_and_close)
{
    FakeFS fs;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    auto file = fopen("fs0:/FILE", O_READ | O_WRITE);
    unsigned char b[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    CHECK(fwrite(b, 2, 4, file) == 0);
    CHECK(fs.transferred == 8);
    CHECK(test_equal(fs.contents, b, 8));
    test_fill(b, 0, 8);
    CHECK(fread(b, 2, 4, file) == 0);
    CHECK(b[0] == 1 && b[7] == 8);
    fclose(file);
    CHECK(fs.closed);
    return 0;
}

TEST(vfs_io_propagates_error)
{
    FakeFS fs;
    fs.io_result = -7;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    auto file = fopen("fs0:/FILE", O_READ);
    char b[2];
    CHECK(fread(b, 1, 2, file) == -7);
    return 0;
}

TEST(vfs_null_handle)
{
    char b[1];
    CHECK(fread(b, 1, 1, nullptr) < 0);
    return 0;
}

TEST(vfs_rejects_path_without_volume)
{
    InitialiseFilesystems();
    CHECK(fopen("FILE", O_READ) == nullptr);
    return 0;
}

TEST(vfs_invalid_handle_index)
{
    FakeFile file;
    file.fs_index = -1;
    char b[1];
    CHECK(fread(b, 1, 1, &file) < 0);
    CHECK(fwrite(b, 1, 1, &file) < 0);
    fclose(&file);
    return 0;
}

TEST(vfs_handle_for_unmounted_volume)
{
    InitialiseFilesystems();
    FakeFile file;
    file.fs_index = 5;
    char b[1];
    CHECK(fread(b, 1, 1, &file) < 0);
    CHECK(fwrite(b, 1, 1, &file) < 0);
    fclose(&file);
    return 0;
}

TEST(filesystem_registration_reuses_empty_slot)
{
    FakeFS a, b, c;
    InitialiseFilesystems();
    RegisterFilesystem(&a);
    RegisterFilesystem(&b);
    CHECK(filesystems[0] == &a && filesystems[1] == &b);
    UnregisterFilesystem(&a);
    CHECK(filesystems[0] == nullptr && filesystems[1] == &b);
    RegisterFilesystem(&c);
    CHECK(filesystems[0] == &c);
    return 0;
}

class MemoryDisk : public Device
{
  public:
    uint64_t SectorCount() const override
    {
        return size / 512;
    }

    unsigned char* bytes;
    size_t size;
    bool readError = false, writeError = false, flushError = false;
    unsigned reads = 0;
    unsigned writes = 0, failWrite = 0;
    unsigned flushes = 0, failFlush = 0;
    int cutSectors = -1;
    bool powerLost = false;

    MemoryDisk()
    {
        bytes = (unsigned char*)test_disk_load(&size);
    }

    ~MemoryDisk() override
    {
        test_free(bytes);
    }

    DeviceType GetType() const override
    {
        return DeviceType::ATA_PIO;
    }

    bool Read(uint64_t lba, void* p, uint64_t count) override
    {
        ++reads;
        if (readError || powerLost)
        {
            return false;
        }

        bounds(lba, count);
        test_copy(p, bytes + lba * 512, count * 512);
        return true;
    }

    bool Write(uint64_t lba, void* p, uint64_t count) override
    {
        ++writes;
        if (writeError || powerLost || (failWrite && writes == failWrite))
        {
            return false;
        }

        bounds(lba, count);
        for (uint64_t i = 0; i < count; ++i)
        {
            if (cutSectors == 0)
            {
                powerLost = true;
                return false;
            }

            test_copy(bytes + (lba + i) * 512, (uint8_t*)p + i * 512, 512);
            if (cutSectors > 0)
            {
                --cutSectors;
            }
        }
        return true;
    }

    bool Flush() override
    {
        ++flushes;
        return !flushError && !powerLost && (!failFlush || flushes != failFlush);
    }

    void bounds(uint64_t lba, uint64_t count)
    {
        if (lba > size / 512 || count > size / 512 - lba)
        {
            test_abort("FAT driver accessed outside disk");
        }
    }
};

TEST(fat_read_fixture)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
    CHECK(file != nullptr);
    CHECK(file->GetSize() == 13);
    char b[13];
    CHECK(fs.Read(file, b, 13) == 0);
    CHECK(test_equal(b, "Hello HackOS\n", 13));
    fs.CloseFile(file);
    return 0;
}

TEST(fat_read_across_clusters)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file != nullptr);
    CHECK(file->GetSize() == 8193);
    auto b = (unsigned char*)test_alloc(8193);
    CHECK(fs.Read(file, b, 8193) == 0);
    for (unsigned i = 0; i < 8193; ++i)
    {
        CHECK(b[i] == (unsigned char)(i * 17 + 3));
    }

    test_free(b);
    fs.CloseFile(file);
    return 0;
}

TEST(fat_missing_file)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    CHECK(fs.OpenFile("~/DOESNOT.EXI") == nullptr);
    return 0;
}

TEST(fat_directory_enumeration)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    CHECK(fs.GetDirectoryEntryCount("~/TESTS") >= 2);
    CHECK(fs.GetDirectoryEntryCount("~/TESTS/HELLO.TXT") == 0);
    CHECK(fs.GetFile("~/TESTS/HELLO.TXT", 0) == nullptr);
    return 0;
}

TEST(fat_rewrite_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
        CHECK(file != nullptr);
        CHECK(fs.Write(file, (void*)"Changed data\n", 13) == 0);
        fs.CloseFile(file);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_read_long_filename)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/Long file name.txt");
    CHECK(file != nullptr);
    CHECK(file->GetSize() == 10);
    char b[10];
    CHECK(fs.Read(file, b, 10) == 0);
    CHECK(test_equal(b, "Long name\n", 10));
    fs.CloseFile(file);
    return 0;
}

TEST(fat_zero_length_read)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/EMPTY.TXT");
    CHECK(file != nullptr);
    CHECK(file->GetSize() == 0);
    char b = 42;
    CHECK(fs.Read(file, &b, 0) == 0);
    CHECK(b == 42);
    CHECK(file->seek_position == 0);
    fs.CloseFile(file);
    return 0;
}

TEST(fat_seeked_read)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file != nullptr);
    file->seek_position = 511;
    unsigned char b[1025];
    CHECK(fs.Read(file, b, sizeof b) == 0);
    CHECK(file->seek_position == 1536);
    for (unsigned i = 0; i < sizeof b; ++i)
    {
        CHECK(b[i] == (unsigned char)((i + 511) * 17 + 3));
    }

    fs.CloseFile(file);
    return 0;
}

TEST(fat_create_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.CreateFile("~/TESTS", "NEW.TXT", ARCHIVE);
        CHECK(file != nullptr);
        fs.ResizeFile(file, 13);
        CHECK(fs.Write(file, (void*)"Created data\n", 13) == 0);
        fs.CloseFile(file);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_shrink_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
        CHECK(file != nullptr);
        fs.ResizeFile(file, 5);
        CHECK(file->GetSize() == 5);
        fs.CloseFile(file);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_delete_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
        CHECK(file != nullptr);
        fs.DeleteFile(file);
        CHECK(fs.OpenFile("~/TESTS/HELLO.TXT") == nullptr);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_rejects_zero_sectors_per_cluster)
{
    MemoryDisk disk;
    disk.bytes[13] = 0;
    FAT32 fs(&disk, 0);
    CHECK(fs.OpenFile("~/TESTS/HELLO.TXT") == nullptr);
    return 0;
}

TEST(fat_partial_write_preserves_neighbors)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file);
    file->seek_position = 511;
    unsigned char patch[3] = {21, 22, 23};
    CHECK(fs.Write(file, patch, 3) == 0);
    file->seek_position = 0;
    unsigned char bytes[8193];
    CHECK(fs.Read(file, bytes, sizeof bytes) == 0);
    for (unsigned i = 0; i < sizeof bytes; ++i)
    {
        CHECK(bytes[i] == (i >= 511 && i < 514 ? patch[i - 511] : (unsigned char)(i * 17 + 3)));
    }

    fs.CloseFile(file);
    return 0;
}

TEST(fat_grow_empty_and_zero_gap)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/EMPTY.TXT");
    CHECK(file);
    file->seek_position = 1025;
    CHECK(fs.Write(file, (void*)"OK", 2) == 0);
    CHECK(file->GetSize() == 1027);
    file->seek_position = 0;
    unsigned char bytes[1027];
    CHECK(fs.Read(file, bytes, sizeof bytes) == 0);
    for (unsigned i = 0; i < 1025; ++i)
    {
        CHECK(bytes[i] == 0);
    }

    CHECK(bytes[1025] == 'O' && bytes[1026] == 'K');
    fs.CloseFile(file);
    return 0;
}

TEST(fat_truncate_to_zero_and_regrow)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/PATTERN.BIN");
    CHECK(file);
    fs.ResizeFile(file, 0);
    CHECK(file->GetSize() == 0);
    fs.ResizeFile(file, 1025);
    CHECK(file->GetSize() == 1025);
    unsigned char bytes[1025];
    CHECK(fs.Read(file, bytes, sizeof bytes) == 0);
    for (unsigned i = 0; i < sizeof bytes; ++i)
    {
        CHECK(bytes[i] == 0);
    }

    fs.CloseFile(file);
    return 0;
}

TEST(fat_eof_read_clamps_position_and_buffer)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
    CHECK(file);
    file->seek_position = 12;
    char bytes[4] = {42, 42, 42, 42};
    CHECK(fs.Read(file, bytes, 4) == 0);
    CHECK(bytes[0] == '\n' && bytes[1] == 42 && file->seek_position == 13);
    file->seek_position = 100;
    CHECK(fs.Read(file, bytes, 4) == 0);
    CHECK(file->seek_position == 100);
    fs.CloseFile(file);
    return 0;
}

TEST(fat_long_name_create_and_flush)
{
    MemoryDisk disk;
    {
        FAT32 fs(&disk, 0);
        auto file = fs.CreateFile("~/TESTS", "Another long filename.txt", ARCHIVE);
        CHECK(file);
        CHECK(fs.Write(file, (void*)"Long create\n", 12) == 0);
        fs.CloseFile(file);
        file = fs.OpenFile("~/TESTS/another LONG filename.txt");
        CHECK(file);
        CHECK(file->GetSize() == 12);
        fs.CloseFile(file);
    }

    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_directory_extension_and_lfn_boundary)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    for (unsigned i = 0; i < 24; ++i)
    {
        char name[] = "Boundary filename 00.txt";
        name[18] = '0' + i / 10;
        name[19] = '0' + i % 10;
        auto file = fs.CreateFile("~/TESTS", name, ARCHIVE);
        CHECK(file);
        CHECK(fs.Write(file, (void*)"data", 4) == 0);
        fs.CloseFile(file);
    }

    for (unsigned i = 0; i < 24; ++i)
    {
        char path[] = "~/TESTS/Boundary filename 00.txt";
        path[26] = '0' + i / 10;
        path[27] = '0' + i % 10;
        auto file = fs.OpenFile(path);
        CHECK(file);
        char bytes[4];
        CHECK(fs.Read(file, bytes, 4) == 0);
        CHECK(test_equal(bytes, "data", 4));
        fs.CloseFile(file);
    }

    return 0;
}

TEST(fat_long_name_delete_keeps_neighbors)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/Long file name.txt");
    CHECK(file);
    fs.DeleteFile(file);
    CHECK(fs.OpenFile("~/TESTS/Long file name.txt") == nullptr);
    file = fs.OpenFile("~/TESTS/HELLO.TXT");
    CHECK(file);
    char bytes[13];
    CHECK(fs.Read(file, bytes, 13) == 0);
    CHECK(test_equal(bytes, "Hello HackOS\n", 13));
    fs.CloseFile(file);
    return 0;
}

TEST(fat_new_directory_dot_entries)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    auto dir = fs.CreateFile("~/TESTS", "SUBDIR", DIRECTORY);
    CHECK(dir);
    fs.CloseFile(dir);
    auto file = fs.CreateFile("~/TESTS/SUBDIR", "NESTED.TXT", ARCHIVE);
    CHECK(file);
    fs.CloseFile(file);
    file = fs.OpenFile("~/TESTS/SUBDIR/../HELLO.TXT");
    CHECK(file);
    fs.CloseFile(file);
    return 0;
}

TEST(fat_rejects_cyclic_directory_chain)
{
    MemoryDisk disk;
    auto* boot = (FAT32_BootSector*)disk.bytes;
    auto* fat = (uint32_t*)(disk.bytes + boot->ReservedSectors * 512);
    fat[boot->RootDirStart] = boot->RootDirStart;
    FAT32 fs(&disk, 0);
    CHECK(fs.OpenFile("~/TESTS") == nullptr);
    return 0;
}

TEST(fat_rejects_invalid_bpb_without_writing)
{
    for (unsigned variant = 0; variant < 4; ++variant)
    {
        MemoryDisk disk;
        auto* boot = (FAT32_BootSector*)disk.bytes;
        switch (variant)
        {
        case 0:
            boot->BytesPerSector = 1024;
            break;
        case 1:
            boot->SectorsPerCluster = 3;
            break;
        case 2:
            boot->NumberOfFATs = 0;
            break;
        case 3:
            boot->SectorsPerFAT32 = 1;
            break;
        }

        unsigned char before[512];
        test_copy(before, disk.bytes, 512);
        {
            FAT32 fs(&disk, 0);
            CHECK(fs.OpenFile("~/TESTS") == nullptr);
        }

        CHECK(test_equal(before, disk.bytes, 512));
    }

    return 0;
}

TEST(fat_account_storage_maps_separate_bounded_sectors)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    uint64_t sectors[2][16];
    Device* device = nullptr;
    CHECK(fs.AccountStorage(sectors, device) && device == &disk);
    for (unsigned i = 0; i < 32; ++i)
    {
        uint64_t sector = sectors[i / 16][i % 16];
        CHECK(sector > 0 && sector < disk.size / 512);
        for (unsigned j = 0; j < i; ++j)
        {
            CHECK(sector != sectors[j / 16][j % 16]);
        }
    }

    return 0;
}

TEST(fat_account_storage_rejects_bad_size_and_shared_chain)
{
    for (unsigned fault = 0; fault < 2; ++fault)
    {
        MemoryDisk disk;
        uint8_t* entries[2] = {};
        for (unsigned offset = 0; offset + 32 <= disk.size; offset += 32)
        {
            if (test_equal(disk.bytes + offset, "ACCT0   DAT", 11))
            {
                entries[0] = disk.bytes + offset;
            }
            else if (test_equal(disk.bytes + offset, "ACCT1   DAT", 11))
            {
                entries[1] = disk.bytes + offset;
            }
        }

        CHECK(entries[0] && entries[1]);
        if (fault == 0)
        {
            entries[0][28] = 1; // 8193 rather than 8192 bytes
        }
        else
        {
            test_copy(entries[1] + 20, entries[0] + 20, 2);
            test_copy(entries[1] + 26, entries[0] + 26, 2);
        }

        FAT32 fs(&disk, 0);
        uint64_t sectors[2][16];
        Device* device = nullptr;
        CHECK(!fs.AccountStorage(sectors, device));
    }

    return 0;
}

TEST(fat_account_storage_propagates_read_failure)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    disk.readError = true;
    uint64_t sectors[2][16];
    Device* device = nullptr;
    CHECK(!fs.AccountStorage(sectors, device));
    FAT32 unreadable(&disk, 0);
    CHECK(!unreadable.AccountStorage(sectors, device));
    return 0;
}

TEST(fat_checked_mutations_survive_remount)
{
    MemoryDisk disk;
    const char data[] = "Saved across a remount\n";
    {
        FAT32 fs(&disk, 0);
        CHECK(fs.MakeDirectory("~/WORK") == 0);
        CHECK(fs.MakeDirectory("~/WORK") != 0);
        CHECK(fs.MakeDirectory("~/ABSENT/CHILD") != 0);
        CHECK(fs.SaveFile("~/WORK/NEW.TXT", data, sizeof(data) - 1, true) == 0);
        CHECK(fs.SaveFile("~/WORK/NEW.TXT", "bad", 3, true) != 0);
        CHECK(fs.SaveFile("~/WORK/MISSING", "bad", 3, false) != 0);
        CHECK(fs.SaveFile("~/WORK", "bad", 3, false) != 0);
        CHECK(fs.RemoveFile("~/WORK") != 0);
        CHECK(fs.MoveFile("~/WORK", "~/OTHER") != 0);
        CHECK(fs.MoveFile("~/WORK/NEW.TXT", "~/TESTS/HELLO.TXT") != 0);
        CHECK(fs.MoveFile("~/WORK/NEW.TXT", "~/Moved long file.txt") == 0);
        CHECK(fs.OpenFile("~/WORK/NEW.TXT") == nullptr);
    }

    {
        FAT32 fs(&disk, 0);
        auto file = fs.OpenFile("~/Moved long file.txt");
        CHECK(file && file->GetSize() == sizeof(data) - 1);
        char buffer[64] = {};
        CHECK(fs.Read(file, buffer, sizeof(buffer)) == 0);
        CHECK(test_equal(buffer, data, sizeof(data) - 1));
        fs.CloseFile(file);
        CHECK(fs.SaveFile("~/Moved long file.txt", "short", 5, false) == 0);
    }

    {
        FAT32 fs(&disk, 0);
        auto file = fs.OpenFile("~/Moved long file.txt");
        char buffer[8] = {};
        CHECK(file && file->GetSize() == 5 && fs.Read(file, buffer, 8) == 0);
        CHECK(test_equal(buffer, "short", 5));
        fs.CloseFile(file);
        CHECK(fs.RemoveFile("~/Moved long file.txt") == 0);
        CHECK(fs.SaveFile("~/WORK/EMPTY", nullptr, 0, true) == 0);
        CHECK(fs.SaveFile("~/WORK/RESULT.TXT", data, sizeof(data) - 1, true) == 0);
    }

    FAT32 fs(&disk, 0);
    CHECK(fs.OpenFile("~/Moved long file.txt") == nullptr);
    auto empty = fs.OpenFile("~/WORK/EMPTY");
    CHECK(empty && empty->GetSize() == 0);
    fs.CloseFile(empty);
    test_disk_save(disk.bytes, disk.size);
    return 0;
}

TEST(fat_checked_mutations_reject_bad_names_and_readonly)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    const char* names[] = {
        "~", "~/", "~/.", "~/..", "~/BAD.", "~/BAD ", "~/B?D", "~/BAD:NAME", "~/TESTS/HELLO.TXT/CHILD"};
    for (const char* path : names)
    {
        CHECK(fs.MakeDirectory(path) != 0);
        CHECK(fs.SaveFile(path, "x", 1, true) != 0);
        CHECK(fs.MoveFile("~/TESTS/HELLO.TXT", path) != 0);
    }

    auto file = fs.CreateFile("~", "LOCKED", READ_ONLY);
    CHECK(file);
    fs.CloseFile(file);
    CHECK(fs.SaveFile("~/LOCKED", "x", 1, false) != 0);
    CHECK(fs.RemoveFile("~/LOCKED") != 0);
    CHECK(fs.MoveFile("~/LOCKED", "~/OTHER") != 0);
    return 0;
}

TEST(fat_save_write_failures_keep_old_or_complete_new_data)
{
    // Fail each write phase, then remount the actual bytes. No partial payload
    // may replace the old file, and a failed driver must not keep mutating.
    for (unsigned fail = 1; fail <= 18; ++fail)
    {
        MemoryDisk disk;
        unsigned char original[64] = {};
        uint64_t originalSize;
        int status;
        {
            FAT32 fs(&disk, 0);
            auto file = fs.OpenFile("~/TESTS/HELLO.TXT");
            CHECK(file && file->GetSize() < sizeof(original));
            originalSize = file->GetSize();
            CHECK(fs.Read(file, original, originalSize) == 0);
            fs.CloseFile(file);
            disk.failWrite = fail;
            status = fs.SaveFile("~/TESTS/HELLO.TXT", "replacement", 11, false);
            if (status)
            {
                unsigned writes = disk.writes;
                CHECK(fs.MakeDirectory("~/AFTER") != 0);
                CHECK(disk.writes == writes);
            }
        }

        disk.failWrite = 0;
        FAT32 reopened(&disk, 0);
        auto file = reopened.OpenFile("~/TESTS/HELLO.TXT");
        unsigned char actual[64] = {};
        CHECK(file && reopened.Read(file, actual, sizeof(actual)) == 0);
        bool old = file->GetSize() == originalSize && test_equal(actual, original, originalSize);
        bool replaced = file->GetSize() == 11 && test_equal(actual, "replacement", 11);
        CHECK(old || replaced);
        if (!status)
        {
            CHECK(replaced);
        }
        reopened.CloseFile(file);
    }

    return 0;
}

TEST(fat_mutations_report_device_failures)
{
    for (unsigned operation = 0; operation < 4; ++operation)
    {
        for (unsigned fault = 0; fault < 3; ++fault)
        {
            MemoryDisk disk;
            FAT32 fs(&disk, 0);
            disk.readError = fault == 0;
            disk.writeError = fault == 1;
            disk.flushError = fault == 2;
            int result = operation == 0   ? fs.MakeDirectory("~/NEW")
                         : operation == 1 ? fs.RemoveFile("~/TESTS/HELLO.TXT")
                         : operation == 2 ? fs.MoveFile("~/TESTS/HELLO.TXT", "~/NEW")
                                          : fs.SaveFile("~/TESTS/HELLO.TXT", "new", 3, false);
            CHECK(result != 0);
        }
    }

    return 0;
}

TEST(fat_full_directory_save_returns_staged_clusters)
{
    MemoryDisk disk;
    auto& boot = *(FAT32_BootSector*)disk.bytes;
    uint32_t dataSector = boot.ReservedSectors + boot.NumberOfFATs * boot.SectorsPerFAT32;
    uint32_t clusterCount = (boot.LargeTotalSectors - dataSector) / boot.SectorsPerCluster;
    // Fill the root through production creation so every occupied slot has
    // permission metadata. This must fail for capacity, not missing records.
    {
        FAT32 fs(&disk, 0);
        unsigned slots = boot.SectorsPerCluster * 512 / 32;
        for (unsigned index = 2; index < slots; ++index)
        {
            char name[] = "FULL00";
            name[4] = '0' + index / 10;
            name[5] = '0' + index % 10;
            auto file = fs.CreateFile("~", name, ARCHIVE);
            CHECK(file);
            fs.CloseFile(file);
        }
    }

    uint32_t remaining = 0;
    // Leave exactly one free cluster for the payload, so a full directory
    // cannot grow after staging it. Mark other free clusters allocated.
    for (unsigned copy = 0; copy < boot.NumberOfFATs; ++copy)
    {
        auto* fat = (uint32_t*)(disk.bytes + (boot.ReservedSectors + copy * boot.SectorsPerFAT32) * 512);
        for (uint32_t cluster = 2; cluster < clusterCount + 2; ++cluster)
        {
            if ((fat[cluster] & 0x0fffffff) == 0)
            {
                if (!remaining)
                {
                    remaining = cluster;
                }
                if (cluster != remaining)
                {
                    fat[cluster] = END_CLUSTER;
                }
            }
        }
    }

    CHECK(remaining);
    {
        FAT32 fs(&disk, 0);
        for (unsigned attempt = 0; attempt < 3; ++attempt)
        {
            CHECK(fs.SaveFile("~/NO_ROOM.TXT", "x", 1, true) != 0);
            auto* fat = (uint32_t*)(disk.bytes + boot.ReservedSectors * 512);
            CHECK((fat[remaining] & 0x0fffffff) == 0);
        }
    }

    FAT32 fs(&disk, 0);
    CHECK(fs.OpenFile("~/NO_ROOM.TXT") == nullptr);
    return 0;
}

TEST(storage_mount)
{
    MemoryDisk disk;
    JournalDevice journal(&disk, 0, disk.size / 512);
    CHECK(journal.Healthy());
    FAT32Driver driver(&journal, 0);
    CHECK(driver.IsValid());
    DirEntry entry;
    CHECK(driver.OpenFile("~", &entry) == 0);
    CHECK(driver.OpenFile("~/TESTS", &entry) == 0);
    return 0;
}

TEST(vfs_modes_and_stale_tokens)
{
    FakeFS fs;
    InitialiseFilesystems();
    filesystems[0] = &fs;
    CHECK(!fopen("fs0:/FILE", 0));
    CHECK(!fopen("fs0:/FILE", O_CREAT));
    CHECK(!fopen("fs0:/FILE", 0x80 | O_READ));
    auto read = fopen("fs0:/FILE", O_READ);
    CHECK(read);
    char byte = 0;
    CHECK(fwrite(&byte, 1, 1, read) < 0);
    CHECK(fclose(read) == 0);
    auto write = fopen("fs0:/FILE", O_WRITE);
    CHECK(write && write != read);
    CHECK(fread(&byte, 1, 1, write) < 0);
    CHECK(fread(&byte, 1, 1, read) < 0);
    CHECK(fread(&byte, 1, 1, (void*)0xdeadbeef) < 0);
    CHECK(fclose(read) < 0);
    fclose(write);
    return 0;
}

TEST(storage_permission_identity_and_lifetime)
{
    MemoryDisk disk;
    FilePermissions owner = {1001, 1002, 0640};
    {
        FAT32 fs(&disk, 0);
        CHECK(fs.SaveFile("~/OWNED", "secret", 6, true, &owner) == 0);
        CHECK(fs.MoveFile("~/OWNED", "~/TESTS/RENAMED") == 0);
        CHECK(fs.SaveFile("~/TESTS/RENAMED", "replacement", 11, false) == 0);
    }

    FAT32 fs(&disk, 0);
    auto file = fs.OpenFile("~/TESTS/RENAMED");
    CHECK(file);
    auto permissions = file->GetPermissions();
    CHECK(permissions.uid == owner.uid && permissions.gid == owner.gid && permissions.mode == owner.mode);
    fs.CloseFile(file);
    CHECK(fs.RemoveFile("~/TESTS/RENAMED") == 0);
    CHECK(fs.SaveFile("~/TESTS/RENAMED", nullptr, 0, true) == 0);
    file = fs.OpenFile("~/TESTS/RENAMED");
    CHECK(file && file->GetPermissions().uid == 0 && file->GetPermissions().mode == 0644);
    fs.CloseFile(file);
    return 0;
}

TEST(storage_access_owner_group_other_and_traversal)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    InitialiseFilesystems();
    filesystems[0] = &fs;
    FilePermissions permissions = {1001, 1002, 0640};
    CHECK(fs.SaveFile("~/PRIVATE", "x", 1, true, &permissions) == 0);
    HackosCredentials owner = {1001, 1000};
    HackosCredentials group = {1003, 1002};
    HackosCredentials other = {1004, 1000};
    CHECK(PathAccess("~/PRIVATE", owner, 6));
    CHECK(PathAccess("~/PRIVATE", group, 4));
    CHECK(!PathAccess("~/PRIVATE", group, 2));
    CHECK(!PathAccess("~/PRIVATE", other, 4));
    other.groups[0] = 1002;
    other.groupCount = 1;
    CHECK(PathAccess("~/PRIVATE", other, 4));
    CHECK(!PathAccess("~/PRIVATE", HackosCredentials{}, 1));
    permissions.mode = 0700;
    CHECK(fs.MakeDirectory("~/LOCKED", &permissions) == 0);
    permissions.mode = 0666;
    CHECK(fs.SaveFile("~/LOCKED/PUBLIC", "x", 1, true, &permissions) == 0);
    CHECK(!PathAccess("~/LOCKED/PUBLIC", group, 4));
    CHECK(PathAccess("~/LOCKED/PUBLIC", owner, 6));
    CHECK(!PathAccess("~/LOCKED/NEW", group, 3, true));
    return 0;
}

TEST(storage_sector_interruptions_replay_complete_transactions)
{
    // Cut every sector boundary, including journal payload, publication,
    // home-sector replay and clean marker. An interrupted rename never exposes
    // two owners for one data chain after remount.
    for (unsigned operation = 0; operation < 3; ++operation)
    {
        for (int cut = 0; cut < 36; ++cut)
        {
            MemoryDisk disk;
            {
                FAT32 fs(&disk, 0);
                disk.cutSectors = cut;
                if (operation == 0)
                {
                    fs.SaveFile("~/TESTS/HELLO.TXT", "replacement", 11, false);
                }
                else if (operation == 1)
                {
                    fs.MoveFile("~/TESTS/HELLO.TXT", "~/MOVED");
                }
                else
                {
                    FilePermissions permissions = {1001, 1002, 0600};
                    fs.SaveFile("~/CREATED", "complete", 8, true, &permissions);
                }
            }

            disk.cutSectors = -1;
            disk.powerLost = false;
            FAT32 reopened(&disk, 0);
            auto old = reopened.OpenFile("~/TESTS/HELLO.TXT");
            if (operation == 0)
            {
                char bytes[16] = {};
                CHECK(old && reopened.Read(old, bytes, sizeof(bytes)) == 0);
                CHECK((old->GetSize() == 13 && test_equal(bytes, "Hello HackOS\n", 13)) ||
                      (old->GetSize() == 11 && test_equal(bytes, "replacement", 11)));
            }
            else if (operation == 1)
            {
                auto moved = reopened.OpenFile("~/MOVED");
                CHECK((old != nullptr) != (moved != nullptr));
                if (moved)
                {
                    char bytes[13];
                    CHECK(reopened.Read(moved, bytes, sizeof(bytes)) == 0);
                    CHECK(test_equal(bytes, "Hello HackOS\n", 13));
                    reopened.CloseFile(moved);
                }
            }
            else
            {
                CHECK(old);
                auto created = reopened.OpenFile("~/CREATED");
                if (created)
                {
                    char bytes[8];
                    CHECK(created->GetSize() == 8 && reopened.Read(created, bytes, 8) == 0);
                    CHECK(test_equal(bytes, "complete", 8));
                    auto permissions = created->GetPermissions();
                    CHECK(permissions.uid == 1001 && permissions.gid == 1002 && permissions.mode == 0600);
                    reopened.CloseFile(created);
                }
            }

            if (old)
            {
                reopened.CloseFile(old);
            }

            CHECK(reopened.SaveFile("~/AFTER", "ok", 2, true) == 0);
        }
    }

    return 0;
}

TEST(storage_corrupt_journal_and_permissions_fail_closed)
{
    for (unsigned fault = 0; fault < 3; ++fault)
    {
        MemoryDisk disk;
        unsigned position = fault == 0 ? 8 * 512 : fault == 1 ? PERMISSION_START * 512 + 12 : 8 * 512 + 12;
        disk.bytes[position] ^= 1;
        FAT32 fs(&disk, 0);
        CHECK(!fs.OpenFile("~/TESTS/HELLO.TXT"));
        CHECK(fs.SaveFile("~/NEW", "x", 1, true) != 0);
        CHECK(disk.writes == 0);
    }

    return 0;
}

TEST(storage_capacity_and_transaction_limit)
{
    MemoryDisk disk;
    auto* boot = (FAT32_BootSector*)disk.bytes;
    boot->LargeTotalSectors = disk.size / 512 + 1;
    {
        FAT32 fs(&disk, 0);
        CHECK(!fs.OpenFile("~/TESTS/HELLO.TXT"));
        CHECK(disk.writes == 0);
    }

    boot->LargeTotalSectors = disk.size / 512;
    JournalDevice journal(&disk, 0, disk.size / 512);
    CHECK(journal.Begin());
    uint8_t sector[512] = {};
    sector[0] = 42;
    for (unsigned i = 0; i < JOURNAL_LIMIT; ++i)
    {
        CHECK(journal.Write(5000 + i, sector, 1));
    }

    CHECK(!journal.Write(5000 + JOURNAL_LIMIT, sector, 1));
    CHECK(!journal.Finish(false));
    CHECK(disk.writes == 0);
    CHECK(journal.Begin());
    CHECK(journal.Write(5000, sector, 1));
    CHECK(journal.Finish(true));
    CHECK(disk.bytes[5000 * 512] == 42);
    return 0;
}

TEST(storage_flush_failures_freeze_and_recover)
{
    for (unsigned failure = 1; failure <= 4; ++failure)
    {
        MemoryDisk disk;
        {
            FAT32 fs(&disk, 0);
            disk.failFlush = failure;
            CHECK(fs.SaveFile("~/TESTS/HELLO.TXT", "replacement", 11, false) != 0);
            unsigned writes = disk.writes;
            CHECK(fs.SaveFile("~/AFTER", "bad", 3, true) != 0);
            CHECK(disk.writes == writes);
        }

        disk.failFlush = 0;
        FAT32 reopened(&disk, 0);
        auto file = reopened.OpenFile("~/TESTS/HELLO.TXT");
        char buffer[16] = {};
        CHECK(file && reopened.Read(file, buffer, sizeof(buffer)) == 0);
        CHECK(test_equal(buffer, "Hello HackOS\n", 13) || test_equal(buffer, "replacement", 11));
        reopened.CloseFile(file);
        CHECK(reopened.SaveFile("~/AFTER", "ok", 2, true) == 0);
    }

    return 0;
}

TEST(storage_directory_aliases_keep_names_and_permissions)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    FilePermissions owner = {1001, 1002, 0700};
    CHECK(fs.MakeDirectory("~/PRIVATE", &owner) == 0);
    auto dot = fs.GetFile("~/PRIVATE", 0);
    auto parent = fs.GetFile("~/PRIVATE", 1);
    CHECK(dot && parent);
    CHECK(test_name_equal(dot->GetName(), ".") && dot->GetPermissions().uid == 1001 &&
          dot->GetPermissions().mode == 0700);
    CHECK(test_name_equal(parent->GetName(), "..") && parent->GetPermissions().uid == 0 &&
          parent->GetPermissions().mode == 0755);
    fs.CloseFile(dot);
    fs.CloseFile(parent);
    auto alias = fs.CreateFile("~/PRIVATE", "SHORT~1", ARCHIVE);
    CHECK(alias);
    fs.CloseFile(alias);
    alias = fs.OpenFile("~/PRIVATE/SHORT~1");
    CHECK(alias);
    fs.CloseFile(alias);
    return 0;
}

TEST(fat_directory_batches_match_individual_metadata_and_reduce_reads)
{
    MemoryDisk disk;
    FAT32 fs(&disk, 0);
    HackosFileInfo entries[16];
    uint64_t count = 0;
    unsigned before = disk.reads;
    CHECK(fs.ReadDirectory("~/TESTS", 0, entries, 16, count) == 0 && count > 2 && count < 16);
    unsigned batched = disk.reads - before;
    before = disk.reads;
    for (uint64_t i = 0; i < count; ++i)
    {
        auto file = fs.GetFile("~/TESTS", i);
        CHECK(file);
        CHECK(test_name_equal(file->GetName(), entries[i].name));
        CHECK(file->GetSize() == entries[i].size && file->GetAttributes() == entries[i].attributes);
        auto permissions = file->GetPermissions();
        CHECK(permissions.uid == entries[i].uid && permissions.gid == entries[i].gid &&
              permissions.mode == entries[i].mode);
        fs.CloseFile(file);
    }
    CHECK(disk.reads - before > batched);
    uint64_t next = 99;
    CHECK(fs.ReadDirectory("~/TESTS", count, entries, 16, next) == 0 && next == 0);
    CHECK(fs.ReadDirectory("~/TESTS", ~0ULL, entries, 16, next) == 0 && next == 0);
    CHECK(fs.ReadDirectory("~/TESTS/HELLO.TXT", 0, entries, 16, next) != 0);
    CHECK(fs.ReadDirectory("~/TESTS", 0, entries, 0, next) != 0);
    CHECK(fs.SetPermissions("~/TESTS/HELLO.TXT", {42, 43, 0600}) == 0);
    CHECK(fs.ReadDirectory("~/TESTS", 0, entries, 16, count) == 0);
    bool found = false;
    for (uint64_t i = 0; i < count; ++i)
    {
        if (test_name_equal(entries[i].name, "HELLO.TXT"))
        {
            CHECK(entries[i].uid == 42 && entries[i].gid == 43 && entries[i].mode == 0600);
            found = true;
        }
    }
    CHECK(found);
    disk.readError = true;
    CHECK(fs.ReadDirectory("~/TESTS", 0, entries, 16, count) != 0);
    return 0;
}
