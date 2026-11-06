#include "widescreen-bounds.h"
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <initializer_list>

static void check(bool value) { if (!value) std::abort(); }
int main() {
    using sf3::HorizontalBounds;
    HorizontalBounds out{};
    check(sf3::expanded_visibility_bounds(true,320,427,53,{-160,159},out));
    check(out.left == -213 && out.right == 213);
    // Preserve every original pixel; add exactly the requested side columns.
    for (int width : {320,352,640,704}) {
        int wide=(width*4+2)/3, extra=wide-width;
        HorizontalBounds in{int16_t(-width/2),int16_t(width/2-1)};
        check(sf3::expanded_visibility_bounds(true,width,wide,extra/2,in,out));
        check(out.right-out.left+1 == wide);
        check(in.left-out.left == extra/2);
        check(out.right-in.right == extra-extra/2);
        for(int x=in.left;x<=in.right;++x) check(x>=out.left && x<=out.right);
        check(!sf3::expanded_visibility_bounds(false,width,wide,extra/2,in,out));
        check(out.left==in.left && out.right==in.right);
    }
    for (auto in : {HorizontalBounds{-159,159},HorizontalBounds{-160,158},
                    HorizontalBounds{0,319},HorizontalBounds{10,20}}) {
        check(!sf3::expanded_visibility_bounds(true,320,427,53,in,out));
        check(out.left==in.left && out.right==in.right);
    }
    check(!sf3::expanded_visibility_bounds(true,320,320,0,{-160,159},out));
    check(!sf3::expanded_visibility_bounds(true,320,427,54,{-160,159},out));
    check(!sf3::expanded_visibility_bounds(true,0,427,53,{-160,159},out));
    check(!sf3::expanded_visibility_bounds(true,INT_MAX,INT_MAX,0,{-160,159},out));
    check(!sf3::expanded_visibility_bounds(true,320,INT_MAX,53,{-160,159},out));
    std::puts("widescreen bounds passed (synthetic; no scenario gameplay)");
}
