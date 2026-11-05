// strikers-quest r149: the LANCINI TIZM 1/1 autographs (xr_sign.h). A small handwriting model: hand-authored cursive /
// print / block letter skeletons and doodles (xr_sign_data.inc), laid out in the character's hand from a per-character
// style table (slant, size, joins, wobble, mess, pen, i-dots, underline, flourishes, doodles), smoothed, fitted to the
// box, timed stroke by stroke and given a pen's width. No fonts, no images, no AI; the same on every headset.
//
// C:\StrikersMR\tools\outside\sign_proof.py is this file's Python mirror (same data, same RNG, same steps in the same
// order) and renders the PC proof sheets: keep the two in step. Plain C++11, std only (built in strikers_platform).
#include "xr_sign.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace xrsign {
namespace {

#include "xr_sign_data.inc"

const double kSignPi = 3.14159265358979323846;
const double kAspect = (double)kBoxAspect;

// ----------------------------------------------------------------------------------------------------------- data
struct CP { double x, y, c; };  // control point; c = corner flag (or a disc's diameter)
struct GStroke { char kind; std::vector<CP> pts; };  // kind 0 ink, '.' tittle, '-' cross, '*' dot, 'O' discs
struct Glyph { double adv; bool joins, noin; std::vector<GStroke> strokes; };
typedef std::vector<std::pair<std::string, std::string> > KV;
typedef std::map<std::string, std::string> Params;

struct Data {
  std::map<std::string, Glyph> glyphs;
  std::map<std::string, std::vector<GStroke> > doodles;
  KV defaults;
  std::map<std::string, KV> presets, styles, types, moods;
  std::map<std::string, std::string> poke;
};

std::vector<std::string> tokens(const std::string& line) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];
    if (c == ' ' || c == '\t' || c == '\r') {
      if (!cur.empty()) out.push_back(cur);
      cur.clear();
    } else {
      cur += c;
    }
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}

std::vector<std::string> lines_of(const char* text) {
  std::vector<std::string> out;
  std::string cur;
  for (const char* p = text; *p; ++p) {
    if (*p == '\n') {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += *p;
    }
  }
  out.push_back(cur);
  return out;
}

// Like Python's str.split(c): keeps empty pieces.
std::vector<std::string> split(const std::string& s, char c) {
  std::vector<std::string> out;
  std::string cur;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == c) {
      out.push_back(cur);
      cur.clear();
    } else {
      cur += s[i];
    }
  }
  out.push_back(cur);
  return out;
}

bool blank(const std::string& line) {
  for (size_t i = 0; i < line.size(); ++i)
    if (line[i] != ' ' && line[i] != '\t' && line[i] != '\r') return false;
  return true;
}

std::vector<GStroke> parse_strokes(const std::vector<std::string>& t, size_t from) {
  std::vector<GStroke> out;
  GStroke cur;
  cur.kind = 0;
  for (size_t i = from; i < t.size(); ++i) {
    const std::string& tok = t[i];
    if (tok == "/") {
      out.push_back(cur);
      cur = GStroke();
      cur.kind = 0;
      continue;
    }
    if ((tok == "." || tok == "-" || tok == "*" || tok == "O") && cur.pts.empty() && cur.kind == 0) {
      cur.kind = tok[0];
      continue;
    }
    std::string s = tok;
    bool corner = false;
    if (!s.empty() && s[s.size() - 1] == '!') {
      corner = true;
      s.erase(s.size() - 1);
    }
    double v[3] = {0.0, 0.0, 0.0};
    std::vector<std::string> parts = split(s, ',');
    for (size_t k = 0; k < parts.size() && k < 3; ++k) v[k] = std::strtod(parts[k].c_str(), nullptr);
    CP p;
    p.x = v[0];
    p.y = v[1];
    p.c = (cur.kind == 'O') ? v[2] : (corner ? 1.0 : 0.0);
    cur.pts.push_back(p);
  }
  out.push_back(cur);
  return out;
}

void parse_kv(const std::vector<std::string>& t, size_t from, KV& into) {
  for (size_t i = from; i < t.size(); ++i) {
    size_t eq = t[i].find('=');
    if (eq == std::string::npos) continue;
    into.push_back(std::make_pair(t[i].substr(0, eq), t[i].substr(eq + 1)));
  }
}

Data* build_data() {
  Data* d = new Data();
  std::vector<std::string> ls = lines_of(kGlyphData);
  for (size_t i = 0; i < ls.size(); ++i) {
    if (blank(ls[i]) || ls[i][0] == '#') continue;
    std::vector<std::string> t = tokens(ls[i]);
    if (t.size() < 4) continue;
    Glyph g;
    g.adv = std::strtod(t[1].c_str(), nullptr);
    g.joins = t[2].find('j') != std::string::npos;
    g.noin = t[2].find('n') != std::string::npos;
    g.strokes = parse_strokes(t, 3);
    d->glyphs[t[0]] = g;
  }
  ls = lines_of(kDoodleData);
  for (size_t i = 0; i < ls.size(); ++i) {
    if (blank(ls[i]) || ls[i][0] == '#') continue;
    std::vector<std::string> t = tokens(ls[i]);
    if (t.size() < 2) continue;
    d->doodles[t[0]] = parse_strokes(t, 1);
  }
  ls = lines_of(kStyleData);
  for (size_t i = 0; i < ls.size(); ++i) {
    if (blank(ls[i]) || ls[i][0] == '#') continue;
    std::vector<std::string> t = tokens(ls[i]);
    if (t.empty()) continue;
    if (t[0] == "@defaults") {
      parse_kv(t, 1, d->defaults);
    } else if (t[0][0] == '@') {
      KV kv;
      parse_kv(t, 1, kv);
      d->presets[t[0].substr(1)] = kv;
    } else {
      KV kv;
      parse_kv(t, 1, kv);
      d->styles[t[0]] = kv;
    }
  }
  ls = lines_of(kPokeData);
  for (size_t i = 0; i < ls.size(); ++i) {
    if (blank(ls[i]) || ls[i][0] == '#') continue;
    std::vector<std::string> t = tokens(ls[i]);
    if (t.empty()) continue;
    if (t[0] == "@type" && t.size() >= 2) {
      KV kv;
      parse_kv(t, 2, kv);
      d->types[t[1]] = kv;
    } else if (t[0] == "@mood" && t.size() >= 2) {
      KV kv;
      parse_kv(t, 2, kv);
      d->moods[t[1]] = kv;
    } else {
      for (size_t k = 0; k < t.size(); ++k) {
        size_t c = t[k].find(':');
        if (c != std::string::npos) d->poke[t[k].substr(0, c)] = t[k].substr(c + 1);
      }
    }
  }
  return d;
}

const Data& D() {
  static Data* d = build_data();  // thread-safe static init (C++11)
  return *d;
}

// ----------------------------------------------------------------------------------------------------------- rng
uint32_t fnv(const std::string& s) {
  uint32_t h = 0x811C9DC5u;
  for (size_t i = 0; i < s.size(); ++i) {
    h ^= (uint8_t)s[i];
    h *= 0x01000193u;
  }
  return h;
}

struct Rng {
  uint32_t s;
  explicit Rng(uint32_t seed) : s(seed ? seed : 0x9E3779B9u) {}
  uint32_t u32() {
    uint32_t x = s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s = x;
    return x;
  }
  double f() { return (double)(u32() >> 8) * (1.0 / 16777216.0); }
  double n() { return f() * 2.0 - 1.0; }
  double r(double a, double b) { return a + (b - a) * f(); }
};

struct Noise {
  double k[32];
  void init(Rng& rng) {
    for (int i = 0; i < 32; ++i) k[i] = rng.n();
  }
  double at(double x) const {
    double i = std::floor(x);
    double f = x - i;
    double s = f * f * (3.0 - 2.0 * f);
    long long ii = (long long)i;
    double a = k[ii & 31];
    double b = k[(ii + 1) & 31];
    return a + (b - a) * s;
  }
};

// ----------------------------------------------------------------------------------------------------------- style
std::string sv(const Params& P, const char* k, const char* dflt = "") {
  Params::const_iterator it = P.find(k);
  return it == P.end() ? std::string(dflt) : it->second;
}

double fv(const Params& P, const char* k) {
  Params::const_iterator it = P.find(k);
  if (it == P.end() || it->second.empty()) return 0.0;
  return std::strtod(it->second.c_str(), nullptr);
}

uint32_t colv(const Params& P, const char* k, uint32_t dflt) {
  Params::const_iterator it = P.find(k);
  if (it == P.end() || it->second.empty()) return dflt;
  return (uint32_t)std::strtoul(it->second.c_str(), nullptr, 16);
}

void apply_style(Params& P, const KV& d) {
  std::string use;
  for (size_t i = 0; i < d.size(); ++i)
    if (d[i].first == "use") use = d[i].second;
  if (!use.empty()) {
    std::vector<std::string> us = split(use, '+');
    for (size_t i = 0; i < us.size(); ++i) {
      std::map<std::string, KV>::const_iterator it = D().presets.find(us[i]);
      if (it != D().presets.end()) apply_style(P, it->second);
    }
  }
  for (size_t i = 0; i < d.size(); ++i) {
    if (d[i].first == "use") continue;
    if (d[i].first == "doo+") {
      std::string v = P["doo"];
      v = v.empty() ? d[i].second : v + "," + d[i].second;
      P["doo"] = v;
    } else {
      P[d[i].first] = d[i].second;
    }
  }
}

bool starts(const std::string& s, const char* p) { return s.compare(0, std::strlen(p), p) == 0; }
bool ends(const std::string& s, const char* p) {
  size_t n = std::strlen(p);
  return s.size() >= n && s.compare(s.size() - n, n, p) == 0;
}

// The style for a key: defaults <- group rule <- presets <- the key's own entry.
Params resolve(const std::string& key) {
  const Data& d = D();
  Params P;
  apply_style(P, d.defaults);
  std::vector<const KV*> chain;
  std::string base = key;
  if (starts(key, "mua_")) {
    std::string b = key;
    const char* sufs[] = {"_c2", "_c3", "_c4", "_c5"};
    for (int i = 0; i < 4; ++i)
      if (ends(b, sufs[i])) b = b.substr(0, b.size() - 3);
    base = b;
  }
  if (starts(key, "strikers_")) base = key.substr(9);
  const KV* entry = nullptr;
  std::map<std::string, KV>::const_iterator it = d.styles.find(key);
  if (it != d.styles.end() && !it->second.empty()) entry = &it->second;
  if (!entry) {
    it = d.styles.find(base);
    if (it != d.styles.end()) entry = &it->second;
  }
  static const KV kEmpty;
  if (starts(key, "pk_")) {
    std::map<std::string, std::string>::const_iterator pc = d.poke.find(key.substr(3));
    std::string code = pc == d.poke.end() ? std::string() : pc->second;
    if (!code.empty()) {
      std::map<std::string, KV>::const_iterator ty = d.types.find(code.substr(0, 1));
      chain.push_back(ty == d.types.end() ? &kEmpty : &ty->second);
      std::map<std::string, KV>::const_iterator mo = d.moods.find(code.size() > 1 ? code.substr(1, 1) : std::string());
      chain.push_back(mo == d.moods.end() ? &kEmpty : &mo->second);
    } else {
      std::map<std::string, KV>::const_iterator pr = d.presets.find("pk_generic");
      chain.push_back(pr == d.presets.end() ? &kEmpty : &pr->second);
    }
  } else if (!entry) {
    std::map<std::string, KV>::const_iterator pr = d.presets.find(starts(key, "mua_") ? "marvel_generic" : "generic");
    chain.push_back(pr == d.presets.end() ? &kEmpty : &pr->second);
  }
  if (entry) chain.push_back(entry);
  for (size_t i = 0; i < chain.size(); ++i) apply_style(P, *chain[i]);
  return P;
}

// ----------------------------------------------------------------------------------------------------------- strokes
enum Role { R_NAME, R_DOT, R_LICK, R_BLOT, R_XOUT, R_FLOUR, R_DOODLE, R_XTEXT, R_HALO };

struct Stk {
  char kind;
  std::vector<CP> pts;
  int li;
  int role;
  double wmul;
  double wcap;
  std::vector<CP> dense;
};

Stk mk(char kind, const std::vector<CP>& pts, int li, int role) {
  Stk s;
  s.kind = kind;
  s.pts = pts;
  s.li = li;
  s.role = role;
  s.wmul = 1.0;
  s.wcap = 0.0;
  return s;
}

CP cp(double x, double y, double c) {
  CP p;
  p.x = x;
  p.y = y;
  p.c = c;
  return p;
}

std::string clean_text(const std::string& name) {
  size_t a = 0, b = name.size();
  while (a < b && std::isspace((unsigned char)name[a])) ++a;
  while (b > a && std::isspace((unsigned char)name[b - 1])) --b;
  std::string t = name.substr(a, b - a);
  const char* sufs[] = {" II", " III", " IV", " V"};
  for (int i = 0; i < 4; ++i)
    if (ends(t, sufs[i])) t = t.substr(0, t.size() - std::strlen(sufs[i]));
  if (starts(t, "The ")) t = t.substr(4);
  return t;
}

bool has_glyph(const std::string& id) { return D().glyphs.find(id) != D().glyphs.end(); }

std::string glyph_for(char ch, const Params& P, char fam) {
  if (ch == ' ') return std::string();
  std::string gl = sv(P, "gl");
  if (!gl.empty()) {
    std::vector<std::string> pairs = split(gl, ';');
    for (size_t i = 0; i < pairs.size(); ++i) {
      const std::string& pr = pairs[i];
      if (pr.size() > 2 && pr[0] == ch && pr[1] == ':' && has_glyph(pr.substr(2))) return pr.substr(2);
    }
  }
  if (fam == 'b') {
    ch = (char)std::toupper((unsigned char)ch);
    fam = 'p';
  }
  std::string gid = std::string(1, fam) + ch;
  if (has_glyph(gid)) return gid;
  gid = std::string("p") + ch;
  if (has_glyph(gid)) return gid;
  gid = std::string("p") + (char)std::toupper((unsigned char)ch);
  if (has_glyph(gid)) return gid;
  return std::string();
}

std::string join_words(const std::vector<std::string>& w, size_t a, size_t b) {
  std::string s;
  for (size_t i = a; i < b; ++i) {
    if (i > a) s += ' ';
    s += w[i];
  }
  return s;
}

// Lay the text out (text units: x-height 10, y up, the first line's baseline at 0). base = the last line's baseline.
std::vector<Stk> layout_text(const std::string& text, const Params& P, Rng& rng, double& baseOut) {
  const char fam = sv(P, "fam", "c").empty() ? 'c' : sv(P, "fam", "c")[0];
  const double wid = fv(P, "wid"), capr = fv(P, "capr"), asc = fv(P, "asc"), spc = fv(P, "spc");
  const double szj = fv(P, "szj"), bnc = fv(P, "bnc"), rotj = fv(P, "rotj"), scrawl = fv(P, "scrawl");
  const double wgap = fv(P, "wgap"), spj = fv(P, "spj");
  int nlines = (int)fv(P, "lines");
  std::vector<std::string> words = split(text, ' ');
  if (nlines == 0) nlines = (text.size() > 11 && words.size() > 1) ? 2 : 1;
  std::vector<std::string> lines;
  if (nlines == 2 && words.size() > 1) {
    double best = 1e9;
    size_t bi = 1;
    for (size_t i = 1; i < words.size(); ++i) {
      double a = (double)join_words(words, 0, i).size();
      double b = (double)join_words(words, i, words.size()).size();
      if (std::fabs(a - b) < best) {
        best = std::fabs(a - b);
        bi = i;
      }
    }
    lines.push_back(join_words(words, 0, bi));
    lines.push_back(join_words(words, bi, words.size()));
  } else {
    lines.push_back(text);
    nlines = 1;
  }
  // the crossed-out letter (Wario) and the backwards letter (a kid)
  std::string xout = sv(P, "xout");
  int xo_at = -1;
  char xo_ch = 0;
  if (xout.size() >= 2) {
    xo_at = std::atoi(xout.substr(0, xout.size() - 1).c_str());
    xo_ch = xout[xout.size() - 1];
  }
  int kid_at = -1;
  if (fv(P, "kid") > 0.0) {
    std::vector<int> cands;
    for (size_t i = 0; i < text.size(); ++i)
      if (std::strchr("RSNZszk", text[i]) && text[i]) cands.push_back((int)i);
    if (!cands.empty()) {
      int n = (int)cands.size();
      kid_at = cands[(int)(rng.f() * n) % n];
    }
  }
  const std::string tx = sv(P, "tx", "n");
  std::vector<Stk> out;
  int li = 0, gi = 0;
  double base = 0.0, shift = 0.0;
  const double lineGap = 23.0 * std::max(1.0, capr * 0.9);
  for (size_t ln = 0; ln < lines.size(); ++ln) {
    double penx = shift;
    base = -(double)ln * lineGap;
    std::vector<std::string> lw = split(lines[ln], ' ');
    for (size_t wi = 0; wi < lw.size(); ++wi) {
      const std::string& word = lw[wi];
      Stk cur;
      bool hasCur = false, prevj = false;
      std::vector<Stk> deferred;
      std::vector<std::pair<char, int> > seq;
      for (size_t ci = 0; ci < word.size(); ++ci) {
        if (gi == xo_at && xo_ch) seq.push_back(std::make_pair(xo_ch, -2));
        seq.push_back(std::make_pair(word[ci], gi));
        gi += 1;
      }
      gi += 1;
      for (size_t si = 0; si < seq.size(); ++si) {
        const bool first_in_word = si == 0;
        const char ch = seq[si].first;
        const int tag = seq[si].second;
        std::string gid = glyph_for(ch, P, fam);
        if (gid.empty()) {
          penx += 5.0 * wid;
          prevj = false;
          if (hasCur) {
            out.push_back(cur);
            hasCur = false;
          }
          continue;
        }
        const Glyph& g = D().glyphs.find(gid)->second;
        const double s = 1.0 + szj * rng.n();
        const double bounce = bnc * rng.n();
        const double rot = rotj * rng.n();
        const bool mirror = tag == kid_at;
        const bool lower = std::islower((unsigned char)ch) && fam != 'b';
        double ys = 1.0;
        if (fam == 'b')
          ys = first_in_word ? capr : 1.0;
        else if (std::isupper((unsigned char)ch))
          ys = capr;
        const double aw = g.adv * wid * s, cxr = aw * 0.5, cyr = 5.0 * s;
        const double cr = std::cos(rot), sr = std::sin(rot);
        const double adv = g.adv;
        const double basey = base;
        const double px0 = penx;
        auto T = [&](double px, double py, double& ox, double& oy) {
          double y = py;
          if (lower) {
            if (y > 10.0)
              y = 10.0 + (y - 10.0) * asc;
            else if (y < 0.0)
              y = y * asc;
            if (scrawl > 0.0 && py >= 0.0 && py <= 10.0) y = 5.0 + (y - 5.0) * (1.0 - scrawl);
          }
          y *= ys;
          double x = px * wid;
          if (mirror) x = adv * wid - x;
          x *= s;
          y *= s;
          double dx = x - cxr, dy = y - cyr;
          x = cxr + dx * cr - dy * sr;
          y = cyr + dx * sr + dy * cr;
          ox = x + px0;
          oy = y + basey + bounce;
        };
        bool first_main = true;
        const size_t gstart = out.size();
        for (size_t k = 0; k < g.strokes.size(); ++k) {
          const GStroke& gs = g.strokes[k];
          std::vector<CP> tp;
          for (size_t q = 0; q < gs.pts.size(); ++q) {
            double x, y;
            T(gs.pts[q].x, gs.pts[q].y, x, y);
            tp.push_back(cp(x, y, gs.kind == 'O' ? gs.pts[q].c * s : gs.pts[q].c));
          }
          if (gs.kind == 0) {
            if (fam == 'c' && first_main && hasCur && prevj && !g.noin) {
              cur.pts.insert(cur.pts.end(), tp.begin(), tp.end());
            } else {
              if (hasCur) out.push_back(cur);
              cur = mk(0, tp, li, R_NAME);
              hasCur = true;
            }
            first_main = false;
          } else if ((gs.kind == '.' || gs.kind == '-') && fam == 'c') {
            deferred.push_back(mk(gs.kind, tp, li, R_NAME));
          } else {
            if (hasCur) {
              out.push_back(cur);
              hasCur = false;
            }
            out.push_back(mk(gs.kind, tp, li, R_NAME));
          }
        }
        if (tag == -2) {
          // the wrong letter: finish it, then scribble it out
          if (hasCur) {
            out.push_back(cur);
            hasCur = false;
          }
          bool any = false;
          double bx0 = 0, bx1 = 0, by0 = 0, by1 = 0;
          for (size_t k = gstart; k < out.size(); ++k)
            for (size_t q = 0; q < out[k].pts.size(); ++q) {
              const CP& p = out[k].pts[q];
              if (!any) {
                bx0 = bx1 = p.x;
                by0 = by1 = p.y;
                any = true;
              }
              bx0 = std::min(bx0, p.x);
              bx1 = std::max(bx1, p.x);
              by0 = std::min(by0, p.y);
              by1 = std::max(by1, p.y);
            }
          if (any) {
            std::vector<CP> sc;
            const int n = 8;
            for (int k = 0; k < n; ++k) {
              double fx = bx0 - 1.0 + (bx1 - bx0 + 2.0) * (k / (n - 1.0));
              double yy = (k % 2 == 0) ? by1 + 1.0 : by0 - 0.5;
              double jx = rng.n() * 0.6;
              double jy = rng.n() * 1.2;
              sc.push_back(cp(fx + jx, yy + jy, 1.0));
            }
            Stk xs = mk(0, sc, li, R_XOUT);
            xs.wmul = 0.6;
            out.push_back(xs);
          }
          prevj = false;
        } else {
          if (fam != 'c' || !g.joins) {
            if (hasCur) {
              out.push_back(cur);
              hasCur = false;
            }
          }
          prevj = fam == 'c' && g.joins;
        }
        penx += aw + spc + spj * rng.n();
        li += 1;
      }
      if (hasCur) out.push_back(cur);
      // cursive writers dot the i's and cross the t's when the word is done
      const double wend = penx;
      for (size_t k = 0; k < deferred.size(); ++k) {
        Stk& d = deferred[k];
        if (d.kind == '-' && d.pts.size() >= 2 &&
            std::fabs(d.pts.back().y - d.pts[0].y) < std::fabs(d.pts.back().x - d.pts[0].x)) {
          if (tx == "l") {
            d.pts.back().x = std::max(d.pts.back().x, wend + 2.0);
            d.pts.back().y += 1.2;
          } else if (tx == "s") {
            d.pts[0].y -= 1.6;
            d.pts.back().y += 1.6;
          }
        }
        out.push_back(d);
      }
      penx += wgap * wid;
    }
    if (ln == 0 && nlines == 2) shift = penx * 0.22;
  }
  baseOut = base;
  return out;
}

void place_doodle(const std::string& name, double cx, double cy, double size, Rng& rng, int role, bool fixedRot,
                  double rotv, double sx, std::vector<Stk>& out) {
  std::map<std::string, std::vector<GStroke> >::const_iterator it = D().doodles.find(name);
  if (it == D().doodles.end()) return;
  const double sc = size / 10.0;
  const double a = fixedRot ? rotv : rng.n() * 0.12;
  const double ca = std::cos(a), sa = std::sin(a);
  for (size_t k = 0; k < it->second.size(); ++k) {
    const GStroke& gs = it->second[k];
    std::vector<CP> tp;
    for (size_t q = 0; q < gs.pts.size(); ++q) {
      const CP& p = gs.pts[q];
      double dx = (p.x - 5.0) * sc * sx;
      double dy = (p.y - 5.0) * sc;
      double x = cx + dx * ca - dy * sa;
      double y = cy + dx * sa + dy * ca;
      if (gs.kind == 'O') {
        tp.push_back(cp(x, y, p.c * sc));
      } else {
        double jx = rng.n() * 0.12 * sc * 10.0 * 0.1;
        double jy = rng.n() * 0.12 * sc;
        tp.push_back(cp(x + jx, y + jy, p.c));
      }
    }
    Stk st = mk((gs.kind == 'O' || gs.kind == '*') ? gs.kind : 0, tp, 0, role);
    st.wcap = size * 0.085;
    out.push_back(st);
  }
}

struct Shaped {
  std::vector<Stk> st;
  double base, rise, wob, wobf;
  Noise noise;
  double blf(double x) const { return base + rise * x + wob * noise.at(x * wobf / 40.0); }
};

// The text laid out and put through the hand: bow, overshoot, slant, rise, wobble, mess, the style's i-dots.
void shape_text(const std::string& text, const Params& P, Rng& rng, Shaped& sh) {
  double base = 0.0;
  std::vector<Stk> strokes = layout_text(text, P, rng, base);
  const double bow = fv(P, "bow");
  const double ovr = sv(P, "fam", "c") != "c" ? fv(P, "ovr") : 0.0;
  for (size_t si = 0; si < strokes.size(); ++si) {
    Stk& s = strokes[si];
    if (s.kind != 0 || s.pts.size() < 2) continue;
    std::vector<CP> np;
    np.push_back(s.pts[0]);
    for (size_t i = 1; i < s.pts.size(); ++i) {
      const CP a = s.pts[i - 1], b = s.pts[i];
      double dx = b.x - a.x, dy = b.y - a.y;
      double L = std::sqrt(dx * dx + dy * dy);
      if (bow > 0.0 && L > 6.0) {
        double o = bow * L * 0.05 * rng.n();
        np.push_back(cp(a.x + dx * 0.5 - dy / L * o, a.y + dy * 0.5 + dx / L * o, 0.0));
      }
      np.push_back(b);
    }
    if (ovr > 0.0) {
      for (int e = 0; e < 2; ++e) {
        CP& p = e == 0 ? np[0] : np[np.size() - 1];
        const CP& q = e == 0 ? np[1] : np[np.size() - 2];
        double dx = p.x - q.x, dy = p.y - q.y;
        double L = std::sqrt(dx * dx + dy * dy);
        if (L > 1e-6) {
          double m = ovr * rng.n() / L;
          p.x += dx * m;
          p.y += dy * m;
        }
      }
    }
    s.pts = np;
  }
  const double slant = fv(P, "slant"), rise = fv(P, "rise"), wob = fv(P, "wob"), wobf = fv(P, "wobf");
  const double jit = fv(P, "jit"), pix = fv(P, "pix");
  sh.noise.init(rng);
  for (size_t si = 0; si < strokes.size(); ++si) {
    Stk& s = strokes[si];
    const double jm = (s.kind == '.' || s.kind == '*' || s.kind == 'O') ? 0.5 : 1.0;
    for (size_t i = 0; i < s.pts.size(); ++i) {
      CP& p = s.pts[i];
      double x = p.x, y = p.y;
      x += slant * y;
      y += rise * x + wob * sh.noise.at(x * wobf / 40.0);
      x += jit * jm * rng.n();
      y += jit * jm * rng.n();
      if (pix > 0.0 && s.kind != 'O') {
        x = std::nearbyint(x / pix) * pix;
        y = std::nearbyint(y / pix) * pix;
        p.c = 1.0;
      }
      p.x = x;
      p.y = y;
    }
  }
  sh.base = base;
  sh.rise = rise;
  sh.wob = wob;
  sh.wobf = wobf;
  // the tittles: the style's dot
  const std::string dot = sv(P, "dot", "d");
  const double dotx = fv(P, "dotx");
  sh.st.clear();
  for (size_t si = 0; si < strokes.size(); ++si) {
    Stk& s = strokes[si];
    if (s.kind != '.') {
      sh.st.push_back(s);
      continue;
    }
    double x = s.pts[0].x + dotx + rng.n() * 0.5;
    double y = s.pts[0].y + rng.n() * 0.5;
    if (dot == "n") continue;
    if (dot == "d") {
      std::vector<CP> v;
      v.push_back(cp(x, y, 0));
      v.push_back(cp(x + 0.6, y + 0.45, 0));
      Stk d = mk(0, v, s.li, R_DOT);
      d.wmul = 1.35;
      sh.st.push_back(d);
    } else if (dot == "l") {
      std::vector<CP> v;
      v.push_back(cp(x - 1.6, y - 0.8, 0));
      v.push_back(cp(x + 2.2, y + 1.0, 0));
      sh.st.push_back(mk(0, v, s.li, R_DOT));
    } else if (dot == "o" || dot == "c") {
      const double r = dot == "o" ? 1.5 : 2.3;
      std::vector<CP> v;
      for (int i = 0; i < 8; ++i) {
        double a = kSignPi * 0.5 + i * (2.0 * kSignPi / 7.0);
        v.push_back(cp(x + r * std::cos(a), y + r + r * std::sin(a), 0.0));
      }
      Stk d = mk(0, v, s.li, R_DOT);
      d.wmul = 0.8;
      sh.st.push_back(d);
    } else if (dot == "b") {
      std::vector<CP> v;
      v.push_back(cp(x, y + 0.6, 2.8));
      sh.st.push_back(mk('O', v, s.li, R_DOT));
    } else {
      const char* nm = dot == "h" ? "heart" : dot == "s" ? "star" : dot == "f" ? "flame" : dot == "k" ? "skull"
                     : dot == "z" ? "bolt" : "star";
      std::vector<Stk> ds;
      place_doodle(nm, x + 0.4, y + 2.0, 5.5, rng, R_DOT, false, 0.0, 1.0, ds);
      for (size_t k = 0; k < ds.size(); ++k) {
        ds[k].li = s.li;
        ds[k].wmul = 0.75;
        sh.st.push_back(ds[k]);
      }
    }
  }
}

void bbox(const std::vector<Stk>& v, double& x0, double& x1, double& y0, double& y1) {
  bool any = false;
  for (size_t i = 0; i < v.size(); ++i)
    for (size_t k = 0; k < v[i].pts.size(); ++k) {
      const CP& p = v[i].pts[k];
      if (!any) {
        x0 = x1 = p.x;
        y0 = y1 = p.y;
        any = true;
      }
      x0 = std::min(x0, p.x);
      x1 = std::max(x1, p.x);
      y0 = std::min(y0, p.y);
      y1 = std::max(y1, p.y);
    }
  if (!any) {
    x0 = 0.0;
    x1 = 1.0;
    y0 = 0.0;
    y1 = 1.0;
  }
}

std::vector<CP> cycloid_loops(double xs, double xe, double y0, double r, int k, Rng& rng) {
  std::vector<CP> pts;
  const double c = (xe - xs) / (k * 2.0 * kSignPi);
  const int n = k * 6;
  for (int i = 0; i <= n; ++i) {
    double t = i * (2.0 * kSignPi / 6.0);
    double x = xs + c * t - r * 0.9 * std::sin(t);
    double y = y0 - r * std::cos(t) * 0.55 + r * 0.55;
    double jx = rng.n() * 0.3;
    double jy = rng.n() * 0.3;
    pts.push_back(cp(x + jx, y + jy, 0.0));
  }
  return pts;
}

void underline(const std::string& kind, double x0, double x1, const Shaped& sh, Rng& rng, std::vector<Stk>& out) {
  const double W = std::max(10.0, x1 - x0);
  auto blf = [&](double x) { return sh.blf(x); };
  auto j = [&]() { return rng.n() * 0.5; };
  std::vector<CP> v;
  if (kind == "line") {
    v.push_back(cp(x0 + 0.04 * W, blf(x0) - 3.2, 0));
    double j1 = j();
    v.push_back(cp(x0 + 0.5 * W, blf(x0 + 0.5 * W) - 3.9 + j1, 0));
    double j2 = j();
    v.push_back(cp(x1 + 2.0, blf(x1) - 2.8 + j2, 0));
    out.push_back(mk(0, v, 0, R_FLOUR));
  } else if (kind == "double") {
    v.push_back(cp(x0 + 0.02 * W, blf(x0) - 3.0, 0));
    double j1 = j();
    v.push_back(cp(x0 + 0.5 * W, blf(x0 + 0.5 * W) - 3.6 + j1, 0));
    v.push_back(cp(x1 + 2.0, blf(x1) - 2.6, 0));
    out.push_back(mk(0, v, 0, R_FLOUR));
    v.clear();
    v.push_back(cp(x0 + 0.12 * W, blf(x0) - 5.8, 0));
    double j2 = j();
    v.push_back(cp(x0 + 0.55 * W, blf(x0 + 0.5 * W) - 6.4 + j2, 0));
    v.push_back(cp(x1 - 0.05 * W, blf(x1) - 5.6, 0));
    out.push_back(mk(0, v, 0, R_FLOUR));
  } else if (kind == "swoosh") {
    v.push_back(cp(x0 - 1.0, blf(x0) - 1.6, 0));
    double j1 = j();
    v.push_back(cp(x0 + 0.25 * W, blf(x0 + 0.25 * W) - 4.8 + j1, 0));
    double j2 = j();
    v.push_back(cp(x0 + 0.7 * W, blf(x0 + 0.7 * W) - 4.6 + j2, 0));
    v.push_back(cp(x1 + 3.0, blf(x1) - 2.2, 0));
    v.push_back(cp(x1 + 7.0, blf(x1) + 2.8, 0));
    out.push_back(mk(0, v, 0, R_FLOUR));
  } else if (kind == "long") {
    v.push_back(cp(x0 - 7.0, blf(x0) - 2.4, 0));
    double j1 = j();
    v.push_back(cp(x0 + 0.3 * W, blf(x0 + 0.3 * W) - 5.2 + j1, 0));
    v.push_back(cp(x1 + 8.0, blf(x1) - 5.6, 0));
    v.push_back(cp(x1 + 17.0, blf(x1) - 4.6, 0));
    v.push_back(cp(x1 + 21.0, blf(x1) - 1.0, 0));
    v.push_back(cp(x1 + 20.0, blf(x1) + 1.2, 0));
    out.push_back(mk(0, v, 0, R_FLOUR));
  } else if (kind == "zigzag" || kind == "spiky") {
    const bool zig = kind == "zigzag";
    const int n = std::max(4, (int)(W / (zig ? 5.0 : 4.0)));
    for (int i = 0; i <= n; ++i) {
      double x = x0 - 1.0 + (W + 4.0) * i / n;
      bool hi = (i % 2 == 0);
      double amp = zig ? 3.6 : rng.r(3.0, 6.0);
      double jj = j();
      v.push_back(cp(x + jj, blf(x) - 2.4 - (hi ? 0.0 : amp), 1.0));
    }
    if (!zig) v.push_back(cp(x1 + 6.0, blf(x1) + 3.0, 1.0));
    out.push_back(mk(0, v, 0, R_FLOUR));
  } else if (kind == "wave") {
    const int n = std::max(6, (int)(W / 2.0));
    for (int i = 0; i <= n; ++i) {
      double x = x0 - 2.0 + (W + 6.0) * i / n;
      v.push_back(cp(x, blf(x) - 4.0 + 1.5 * std::sin(i * 1.3), 0.0));
    }
    out.push_back(mk(0, v, 0, R_FLOUR));
  } else if (kind == "loop") {
    double y0 = blf(x0 + 0.5 * W) - 6.0;
    out.push_back(mk(0, cycloid_loops(x0 + 0.1 * W, x1 + 4.0, y0, 3.2, std::max(3, (int)(W / 14.0)), rng), 0, R_FLOUR));
  } else if (kind == "web") {
    v.push_back(cp(x0 - 1.0, blf(x0) - 3.6, 0));
    v.push_back(cp(x0 + 0.5 * W, blf(x0 + 0.5 * W) - 4.6, 0));
    v.push_back(cp(x1 + 5.0, blf(x1) - 4.4, 0));
    out.push_back(mk(0, v, 0, R_FLOUR));
    place_doodle("web", x1 + 9.5, blf(x1) - 4.0, 11.0, rng, R_FLOUR, false, 0.0, 1.0, out);
  } else if (kind == "arrow") {
    auto y = [&](double x) { return blf(x) - 4.4; };
    v.push_back(cp(x0 - 4.0, y(x0), 0));
    v.push_back(cp(x1 + 7.0, y(x1), 0));
    out.push_back(mk(0, v, 0, R_FLOUR));
    v.clear();
    v.push_back(cp(x1 + 3.4, y(x1) + 2.6, 0));
    v.push_back(cp(x1 + 7.4, y(x1), 1));
    v.push_back(cp(x1 + 3.4, y(x1) - 2.6, 0));
    out.push_back(mk(0, v, 0, R_FLOUR));
    const double fl[2] = {0.0, 2.2};
    for (int i = 0; i < 2; ++i) {
      double f = fl[i];
      v.clear();
      v.push_back(cp(x0 - 1.0 - f, y(x0) + 2.2, 0));
      v.push_back(cp(x0 - 3.0 - f, y(x0), 1));
      v.push_back(cp(x0 - 1.0 - f, y(x0) - 2.2, 0));
      out.push_back(mk(0, v, 0, R_FLOUR));
    }
  } else if (kind == "sword") {
    place_doodle("sword", x0 + 0.5 * W + 1.0, blf(x0 + 0.5 * W) - 5.0, 10.0, rng, R_FLOUR, true, 0.0, (W + 6.0) / 13.0,
                 out);
  } else if (kind == "bolt") {
    const int n = std::max(4, (int)(W / 6.0));
    for (int i = 0; i <= n; ++i) {
      double x = x0 - 2.0 + (W + 6.0) * i / n;
      double jx = rng.n() * 1.2;
      double jj = j();
      v.push_back(cp(x + jx, blf(x) - 4.0 + ((i % 2) ? 2.2 : -2.2) + jj, 1.0));
    }
    out.push_back(mk(0, v, 0, R_FLOUR));
  } else if (kind == "flame") {
    const int n = std::max(6, (int)(W / 2.0));
    for (int i = 0; i <= n; ++i) {
      double x = x0 - 1.0 + (W + 3.0) * i / n;
      v.push_back(cp(x, blf(x) - 4.0 + 1.0 * std::sin(i * 1.7), 0.0));
    }
    out.push_back(mk(0, v, 0, R_FLOUR));
    place_doodle("flame", x1 + 6.0, blf(x1) - 2.0, 8.0, rng, R_FLOUR, false, 0.0, 1.0, out);
  } else if (kind == "tail") {
    place_doodle("tail", x0 + 0.5 * W + 2.0, blf(x0 + 0.5 * W) - 5.0, 10.0, rng, R_FLOUR, true, 0.0, (W + 8.0) / 11.0,
                 out);
  } else if (kind == "dash") {
    v.push_back(cp(x1 - 0.3 * W, blf(x1) - 3.6, 0));
    v.push_back(cp(x1 + 1.0, blf(x1) - 3.0, 0));
    out.push_back(mk(0, v, 0, R_FLOUR));
  }
}

// Catmull-Rom through the control points (a corner point breaks the curve), sampled ~step apart.
void smooth_stroke(const std::vector<CP>& pts, double smooth, double step, std::vector<CP>& out) {
  out.clear();
  if (pts.empty()) return;
  out.push_back(cp(pts[0].x, pts[0].y, 0.0));
  if (pts.size() == 1) return;
  std::vector<std::vector<CP> > segs;
  std::vector<CP> cur;
  cur.push_back(pts[0]);
  for (size_t i = 1; i < pts.size(); ++i) {
    cur.push_back(pts[i]);
    if (pts[i].c > 0.5) {
      segs.push_back(cur);
      cur.clear();
      cur.push_back(pts[i]);
    }
  }
  if (cur.size() > 1) segs.push_back(cur);
  for (size_t sgi = 0; sgi < segs.size(); ++sgi) {
    const std::vector<CP>& seg = segs[sgi];
    const size_t n = seg.size();
    for (size_t i = 0; i + 1 < n; ++i) {
      const CP& p1 = seg[i];
      const CP& p2 = seg[i + 1];
      const CP& p0 = i > 0 ? seg[i - 1] : p1;
      const CP& p3 = i + 2 < n ? seg[i + 2] : p2;
      double d = std::sqrt((p2.x - p1.x) * (p2.x - p1.x) + (p2.y - p1.y) * (p2.y - p1.y));
      int m = std::max(1, (int)std::ceil(d / step));
      for (int jj = 1; jj <= m; ++jj) {
        double u = (double)jj / m;
        double x, y;
        if (smooth > 0.5) {
          double u2 = u * u, u3 = u2 * u;
          x = 0.5 * ((2 * p1.x) + (-p0.x + p2.x) * u + (2 * p0.x - 5 * p1.x + 4 * p2.x - p3.x) * u2 +
                     (-p0.x + 3 * p1.x - 3 * p2.x + p3.x) * u3);
          y = 0.5 * ((2 * p1.y) + (-p0.y + p2.y) * u + (2 * p0.y - 5 * p1.y + 4 * p2.y - p3.y) * u2 +
                     (-p0.y + 3 * p1.y - 3 * p2.y + p3.y) * u3);
        } else {
          x = p1.x + (p2.x - p1.x) * u;
          y = p1.y + (p2.y - p1.y) * u;
        }
        out.push_back(cp(x, y, 0.0));
      }
    }
  }
}

struct Pen { double base, tin, tout, sthin, nib, nibmin, noise; };

Pen pen_for(const std::string& p) {
  // base width (box), taper in, taper out (box units), speed thinning, nib angle (0 none), nib min, noise
  if (p == "m") return Pen{0.030, 0.006, 0.012, 0.12, 0.0, 0.0, 0.02};
  if (p == "b") return Pen{0.010, 0.008, 0.020, 0.30, 0.0, 0.0, 0.04};
  if (p == "f") return Pen{0.028, 0.006, 0.016, 0.20, 0.7, 0.2, 0.02};
  if (p == "g") return Pen{0.015, 0.008, 0.020, 0.25, 0.0, 0.0, 0.03};
  if (p == "p") return Pen{0.024, 0.003, 0.004, 0.08, 0.0, 0.0, 0.05};
  if (p == "c") return Pen{0.028, 0.004, 0.006, 0.00, 0.0, 0.0, 0.30};
  if (p == "r") return Pen{0.040, 0.025, 0.050, 0.55, 0.0, 0.0, 0.06};
  return Pen{0.020, 0.010, 0.025, 0.25, 0.0, 0.0, 0.02};  // s: sharpie
}

struct OutStroke { std::vector<Point> pts; uint32_t rgba; int role; };

// Smooth, fit to the box, time and width every point.
void finish(std::vector<Stk>& allst, const Params& P, Rng& rng, Signature& sig) {
  const double smooth = fv(P, "smooth"), trem = fv(P, "trem"), pix = fv(P, "pix");
  for (size_t si = 0; si < allst.size(); ++si) {
    Stk& s = allst[si];
    if (s.kind == 'O') {
      s.dense = s.pts;
      continue;
    }
    double sm = (s.role == R_NAME || s.role == R_LICK) ? smooth : 1.0;
    if (s.role == R_NAME && pix > 0.0) sm = 0.0;
    smooth_stroke(s.pts, sm, 0.9, s.dense);
    if (trem > 0.0 && (s.role == R_NAME || s.role == R_FLOUR || s.role == R_LICK)) {
      double ox = 0.0, oy = 0.0;
      for (size_t i = 0; i < s.dense.size(); ++i) {
        ox = 0.55 * ox + 0.45 * rng.n();
        oy = 0.55 * oy + 0.45 * rng.n();
        s.dense[i].x += trem * ox;
        s.dense[i].y += trem * oy;
      }
    }
  }
  // fit
  bool any = false;
  double bx0 = 0, bx1 = 0, by0 = 0, by1 = 0;
  for (size_t si = 0; si < allst.size(); ++si)
    for (size_t i = 0; i < allst[si].dense.size(); ++i) {
      const CP& p = allst[si].dense[i];
      if (!any) {
        bx0 = bx1 = p.x;
        by0 = by1 = p.y;
        any = true;
      }
      bx0 = std::min(bx0, p.x);
      bx1 = std::max(bx1, p.x);
      by0 = std::min(by0, p.y);
      by1 = std::max(by1, p.y);
    }
  sig.strokes.clear();
  sig.duration = 0.0f;
  sig.inkGlow = 0xFFFFFFFFu;
  if (!any) return;
  double fill = fv(P, "fill");
  if (fill == 0.0) fill = 0.92;
  const double bw = std::max(1.0, bx1 - bx0), bh = std::max(1.0, by1 - by0);
  const double k = std::min(kAspect * 0.9 * fill / bw, 0.86 * fill / bh);
  const double cx = kAspect * 0.5 + fv(P, "offx") * kAspect, cy = 0.5 + fv(P, "offy");
  const double mx = 0.5 * (bx0 + bx1), my = 0.5 * (by0 + by1);
  const Pen pen = pen_for(sv(P, "pen", "s"));
  double pw = fv(P, "pw");
  if (pw == 0.0) pw = 1.0;
  const double kf = std::min(1.3, std::max(0.7, std::sqrt(k / 0.023)));
  double speed = fv(P, "speed");
  if (speed == 0.0) speed = 1.0;
  const double v = 3.0 * speed;
  const uint32_t ink = colv(P, "col", 0xFFFFFF);
  double alpha = fv(P, "alpha");
  if (alpha == 0.0) alpha = 1.0;
  const double fade = fv(P, "fade");
  int nlet = 1;
  for (size_t si = 0; si < allst.size(); ++si) nlet = std::max(nlet, allst[si].li);
  const uint32_t dooc = colv(P, "dooc", ink), ulc = colv(P, "ulc", ink);
  double T = 0.0;
  bool hasPrev = false;
  double prevx = 0.0, prevy = 0.0;
  std::vector<OutStroke> out;
  std::vector<std::pair<double, double> > bp;
  for (size_t si = 0; si < allst.size(); ++si) {
    const Stk& s = allst[si];
    if (s.dense.empty()) continue;
    bp.clear();
    for (size_t i = 0; i < s.dense.size(); ++i)
      bp.push_back(std::make_pair(cx + (s.dense[i].x - mx) * k, cy - (s.dense[i].y - my) * k));
    uint32_t c = ink;
    if (s.role == R_DOODLE)
      c = dooc;
    else if (s.role == R_FLOUR)
      c = ulc;
    double a = alpha;
    if (fade > 0.0 && (s.role == R_NAME || s.role == R_DOT || s.role == R_LICK)) a *= 1.0 - fade * ((double)s.li / nlet);
    a = std::max(0.0, std::min(1.0, a));
    OutStroke os;
    os.rgba = ((c & 0xFFFFFFu) << 8) | (uint32_t)(int)(a * 255.0 + 0.5);
    os.role = s.role;
    if (s.kind == 'O') {
      for (size_t i = 0; i < bp.size(); ++i) {
        if (hasPrev && i == 0) {
          double dx = bp[i].first - prevx, dy = bp[i].second - prevy;
          T += 0.04 + std::sqrt(dx * dx + dy * dy) / (v * 2.2);
        }
        T += 0.03;
        Point p;
        p.x = (float)bp[i].first;
        p.y = (float)bp[i].second;
        p.t = (float)T;
        p.w = (float)(s.dense[i].c * k);
        os.pts.push_back(p);
        T += 0.03;
      }
      hasPrev = true;
      prevx = bp.back().first;
      prevy = bp.back().second;
      out.push_back(os);
      continue;
    }
    const double vs = v * ((s.role == R_DOODLE || s.role == R_DOT) ? 0.75 : 1.0);
    if (hasPrev) {
      double dx = bp[0].first - prevx, dy = bp[0].second - prevy;
      T += 0.05 + std::sqrt(dx * dx + dy * dy) / (v * 2.2);
    }
    double total = 0.0;
    for (size_t i = 1; i < bp.size(); ++i) {
      double dx = bp[i].first - bp[i - 1].first, dy = bp[i].second - bp[i - 1].second;
      total += std::sqrt(dx * dx + dy * dy);
    }
    double along = 0.0;
    const double wbase = pen.base * pw * kf * s.wmul * (1.0 + 0.1 * rng.n());
    for (size_t i = 0; i < bp.size(); ++i) {
      double sn = 1.0, dx = 0.0, dy = 0.0, seg = 0.0;
      if (i > 0) {
        dx = bp[i].first - bp[i - 1].first;
        dy = bp[i].second - bp[i - 1].second;
        seg = std::sqrt(dx * dx + dy * dy);
        double turn = 0.0;
        if (i > 1) {
          double ex = bp[i - 1].first - bp[i - 2].first, ey = bp[i - 1].second - bp[i - 2].second;
          double l1 = std::sqrt(ex * ex + ey * ey);
          if (l1 > 1e-6 && seg > 1e-6) {
            double cs = (ex * dx + ey * dy) / (l1 * seg);
            turn = std::acos(std::max(-1.0, std::min(1.0, cs)));
          }
        }
        double ramp = std::min(1.0, 0.45 + 0.55 * along / 0.04);
        sn = ramp / (1.0 + 2.0 * turn / kSignPi);
        T += seg / (vs * sn);
        along += seg;
      }
      if (i == 0 && bp.size() > 1) {
        dx = bp[1].first - bp[0].first;
        dy = bp[1].second - bp[0].second;
        seg = std::sqrt(dx * dx + dy * dy);
      }
      double ft = std::min(1.0, 0.35 + 0.65 * along / pen.tin) * std::min(1.0, 0.3 + 0.7 * (total - along) / pen.tout);
      if (bp.size() <= 2) ft = 1.0;
      double fs = 1.0 - pen.sthin * (sn - 0.5);
      double fd;
      if (pen.nib > 0.0 && seg > 1e-6) {
        double th = std::atan2(-dy, dx);
        fd = pen.nibmin + (1.0 - pen.nibmin) * std::fabs(std::sin(th - pen.nib));
      } else {
        fd = 1.0 + 0.12 * (seg > 1e-6 ? std::max(0.0, dy / seg) : 0.0);
      }
      double fn = 1.0 + pen.noise * rng.n();
      double w = std::max(0.002, wbase * ft * fs * fd * fn);
      if (s.wcap > 0.0) w = std::min(w, s.wcap * k);
      Point p;
      p.x = (float)bp[i].first;
      p.y = (float)bp[i].second;
      p.t = (float)T;
      p.w = (float)w;
      os.pts.push_back(p);
    }
    hasPrev = true;
    prevx = bp.back().first;
    prevy = bp.back().second;
    out.push_back(os);
  }
  const double Dur = std::max(T, 1e-3);
  const double target = std::min(std::max(Dur, 1.2), 3.6);
  const double sc = target / Dur;
  for (size_t i = 0; i < out.size(); ++i)
    for (size_t q = 0; q < out[i].pts.size(); ++q) out[i].pts[q].t = (float)(out[i].pts[q].t * sc);
  // halos (a glow / scorch under the ink, written with it)
  const std::string halo = sv(P, "halo");
  double hw = fv(P, "halow");
  if (hw == 0.0) hw = 2.2;
  uint32_t hc = 0;
  if (!halo.empty()) {
    hc = (uint32_t)std::strtoul(halo.c_str(), nullptr, 16);
    if (halo.size() <= 6) hc = (hc << 8) | 0x60u;
  }
  for (size_t i = 0; i < out.size(); ++i) {
    const OutStroke& os = out[i];
    if (!halo.empty() && (os.role == R_NAME || os.role == R_FLOUR || os.role == R_LICK || os.role == R_DOT)) {
      Stroke h;
      h.rgba = hc;
      h.pts = os.pts;
      for (size_t q = 0; q < h.pts.size(); ++q) {
        h.pts[q].w = (float)(h.pts[q].w * hw);
        h.pts[q].x = (float)(h.pts[q].x / kAspect);
      }
      sig.strokes.push_back(h);
    }
    Stroke st;
    st.rgba = os.rgba;
    st.pts = os.pts;
    for (size_t q = 0; q < st.pts.size(); ++q) st.pts[q].x = (float)(st.pts[q].x / kAspect);
    sig.strokes.push_back(st);
  }
  sig.duration = (float)target;
  sig.inkGlow = ((colv(P, "glow", ink) & 0xFFFFFFu) << 8) | 0xFFu;
}

void generate(const std::string& key, const std::string& name, Signature& sig) {
  const Params P = resolve(key);
  Rng rng(fnv(key));
  std::string text = sv(P, "txt");
  if (text.empty()) text = clean_text(name);
  for (size_t i = 0; i < text.size(); ++i)
    if (text[i] == '_') text[i] = ' ';
  if (fv(P, "caps") > 0.0)
    for (size_t i = 0; i < text.size(); ++i) text[i] = (char)std::toupper((unsigned char)text[i]);
  Shaped sh;
  shape_text(text, P, rng, sh);
  std::vector<Stk>& strokes = sh.st;
  double x0, x1, yb, yt;
  bbox(strokes, x0, x1, yb, yt);
  // first-letter swash / lead-in
  const int lead = (int)fv(P, "lead");
  int first = -1;
  for (size_t i = 0; i < strokes.size() && first < 0; ++i)
    if (strokes[i].kind == 0 && strokes[i].role == R_NAME) first = (int)i;
  if (first >= 0 && lead > 0) {
    const CP p0 = strokes[first].pts[0];
    const double b = sh.blf(p0.x);
    const bool high = p0.y > b + 10.0;
    std::vector<CP> pre;
    if (lead == 1) {
      pre.push_back(high ? cp(p0.x - 3.0, p0.y - 2.0, 0.0) : cp(p0.x - 3.0, p0.y - 4.5, 0.0));
    } else if (high) {
      pre.push_back(cp(p0.x + 1.0, p0.y - 3.0, 0.0));
      pre.push_back(cp(p0.x - 3.5, p0.y - 1.5, 0.0));
      pre.push_back(cp(p0.x - 3.0, p0.y + 2.5, 0.0));
    } else {
      pre.push_back(cp(p0.x + 1.5, b - 4.5, 0.0));
      pre.push_back(cp(p0.x - 4.5, b - 5.5, 0.0));
      pre.push_back(cp(p0.x - 8.0, b - 1.5, 0.0));
      pre.push_back(cp(p0.x - 5.5, b + 2.5, 0.0));
    }
    strokes[first].pts.insert(strokes[first].pts.begin(), pre.begin(), pre.end());
  }
  // return swash off the last letter
  if (fv(P, "tail") > 0.0) {
    int last = -1;
    for (size_t i = 0; i < strokes.size(); ++i)
      if (strokes[i].kind == 0 && strokes[i].role == R_NAME) last = (int)i;
    if (last >= 0) {
      const CP e = strokes[last].pts.back();
      const double W = x1 - x0;
      std::vector<CP>& lp = strokes[last].pts;
      lp.push_back(cp(e.x + 2.4, e.y + 0.6, 0.0));
      lp.push_back(cp(e.x + 2.0, sh.blf(x1) - 4.8, 0.0));
      lp.push_back(cp(x0 + 0.55 * W, sh.blf(x0 + 0.55 * W) - 6.4, 0.0));
      lp.push_back(cp(x0 + 0.12 * W, sh.blf(x0) - 5.0, 0.0));
      lp.push_back(cp(x0 - 2.0, sh.blf(x0) - 2.6, 0.0));
    }
  }
  // flame licks off the letters' tops (Ghost Rider)
  const double flames = fv(P, "flames");
  if (flames > 0.0) {
    std::vector<Stk> withlicks;
    int n = 0;
    for (size_t si = 0; si < strokes.size(); ++si) {
      withlicks.push_back(strokes[si]);
      const Stk& s = strokes[si];
      if (s.kind != 0 || s.role != R_NAME || n >= 14) continue;
      for (size_t i = 1; i + 1 < s.pts.size(); ++i) {
        const CP& p = s.pts[i];
        if (p.y >= s.pts[i - 1].y && p.y >= s.pts[i + 1].y && p.y > sh.blf(p.x) + 7.0) {
          const double h = flames * rng.r(0.6, 1.2);
          std::vector<CP> lk;
          lk.push_back(cp(p.x, p.y, 0));
          lk.push_back(cp(p.x + 1.0 * h, p.y + 2.4 * h, 0));
          lk.push_back(cp(p.x - 0.6 * h, p.y + 4.6 * h, 0));
          lk.push_back(cp(p.x + 0.7 * h, p.y + 6.8 * h, 0));
          Stk l = mk(0, lk, s.li, R_LICK);
          l.wmul = 0.5;
          withlicks.push_back(l);
          n += 1;
        }
      }
    }
    strokes.swap(withlicks);
  }
  // ink blots
  const int nb = (int)fv(P, "blot");
  if (nb > 0) {
    std::vector<int> inks;
    for (size_t i = 0; i < strokes.size(); ++i)
      if (strokes[i].kind == 0 && strokes[i].role == R_NAME) inks.push_back((int)i);
    std::vector<std::vector<Stk> > adds(strokes.size());
    for (int b = 0; b < nb; ++b) {
      if (inks.empty()) break;
      const int ni = (int)inks.size();
      const int si = inks[(int)(rng.f() * ni) % ni];
      const Stk& s = strokes[si];
      const int np = (int)s.pts.size();
      const CP p = s.pts[(int)(rng.f() * np) % np];
      std::vector<CP> discs;
      double dx = rng.n() * 0.8;
      double dy = rng.n() * 0.8;
      double dd = rng.r(3.2, 4.8);
      discs.push_back(cp(p.x + dx, p.y + dy, dd));
      for (int q = 0; q < 3; ++q) {
        double a = rng.f() * 6.283;
        double d = rng.r(2.6, 5.0);
        double sz = rng.r(0.5, 1.2);
        discs.push_back(cp(p.x + std::cos(a) * d, p.y + std::sin(a) * d, sz));
      }
      adds[si].push_back(mk('O', discs, s.li, R_BLOT));
    }
    std::vector<Stk> ns;
    for (size_t i = 0; i < strokes.size(); ++i) {
      ns.push_back(strokes[i]);
      for (size_t q = 0; q < adds[i].size(); ++q) ns.push_back(adds[i][q]);
    }
    strokes.swap(ns);
  }
  bbox(strokes, x0, x1, yb, yt);
  // underline / flourish
  std::vector<Stk> flour;
  const std::string ul = sv(P, "ul", "n");
  if (ul != "n") underline(ul, x0, x1, sh, rng, flour);
  double ulw = fv(P, "ulw");
  if (ulw == 0.0) ulw = 1.0;
  for (size_t i = 0; i < flour.size(); ++i) flour[i].wmul *= ulw;
  // doodles: "name@pos:size,..."
  std::vector<Stk> doodles;
  const std::string doo = sv(P, "doo");
  if (!doo.empty()) {
    std::vector<Stk> both(strokes);
    both.insert(both.end(), flour.begin(), flour.end());
    double bx0, bx1, byb, byt;
    bbox(both, bx0, bx1, byb, byt);
    std::vector<std::string> items = split(doo, ',');
    for (size_t i = 0; i < items.size(); ++i) {
      if (items[i].empty()) continue;
      size_t at = items[i].find('@'), co = items[i].find(':');
      if (at == std::string::npos || co == std::string::npos || co < at) continue;
      const std::string nm = items[i].substr(0, at);
      const std::string pos = items[i].substr(at + 1, co - at - 1);
      const double size = std::strtod(items[i].substr(co + 1).c_str(), nullptr);
      double cx, cy;
      if (pos == "e") {
        cx = bx1 + 2.5 + size * 0.5;
        cy = sh.blf(x1) + 6.0;
      } else if (pos == "a") {
        cx = x0 + 6.0;
        cy = yt + size * 0.5 + 0.5;
      } else if (pos == "t") {
        cx = x1 - size * 0.2;
        cy = yt + size * 0.3;
      } else if (pos == "u") {
        cx = x1 - size * 0.3;
        cy = sh.blf(x1) - 5.0 - size * 0.5;
      } else if (pos == "l") {
        cx = bx0 - 2.5 - size * 0.5;
        cy = sh.blf(x0) + 6.0;
      } else if (pos == "c") {
        cx = 0.5 * (x0 + x1);
        cy = sh.blf(0.5 * (x0 + x1)) - 4.0 - size * 0.5;
      } else {  // "x": over the name
        cx = 0.5 * (x0 + x1);
        cy = 0.5 * (yb + yt);
      }
      place_doodle(nm, cx, cy, size, rng, R_DOODLE, false, 0.0, 1.0, doodles);
    }
  }
  // extra text ("chimichanga!")
  std::vector<Stk> extra;
  std::string xt = sv(P, "xt");
  if (!xt.empty()) {
    Params P2;
    apply_style(P2, D().defaults);
    std::map<std::string, KV>::const_iterator pr = D().presets.find(sv(P, "xst", "note"));
    if (pr != D().presets.end()) apply_style(P2, pr->second);
    P2["col"] = sv(P, "col");
    P2["lines"] = "1";
    Rng sub(fnv(key) ^ 0x5EED5EEDu);
    for (size_t i = 0; i < xt.size(); ++i)
      if (xt[i] == '_') xt[i] = ' ';
    Shaped esh;
    shape_text(xt, P2, sub, esh);
    double xsc = fv(P, "xsz");
    if (xsc == 0.0) xsc = 0.45;
    double ex0, ex1, eyb, eyt;
    bbox(esh.st, ex0, ex1, eyb, eyt);
    std::vector<Stk> all3(strokes);
    all3.insert(all3.end(), flour.begin(), flour.end());
    all3.insert(all3.end(), doodles.begin(), doodles.end());
    double bx0, bx1, byb, byt;
    bbox(all3, bx0, bx1, byb, byt);
    const std::string pos = sv(P, "xpos", "u");
    double ox, oy;
    if (pos == "u") {
      ox = bx1 - (ex1 - ex0) * xsc;
      oy = byb - 2.0 - eyt * xsc;
    } else if (pos == "a") {
      ox = x0 + 4.0;
      oy = byt + 2.0 - eyb * xsc;
    } else {
      ox = bx1 + 3.0;
      oy = sh.blf(x1) + 1.0;
    }
    for (size_t i = 0; i < esh.st.size(); ++i) {
      Stk& s = esh.st[i];
      for (size_t q = 0; q < s.pts.size(); ++q) {
        s.pts[q].x = ox + (s.pts[q].x - ex0) * xsc;
        s.pts[q].y = oy + s.pts[q].y * xsc;
        if (s.kind == 'O') s.pts[q].c *= xsc;
      }
      s.role = R_XTEXT;
      s.wcap = 0.9;
    }
    extra.swap(esh.st);
  }
  std::vector<Stk> allst(strokes);
  allst.insert(allst.end(), flour.begin(), flour.end());
  allst.insert(allst.end(), doodles.begin(), doodles.end());
  allst.insert(allst.end(), extra.begin(), extra.end());
  finish(allst, P, rng, sig);
}

// ----------------------------------------------------------------------------------------------------------- draw
struct Seg { float ax, ay, ar, bx, by, br; };

// A halo is written with the stroke after it: same points and times, wider.
bool is_halo(const Signature& s, size_t i) {
  if (i + 1 >= s.strokes.size()) return false;
  const Stroke& a = s.strokes[i];
  const Stroke& b = s.strokes[i + 1];
  if (a.pts.empty() || a.pts.size() != b.pts.size()) return false;
  return a.pts[0].x == b.pts[0].x && a.pts[0].y == b.pts[0].y && a.pts[0].t == b.pts[0].t &&
         a.pts[0].w > b.pts[0].w * 1.2f;
}

void composite(uint8_t* rgba, int stride, int x0, int y0, int bw, int bh, const std::vector<float>& cov, float cr,
               float cg, float cb, float ca) {
  for (int y = 0; y < bh; ++y) {
    uint8_t* row = rgba + (size_t)(y0 + y) * (size_t)stride + (size_t)x0 * 4;
    const float* cv = &cov[(size_t)y * bw];
    for (int x = 0; x < bw; ++x) {
      float sa = cv[x] * ca;
      if (sa <= 0.0f) continue;
      uint8_t* d = row + (size_t)x * 4;
      float da = d[3] * (1.0f / 255.0f);
      float oa = sa + da * (1.0f - sa);
      if (oa <= 1e-6f) continue;
      float k = da * (1.0f - sa);
      float r = (cr * sa + d[0] * (1.0f / 255.0f) * k) / oa;
      float g = (cg * sa + d[1] * (1.0f / 255.0f) * k) / oa;
      float b = (cb * sa + d[2] * (1.0f / 255.0f) * k) / oa;
      d[0] = (uint8_t)std::min(255.0f, r * 255.0f + 0.5f);
      d[1] = (uint8_t)std::min(255.0f, g * 255.0f + 0.5f);
      d[2] = (uint8_t)std::min(255.0f, b * 255.0f + 0.5f);
      d[3] = (uint8_t)std::min(255.0f, oa * 255.0f + 0.5f);
    }
  }
}

// Union (max) of a capsule's coverage into the buffer cov (origin bx0, by0; bw x bh). extra / gain: wider / fainter.
void raster(const Seg& s, std::vector<float>& cov, int bx0, int by0, int bw, int bh, float extra, float gain) {
  const float rmax = std::max(s.ar, s.br) + extra + 1.0f;
  int x0 = std::max(bx0, (int)std::floor(std::min(s.ax, s.bx) - rmax));
  int x1 = std::min(bx0 + bw - 1, (int)std::ceil(std::max(s.ax, s.bx) + rmax));
  int y0 = std::max(by0, (int)std::floor(std::min(s.ay, s.by) - rmax));
  int y1 = std::min(by0 + bh - 1, (int)std::ceil(std::max(s.ay, s.by) + rmax));
  if (x1 < x0 || y1 < y0) return;
  const float dx = s.bx - s.ax, dy = s.by - s.ay;
  const float L2 = dx * dx + dy * dy;
  const float inv = L2 > 1e-9f ? 1.0f / L2 : 0.0f;
  for (int y = y0; y <= y1; ++y) {
    const float py = y + 0.5f;
    float* row = cov.data() + (size_t)(y - by0) * bw;
    for (int x = x0; x <= x1; ++x) {
      const float px = x + 0.5f;
      float h = ((px - s.ax) * dx + (py - s.ay) * dy) * inv;
      h = h < 0.0f ? 0.0f : (h > 1.0f ? 1.0f : h);
      const float ex = px - (s.ax + h * dx), ey = py - (s.ay + h * dy);
      const float d = std::sqrt(ex * ex + ey * ey);
      const float rr = s.ar + (s.br - s.ar) * h + extra;
      const float r = std::max(rr, 0.5f);
      float c = r - d + 0.5f;
      if (c <= 0.0f) continue;
      if (c > 1.0f) c = 1.0f;
      if (rr < 0.5f) c *= std::max(0.0f, rr * 2.0f);
      c *= gain;
      if (c > row[x - bx0]) row[x - bx0] = c;
    }
  }
}

}  // namespace

const Signature& get(const std::string& key, const std::string& name) {
  static std::mutex mu;
  static std::map<std::string, std::unique_ptr<Signature> > cache;
  std::lock_guard<std::mutex> lock(mu);
  const std::string ck = key + '\n' + name;
  std::map<std::string, std::unique_ptr<Signature> >::iterator it = cache.find(ck);
  if (it != cache.end()) return *it->second;
  std::unique_ptr<Signature> sig(new Signature());
  sig->duration = 0.0f;
  sig->inkGlow = 0xFFFFFFFFu;
  generate(key, name, *sig);
  const Signature& ref = *sig;
  cache[ck] = std::move(sig);
  return ref;
}

void draw(const Signature& s, float t, uint8_t* rgba, int w, int h, int stride) {
  if (!rgba || w <= 0 || h <= 0) return;
  const float W = (float)w, H = (float)h;
  std::vector<Seg> segs;
  std::vector<float> cov;
  for (size_t si = 0; si < s.strokes.size(); ++si) {
    const Stroke& st = s.strokes[si];
    if (st.pts.empty() || st.pts[0].t > t) continue;
    segs.clear();
    if (st.pts.size() == 1) {
      const Point& p = st.pts[0];
      Seg sg = {p.x * W, p.y * H, p.w * H * 0.5f, p.x * W, p.y * H, p.w * H * 0.5f};
      segs.push_back(sg);
    } else {
      for (size_t i = 1; i < st.pts.size(); ++i) {
        const Point& a = st.pts[i - 1];
        Point b = st.pts[i];
        if (a.t > t) break;
        if (b.t > t) {
          float u = (t - a.t) / std::max(1e-6f, b.t - a.t);
          b.x = a.x + (b.x - a.x) * u;
          b.y = a.y + (b.y - a.y) * u;
          b.w = a.w + (b.w - a.w) * u;
        }
        Seg sg = {a.x * W, a.y * H, a.w * H * 0.5f, b.x * W, b.y * H, b.w * H * 0.5f};
        segs.push_back(sg);
      }
    }
    if (segs.empty()) continue;
    float fx0 = 1e30f, fy0 = 1e30f, fx1 = -1e30f, fy1 = -1e30f;
    for (size_t i = 0; i < segs.size(); ++i) {
      const Seg& g = segs[i];
      const float r = std::max(g.ar, g.br) + 1.0f;
      fx0 = std::min(fx0, std::min(g.ax, g.bx) - r);
      fx1 = std::max(fx1, std::max(g.ax, g.bx) + r);
      fy0 = std::min(fy0, std::min(g.ay, g.by) - r);
      fy1 = std::max(fy1, std::max(g.ay, g.by) + r);
    }
    const int bx0 = std::max(0, (int)std::floor(fx0)), by0 = std::max(0, (int)std::floor(fy0));
    const int bx1 = std::min(w - 1, (int)std::ceil(fx1)), by1 = std::min(h - 1, (int)std::ceil(fy1));
    if (bx1 < bx0 || by1 < by0) continue;
    const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
    cov.assign((size_t)bw * bh, 0.0f);
    for (size_t i = 0; i < segs.size(); ++i) raster(segs[i], cov, bx0, by0, bw, bh, 0.0f, 1.0f);
    const uint32_t c = st.rgba;
    composite(rgba, stride, bx0, by0, bw, bh, cov, ((c >> 24) & 255) / 255.0f, ((c >> 16) & 255) / 255.0f,
              ((c >> 8) & 255) / 255.0f, (c & 255) / 255.0f);
  }
  if (t >= s.duration) return;
  // the wet ink glows: the last 0.3 s of ink, and a soft light on the pen tip
  const Point* tip = nullptr;
  segs.clear();
  std::vector<float> fresh;
  for (size_t si = 0; si < s.strokes.size(); ++si) {
    const Stroke& st = s.strokes[si];
    for (size_t i = 0; i < st.pts.size(); ++i)
      if (st.pts[i].t <= t) tip = &st.pts[i];
    if (is_halo(s, si)) continue;
    for (size_t i = 1; i < st.pts.size(); ++i) {
      const Point& a = st.pts[i - 1];
      const Point& b = st.pts[i];
      if (b.t > t || t - b.t > 0.3f) continue;
      Seg sg = {a.x * W, a.y * H, 0.0f, b.x * W, b.y * H, 0.0f};
      sg.ar = sg.br = b.w * H * 0.5f + 1.5f;
      segs.push_back(sg);
      fresh.push_back(1.0f - (t - b.t) / 0.3f);
    }
  }
  float fx0 = 1e30f, fy0 = 1e30f, fx1 = -1e30f, fy1 = -1e30f;
  for (size_t i = 0; i < segs.size(); ++i) {
    const Seg& g = segs[i];
    fx0 = std::min(fx0, std::min(g.ax, g.bx) - g.ar - 1.0f);
    fx1 = std::max(fx1, std::max(g.ax, g.bx) + g.ar + 1.0f);
    fy0 = std::min(fy0, std::min(g.ay, g.by) - g.ar - 1.0f);
    fy1 = std::max(fy1, std::max(g.ay, g.by) + g.ar + 1.0f);
  }
  float tx = 0, ty = 0, R = 0;
  if (tip) {
    tx = tip->x * W;
    ty = tip->y * H;
    R = std::max(4.0f, tip->w * H * 3.0f);
    fx0 = std::min(fx0, tx - 2.0f * R);
    fx1 = std::max(fx1, tx + 2.0f * R);
    fy0 = std::min(fy0, ty - 2.0f * R);
    fy1 = std::max(fy1, ty + 2.0f * R);
  }
  if (fx1 < fx0) return;
  const int bx0 = std::max(0, (int)std::floor(fx0)), by0 = std::max(0, (int)std::floor(fy0));
  const int bx1 = std::min(w - 1, (int)std::ceil(fx1)), by1 = std::min(h - 1, (int)std::ceil(fy1));
  if (bx1 < bx0 || by1 < by0) return;
  const int bw = bx1 - bx0 + 1, bh = by1 - by0 + 1;
  cov.assign((size_t)bw * bh, 0.0f);
  for (size_t i = 0; i < segs.size(); ++i) raster(segs[i], cov, bx0, by0, bw, bh, 0.0f, fresh[i] * 0.8f);
  if (tip) {
    const int x0 = std::max(bx0, (int)(tx - R * 2.0f)), x1 = std::min(bx1, (int)(tx + R * 2.0f));
    const int y0 = std::max(by0, (int)(ty - R * 2.0f)), y1 = std::min(by1, (int)(ty + R * 2.0f));
    for (int y = y0; y <= y1; ++y)
      for (int x = x0; x <= x1; ++x) {
        float ddx = x + 0.5f - tx, ddy = y + 0.5f - ty;
        float c = std::exp(-(ddx * ddx + ddy * ddy) / (R * R) * 1.6f) * 0.9f;
        float& d = cov[(size_t)(y - by0) * bw + (x - bx0)];
        if (c > d) d = c;
      }
  }
  const uint32_t g = s.inkGlow;
  composite(rgba, stride, bx0, by0, bw, bh, cov, ((g >> 24) & 255) / 255.0f, ((g >> 16) & 255) / 255.0f,
            ((g >> 8) & 255) / 255.0f, 1.0f);
}

}  // namespace xrsign
