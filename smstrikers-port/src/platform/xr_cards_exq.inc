// strikers-quest r230 (Lance 11-12, C:\StrikersMR\EXQUISITE.md): LANCINI EXQUISITE - the set (set 2: its card ids key + "_exq", after the
// JAM '96 set in g_set: no index of either set moves), its packs (3 cards: 2 numbered + 1 hit, every number unique per character per
// career - xr_exq.h), its words. Included inside xr_cards.cpp's anonymous namespace after roll_pack (its Card / Ref / Pack / g_* / world_*).
// (g_mutex held wherever a function says so.)

bool exq_card(int card) { return card >= 0 && card < static_cast<int>(g_set.size()) && g_set[static_cast<size_t>(card)].set == 2; }
int exq_tier(const Ref& r) { return exq_card(r.card) ? xrexq::tier_of(r.serial) : 0; }
// an ORIGINALS rookie (the RC shield, the ROOKIE PATCH AUTOS)
bool exq_rookie(const Card& c) { return c.series == 5; }

// this career's collector world holds this numbered copy (COLLECTOR TRADES tells its copies as "<id>#<serial>" beside its 1/1s' ids)
bool world_serial(int card, int serial) {  // (g_mutex held)
  if (!exq_card(card)) return false;
  world_black(card);  // (this career's list read first if it wasn't yet)
  std::lock_guard<std::mutex> lock(g_worldM);
  return std::binary_search(g_worldKeys.begin(), g_worldKeys.end(), g_set[static_cast<size_t>(card)].id + "#" + std::to_string(serial));
}
// a number of a card's run is taken: yours (in the binder, a slab, a pack waiting), in the pack being rolled, a CPU collector's.
// The MASTERPIECE: one per character per career (the 1/1 rule) - any 1/1 of it anywhere. (g_mutex held)
bool exq_taken(int card, int serial, const std::vector<Ref>& also) {
  if (card < 0 || card >= static_cast<int>(g_owned.size())) return true;
  const Owned& o = g_owned[static_cast<size_t>(card)];
  const bool one = xrexq::tier_of(serial) == xrexq::kMasterpiece;
  auto same = [&](const Ref& q) { return q.card == card && (q.serial == serial || (one && xrexq::tier_of(q.serial) == xrexq::kMasterpiece)); };
  for (const Ref& h : o.hits)
    if (same(h)) return true;
  for (const Ref& h : o.sealed)
    if (same(h)) return true;
  for (const Slab& s : g_slabs)
    if (same(s.ref)) return true;
  for (const Pack& p : g_packs)
    for (const Ref& q : p.cards)
      if (same(q)) return true;
  for (const Ref& q : also)
    if (same(q)) return true;
  if (one) return world_black(card);
  return world_serial(card, serial);
}
// a free number of the card's run in this tier (0 none): a few random tries, then a walk from a random start - every free number as likely
int exq_free_number(uint64_t& rng, int card, int tier, const std::vector<Ref>& also) {
  const int run = xrexq::kRun[tier];
  if (run <= 0) return 0;
  if (run == 1) return exq_taken(card, xrexq::serial_of(tier, 1), also) ? 0 : 1;
  for (int k = 0; k < 24; ++k) {
    const int n = 1 + static_cast<int>(mix64(rng) % static_cast<uint64_t>(run));
    if (!exq_taken(card, xrexq::serial_of(tier, n), also)) return n;
  }
  const int start = static_cast<int>(mix64(rng) % static_cast<uint64_t>(run));
  for (int k = 0; k < run; ++k) {
    const int n = 1 + (start + k) % run;
    if (!exq_taken(card, xrexq::serial_of(tier, n), also)) return n;
  }
  return 0;
}
Ref exq_ref(int card, int tier, int num) {
  Ref r;
  r.card = card;
  r.parallel = xrexq::kClass[tier];
  r.hit = xrexq::kSigned[tier] ? kHitSigned : kHitNone;
  r.shiny = false;
  r.serial = xrexq::serial_of(tier, num);
  return r;
}
int exq_score(const Ref& r) { return xrexq::rank(exq_tier(r)) * 100 + (r.card >= 0 ? g_set[static_cast<size_t>(r.card)].rarity : 0); }
// r230: a LANCINI EXQUISITE pack - 2 numbered + 1 hit (xr_exq.h's rates), every number free, never the same character twice; a sold-out
// run rolls down a tier (1/1 -> /10 -> ... ; the hit's 1/1 -> PATCH AUTO /10 -> PATCH /25 -> AUTO /49), then another character. Ordered
// for the opening: the biggest last. (g_mutex held)
Pack roll_exq_pack(uint64_t& rng, const std::vector<Ref>& earlier) {
  std::vector<int> pool, rookies;
  for (size_t i = 0; i < g_set.size(); ++i)
    if (g_set[i].here && g_set[i].set == 2) {
      pool.push_back(static_cast<int>(i));
      if (exq_rookie(g_set[i])) rookies.push_back(static_cast<int>(i));
    }
  Pack p;
  if (pool.empty()) return p;
  std::vector<Ref> also = earlier;
  for (int slot = 0; slot < xrexq::kPackCards; ++slot) {
    const bool hitSlot = slot == xrexq::kPackCards - 1;
    const int want = hitSlot ? xrexq::roll_hit(rand01(rng)) : xrexq::roll_num(rand01(rng));
    Ref got;
    for (int tries = 0; tries < 24 && got.card < 0; ++tries) {
      int t = want;
      if (t == xrexq::kRpa && rookies.empty()) t = xrexq::kAuto;  // (a headset without the ORIGINALS)
      const std::vector<int>& from = t == xrexq::kRpa ? rookies : pool;
      const int pick = from[static_cast<size_t>(mix64(rng) % from.size())];
      bool twice = false;
      for (int k = 0; k < slot; ++k) twice = twice || p.cards[k].card == pick;
      if (twice) continue;
      for (int tt = t; tt > 0; tt = xrexq::down_in_slot(tt, hitSlot)) {
        const int n = exq_free_number(rng, pick, tt, also);
        if (n > 0) {
          got = exq_ref(pick, tt, n);
          break;
        }
      }
    }
    if (got.card < 0) break;  // (every run of every character sold out: nothing left to give)
    p.cards[slot] = got;
    also.push_back(got);
  }
  std::stable_sort(p.cards, p.cards + xrexq::kPackCards, [](const Ref& a, const Ref& b) { return exq_score(a) < exq_score(b); });
  return p;
}
// "EXQUISITE SAPPHIRE MARIO 03/10", "EXQUISITE MASTERPIECE 1/1 CHARIZARD", "EXQUISITE ROOKIE PATCH AUTO REDLINE 42/99"
std::string exq_pull_text(const Ref& r) {
  const Card& c = g_set[static_cast<size_t>(r.card)];
  const int t = xrexq::tier_of(r.serial);
  std::string s = "EXQUISITE " + std::string(xrexq::name(t, exq_rookie(c))) + " " + upper(c.name);
  return t == xrexq::kMasterpiece ? "EXQUISITE MASTERPIECE 1/1 " + upper(c.name) + " PATCH AUTO" : s + " " + xrexq::num_text(r.serial);
}
// a card's best Exquisite copy (its highest tier, the lowest number of it; card -1 none). (g_mutex held)
Ref exq_best(const Owned& o, bool sealedToo) {
  Ref best;
  auto take = [&](const Ref& h) {
    if (xrexq::tier_of(h.serial) == 0) return;
    if (best.card < 0 || exq_score(h) > exq_score(best) || (exq_score(h) == exq_score(best) && h.serial < best.serial)) best = h;
  };
  for (const Ref& h : o.hits) take(h);
  if (sealedToo)
    for (const Ref& h : o.sealed) take(h);
  return best;
}

// the GRAND THREADS patch's colours: its character's club in this career (r229's 32 clubs; the market's franchise lines, asked on the game
// thread every few seconds - never from a drawing thread under the binder's lock), else a club of the league picked by its key
std::mutex g_exqClubM;  // (a leaf)
std::unordered_map<int, int> g_exqClub;
uint64_t g_exqClubNs = 0;
void exq_clubs_refresh(uint64_t now) {  // (game thread, no lock held)
  if (g_exqClubNs != 0 && now < g_exqClubNs + 5000000000ull) return;
  g_exqClubNs = now;
  std::vector<int> cards;
  {
    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < g_set.size(); ++i)
      if (g_set[i].set == 2) cards.push_back(static_cast<int>(i));
  }
  std::unordered_map<int, int> m;
  for (int c : cards) m[c] = xrmarket::club_of(c);
  std::lock_guard<std::mutex> lock(g_exqClubM);
  g_exqClub.swap(m);
}
void exq_club(const Card& c, uint32_t* c1, uint32_t* c2) {
  *c1 = c.c1, *c2 = c.c2;
  const int n = PortFranchiseClubCount();
  if (n <= 0) return;
  int club = -1;
  {
    std::lock_guard<std::mutex> lock(g_exqClubM);
    for (const auto& kv : g_exqClub)
      if (kv.first >= 0 && kv.first < static_cast<int>(g_set.size()) && g_set[static_cast<size_t>(kv.first)].key == c.key) club = kv.second;
  }
  if (club < 0 || club >= n) {
    uint32_t h = 2166136261u;
    for (char ch : c.key) h = (h ^ static_cast<unsigned char>(ch)) * 16777619u;
    club = static_cast<int>(h % static_cast<uint32_t>(n));
  }
  *c1 = PortFranchiseClubColor(club, 0) & 0xFFFFFFu;
  *c2 = PortFranchiseClubColor(club, 1) & 0xFFFFFFu;
  if (luma(*c1) < 0.06f && luma(*c2) < 0.06f) *c2 = PortFranchiseClubColor(club, 2) & 0xFFFFFFu;  // (two near-black colours: its accent)
}