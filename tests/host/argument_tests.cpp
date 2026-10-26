#include "support.hpp"
#include "hackos/runtime/arguments.h"
TEST(arguments_quotes_escapes_and_empty_words)
{
    char line[] = "  prog alpha \"two words\" '' escaped\\ space \"&\" 'a\"b'  ";
    char* args[16]; bool background;
    CHECK(ParseArguments(line,args,16,background) == 7 && !background);
    const char* expected[] = {"prog","alpha","two words","","escaped space","&","a\"b"};
    for (int i=0;i<7;++i) CHECK(test_name_equal(args[i],expected[i]));
    char joined[] = "p a\"b c\"d";
    CHECK(ParseArguments(joined,args,16,background) == 2);
    CHECK(test_name_equal(args[1],"ab cd"));
    return 0;
}
TEST(arguments_background_and_syntax_limits)
{
    char* args[2]; bool background;
    char valid[] = "p x &  ";
    CHECK(ParseArguments(valid,args,2,background) == 2 && background);
    char overflow[] = "p x y";
    CHECK(ParseArguments(overflow,args,2,background) == -2);
    char unmatched[] = "p 'x";
    CHECK(ParseArguments(unmatched,args,2,background) == -1);
    char escape[] = "p x\\";
    CHECK(ParseArguments(escape,args,2,background) == -1);
    char misplaced[] = "p & x";
    CHECK(ParseArguments(misplaced,args,2,background) == -1);
    char alone[] = "&";
    CHECK(ParseArguments(alone,args,2,background) == -1);
    char blank[] = " \t ";
    CHECK(ParseArguments(blank,args,2,background) == 0);
    return 0;
}
