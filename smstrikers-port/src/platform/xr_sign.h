// strikers-quest r149 (Lance: "add the autgraphs ... that come with a 1/1. im talking virtual signature but unique to each
// one, with shit like wario clearly having bad handwriting and not using cursive, and daisy dotting her i with a heart"):
// the LANCINI TIZM 1/1 autographs. Every character signs in their own hand: a procedural handwriting model (cursive /
// print / block letter skeletons, then the character's slant, size, spacing, joins, wobble, mess, pen, i-dots,
// underline, flourishes and doodles from a per-character style table). No fonts, no images, no AI: the same strokes on
// every headset (deterministic per key).
//
// Self-contained (std only, any thread). Conventions:
//  * Box: the signature is laid out in a box kBoxAspect wide : 1 high, then x is normalised to 0..1 (so draw it into a
//    w x h region with w / h = kBoxAspect to keep it undistorted). (0,0) = the box's top-left, y grows DOWN.
//  * Point.w = the stroke's full width at that point in BOX-HEIGHT units (pixels = w * h).
//  * Point.t = seconds from the first touch of the pen; strokes are in drawing order and their times only grow, so
//    "writes itself" = draw(sig, t) with t running 0 .. sig.duration (pen lifts are gaps in t).
//  * Stroke.rgba / Signature.inkGlow = 0xRRGGBBAA. Inks are bright (gold, silver, paint colours): they read on the
//    BLACK TIZM stock. inkGlow = the glowing wet-ink colour draw() uses at the pen tip while t < duration.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace xrsign {

constexpr float kBoxAspect = 2.0f;  // box width / box height the strokes are laid out for

struct Point { float x, y, t, w; };            // 0..1 card-local box coords, seconds from start, stroke width (box units)
struct Stroke { std::vector<Point> pts; uint32_t rgba; };
struct Signature { std::vector<Stroke> strokes; float duration; uint32_t inkGlow; };

// deterministic per key; name = the card's display name ("Ghost Rider III" -> signs "Ghost Rider"). Built once per key
// and cached (the reference stays valid for the life of the process). Thread-safe.
const Signature& get(const std::string& key, const std::string& name);

// draw into an RGBA image (w x h, straight alpha) up to time t (t >= duration = finished), anti-aliased. Composites
// "over" what is already in the image (clear it to 0 for a transparent layer). While t < duration the freshest ink
// glows (inkGlow) and a soft glow sits on the pen tip. stride = bytes per row.
void draw(const Signature& s, float t, uint8_t* rgba, int w, int h, int stride);

}  // namespace xrsign
