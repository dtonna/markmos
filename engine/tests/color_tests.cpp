// color tests — plain main() + assert(), no framework
#include "../math/mm_color.h"
#include "../math/mm_math.h"
#include <cassert>
#include <cmath>
#include <cstdio>

static bool Near(f32 a, f32 b, f32 eps = 1e-5f) noexcept { return __builtin_fabsf(a - b) <= eps; }
static bool Near(const mm_math::color &a, const mm_math::color &b, f32 eps = 1e-5f) noexcept {
    return Near(a.r, b.r, eps) && Near(a.g, b.g, eps) && Near(a.b, b.b, eps) && Near(a.a, b.a, eps);
}

int main() {
    using namespace mm_math;

    // ─── Constructors ───
    {
        color c1; assert(Near(c1, color::WHITE));
        color c2(1, 0, 0); assert(Near(c2, color::RED));
        color c3(0, 1, 0, 0.5f); assert(Near(c3.g, 1) && Near(c3.a, 0.5f));
        color c4(float4{0.25f, 0.5f, 0.75f, 1}); assert(Near(c4.r, 0.25f) && Near(c4.g, 0.5f) && Near(c4.b, 0.75f));
        color c5(vec4{1, 0, 0, 1}); assert(Near(c5, color::RED));
    }

    // ─── Operators ───
    {
        color a(1, 0.5f, 0.25f, 1), b(0.5f, 0.25f, 0.125f, 0.5f);
        assert(Near(a + b, color(1.5f, 0.75f, 0.375f, 1.5f)));
        assert(Near(a - b, color(0.5f, 0.25f, 0.125f, 0.5f)));
        assert(Near(a * b, color(0.5f, 0.125f, 0.03125f, 0.5f)));
        assert(Near(a * 2.0f, color(2, 1, 0.5f, 2)));
        assert(Near(a / 2.0f, color(0.5f, 0.25f, 0.125f, 0.5f)));
        assert(Near(2.0f * a, a * 2.0f));
        a += b; assert(Near(a, color(1.5f, 0.75f, 0.375f, 1.5f)));
        a -= b; assert(Near(a, color(1, 0.5f, 0.25f, 1)));
        a *= b; assert(Near(a, color(0.5f, 0.125f, 0.03125f, 0.5f)));
        a *= 2.0f; assert(Near(a, color(1, 0.25f, 0.0625f, 1)));
        // equality
        assert(color(1, 0, 0) == color::RED);
        assert(color(1, 0, 0) != color::GREEN);
    }

    // ─── Utility Functions ───
    {
        color c(1.5f, -0.5f, 2, -1);
        assert(Near(c.clamped(), color(1, 0, 1, 0)));
        c.clamp(); assert(Near(c, color(1, 0, 1, 0)));
        color c2(0.5f, 0.5f, 0.5f, 1);
        assert(Near(c2.with_alpha(0.5f), color(0.5f, 0.5f, 0.5f, 0.5f)));
        assert(Near(c2.premultiplied(), color(0.5f, 0.5f, 0.5f, 1)));
    }

    // ─── lerp (free function) ───
    {
        color a(1, 0, 0), b(0, 1, 0);
        assert(Near(lerp(a, b, 0), a));
        assert(Near(lerp(a, b, 1), b));
        assert(Near(lerp(a, b, 0.5f), color(0.5f, 0.5f, 0)));
    }

    // ─── blend_over ───
    {
        // Opaque over opaque = src
        assert(Near(color(1, 0, 0).blend_over(color(0, 1, 0)), color(1, 0, 0)));
        // Transparent over opaque
        color semi(1, 0, 0, 0.5f);
        color result = semi.blend_over(color(0, 1, 0));
        assert(Near(result.r, 0.5f) && Near(result.g, 0.5f) && Near(result.b, 0));
        // Fully transparent
        assert(Near(color::TRANSPARENT.blend_over(color::RED), color::RED));
    }

    // ─── grayscale ───
    {
        color c(1, 0, 0);
        color g = c.grayscale();
        // 0.299*1 + 0.587*0 + 0.114*0 = 0.299
        assert(Near(g.r, 0.299f) && Near(g.g, 0.299f) && Near(g.b, 0.299f) && Near(g.a, 1));
    }

    // ─── adjust_brightness ───
    {
        color c(0.5f, 0.5f, 0.5f);
        assert(Near(c.adjust_brightness(2.0f), color(1, 1, 1)));
        assert(Near(c.adjust_brightness(0.5f), color(0.25f, 0.25f, 0.25f)));
    }

    // ─── adjust_saturation ───
    {
        color c(1, 0, 0); // Pure red
        color gray = c.grayscale(); // 0.299, 0.299, 0.299
        // saturation 0 = gray
        assert(Near(c.adjust_saturation(0), gray));
        // saturation 2 = more saturated
        color sat2 = c.adjust_saturation(2.0f);
        assert(sat2.r > c.r || Near(sat2.r, 1)); // clamped
    }

    // ─── Packed u32 conversions ───
    {
        // to_u32_rgba: 0xRRGGBBAA
        color c1(1, 0, 0, 1); // Red
        u32 rgba = c1.to_u32_rgba();
        assert(((rgba >> 24) & 0xFF) == 255); // R
        assert(((rgba >> 16) & 0xFF) == 0);   // G
        assert(((rgba >> 8) & 0xFF) == 0);    // B
        assert((rgba & 0xFF) == 255);         // A

        // to_u32_argb: 0xAARRGGBB (engine format)
        u32 argb = c1.to_u32_argb();
        assert(((argb >> 24) & 0xFF) == 255); // A
        assert(((argb >> 16) & 0xFF) == 255); // R
        assert(((argb >> 8) & 0xFF) == 0);    // G
        assert((argb & 0xFF) == 0);           // B

        // Round-trip
        color c2 = color::from_u32_argb(argb);
        assert(Near(c2, c1));
        color c3 = color::from_u32_rgba(rgba);
        assert(Near(c3, c1));
    }

    // ─── HSV conversion ───
    {
        // Red = 0° hue
        color c = color::from_hsv(0, 1, 1);
        assert(Near(c, color::RED, 1e-4f));
        // Green = 120°
        c = color::from_hsv(120, 1, 1);
        assert(Near(c, color::GREEN, 1e-4f));
        // Blue = 240°
        c = color::from_hsv(240, 1, 1);
        assert(Near(c, color::BLUE, 1e-4f));
        // White (s=0)
        c = color::from_hsv(0, 0, 1);
        assert(Near(c, color::WHITE));
        // Black (v=0)
        c = color::from_hsv(0, 1, 0);
        assert(Near(c, color::BLACK));
    }

    // ─── HSV hue wrap (h outside [0, 360)) ───
    {
        // 360° wraps to 0° (red); 480° wraps to 120° (green)
        assert(Near(color::from_hsv(360, 1, 1), color::from_hsv(0, 1, 1), 1e-4f));
        assert(Near(color::from_hsv(480, 1, 1), color::from_hsv(120, 1, 1), 1e-4f));
        assert(Near(color::from_hsv(480, 1, 1), color::GREEN, 1e-4f));
        // Negative hue wraps: -60° == 300° (magenta)
        assert(Near(color::from_hsv(-60, 1, 1), color::from_hsv(300, 1, 1), 1e-4f));
        assert(Near(color::from_hsv(-60, 1, 1), color::MAGENTA, 1e-4f));
    }

    // ─── Constants ───
    {
        assert(Near(color::WHITE, color(1, 1, 1, 1)));
        assert(Near(color::BLACK, color(0, 0, 0, 1)));
        assert(Near(color::RED, color(1, 0, 0, 1)));
        assert(Near(color::GREEN, color(0, 1, 0, 1)));
        assert(Near(color::BLUE, color(0, 0, 1, 1)));
        assert(Near(color::YELLOW, color(1, 1, 0, 1)));
        assert(Near(color::CYAN, color(0, 1, 1, 1)));
        assert(Near(color::MAGENTA, color(1, 0, 1, 1)));
        assert(Near(color::TRANSPARENT, color(0, 0, 0, 0)));
    }

    // ─── HSV clamp (output should be in [0,1]) ───
    {
        color c = color::from_hsv(360, 2, 2); // out of range inputs
        // Should not crash, output should be reasonable
        assert(c.r >= 0 && c.r <= 1);
        assert(c.g >= 0 && c.g <= 1);
        assert(c.b >= 0 && c.b <= 1);
        assert(c.a >= 0 && c.a <= 1);
    }

    printf("[color] all tests passed\n");
    return 0;
}