LIBRARY_CASE(split_fields_and_ownership)
{
    prepare_split_allocator();
    char input[] = ",alpha,,beta,";
    int count = -1;
    char** parts = split(input, ',', &count);
    CHECK(parts && count == 5 && parts[count] == nullptr);
    const char* expected[] = {"", "alpha", "", "beta", ""};
    for (int i = 0; i < count; ++i)
    {
        CHECK(test_name_equal(parts[i], expected[i]));
    }

    CHECK(test_name_equal(input, ",alpha,,beta,"));
    parts[1][0] = 'A';
    CHECK(input[1] == 'a');
    for (int i = 0; i < count; ++i)
    {
        release_split_allocation(parts[i]);
    }

    release_split_allocation(parts);
    return 0;
}

LIBRARY_CASE(split_empty_and_unsplit)
{
    prepare_split_allocator();
    const char* inputs[] = {"", "abc", ","};
    for (const char* input : inputs)
    {
        int count = -1;
        char** parts = split(input, '\0', &count);
        CHECK(parts && count == 1 && !parts[1]);
        CHECK(test_name_equal(parts[0], input));
        release_split_allocation(parts[0]);
        release_split_allocation(parts);
    }

    int count = -1;
    char** parts = split("", ',', &count);
    CHECK(parts && count == 1 && parts[0][0] == 0 && !parts[1]);
    release_split_allocation(parts[0]);
    release_split_allocation(parts);
    parts = split("abc", ',', &count);
    CHECK(parts && count == 1 && test_name_equal(parts[0], "abc") && !parts[1]);
    release_split_allocation(parts[0]);
    release_split_allocation(parts);
    return 0;
}

LIBRARY_CASE(split_invalid_input)
{
    prepare_split_allocator();
    int count = 42;
    CHECK(!split(nullptr, ',', &count) && count == 0);
    CHECK(!split("abc", ',', nullptr));
    return 0;
}
