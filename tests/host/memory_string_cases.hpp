// Shared contracts applied independently to each real library implementation.
LIBRARY_CASE(memory_copy_alignment_and_guards) {
    for (size_t offset = 0; offset < 8; ++offset) for (size_t length = 0; length < 32; ++length) {
        unsigned char source[48], actual[48], expected[48];
        for (size_t i=0; i<48; ++i) source[i]=(unsigned char)(i*13+7);
        test_fill(actual,0xa5,48); test_fill(expected,0xa5,48);
        test_copy(expected+offset,source+7-offset,length);
        CHECK(memcpy(actual+offset,source+7-offset,length)==actual+offset);
        CHECK(test_equal(actual,expected,48));
    } return 0;
}
LIBRARY_CASE(memory_set_standard_byte_fill) {
    for (size_t offset=0; offset<8; ++offset) for (size_t n=0; n<24; ++n) {
        unsigned char actual[40], expected[40];
        test_fill(actual,0xcc,40); test_fill(expected,0xcc,40);
        test_fill(expected+offset,0xa5,n);
        CHECK(memset(actual+offset,0xa5,n)==actual+offset);
        CHECK(test_equal(actual,expected,40));
    } return 0;
}
LIBRARY_CASE(memory_set_truncates_to_unsigned_byte) {
    unsigned char b[16]; memset(b,0x1234,sizeof b);
    for (auto byte:b) CHECK(byte==0x34); return 0;
}
LIBRARY_CASE(memory_compare_equal) { char a[]="same"; CHECK(memcmp(a,a,4)==0); return 0; }
LIBRARY_CASE(memory_compare_empty) { char a=1,b=2; CHECK(memcmp(&a,&b,0)==0); return 0; }
LIBRARY_CASE(memory_compare_unsigned_order) {
    unsigned char a[]={0x80},b[]={0x7f}; CHECK(memcmp(a,b,1)>0); CHECK(memcmp(b,a,1)<0); return 0;
}
LIBRARY_CASE(string_length) { CHECK(strlen((char*)"")==0); CHECK(strlen((char*)"HackOS")==6); return 0; }
LIBRARY_CASE(string_compare_order_and_prefix) {
    CHECK(strcmp((char*)"",(char*)"")==0); CHECK(strcmp((char*)"a",(char*)"ab")<0);
    CHECK(strcmp((char*)"b",(char*)"a")>0); return 0;
}
LIBRARY_CASE(string_compare_unsigned_bytes) {
    char a[]={(char)0x80,0},b[]={(char)0x7f,0}; CHECK(strcmp(a,b)>0); return 0;
}
LIBRARY_CASE(string_ncompare_order) { CHECK(strncmp((char*)"a",(char*)"b",1)<0); return 0; }
LIBRARY_CASE(string_ncompare_stops_at_nul) {
    char a[]={0,'x'},b[]={0,'y'}; CHECK(strncmp(a,b,2)==0); return 0;
}
LIBRARY_CASE(string_ncompare_empty) { CHECK(strncmp((char*)"a",(char*)"b",0)==0); return 0; }
LIBRARY_CASE(string_copy_return_and_terminator) {
    char b[16]; test_fill(b,'!',sizeof b); CHECK(strcpy(b,"abc")==b);
    CHECK(test_equal(b,"abc",4)); CHECK(b[4]=='!'); return 0;
}
LIBRARY_CASE(string_reverse_append_backspace) {
    char b[16]="abc"; reverse(b); CHECK(test_equal(b,"cba",4));
    append(b,'d'); CHECK(test_equal(b,"cbad",5)); backspace(b); CHECK(test_equal(b,"cba",4)); return 0;
}
LIBRARY_CASE(string_itoa_boundaries) {
    uint8_t b[32]; itoa(b,'d',0); CHECK(test_equal(b,"0",2));
    itoa(b,'x',0xffffffffffffffffUL); CHECK(test_equal(b,"FFFFFFFFFFFFFFFF",17));
    itoa(b,'d',0xffffffffffffffffUL); CHECK(test_equal(b,"18446744073709551615",21)); return 0;
}
