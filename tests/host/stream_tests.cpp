#include "support.hpp"
#include "hackos/runtime/pipeline.h"
#include "hackos/runtime/completion.h"
#include "drivers/keyboard/input.h"

TEST(pipeline_operators_and_quotes)
{
    ShellPipeline pipeline;
    CHECK(ParsePipeline("print 'a|b' \"c>d\" \\&|cat<\"two words\">out 2>&1 &", pipeline));
    CHECK(pipeline.count == 2 && pipeline.background);
    CHECK(pipeline.stages[0].argc == 4);
    CHECK(test_name_equal(pipeline.stages[0].argv[1], "a|b"));
    CHECK(test_name_equal(pipeline.stages[0].argv[2], "c>d"));
    CHECK(test_name_equal(pipeline.stages[0].argv[3], "&"));
    auto& stage = pipeline.stages[1];
    CHECK(stage.argc == 1 && stage.redirectCount == 3);
    CHECK(stage.redirects[0].target == 0 && test_name_equal(stage.redirects[0].path, "two words"));
    CHECK(stage.redirects[1].target == 1 && (stage.redirects[1].mode & HACKOS_OPEN_TRUNCATE));
    CHECK(stage.redirects[2].target == 2 && stage.redirects[2].source == 1);
    return 0;
}

TEST(pipeline_order_and_empty_arguments)
{
    ShellPipeline pipeline;
    CHECK(ParsePipeline("print '' 2>&1 >>x", pipeline));
    auto& stage = pipeline.stages[0];
    CHECK(stage.argc == 2 && !*stage.argv[1]);
    CHECK(stage.redirects[0].source == 1 && stage.redirects[0].target == 2);
    CHECK(stage.redirects[1].mode == (HACKOS_OPEN_WRITE | HACKOS_OPEN_CREATE | HACKOS_OPEN_APPEND));
    CHECK(ParsePipeline("   \t", pipeline) && pipeline.count == 0);
    CHECK(ParsePipeline("<x cat|cat|cat|cat", pipeline) && pipeline.count == 4);
    return 0;
}

TEST(pipeline_rejects_malformed_syntax)
{
    ShellPipeline pipeline;
    const char* invalid[] = {"| cat",
                             "cat |",
                             "cat || cat",
                             "cat && cat",
                             "cat >",
                             "cat < > x",
                             "cat >''",
                             "cat 2>&9",
                             "cat 2>&1tail",
                             "cat 'open",
                             "cat \\",
                             "&",
                             "cat & tail",
                             "cat <<x",
                             "cat|cat|cat|cat|cat",
                             "cat >a >b >c >d >e >f >g >h >i"};
    for (auto text : invalid)
    {
        CHECK(!ParsePipeline(text, pipeline));
    }

    return 0;
}

TEST(pipeline_bounds)
{
    ShellPipeline pipeline;
    char input[1024];
    for (auto& ch : input)
    {
        ch = 'a';
    }
    input[sizeof(input) - 1] = 0;
    CHECK(!ParsePipeline(input, pipeline));
    CHECK(!ParsePipeline("p a b c d e f g h i j k l m n o p q", pipeline));
    CHECK(ParsePipeline("user p a b c d e f g h i j k l m n o", pipeline));
    CHECK(pipeline.stages[0].argc == HACKOS_MAX_ARGS + 1);
    return 0;
}

TEST(stream_completion_preserves_operators)
{
    CompletionWord word;
    CHECK(CompletionPrefix("print x|ca", word) && word.index == 0 && word.start == 8);
    CHECK(test_name_equal(word.text, "ca"));
    CHECK(CompletionPrefix("print x>two", word) && word.index != 0 && word.start == 8);
    CHECK(test_name_equal(word.text, "two"));
    char completed[256];
    CHECK(CompletionReplace("cat ", 4, "a|b<c>d", ' ', completed));
    ShellPipeline pipeline;
    CHECK(ParsePipeline(completed, pipeline) && pipeline.count == 1 && pipeline.stages[0].redirectCount == 0);
    CHECK(test_name_equal(pipeline.stages[0].argv[1], "a|b<c>d"));
    return 0;
}

TEST(terminal_stream_control_d)
{
    KeyboardInput input;
    input.PushScancode(0x1d);
    input.PushScancode(0x20);
    char ch;
    CHECK(input.Pop(ch) && ch == 4);
    input.PushScancode(0xa0);
    input.PushScancode(0x9d);
    input.PushScancode(0x20);
    CHECK(input.Pop(ch) && ch == 'd');
    return 0;
}
