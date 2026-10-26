#include "support.hpp"
#include "drivers/screen/framebuffer.h"

static Framebuffer make_fb(uint32_t* data, uint32_t w=8, uint32_t h=6) {
    Framebuffer f; f.address=data; f.width=w; f.height=h; f.bpp=32; f.pitch=w; return f;
}
TEST(framebuffer_pixel_bounds) {
    uint32_t data[50]={}; auto f=make_fb(data+1);
    f.PutPixel(7,5,0x12345678); CHECK(f.GetPixel(7,5)==0x12345678);
    f.PutPixel(8,5,1); f.PutPixel(7,6,1); CHECK(data[0]==0 && data[49]==0);
    CHECK(f.GetPixel(8,0)==0); return 0;
}
TEST(framebuffer_rectangle_inside) {
    uint32_t data[48]={}; auto f=make_fb(data); f.DrawRect(2,1,3,2,99);
    for(unsigned y=0;y<6;++y) for(unsigned x=0;x<8;++x)
        CHECK(data[y*8+x]==((x>=2&&x<5&&y>=1&&y<3)?99U:0U)); return 0;
}
TEST(framebuffer_clip_right_only) {
    uint32_t data[48]={}; auto f=make_fb(data); f.DrawRect(7,1,4,2,99);
    for(unsigned y=0;y<6;++y) for(unsigned x=0;x<8;++x)
        CHECK(data[y*8+x]==((x==7&&y>=1&&y<3)?99U:0U)); return 0;
}
TEST(framebuffer_clip_bottom_only) {
    uint32_t data[48]={}; auto f=make_fb(data); f.DrawRect(1,5,2,4,99);
    for(unsigned y=0;y<6;++y) for(unsigned x=0;x<8;++x)
        CHECK(data[y*8+x]==((y==5&&x>=1&&x<3)?99U:0U)); return 0;
}
TEST(framebuffer_padded_pitch) {
    uint32_t data[60]={}; auto f=make_fb(data); f.pitch=10;
    f.PutPixel(1,1,99); CHECK(data[11]==99); CHECK(data[9]==0); return 0;
}
TEST(framebuffer_clear) {
    uint32_t data[48]={}; auto f=make_fb(data); f.Clear(0xabcdef12);
    for(auto pixel:data) CHECK(pixel==0xabcdef12); return 0;
}
TEST(framebuffer_cursor_restores_background) {
    uint32_t data[48]; for(unsigned i=0;i<48;++i)data[i]=i;
    auto f=make_fb(data); uint8_t cursor[32]; test_fill(cursor,0xff,32);
    f.DrawMouseCursor(cursor,6,4,99); CHECK(f.GetPixel(6,4)==99);
    f.ClearMouseCursor(cursor,6,4); for(unsigned i=0;i<48;++i)CHECK(data[i]==i); return 0;
}
TEST(framebuffer_cursor_preserves_new_drawing) {
    uint32_t data[48]={}; auto f=make_fb(data); uint8_t cursor[32]; test_fill(cursor,0xff,32);
    f.DrawMouseCursor(cursor,6,4,99); f.PutPixel(6,4,42); f.ClearMouseCursor(cursor,6,4);
    CHECK(f.GetPixel(6,4)==42); return 0;
}
