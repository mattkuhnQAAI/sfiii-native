// Synthetic hardware regressions: no game bytes or assets.
#include "saturn.h"
#include "video.h"
#include "state.h"
#include "test.h"
#include <cstring>

TEST(division_result_shadows_survive_preparing_the_next_dividend) {
    SH2Context c{};
    onchip_reset(0);
    onchip_write(c, 0xFFFFFF00, 65536, 4);
    onchip_write(c, 0xFFFFFF10, 1, 4);
    onchip_write(c, 0xFFFFFF14, 3, 4);
    CHECK_EQ(onchip_read(c, 0xFFFFFF1C, 4), 65536);
    CHECK_EQ(onchip_read(c, 0xFFFFFF18, 4), 3);
    CHECK_EQ(onchip_read(c, 0xFFFFFF3C, 4), 65536);
    CHECK_EQ(onchip_read(c, 0xFFFFFF38, 4), 3);
    onchip_write(c, 0xFFFFFF00, 7, 4);
    onchip_write(c, 0xFFFFFF10, 0, 4);
    CHECK_EQ(onchip_read(c, 0xFFFFFF1C, 4), 65536);
    CHECK_EQ(onchip_read(c, 0xFFFFFF18, 4), 3);
    onchip_write(c, 0xFFFFFF14, 100, 4);
    CHECK_EQ(onchip_read(c, 0xFFFFFF1C, 4), 14);
    CHECK_EQ(onchip_read(c, 0xFFFFFF18, 4), 2);
}

TEST(signed_division_shadows_are_private_to_each_cpu) {
    SH2Context master{}, slave{};
    slave.cpu = 1;
    onchip_reset(0); onchip_reset(1);
    onchip_write(master, 0xFFFFFF20, 7, 4);
    onchip_write(master, 0xFFFFFF24, (uint32_t)-100, 4);
    CHECK_EQ(onchip_read(master, 0xFFFFFF1C, 4), (uint32_t)-14);
    CHECK_EQ(onchip_read(master, 0xFFFFFF18, 4), (uint32_t)-2);
    onchip_write(slave, 0xFFFFFF00, 9, 4);
    onchip_write(slave, 0xFFFFFF04, 101, 4);
    CHECK_EQ(onchip_read(slave, 0xFFFFFF1C, 4), 11);
    CHECK_EQ(onchip_read(slave, 0xFFFFFF18, 4), 2);
    CHECK_EQ(onchip_read(master, 0xFFFFFF3C, 4), (uint32_t)-14);
}

TEST(dmac_burst_copies_every_longword_and_preserves_surrounding_memory) {
    SH2Context master{};
    onchip_reset(0);
    constexpr uint32_t source = 0x06080000, destination = 0x06081000;
    for (unsigned i = 0; i < 512; ++i) {
        st8(source + i, (i * 17 + i / 7) & 255);
        st8(destination + i, 0xCC);
    }
    st32(destination - 4, 0x12345678); st32(destination + 512, 0x89ABCDEF);
    onchip_write(master, 0xFFFFFF80, source, 4);
    onchip_write(master, 0xFFFFFF84, destination, 4);
    onchip_write(master, 0xFFFFFF88, 128, 4); // Burst count is in longwords.
    onchip_write(master, 0xFFFFFF8C, 0x5E01, 4); // Auto, increment both, 16-byte burst.
    onchip_write(master, 0xFFFFFFB0, 1, 4);
    for (unsigned i = 0; i < 512; ++i) CHECK_EQ(ld8(destination+i), ld8(source+i));
    CHECK_EQ(ld32(destination - 4), 0x12345678u);
    CHECK_EQ(ld32(destination + 512), 0x89ABCDEFu);
    CHECK_EQ(onchip_read(master, 0xFFFFFF80, 4), source + 512);
    CHECK_EQ(onchip_read(master, 0xFFFFFF84, 4), destination + 512);
    CHECK_EQ(onchip_read(master, 0xFFFFFF88, 4), 0);
    CHECK_EQ(onchip_read(master, 0xFFFFFF8C, 4), 0x5E03);
}

namespace {
void reg16(unsigned off, uint16_t value) { mem_wr(g_vdp2_regs, off, value, 2); }
void vr32(unsigned off, uint32_t value) { mem_wr(g_vdp2_vram, off, value, 4); }
void rotation_bitmap() {
    std::memset(g_vdp2_regs, 0, sizeof g_vdp2_regs);
    std::memset(g_vdp2_vram, 0, sizeof g_vdp2_vram);
    std::memset(g_vdp2_cram, 0, sizeof g_vdp2_cram);
    reg16(0x00, 0x8000); reg16(0x20, 0x10);
    reg16(0xAC, 3); // Black back screen at VRAM 0x60000, outside the bitmap.
    reg16(0x2A, 0x1200); reg16(0xFC, 1);
    reg16(0xBC, 1); reg16(0xBE, 0xFF00);
    std::memset(g_vdp2_vram, 2, 512 * 256);
    mem_wr(g_vdp2_cram, 4, 0x03E0, 2);
    vr32(0x3FE10, 0x10000); vr32(0x3FE14, 0x10000);
    vr32(0x3FE1C, 0x10000); vr32(0x3FE2C, 0x10000);
    vr32(0x3FE4C, 0x10000); vr32(0x3FE50, 0x10000);
}
}

TEST(rotation_bitmap_coordinates_and_transparency) {
    rotation_bitmap();
    g_vdp2_vram[5] = 0;
    Frame f;
    vdp2_compose(f);
    CHECK_EQ(f.px[4], 0x00F800);
    CHECK_EQ(f.px[5], 0);
    CHECK_EQ(f.px[6], 0x00F800);
    // Move the texture start one pixel: the transparent texel moves left.
    vr32(0x3FE00, 0x10000);
    vdp2_compose(f);
    CHECK_EQ(f.px[4], 0);
    CHECK_EQ(f.px[5], 0x00F800);
}

TEST(rotation_coefficient_transparency_and_normal_background_priority) {
    rotation_bitmap();
    // Two-word coefficient 1.0, then transparent, at VRAM 0x40000.
    reg16(0x0E, 0x0310); reg16(0xB4, 1); reg16(0xB6, 1);
    vr32(0x3FE5C, 0x00010000);
    vr32(0x40000, 0x00010000); vr32(0x40004, 0x80010000);
    Frame f;
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x00F800);
    CHECK_EQ(f.px[1], 0);
    // Higher-priority NBG0 remains above RBG0.
    reg16(0x20, 0x11); reg16(0x28, 0x12); reg16(0x3C, 1);
    reg16(0x78, 1); reg16(0x7C, 1); reg16(0xF8, 2);
    std::memset(g_vdp2_vram + 0x20000, 3, 512 * 256);
    mem_wr(g_vdp2_cram, 6, 0x7C00, 2);
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x0000F8);
    CHECK_EQ(f.px[1], 0x0000F8);
}

namespace {
// NBG0 (and NBG1) as 512x256 256-colour bitmaps at VRAM 0 (and 0x20000), zoom 1, a blue back screen
void two_bitmaps() {
    std::memset(g_vdp2_regs, 0, sizeof g_vdp2_regs);
    std::memset(g_vdp2_vram, 0, sizeof g_vdp2_vram);
    std::memset(g_vdp2_cram, 0, sizeof g_vdp2_cram);
    reg16(0x00, 0x8000); reg16(0x20, 0x03); reg16(0x28, 0x1212); reg16(0x3C, 0x0010);
    reg16(0x78, 1); reg16(0x7C, 1); reg16(0x88, 1); reg16(0x8C, 1);
    reg16(0xF8, 0x0203);
    reg16(0xAC, 3); mem_wr(g_vdp2_vram, 0x60000, 0x7C00, 2);
    mem_wr(g_vdp2_cram, 2, 0x001F, 2); mem_wr(g_vdp2_cram, 4, 0x03E0, 2); mem_wr(g_vdp2_cram, 6, 0x7C00, 2);
    std::memset(g_vdp2_vram, 1, 512 * 256);
    std::memset(g_vdp2_vram + 0x20000, 2, 512 * 256);
}
}

TEST(nbg_line_scroll_horizontal_vertical_and_zoom_tables) {
    two_bitmaps();
    reg16(0x20, 0x01);                     // NBG0 alone
    g_vdp2_vram[2] = 3;                    // row 0, dot 2: blue (red elsewhere)
    std::memset(g_vdp2_vram + 5 * 512, 2, 512); // row 5: green
    reg16(0xA0, 2); reg16(0xA2, 0);        // LSTA0: VRAM 0x40000
    Frame f;
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0xF80000u);
    CHECK_EQ(f.px[2], 0x0000F8u);
    reg16(0x9A, 0x02);                     // horizontal: line 0 scrolled 2 dots
    vr32(0x40000, 2 << 16);
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x0000F8u);
    CHECK_EQ(f.px[(size_t)f.w], 0xF80000u); // line 1's entry is 0
    reg16(0x9A, 0x04);                     // vertical: line 0 shows row 5
    vr32(0x40000, 5 << 16); vr32(0x40004, 1 << 16);
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x00F800u);
    CHECK_EQ(f.px[(size_t)f.w + 2], 0xF80000u);
    reg16(0x9A, 0x08);                     // zoom: line 0 steps 2 dots a dot
    vr32(0x40000, 0x200 << 8); vr32(0x40004, 0x100 << 8);
    vdp2_compose(f);
    CHECK_EQ(f.px[1], 0x0000F8u);
    CHECK_EQ(f.px[2], 0xF80000u);
    reg16(0x9A, 0x16);                     // horizontal and vertical, an entry pair every 2 lines
    vr32(0x40000, 2 << 16); vr32(0x40004, 0); vr32(0x40008, 0); vr32(0x4000C, 5 << 16);
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x0000F8u);
    CHECK_EQ(f.px[(size_t)f.w], 0x0000F8u);
    CHECK_EQ(f.px[(size_t)f.w * 2], 0x00F800u);
}

TEST(extended_colour_calculation_averages_the_second_and_third_images) {
    two_bitmaps();
    reg16(0x108, 0x0F0F);                  // ratio 15: half and half
    reg16(0xEC, 0x0003);                   // NBG0 over NBG1, NBG1 over the back screen
    Frame f;
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x7C7C00u);
    reg16(0xEC, 0x0403);                   // extended: NBG1 is first averaged with the back screen
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x7C3E3Eu);
    reg16(0xEC, 0x0401);                   // not when NBG1's own colour calculation is off
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x7C7C00u);
    reg16(0x0E, 0x1000);                   // colour RAM mode 1: the back screen is RGB, so still averaged
    reg16(0xEC, 0x0403);
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x7C3E3Eu);
    reg16(0x20, 0x0407); reg16(0xFA, 0x0001); // NBG2 third, its transparent dots drawn (colour RAM 0)
    reg16(0xEC, 0x0403);
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x7C7C00u);          // a colour RAM dot third: not averaged in modes 1 and 2
    reg16(0x0E, 0x0000);
    vdp2_compose(f);
    CHECK_EQ(f.px[0], 0x7C3E00u);          // mode 0 averages it
}

TEST(vdp1_corner_coverage_does_not_count_one_end_code_twice) {
    vdp1_init();
    std::memset(g_vdp1_vram, 0, sizeof g_vdp1_vram);
    auto cmd = [](unsigned a, unsigned off, uint16_t v) { mem_wr(g_vdp1_vram, a+off, v, 2); };
    cmd(0,0,9); cmd(0,20,319); cmd(0,22,223); // System clipping.
    cmd(32,0,10); // Local coordinate origin.
    cmd(64,0,2); cmd(64,4,0x20); cmd(64,6,0x100); // 256-colour textured line-shaped quad.
    cmd(64,8,0x200); cmd(64,10,0x101); // 8x1 source at 0x1000.
    cmd(64,16,7); cmd(64,18,3); cmd(64,20,7); cmd(64,22,3);
    cmd(96,0,0x8000);
    for (unsigned i=0; i<8; ++i) g_vdp1_vram[0x1000+i]=i+1;
    g_vdp1_vram[0x1001]=0xFF; // This texel also falls on an extra corner-coverage pixel.
    g_vdp1_vram[0x1003]=0xFF; // The second end code really terminates the line.
    vdp1_reg_write(4,1,2); vdp1_vblank_out();
    CHECK_EQ(vdp1_dot(2,1,false),0x103); // After first end code: still drawn.
    CHECK_EQ(vdp1_dot(4,2,false),0); // After second end code: not drawn.
}

TEST(vdp1_expansion_counts_source_end_codes_once) {
    for (bool flip : {false, true}) for (bool disable : {false, true}) {
        vdp1_init();
        std::memset(g_vdp1_vram, 0, sizeof g_vdp1_vram);
        auto cmd = [](unsigned a, unsigned off, uint16_t v) { mem_wr(g_vdp1_vram, a+off, v, 2); };
        cmd(0,0,9); cmd(0,20,319); cmd(0,22,223);
        cmd(32,0,10);
        cmd(64,0,flip ? 0x12 : 2); cmd(64,4,0x20 | (disable ? 0x80 : 0));
        cmd(64,6,0x100); cmd(64,8,0x200); cmd(64,10,0x101);
        // Expand eight source texels over sixteen destination pixels.
        cmd(64,16,15); cmd(64,20,15);
        cmd(96,0,0x8000);
        const uint8_t texture[] = {1,0xFF,3,4,0xFF,6,7,8};
        std::memcpy(g_vdp1_vram+0x1000, texture, sizeof texture);
        vdp1_reg_write(4,1,2); vdp1_vblank_out();
        // The first end code occupies two pixels, but does not stop the line.
        CHECK_EQ(vdp1_dot(flip ? 8 : 4,0,false), flip ? 0x104 : 0x103);
        // A distinct second end code stops it; ECD disables that behavior.
        CHECK_EQ(vdp1_dot(flip ? 14 : 10,0,false), disable ? (flip ? 0x101 : 0x106) : 0);
    }
}

TEST(hd_texture_provenance_preserves_native_framebuffer_pixels) {
    auto setup=[] {
        vdp1_init();std::memset(g_vdp1_vram,0,sizeof g_vdp1_vram);
        auto cmd=[](unsigned a,unsigned o,uint16_t v){mem_wr(g_vdp1_vram,a+o,v,2);};
        cmd(0,0,9);cmd(0,20,319);cmd(0,22,223);cmd(32,0,10);
        cmd(64,0,0);cmd(64,4,0x28);cmd(64,8,0x200);cmd(64,10,0x108);cmd(96,0,0x8000);
        for(unsigned i=0;i<64;++i) mem_wr(g_vdp1_vram,0x1000+i*2,0x83E0+i,2);
    };
    vdp1_hd_collect(false);setup();vdp1_reg_write(4,1,2);vdp1_vblank_out();
    uint16_t reference[64];for(int y=0;y<8;++y) for(int x=0;x<8;++x) reference[y*8+x]=vdp1_dot(x,y,false);
    vdp1_hd_collect(true);setup();vdp1_reg_write(4,1,2);vdp1_vblank_out();
    for(int y=0;y<8;++y) for(int x=0;x<8;++x) CHECK_EQ(vdp1_dot(x,y,false),reference[y*8+x]);
    auto scene=vdp1_hd_scene();CHECK_EQ(scene ? scene->quads.size() : 0,1);
    CHECK_EQ(vdp1_hd_owner(3,3,false),1);
    // A CPU upload invalidates only the affected metadata pixel.
    vdp1_reg_write(4,1,2);vdp1_fb_write((3*512+3)*2,0x8000,2);vdp1_vblank_out();
    CHECK_EQ(vdp1_hd_owner(3,3,false),0);CHECK_EQ(vdp1_hd_owner(4,3,false),1);
    vdp1_hd_collect(false);
    // vdp1_init preserves framebuffer RAM; leave other independent fixtures clean.
    for(int plane=0;plane<2;++plane) {
        for(int y=0;y<8;++y) for(int x=0;x<8;++x) vdp1_fb_write((y*512+x)*2,0,2);
        vdp1_vblank_out();
    }
}

namespace {
void clear_sprite_fixture() {
    vdp1_hd_collect(false);vdp1_init();
    for(int plane=0;plane<2;++plane) {
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) vdp1_fb_write((y*512+x)*2,0,2);
        vdp1_vblank_out();
    }
    std::memset(g_vdp1_vram,0,sizeof g_vdp1_vram);
}
void sprite_fixture(unsigned flip,int x,int y,int local_x,int local_y,int max_x=319,int max_y=223) {
    clear_sprite_fixture();
    auto cmd=[](unsigned a,unsigned o,uint16_t v){mem_wr(g_vdp1_vram,a+o,v,2);};
    cmd(0,0,9);cmd(0,20,max_x);cmd(0,22,max_y);
    cmd(32,0,10);cmd(32,12,local_x);cmd(32,14,local_y);
    cmd(64,0,flip<<4);cmd(64,4,0xA8); // RGB555, end-code disabled.
    cmd(64,8,0x200);cmd(64,10,0x102);cmd(64,12,x);cmd(64,14,y);
    cmd(96,0,0x8000);
    for(unsigned i=0;i<16;++i) mem_wr(g_vdp1_vram,0x1000+i*2,i==2 ? 0 : 0x8001+i,2);
    vdp1_reg_write(4,1,2);vdp1_vblank_out();
}
}

TEST(normal_sprite_flips_local_coordinates_and_transparency_agree) {
    for(unsigned flip=0;flip<4;++flip) {
        sprite_fixture(flip,10,10,5,7);
        for(int y=0;y<2;++y) for(int x=0;x<8;++x) {
            const int u=flip&1 ? 7-x : x,v=flip&2 ? 1-y : y,index=v*8+u;
            CHECK_EQ(vdp1_dot(15+x,17+y,false),index==2 ? 0 : 0x8001+index);
        }
        CHECK_EQ(vdp1_dot(14,17,false),0);CHECK_EQ(vdp1_dot(23,17,false),0);
    }
    clear_sprite_fixture();
}

TEST(system_clipping_keeps_the_inclusive_edge_and_rejects_outside_pixels) {
    sprite_fixture(0,10,10,5,7,18,17);
    CHECK_EQ(vdp1_dot(15,17,false),0x8001);
    CHECK_EQ(vdp1_dot(18,17,false),0x8004);
    CHECK_EQ(vdp1_dot(19,17,false),0);CHECK_EQ(vdp1_dot(15,18,false),0);
    clear_sprite_fixture();
}

TEST(sprite_clipping_at_negative_x_retains_the_visible_source_texels) {
    sprite_fixture(0,-2,0,0,0);
    for(int x=0;x<6;++x) CHECK_EQ(vdp1_dot(x,1,false),0x8001+8+x+2);
    CHECK_EQ(vdp1_dot(6,1,false),0);
    clear_sprite_fixture();
}

TEST(internal_resolution_rerasterizes_sloped_edges_without_changing_native_framebuffer) {
    auto polygon=[] {
        clear_sprite_fixture();
        auto cmd=[](unsigned a,unsigned o,uint16_t v){mem_wr(g_vdp1_vram,a+o,v,2);};
        cmd(0,0,9);cmd(0,20,31);cmd(0,22,31);cmd(32,0,10);
        cmd(64,0,4);cmd(64,6,0x83E0);
        const int xy[]={2,2,22,9,18,23,0,15};
        for(unsigned i=0;i<8;++i) cmd(64,12+i*2,xy[i]);
        cmd(96,0,0x8000);vdp1_reg_write(4,1,2);vdp1_vblank_out();
    };
    vdp1_render_scale(1);polygon();
    uint16_t native[32*32];for(int y=0;y<32;++y) for(int x=0;x<32;++x) native[y*32+x]=vdp1_dot(x,y,false);
    for(int scale : {2,4,5}) {
        vdp1_render_scale(scale);polygon();unsigned mixed=0;
        for(int y=0;y<32;++y) for(int x=0;x<32;++x) {
            CHECK_EQ(vdp1_dot(x,y,false),native[y*32+x]);
            const auto first=vdp1_scaled_dot(x*scale,y*scale,false);
            for(int dy=0;dy<scale;++dy) for(int dx=0;dx<scale;++dx)
                mixed+=vdp1_scaled_dot(x*scale+dx,y*scale+dy,false)!=first;
        }
        CHECK_EQ(mixed>0,true); // A resized native image cannot have subpixel coverage.
        CHECK_EQ(vdp1_scaled_dot(-1,0,false),0);
        CHECK_EQ(vdp1_scaled_dot(512*scale,0,false),0);
    }
    vdp1_render_scale(1);clear_sprite_fixture();
}

TEST(internal_resolution_retains_sprite_texels_flips_and_cpu_framebuffer_writes) {
    for(int scale : {2,4,5}) for(unsigned flip=0;flip<4;++flip) {
        vdp1_render_scale(scale);sprite_fixture(flip,2,2,0,0);
        for(int y=0;y<2;++y) for(int x=0;x<8;++x) {
            const int u=flip&1 ? 7-x : x,v=flip&2 ? 1-y : y,index=v*8+u;
            for(int dy=0;dy<scale;++dy) for(int dx=0;dx<scale;++dx)
                CHECK_EQ(vdp1_scaled_dot((2+x)*scale+dx,(2+y)*scale+dy,false),index==2 ? 0 : 0x8001+index);
        }
        clear_sprite_fixture();
        const unsigned at=(5*512+5)*2;
        vdp1_fb_write(at,0x1234ABCD,4);vdp1_fb_write(at,0xFE,1);
        vdp1_vblank_out();
        for(int dy=0;dy<scale;++dy) for(int dx=0;dx<scale;++dx) {
            CHECK_EQ(vdp1_scaled_dot(5*scale+dx,5*scale+dy,false),0xFE34);
            CHECK_EQ(vdp1_scaled_dot(6*scale+dx,5*scale+dy,false),0xABCD);
        }
    }
    vdp1_render_scale(1);clear_sprite_fixture();
}

TEST(internal_resolution_composition_dimensions_and_invalid_choices_are_bounded) {
    std::memset(g_vdp2_regs,0,sizeof g_vdp2_regs);
    for(int scale : {1,2,4,5}) {
        vdp1_render_scale(scale);Frame frame;vdp2_compose(frame);
        CHECK_EQ(frame.w,320*scale);CHECK_EQ(frame.h,224*scale);
        CHECK_EQ(frame.px.size(),size_t(320*224*scale*scale));
        for(auto pixel:frame.px) CHECK_EQ(pixel,0);
    }
    vdp1_render_scale(3);CHECK_EQ(vdp1_render_scale(),1);
    vdp1_render_scale(6);CHECK_EQ(vdp1_render_scale(),1);
    vdp1_render_scale(-1);CHECK_EQ(vdp1_render_scale(),1);
}

TEST(internal_resolution_keeps_native_background_colours_and_cached_layer_order) {
    vdp1_render_scale(1);rotation_bitmap();
    Frame original;vdp2_compose(original);
    for(int scale : {2,4,5}) {
        vdp1_render_scale(scale);Frame enhanced;vdp2_compose(enhanced);
        for(int y=0;y<enhanced.h;++y) for(int x=0;x<enhanced.w;++x)
            CHECK_EQ(enhanced.px[y*enhanced.w+x],original.px[(y/scale)*original.w+x/scale]);
    }
    vdp1_render_scale(1);
}

TEST(widescreen_rotation_floor_extends_viewport_windows_but_preserves_masks_and_native_pixels) {
    vdp1_init();vdp1_gpu_available(true);vdp1_render_scale(2);vdp1_hd_collect(true);
    std::memset(g_vdp1_vram,0,sizeof g_vdp1_vram);
    auto cmd=[](unsigned a,unsigned o,uint16_t v){mem_wr(g_vdp1_vram,a+o,v,2);};
    cmd(0,0,9);cmd(0,20,351);cmd(0,22,255);cmd(32,0,10);
    for(int i=0;i<8;++i)cmd(0x200,i*2,0x83E0);
    for(int i=0;i<3;++i) {
        const unsigned at=64+i*32;cmd(at,0,2);cmd(at,4,0xA8);cmd(at,8,0x40);cmd(at,10,0x101);
        const int xy[]={10,10+i*4,18,10+i*4,18,12+i*4,10,12+i*4};
        for(int j=0;j<8;++j)cmd(at,12+j*2,xy[j]);
    }
    cmd(160,0,0x8000);vdp1_reg_write(4,1,2);vdp1_vblank_out();
    for(int width:{320,352})for(int window:{0,1})for(bool line:{false,true}) {
        rotation_bitmap();reg16(0,0x8000|(width==352));reg16(0xE0,0x20);
        const unsigned b=0xC0+window*8;
        reg16(b,0);reg16(b+4,(width-1)*2);reg16(b+2,8);reg16(b+6,215);
        reg16(0xD4,3<<(window*2));
        if(line) {
            reg16(0xD8+window*4,0x8001);reg16(0xDA+window*4,0x8000); // VRAM 0x30000.
            for(int y=0;y<224;++y) {
                mem_wr(g_vdp2_vram,0x30000+y*4,0,2);
                mem_wr(g_vdp2_vram,0x30002+y*4,(width-1)*2,2);
            }
        }
        Frame native,wide;vdp2_compose(native);CHECK_EQ(vdp2_compose_wide(wide,&native),true);
        auto at=[&](int x,int y){return wide.px[y*wide.w+x];};
        CHECK_EQ(at(0,100),0x00F800);CHECK_EQ(at(wide.w-1,100),0x00F800);
        CHECK_EQ(at(0,7),0);CHECK_EQ(at(wide.w-1,216),0);
        for(int y=0;y<native.h;++y)for(int x=0;x<native.w;++x)
            CHECK_EQ(at(x+wide.origin_x,y),native.px[y*native.w+x]);
        // A partial mask must not be interpreted as the viewport, including per-line masks.
        if(line)mem_wr(g_vdp2_vram,0x30002+100*4,(width-5)*2,2);
        else reg16(b+4,(width-5)*2);
        CHECK_EQ(vdp2_compose_wide(wide),true);
        CHECK_EQ(at(0,100),0);CHECK_EQ(at(wide.w-1,100),0);
        // Inside-window masks retain their own semantics, rather than widening a cutout.
        reg16(0xD4,2<<(window*2));CHECK_EQ(vdp2_compose_wide(wide),true);
        CHECK_EQ(at(0,100),0x00F800);CHECK_EQ(at(wide.w-1,100),0x00F800);
    }
    vdp1_gpu_available(false);vdp1_render_scale(1);vdp1_hd_collect(false);vdp1_init();
}

TEST(native_state_restores_gpu_geometry_and_shared_textures) {
    vdp1_init();vdp1_gpu_available(true);vdp1_render_scale(2);vdp1_hd_collect(true);
    std::memset(g_vdp1_vram,0,sizeof g_vdp1_vram);
    auto cmd=[](unsigned a,unsigned o,uint16_t v){mem_wr(g_vdp1_vram,a+o,v,2);};
    cmd(0,0,9);cmd(0,20,319);cmd(0,22,239);cmd(32,0,10);
    for(int i=0;i<8;++i)cmd(0x200,i*2,0x83E0);
    for(int i=0;i<3;++i) {
        const unsigned at=64+i*32;cmd(at,0,2);cmd(at,4,0xA8);cmd(at,8,0x40);cmd(at,10,0x101);
        const int xy[]={10,10+i*4,18,10+i*4,18,12+i*4,10,12+i*4};
        for(int j=0;j<8;++j)cmd(at,12+j*2,xy[j]);
    }
    cmd(160,0,0x8000);vdp1_reg_write(4,1,2);vdp1_vblank_out();
    const auto before=vdp1_hd_scene();CHECK_EQ(bool(before),true);CHECK_EQ(before->quads.size(),3u);
    State snapshot;vdp1_state(snapshot);
    // Disturb both RAM and the displayed framebuffer before restoration.
    cmd(0x200,0,0xFC00);vdp1_fb_write(0,0xFC00,2);
    snapshot.loading=true;vdp1_state(snapshot);
    const auto after=vdp1_hd_scene();CHECK_EQ(bool(after),true);CHECK_EQ(after->quads.size(),3u);
    CHECK_EQ(after->quads[0].texture==after->quads[1].texture,true);
    CHECK_EQ(after->quads[1].texture==after->quads[2].texture,true);
    CHECK_EQ(after->quads[0].texture->raw==before->quads[0].texture->raw,true);
    CHECK_EQ(std::memcmp(after->quads[0].xy,before->quads[0].xy,sizeof after->quads[0].xy),0);
    CHECK_EQ(snapshot.pos,snapshot.data.size());
    vdp1_gpu_available(false);vdp1_render_scale(1);vdp1_hd_collect(false);vdp1_init();
}
