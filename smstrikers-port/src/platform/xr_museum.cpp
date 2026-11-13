// strikers-quest r201: the CARD MUSEUM's pieces, their contents and spots, its page (xr_museum.h; COIN-SINKS.md 2).
#include "xr_museum.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <mutex>

#include "xr_menu.h"

namespace xrmuseum {

namespace {
const KindInfo kKindInfo[kKinds] = {
    {"WALL FRAME", "A DEEP SHADOWBOX + ITS BRASS PICTURE LIGHT. HANGS ON A WALL OF YOUR ROOM SCAN.", 250, 1, true, false},
    {"GLASS PEDESTAL", "A LACQUER PLINTH, A GLASS CUBE, A DOWNLIGHT IN ITS BRASS CROWN.", 450, 1, false, false},
    {"SLAB RACK", "WALNUT + BRASS, THREE LIT TIERS FOR 9 GRADED SLABS.", 700, 9, false, true},
    {"SHOWCASE CABINET", "BLACK + GLASS, A MIRRORED BACK, THREE LIT SHELVES, 6 CARDS.", 1500, 6, false, false},
    {"1/1 VAULT", "A RIVETED VAULT, A THICK GLASS CASE, A SPOTLIGHT ON A STEEL ARM.", 2000, 1, false, false},
};
constexpr uint32_t kBg0 = 0x0B0F22, kBg1 = 0x05060E, kPanel = 0x121A33, kPanelHi = 0x1C2850, kLine = 0x2C3A66, kInk = 0xF4F6FF,
                   kDim = 0x8E99B8, kGold = 0xFFD23F, kGreen = 0x39FF88, kRed = 0xFF4D5E, kCyan = 0x2EF2FF;

std::mutex g_m;
Wallet g_w;
std::shared_ptr<void> (*g_thumb)(int, const uint8_t**, int*, int*) = nullptr;
unsigned g_career = 0;
std::vector<Piece> g_pieces;
std::map<uint32_t, std::map<std::string, Spot>> g_spots;
uint32_t g_next = 1;
int g_gen = 1, g_roomGen = 1;
double g_now = 0.0, g_last = -1e9;
bool g_walls = false;
std::string g_msg;
uint32_t g_msgRgb = kGold;
double g_msgAt = -1e9;
// the page
int g_focus = 0;      // 0 the shop, 1 your pieces
int g_row[2] = {0, 0};
int g_top = 0;
int g_picking = 0;    // 0 no, 1 a slot, 2 a card for it
int g_slot = 0, g_list = 0, g_listTop = 0;
int g_ask = 0;        // 1 buy

std::string home_dir() {
  const char* h = std::getenv("STRIKERS_HOME");
  return std::string(h != nullptr && *h != '\0' ? h : "/storage/emulated/0/Strikers");
}
std::string museum_path(unsigned c) { return home_dir() + "/cards/museum-" + std::to_string(c) + ".txt"; }
void say(const std::string& m, uint32_t rgb) {
  g_msg = m;
  g_msgRgb = rgb;
  g_msgAt = g_now;
}
void sound(const char* n, float g) {
  if (g_w.sound != nullptr) g_w.sound(n, g);
}
std::string vtext(const xrmarket::Variant& v) {
  if (v.card < 0) return "-";
  char b[200];
  std::snprintf(b, sizeof(b), "%s,%d,%d,%d,%d,%d,%u", xrmarket::card_key(v.card).c_str(), v.parallel, v.shiny ? 1 : 0, v.hit, v.serial, v.grade, v.cert);
  return b;
}
xrmarket::Variant vread(const std::string& s) {
  xrmarket::Variant v;
  if (s == "-" || s.empty()) return v;
  char key[96] = {};
  int p = 0, sh = 0, h = 0, se = 0, g = 0;
  unsigned cert = 0;
  const size_t c = s.find(',');
  if (c == std::string::npos || c >= sizeof(key)) return v;
  for (size_t i = 0; i < c; ++i) key[i] = s[i];
  if (std::sscanf(s.c_str() + c + 1, "%d,%d,%d,%d,%d,%u", &p, &sh, &h, &se, &g, &cert) != 6) return v;
  v.card = xrmarket::card_index(key);
  v.parallel = std::max(0, std::min(3, p));
  v.shiny = sh != 0;
  v.hit = std::max(0, std::min(3, h));
  v.serial = se;
  v.grade = std::max(0, std::min(10, g));
  v.cert = cert;
  return v;
}
bool same(const xrmarket::Variant& a, const xrmarket::Variant& b) {
  return a.card == b.card && a.parallel == b.parallel && a.shiny == b.shiny && a.hit == b.hit && a.serial == b.serial && a.grade == b.grade &&
         a.cert == b.cert;
}

void save() {  // (g_m held)
  if (g_career == 0) return;
  const std::string path = museum_path(g_career), tmp = path + ".tmp";
  FILE* f = std::fopen(tmp.c_str(), "w");
  if (f == nullptr) return;
  std::fprintf(f, "museum201 1\nnext\t%u\n", g_next);
  for (const Piece& p : g_pieces) {
    std::fprintf(f, "P\t%u\t%d\t%d", p.id, p.kind, p.shown ? 1 : 0);
    for (const xrmarket::Variant& v : p.items) std::fprintf(f, "\t%s", vtext(v).c_str());
    std::fprintf(f, "\n");
  }
  for (const auto& kv : g_spots)
    for (const auto& r : kv.second)
      if (r.second.set)
        std::fprintf(f, "S\t%u\t%s\t%.4f\t%.4f\t%.4f\t%.3f\t%.4f\t%d\n", kv.first, r.first.c_str(), r.second.x, r.second.z, r.second.yaw, r.second.scale, r.second.y,
                     r.second.wall);
  std::fclose(f);
  std::rename(tmp.c_str(), path.c_str());
}
void load(unsigned career) {  // (g_m held)
  g_pieces.clear();
  g_spots.clear();
  g_next = 1;
  g_career = career;
  FILE* f = std::fopen(museum_path(career).c_str(), "r");
  if (f == nullptr) return;
  std::string all;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
  std::fclose(f);
  size_t a = 0;
  while (a < all.size()) {
    size_t b = all.find('\n', a);
    if (b == std::string::npos) b = all.size();
    const std::string line = all.substr(a, b - a);
    a = b + 1;
    std::vector<std::string> t;
    size_t p = 0;
    for (;;) {
      const size_t q = line.find('\t', p);
      t.push_back(line.substr(p, q == std::string::npos ? std::string::npos : q - p));
      if (q == std::string::npos) break;
      p = q + 1;
    }
    if (t[0] == "next" && t.size() >= 2) g_next = static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10));
    if (t[0] == "P" && t.size() >= 4) {
      Piece pc;
      pc.id = static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10));
      pc.kind = std::max(0, std::min(kKinds - 1, std::atoi(t[2].c_str())));
      pc.shown = std::atoi(t[3].c_str()) != 0;
      pc.items.assign(static_cast<size_t>(kKindInfo[pc.kind].slots), xrmarket::Variant());
      for (size_t i = 4; i < t.size() && i - 4 < pc.items.size(); ++i) pc.items[i - 4] = vread(t[i]);
      g_pieces.push_back(pc);
      g_next = std::max(g_next, pc.id + 1);
    }
    if (t[0] == "S" && t.size() >= 9) {
      Spot s;
      s.set = true;
      s.x = static_cast<float>(std::atof(t[3].c_str()));
      s.z = static_cast<float>(std::atof(t[4].c_str()));
      s.yaw = static_cast<float>(std::atof(t[5].c_str()));
      s.scale = static_cast<float>(std::atof(t[6].c_str()));
      s.y = static_cast<float>(std::atof(t[7].c_str()));
      s.wall = std::atoi(t[8].c_str());
      if (std::isfinite(s.x + s.z + s.yaw + s.scale + s.y)) g_spots[static_cast<uint32_t>(std::strtoul(t[1].c_str(), nullptr, 10))][t[2]] = s;
    }
  }
}
// your copies a piece may take (the rack: slabs only), the finest first; how many of each are already on show elsewhere
std::vector<xrmarket::Holding> choices(int kind) {
  std::vector<xrmarket::Holding> v;
  for (const xrmarket::Holding& h : xrmarket::my_copies())
    if (!h.grading && h.card >= 0 && (!kKindInfo[kind].slabsOnly || h.grade > 0)) v.push_back(h);
  std::stable_sort(v.begin(), v.end(), [](const xrmarket::Holding& a, const xrmarket::Holding& b) {
    return xrmarket::price(a) > xrmarket::price(b);
  });
  return v;
}
int on_show(const xrmarket::Variant& v) {
  int n = 0;
  for (const Piece& p : g_pieces)
    for (const xrmarket::Variant& it : p.items) n += it.card >= 0 && same(it, v) ? 1 : 0;
  return n;
}
int shown_count() {
  int n = 0;
  for (const Piece& p : g_pieces) n += p.shown ? 1 : 0;
  return n;
}
}  // namespace

const KindInfo& kind_info(int kind) { return kKindInfo[std::max(0, std::min(kKinds - 1, kind))]; }
void set_wallet(const Wallet& w) {
  std::lock_guard<std::mutex> lk(g_m);
  g_w = w;
}
void set_thumbs(std::shared_ptr<void> (*thumb)(int, const uint8_t**, int*, int*)) {
  std::lock_guard<std::mutex> lk(g_m);
  g_thumb = thumb;
}

bool frame(double now) {
  std::lock_guard<std::mutex> lk(g_m);
  g_now = now;
  if (now - g_last < 0.5) return false;
  g_last = now;
  const unsigned c = g_w.career != nullptr ? g_w.career() : 0;
  if (c != g_career) {
    load(c);
    g_focus = 0;
    g_row[0] = g_row[1] = 0;
    g_picking = 0;
    g_ask = 0;
    ++g_gen;
    ++g_roomGen;
    return true;
  }
  if (c == 0) return false;
  // a copy that's gone from your binder (sold, traded in) leaves its piece; the same copy shown twice only as often as you have it
  const std::vector<xrmarket::Holding> have = xrmarket::my_copies();
  if (have.empty()) return false;  // (the binder not read yet: nothing dropped)
  bool changed = false;
  std::vector<std::pair<xrmarket::Variant, int>> left;
  for (const xrmarket::Holding& h : have) left.push_back({h, h.grading ? 0 : h.count});
  for (Piece& p : g_pieces)
    for (xrmarket::Variant& it : p.items) {
      if (it.card < 0) continue;
      bool ok = false;
      for (auto& l : left)
        if (same(l.first, it) && l.second > 0) {
          --l.second;
          ok = true;
          break;
        }
      if (!ok) {
        it = xrmarket::Variant();
        changed = true;
      }
    }
  if (changed) {
    save();
    ++g_gen;
    ++g_roomGen;
  }
  return changed;
}
int generation() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_gen;
}
bool redraw_due() {  // r206
  std::lock_guard<std::mutex> lk(g_m);
  static bool s_fresh = false;
  const bool fresh = g_now - g_msgAt < 6.0;
  if (fresh == s_fresh) return false;
  s_fresh = fresh;
  ++g_gen;
  return true;
}
int room_generation() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_roomGen;
}
std::vector<Piece> shown() {
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<Piece> v;
  for (const Piece& p : g_pieces)
    if (p.shown && static_cast<int>(v.size()) < kMaxShown) v.push_back(p);
  return v;
}
std::vector<xrmarket::Variant> items() {  // r226
  std::lock_guard<std::mutex> lk(g_m);
  std::vector<xrmarket::Variant> v;
  for (const Piece& p : g_pieces)
    for (const xrmarket::Variant& it : p.items)
      if (it.card >= 0) v.push_back(it);
  return v;
}
Spot spot(uint32_t id, const std::string& room) {
  std::lock_guard<std::mutex> lk(g_m);
  auto it = g_spots.find(id);
  if (it == g_spots.end()) return Spot();
  auto r = it->second.find(room);
  return r != it->second.end() ? r->second : Spot();
}
void set_spot(uint32_t id, const std::string& room, const Spot& s) {
  std::lock_guard<std::mutex> lk(g_m);
  g_spots[id][room] = s;
  g_spots[id][room].set = true;
  save();
}
void set_room_scan(bool walls) {
  std::lock_guard<std::mutex> lk(g_m);
  if (walls != g_walls) {
    g_walls = walls;
    ++g_gen;
  }
}

// ============================================================================================ the page's buttons
bool busy() {
  std::lock_guard<std::mutex> lk(g_m);
  return g_picking != 0;
}
bool input(const Input& in) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_career == 0) return false;
  auto done = [&]() {
    ++g_gen;
    return true;
  };
  const int nShop = kKinds, nMine = static_cast<int>(g_pieces.size());
  if (g_ask == 1) {
    if (in.a) {
      const KindInfo& k = kKindInfo[g_row[0]];
      if (g_w.spend != nullptr && g_w.spend(g_career, k.price, "STRIKERZ MUSEUM")) {
        Piece p;
        p.id = g_next++;
        p.kind = g_row[0];
        p.items.assign(static_cast<size_t>(k.slots), xrmarket::Variant());
        p.shown = shown_count() < kMaxShown && (!k.wall || g_walls);
        g_pieces.push_back(p);
        save();
        ++g_roomGen;
        sound("book_ding", 0.8f);
        say(std::string(k.name) + " IS YOURS - " + (p.shown ? "IT'S IN YOUR ROOM. PUT A CARD IN IT (A)" : "PUT A CARD IN IT (A)"), kGreen);
        g_focus = 1;
        g_row[1] = static_cast<int>(g_pieces.size()) - 1;
      } else {
        say("NOT ENOUGH COINS - " + std::to_string(kKindInfo[g_row[0]].price) + " NEEDED", kRed);
      }
      g_ask = 0;
      return done();
    }
    if (in.b) {
      g_ask = 0;
      return done();
    }
    return true;
  }
  if (g_picking == 2) {  // a card for the slot: up / down, A puts it in, B back to the slots
    const Piece& p = g_pieces[static_cast<size_t>(g_row[1])];
    // r206: the sorted list only when a button / step needs it (r201-r205: every game frame while picking - a price per copy)
    const bool needList = (in.step && (in.dir == 3 || in.dir == 4)) || in.a;
    const std::vector<xrmarket::Holding> list = needList ? choices(p.kind) : std::vector<xrmarket::Holding>();
    if (in.step && (in.dir == 3 || in.dir == 4) && !list.empty()) {
      g_list = std::max(0, std::min(static_cast<int>(list.size()) - 1, g_list + (in.dir == 4 ? 1 : -1)));
      sound("book_tick", 0.4f);
      return done();
    }
    if (in.a && g_list < static_cast<int>(list.size())) {
      const xrmarket::Holding& h = list[static_cast<size_t>(g_list)];
      Piece& pp = g_pieces[static_cast<size_t>(g_row[1])];
      xrmarket::Variant old = pp.items[static_cast<size_t>(g_slot)];
      pp.items[static_cast<size_t>(g_slot)] = xrmarket::Variant();
      if (on_show(h) >= h.count) {
        pp.items[static_cast<size_t>(g_slot)] = old;
        say("ALL YOUR COPIES OF THAT CARD ARE ON SHOW ALREADY", kRed);
      } else {
        pp.items[static_cast<size_t>(g_slot)] = h;
        save();
        ++g_roomGen;
        sound("book_ding", 0.7f);
        say(xrmarket::card_name(h.card) + " INTO THE " + kKindInfo[pp.kind].name, kGreen);
        g_picking = 1;
        if (g_slot + 1 < static_cast<int>(pp.items.size())) ++g_slot;
      }
      return done();
    }
    if (in.b) {
      g_picking = 1;
      return done();
    }
    return true;
  }
  if (g_picking == 1) {  // its slots: left / right, A picks a card for it, X empties it, B done
    Piece& p = g_pieces[static_cast<size_t>(g_row[1])];
    const int n = static_cast<int>(p.items.size());
    if (in.step && (in.dir == 1 || in.dir == 2 || in.dir == 3 || in.dir == 4)) {
      const int d = in.dir == 2 ? 1 : (in.dir == 1 ? -1 : (in.dir == 4 ? 3 : -3));
      g_slot = std::max(0, std::min(n - 1, g_slot + d));
      sound("book_tick", 0.4f);
      return done();
    }
    if (in.a) {
      g_picking = 2;
      g_list = g_listTop = 0;
      return done();
    }
    if (in.x && p.items[static_cast<size_t>(g_slot)].card >= 0) {
      p.items[static_cast<size_t>(g_slot)] = xrmarket::Variant();
      save();
      ++g_roomGen;
      say("SLOT EMPTIED (THE CARD STAYS IN YOUR BINDER)", kDim);
      return done();
    }
    if (in.b) {
      g_picking = 0;
      return done();
    }
    return true;
  }
  if (in.step && (in.dir == 1 || in.dir == 2)) {
    g_focus = in.dir == 2 && nMine > 0 ? 1 : 0;
    sound("book_tick", 0.4f);
    return done();
  }
  if (in.step && (in.dir == 3 || in.dir == 4)) {
    const int n = g_focus == 0 ? nShop : nMine;
    if (n > 0) g_row[g_focus] = std::max(0, std::min(n - 1, g_row[g_focus] + (in.dir == 4 ? 1 : -1)));
    sound("book_tick", 0.4f);
    return done();
  }
  if (in.a) {
    if (g_focus == 0) {
      g_ask = 1;
    } else if (g_row[1] < nMine) {
      g_picking = 1;
      g_slot = 0;
    }
    return done();
  }
  if (in.x && g_focus == 1 && g_row[1] < nMine) {
    Piece& p = g_pieces[static_cast<size_t>(g_row[1])];
    if (!p.shown && shown_count() >= kMaxShown) {
      say("6 PIECES OUT AT ONCE - PUT ONE AWAY FIRST", kRed);
    } else if (!p.shown && kKindInfo[p.kind].wall && !g_walls) {
      say("A FRAME HANGS ON A WALL: SET UP YOUR ROOM SCAN (SETTINGS > ROOM)", kRed);
    } else {
      p.shown = !p.shown;
      save();
      ++g_roomGen;
      say(p.shown ? "OUT IN YOUR ROOM - LEFT GRIP MOVES IT, BOTH GRIPS TURN + SIZE IT" : "PUT AWAY", p.shown ? kGreen : kDim);
      sound("book_tick", 0.6f);
    }
    return done();
  }
  return in.x || in.y;
}
bool asking(std::string* q, std::string* line) {
  std::lock_guard<std::mutex> lk(g_m);
  if (g_ask != 1) return false;
  const KindInfo& k = kKindInfo[g_row[0]];
  *q = std::string("BUY THE ") + k.name + " FOR " + std::to_string(k.price) + " COINS?";
  *line = std::string(k.wall && !g_walls ? "IT HANGS ON A WALL: IT NEEDS YOUR ROOM SCAN  -  " : "") + "YOU HAVE " +
          xrcoin::text(g_w.balance != nullptr ? g_w.balance() : 0) + " BIT COINS";  // (r208: commas)
  return true;
}
void hints(std::string* aWord, bool* aDim, std::string* xWord, std::string* yWord) {
  std::lock_guard<std::mutex> lk(g_m);
  *yWord = "";
  *aDim = false;
  *xWord = "";
  if (g_picking == 2) *aWord = "PUT IT IN";
  else if (g_picking == 1) *aWord = "PICK A CARD", *xWord = "EMPTY SLOT";
  else if (g_focus == 0) *aWord = "BUY";
  else {
    *aWord = "FILL IT";
    *xWord = g_row[1] < static_cast<int>(g_pieces.size()) && g_pieces[static_cast<size_t>(g_row[1])].shown ? "PUT AWAY" : "SHOW IN ROOM";
  }
}

// ============================================================================================ the drawing
namespace {
void thumb(xrmenu::Kit& K, std::vector<std::shared_ptr<void>>& keep, int kind, float x0, float y0, float x1, float y1) {
  K.box(x0, y0, x1, y1, 16, 0x0A0E1E, 1.0f, 0x141C38);
  if (g_thumb == nullptr) return;
  const uint8_t* px = nullptr;
  int w = 0, h = 0;
  std::shared_ptr<void> k = g_thumb(kind, &px, &w, &h);
  if (px == nullptr || w <= 0 || h <= 0) return;
  keep.push_back(k);
  xrtext::BoardPiece p;
  p.kind = xrtext::BoardPiece::kImage;
  p.x0 = x0 + 4;
  p.y0 = y0 + 4;
  p.x1 = x1 - 4;
  p.y1 = y1 - 4;
  p.img = px;
  p.iw = w;
  p.ih = h;
  p.crop = true;
  K.v.push_back(p);
}
void coin(xrmenu::Kit& K, float cx, float cy, float r) {
  K.glow(cx - 2.0f * r, cy - 2.0f * r, cx + 2.0f * r, cy + 2.0f * r, kGold, 0.3f);
  K.box(cx - r, cy - r, cx + r, cy + r, r, 0xB8860B);
  K.box(cx - 0.86f * r, cy - 0.86f * r, cx + 0.86f * r, cy + 0.86f * r, 0.86f * r, kGold, 1.0f, 0xE0A800);
  // r216: a BIT COIN - the pixel ring + the 8-bit B (was a "T")
  xrcoin::bit_face([&](float x0, float y0, float x1, float y1, uint32_t rgb) { K.box(x0, y0, x1, y1, 0, rgb); }, cx, cy, r);
}
}  // namespace

void draw(std::vector<xrtext::BoardPiece>& out, std::vector<std::shared_ptr<void>>& keep, float ox, float oy, float k, float t) {
  std::vector<xrtext::BoardPiece> v;
  {
    std::lock_guard<std::mutex> lk(g_m);
    xrmenu::Kit K(v);
    const bool bcast = xrmenu::broadcast();  // r236: inside the hub (LOOK B): the hub's header says where / the coins
    if (bcast) {
      xrmenu::label(K, g_walls ? "ROOM SCAN: WALLS READY" : "NO ROOM SCAN: FLOOR PIECES ONLY", 20, 880, 30, g_walls ? kGreen : 0xFFB020, 'l', 900);  // (at the foot: the page's own words at the top)
    } else {
    K.box(0, 0, kW, kH, 30, kBg0, 1.0f, kBg1);
    K.glow(-200, -200, 520, 360, kGold, 0.16f);
    K.glow(kW - 520, kH - 360, kW + 200, kH + 200, kCyan, 0.12f);
    {  // the brand: LANCINI TIZM (chrome) CARD MUSEUM (gold)
      xrtext::BoardPiece p;
      p.kind = xrtext::BoardPiece::kTextLeft;
      p.x0 = 22;
      p.y0 = 60;
      p.x1 = 290;
      p.y1 = 88;
      p.rgb = 0xFFFFFF;
      p.rgb2 = 0x9FB2D6;
      p.outline = 7.0f;
      p.outlineRgb = 0x0E1430;
      p.shadow = 5.0f;
      p.shadowRgb = kGold;
      p.shadowA = 0.8f;
      p.text = "STRIKERZ";  // r217 (Lance: the card feature pages are STRIKERZ)
      v.push_back(p);
      p.x0 = 330;
      p.y0 = 64;
      p.x1 = 430;
      p.y1 = 60;
      p.rgb = 0xFFF2B0;
      p.rgb2 = kGold;
      p.outline = 4.0f;
      p.outlineRgb = 0x2A1A00;
      p.shadow = 0.0f;
      p.text = "CARD MUSEUM";
      v.push_back(p);
    }
    K.text(g_walls ? "ROOM SCAN: WALLS READY" : "NO ROOM SCAN: FLOOR PIECES ONLY", 1030, 62, 26, 460, g_walls ? kGreen : 0xFFB020, 0.0f);
    coin(K, 1458, 52, 24);
    K.right(xrcoin::fit(g_w.balance != nullptr ? g_w.balance() : 0, 9), kW - 16, 54, 46, 170, kGold, 2.0f);  // (r208: past 9 characters short)
    K.right("BIT COINS", kW - 16, 98, 22, 170, kDim, 0.0f);
    }  // (r236: the old header - outside the hub)
    const size_t from = v.size();
    if (g_career == 0) {
      K.text("LOAD A FRANCHISE CAREER", 825, 520, 64, 1400, kDim, 2.0f);
    } else {
      // the shop (left)
      K.left("THE SHOP", 24, 150, 30, 300, g_focus == 0 && g_picking == 0 ? kGold : kDim, 0.0f);
      for (int i = 0; i < kKinds; ++i) {
        const KindInfo& ki = kKindInfo[i];
        const float y0 = 176 + i * 144.0f, y1 = y0 + 134, x0 = 0, x1 = 620;
        const bool on = g_focus == 0 && g_row[0] == i && g_picking == 0;
        int owned = 0;
        for (const Piece& p : g_pieces) owned += p.kind == i ? 1 : 0;
        K.box(x0, y0, x1, y1, 22, on ? kPanelHi : kPanel);
        if (on) xrmenu::focus(K, x0, y0, x1, y1, 22);
        thumb(K, keep, i, x0 + 12, y0 + 10, x0 + 132, y1 - 10);
        K.left(ki.name, x0 + 150, y0 + 34, 36, 300, kInk, 2.0f);
        if (!bcast || on) K.left(ki.line, x0 + 150, y0 + 74, 19, 450, 0xB8C2E0, 0.0f);  // (r236: in the hub, the focused piece's line only)
        if (ki.wall && !g_walls) K.left("NEEDS YOUR ROOM SCAN", x0 + 150, y0 + 108, 20, 300, 0xFFB020, 0.0f);
        else if (owned > 0) K.left("YOU HAVE " + std::to_string(owned), x0 + 150, y0 + 108, 20, 300, kGreen, 0.0f);
        coin(K, x1 - 150, y0 + 34, 14);
        K.right(std::to_string(ki.price), x1 - 20, y0 + 36, 40, 120, kGold, 2.0f);
      }
      // your pieces (right), or a piece's slots + your cards while filling it
      const float X0 = 650, X1 = kW;
      if (g_picking == 0) {
        K.left("YOUR PIECES", X0 + 10, 150, 30, 300, g_focus == 1 ? kGold : kDim, 0.0f);
        K.right(std::to_string(shown_count()) + " OF 6 OUT IN YOUR ROOM", X1 - 10, 150, 24, 400, kDim, 0.0f);
        if (g_pieces.empty()) {
          K.text("NOTHING YET", 0.5f * (X0 + X1), 420, 60, X1 - X0 - 40, kDim, 2.0f);
          K.text("BUY A PIECE, PUT YOUR BEST CARDS IN IT,", 0.5f * (X0 + X1), 500, 30, X1 - X0 - 40, kDim, 0.0f);
          K.text("SHOW THEM OFF IN YOUR ROOM - THEIR FIGURES COME WITH THEM", 0.5f * (X0 + X1), 544, 30, X1 - X0 - 40, kDim, 0.0f);
        }
        const int rows = 5;
        g_row[1] = std::max(0, std::min(std::max(0, static_cast<int>(g_pieces.size()) - 1), g_row[1]));
        if (g_row[1] < g_top) g_top = g_row[1];
        if (g_row[1] >= g_top + rows) g_top = g_row[1] - rows + 1;
        for (int r = 0; r < rows && g_top + r < static_cast<int>(g_pieces.size()); ++r) {
          const Piece& p = g_pieces[static_cast<size_t>(g_top + r)];
          const float y0 = 176 + r * 144.0f, y1 = y0 + 134;
          const bool on = g_focus == 1 && g_row[1] == g_top + r;
          K.box(X0, y0, X1, y1, 22, on ? kPanelHi : kPanel);
          if (on) xrmenu::focus(K, X0, y0, X1, y1, 22);
          K.left(kKindInfo[p.kind].name, X0 + 20, y0 + 32, 32, 360, kInk, 2.0f);
          K.left(p.shown ? "IN YOUR ROOM" : "PUT AWAY", X0 + 20, y0 + 70, 22, 300, p.shown ? kGreen : kDim, 0.0f);
          int filled = 0;
          for (const xrmarket::Variant& it : p.items) filled += it.card >= 0 ? 1 : 0;
          K.left(std::to_string(filled) + " / " + std::to_string(p.items.size()) + " FILLED", X0 + 20, y0 + 104, 22, 300, kDim, 0.0f);
          for (size_t s = 0; s < p.items.size() && s < 9; ++s) {
            const float cx0 = X0 + 380 + s * 66.0f, cx1 = cx0 + 58;
            if (p.items[s].card >= 0) {
              K.v.push_back(xrtext::BoardPiece());
              K.v.pop_back();
              std::vector<xrtext::BoardPiece> mv;
              xrmarket::draw_mini(mv, keep, p.items[s], cx0, y0 + 18, cx1, y1 - 18);
              for (auto& m : mv) v.push_back(std::move(m));
            } else {
              K.box(cx0, y0 + 18, cx1, y1 - 18, 8, 0x0A0E1E);
              K.ring(cx0, y0 + 18, cx1, y1 - 18, 8, 2, kLine);
              K.text("+", 0.5f * (cx0 + cx1), 0.5f * (y0 + y1), 34, 50, kLine, 0.0f);
            }
          }
        }
      } else {
        const Piece& p = g_pieces[static_cast<size_t>(g_row[1])];
        K.left(std::string("FILL YOUR ") + kKindInfo[p.kind].name, X0 + 10, 150, 30, 600, kGold, 0.0f);
        K.right(kKindInfo[p.kind].slabsOnly ? "TIZM GRADING SLABS ONLY" : "ANY CARD OR SLAB OF YOURS", X1 - 10, 150, 24, 400, kDim, 0.0f);
        // the slots (3 a row)
        const int n = static_cast<int>(p.items.size());
        const float sw = 96, sh = 134;
        for (int s = 0; s < n; ++s) {
          const float sx0 = X0 + 20 + (s % 3) * (sw + 14), sy0 = 182 + (s / 3) * (sh + 14);
          const bool on = s == g_slot;
          if (p.items[static_cast<size_t>(s)].card >= 0) {
            xrmarket::draw_mini(v, keep, p.items[static_cast<size_t>(s)], sx0, sy0, sx0 + sw, sy0 + sh);
          } else {
            K.box(sx0, sy0, sx0 + sw, sy0 + sh, 10, 0x0A0E1E);
            K.text("+", sx0 + sw * 0.5f, sy0 + sh * 0.5f, 44, 60, kLine, 0.0f);
          }
          if (on) xrmenu::focus(K, sx0 - 4, sy0 - 4, sx0 + sw + 4, sy0 + sh + 4, 12);
        }
        const xrmarket::Variant& cur = p.items[static_cast<size_t>(g_slot)];
        K.left("SLOT " + std::to_string(g_slot + 1) + (cur.card >= 0 ? ": " + xrmarket::card_name(cur.card) : ": EMPTY"), X0 + 20, 646, 28, 330, kInk, 0.0f);
        // your cards (the list), while picking
        const float L0 = X0 + 380;
        K.box(L0, 176, X1, 890, 22, 0x0E152C);
        K.ring(L0, 176, X1, 890, 22, g_picking == 2 ? 3.0f : 2.0f, g_picking == 2 ? kGold : kLine);
        const std::vector<xrmarket::Holding> list = choices(p.kind);
        if (list.empty())
          K.text(kKindInfo[p.kind].slabsOnly ? "NO SLABS YET - SEND CARDS TO TIZM GRADING" : "NO CARDS YET", 0.5f * (L0 + X1), 520, 34, X1 - L0 - 40, kDim, 0.0f);
        constexpr int kRows = 6;
        g_list = std::max(0, std::min(std::max(0, static_cast<int>(list.size()) - 1), g_list));
        if (g_list < g_listTop) g_listTop = g_list;
        if (g_list >= g_listTop + kRows) g_listTop = g_list - kRows + 1;
        for (int r = 0; r < kRows && g_listTop + r < static_cast<int>(list.size()); ++r) {
          const xrmarket::Holding& h = list[static_cast<size_t>(g_listTop + r)];
          const float y0 = 192 + r * 114.0f, y1 = y0 + 104;
          const bool on = g_picking == 2 && g_list == g_listTop + r;
          K.box(L0 + 14, y0, X1 - 14, y1, 16, on ? kPanelHi : kPanel);
          if (on) xrmenu::focus(K, L0 + 14, y0, X1 - 14, y1, 16);
          xrmarket::draw_mini(v, keep, h, L0 + 26, y0 + 8, L0 + 90, y1 - 8);
          K.left(xrmarket::card_name(h.card), L0 + 110, y0 + 36, 34, 420, kInk, 2.0f);
          K.left(xrmarket::variant_words(h), L0 + 110, y0 + 74, 22, 420, kGold, 0.0f);
          const int shownN = on_show(h);
          if (shownN > 0) K.right(shownN >= h.count ? "ON SHOW" : "1 ON SHOW", X1 - 30, y0 + 74, 22, 200, kGreen, 0.0f);
          K.right(xrcoin::fit(xrmarket::price(h), 7), X1 - 30, y0 + 38, 34, 160, kInk, 0.0f);  // (r210: a grail's millions short)
        }
      }
      // the foot: the last word, else how it works
      const bool fresh = g_now - g_msgAt < 6.0;
      K.box(0, 904, kW, 958, 26, 0x070B18);
      K.ring(0, 904, kW, 958, 26, 2, fresh ? g_msgRgb : 0x2C3A66);
      if (!bcast || fresh)  // (r236: in the hub only its last word - the how-it-works sentence was always on)
        K.text(fresh ? g_msg : std::string("CARDS STAY IN YOUR BINDER WHILE THEY'RE ON SHOW  -  LEFT GRIP MOVES A PIECE, BOTH GRIPS TURN + SIZE IT  -  6 OUT AT ONCE"),
               0.5f * kW, 932, 26, kW - 60, fresh ? g_msgRgb : 0xFFB020, 0.0f);
    }
    if (bcast)  // r236: the page under the hub's header (it started at 150 under its own): up 110; its foot stays
      for (size_t i = from; i < v.size(); ++i) {
        xrtext::BoardPiece& p = v[i];
        if (p.y0 >= 900.0f) continue;
        p.y0 -= 110.0f;
        const bool text = p.kind == xrtext::BoardPiece::kText || p.kind == xrtext::BoardPiece::kTextLeft || p.kind == xrtext::BoardPiece::kTextRight;
        if (!text) p.y1 -= 110.0f;
        for (size_t q = 1; q < p.pts.size(); q += 2) p.pts[q] -= 110.0f;
      }
  }
  (void)t;
  for (xrtext::BoardPiece& p : v) {
    p.x0 = ox + p.x0 * k;
    p.y0 = oy + p.y0 * k;
    const bool text = p.kind == xrtext::BoardPiece::kText || p.kind == xrtext::BoardPiece::kTextLeft || p.kind == xrtext::BoardPiece::kTextRight;
    if (text) {
      p.x1 *= k;
      p.y1 *= k;
    } else {
      p.x1 = ox + p.x1 * k;
      p.y1 = oy + p.y1 * k;
    }
    p.radius *= k;
    p.outline *= k;
    p.shadow *= k;
    p.cut *= k;  // r236
    p.lean *= k;
    if (xrmenu::broadcast() && text && !p.styled && !p.art && p.y1 >= 14.0f && p.y1 < 30.0f * k) p.y1 = 30.0f * k;  // r236: nothing under 30 px in the hub
    for (size_t i = 0; i < p.pts.size(); ++i) p.pts[i] = (i % 2 == 0 ? ox : oy) + p.pts[i] * k;
    out.push_back(std::move(p));
  }
}

void debug_add(int kind, const std::vector<xrmarket::Variant>& items) {
  std::lock_guard<std::mutex> lk(g_m);
  Piece p;
  p.id = g_next++;
  p.kind = kind;
  p.items.assign(static_cast<size_t>(kKindInfo[kind].slots), xrmarket::Variant());
  for (size_t i = 0; i < items.size() && i < p.items.size(); ++i) p.items[i] = items[i];
  g_pieces.push_back(p);
  ++g_gen;
}
void debug_cursor(int focus, int row, int pick, int slot) {
  std::lock_guard<std::mutex> lk(g_m);
  g_focus = focus;
  g_row[focus] = row;
  g_picking = pick;
  g_slot = slot;
  g_ask = 0;
}

}  // namespace xrmuseum
