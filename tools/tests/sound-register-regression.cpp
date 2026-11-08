// Invented register values only. MADRS has 32 words, not 64 mirrored words.
#include "sound.h"
#include "test.h"

TEST(scsp_reserved_addresses_do_not_overwrite_any_delay_tap) {
    scsp_reset();
    for(unsigned i=0;i<32;++i)scsp_write(0x780+i*2,0x1200+i,2);
    for(unsigned i=0;i<64;++i)scsp_write(0x7C0+i,i,1);
    for(unsigned i=0;i<32;++i) {
        CHECK_EQ(scsp_read(0x780+i*2,2),0x1200u+i);
        CHECK_EQ(scsp_read(0x7C0+i*2,2),0u);
    }
    scsp_write(0x7BE,0xABCD,2);scsp_write(0x7BF,0xEF,1);
    CHECK_EQ(scsp_read(0x7BE,2),0xABEFu);
    scsp_write(0x7C0,0xDEADBEEF,4);
    CHECK_EQ(scsp_read(0x780,2),0x1200u);
    CHECK_EQ(scsp_read(0x7BE,2),0xABEFu);
}
