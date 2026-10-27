// Super Mario Strikers (Quest) room server: a relay and a quick-match queue, nothing more.
//
// Rooms: each room code is one Durable Object; every headset in the room holds a WebSocket to it,
// and each binary message one headset sends goes to every headset in the room, the sender included
// (the game hears its own messages and skips them, and uses them to know the connection is alive).
// The game itself decides everything else (who hosts, the match setup, the lockstep): see
// src/platform/xr_net.cpp in the port.
//
// Quick match: a headset with nobody to play waits in a queue, one Durable Object per KEY (the game
// builds the KEY from its edition and protocol, e.g. "P5", so only builds that can play each other
// meet; the server never reads it). The two oldest waiting headsets get the same fresh room code,
// are closed, and join that room like any other.
//
// Tournaments: one Durable Object per KEY (as for quick match) keeps the teams, a filling lobby for
// 4 and one for 8, and the brackets. Each tournament match is an ordinary room code on the relay;
// the headsets report the result here afterwards. JSON text messages; see the Tourneys class.
//
// Leagues (r100): one Durable Object per league code keeps an online franchise league for 2-8 headsets:
// its members (a headset's pid, name and club), the shared career (the franchise's save text, with a
// version: an upload only lands on the version it was made from), and who is ready to play whom; two
// members ready for their game get a fresh relay room code. JSON text; see the League class.
//
//   wss://<worker>/room/<CODE>     CODE = 3-8 letters/digits, upper case
//   wss://<worker>/quick/<KEY>     KEY  = 2-12 letters/digits, upper case
//   wss://<worker>/tourney/<KEY>   KEY  = 2-12 letters/digits, upper case
//   wss://<worker>/league/<CODE>   CODE = 4-8 letters/digits, upper case (the game makes 6)
//
// Quick-match messages are text. Headset -> server: "ping" every ~5 s. Server -> headset:
// "waiting <n>" (headsets searching, itself included) after any change and in answer to a ping,
// or "match <CODE>" and then a close (1000 "matched"). Silent for 20 s = dropped (4001 "stale").
//
// Free plan: SQLite-backed Durable Objects with the WebSocket hibernation API, so an idle room,
// queue or tournament menu costs nothing.

const MAX_PER_ROOM = 4; // two players, plus room for a reconnect overlapping a stale socket
const MAX_PER_QUEUE = 200; // far more than ever wait at once; only stops a flood
const STALE_MS = 20000; // the game pings every ~5 s, so this is three or four missed pings
const CODE_CHARS = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no 0/O/1/I; 32 chars, so byte % 32 is fair

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const room = url.pathname.match(/^\/room\/([A-Z0-9]{3,8})$/);
    const quick = url.pathname.match(/^\/quick\/([A-Z0-9]{2,12})$/);
    const tourney = url.pathname.match(/^\/tourney\/([A-Z0-9]{2,12})$/);
    const league = url.pathname.match(/^\/league\/([A-Z0-9]{4,8})$/); // r100
    if (!room && !quick && !tourney && !league) {
      return new Response("Super Mario Strikers Quest room server\n", {
        headers: { "content-type": "text/plain" },
      });
    }
    if (request.headers.get("Upgrade") !== "websocket") {
      return new Response("expected a WebSocket upgrade\n", { status: 426 });
    }
    const ns = room ? env.ROOMS : quick ? env.QUEUES : tourney ? env.TOURNEYS : env.LEAGUES;
    return ns.get(ns.idFromName((room || quick || tourney || league)[1])).fetch(request);
  },
};

export class Room {
  constructor(state, env) {
    this.state = state;
  }

  async fetch(request) {
    const open = this.state.getWebSockets();
    if (open.length >= MAX_PER_ROOM) {
      // The oldest goes: a headset that reconnected leaves its dead socket behind for a while.
      // r44: found by when it came (the attachment), not by place: getWebSockets() is not
      // oldest-first, and taking [0] could drop the fresh reconnect and keep the dead one.
      let oldest = open[0];
      for (const ws of open) {
        if (since_of(ws) < since_of(oldest)) oldest = ws;
      }
      try {
        oldest.close(4000, "room full, oldest dropped");
      } catch (e) {}
    }
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.state.acceptWebSocket(server);
    server.serializeAttachment({ since: Date.now() });
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage(ws, message) {
    if (typeof message === "string" || message.byteLength > 4096) return; // game messages are small binary
    for (const peer of this.state.getWebSockets()) {
      try {
        peer.send(message);
      } catch (e) {}
    }
  }

  webSocketClose(ws, code, reason, wasClean) {
    try {
      ws.close(code, reason);
    } catch (e) {}
  }

  webSocketError(ws, error) {
    try {
      ws.close(1011, "error");
    } catch (e) {}
  }
}

export class Matchmaker {
  constructor(state, env) {
    this.state = state;
  }

  async fetch(request) {
    if (this.state.getWebSockets().length >= MAX_PER_QUEUE) {
      return new Response("queue full\n", { status: 503 });
    }
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.state.acceptWebSocket(server);
    const now = Date.now();
    // The attachment survives hibernation, unlike anything kept on `this`.
    server.serializeAttachment({ since: now, seen: now });
    this.update();
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage(ws, message) {
    if (typeof message !== "string" || message.length > 64) return; // the game only sends "ping"
    const a = ws.deserializeAttachment();
    a.seen = Date.now();
    ws.serializeAttachment(a);
    this.update();
  }

  webSocketClose(ws, code, reason, wasClean) {
    try {
      ws.close(1000, "bye");
    } catch (e) {}
    this.update(ws);
  }

  webSocketError(ws, error) {
    try {
      ws.close(1011, "error");
    } catch (e) {}
    this.update(ws);
  }

  // Drop the stale, pair the oldest live sockets two by two, and tell whoever is left how many are
  // searching. Runs on every arrival, message and departure. There is no timer (it would keep the
  // object awake): a stale socket only matters once someone else arrives, and that runs this.
  update(gone) {
    const now = Date.now();
    const live = [];
    for (const ws of this.state.getWebSockets()) {
      if (ws === gone || ws.readyState !== WebSocket.OPEN) continue; // closing already
      const { since, seen } = ws.deserializeAttachment();
      if (now - Math.max(since, seen) > STALE_MS) {
        try {
          ws.close(4001, "stale");
        } catch (e) {}
      } else {
        live.push({ ws, since });
      }
    }
    live.sort((x, y) => x.since - y.since); // getWebSockets() is not oldest-first (wrangler dev: newest)
    while (live.length >= 2) {
      const code = roomCode();
      for (const { ws } of live.splice(0, 2)) {
        try {
          ws.send("match " + code);
          ws.close(1000, "matched");
        } catch (e) {}
      }
    }
    for (const { ws } of live) {
      try {
        ws.send("waiting " + live.length);
      } catch (e) {}
    }
  }
}

// When a socket was accepted; 0 for one accepted before r44 (no attachment), so it counts as oldest.
function since_of(ws) {
  try {
    const a = ws.deserializeAttachment();
    return a && typeof a.since === "number" ? a.since : 0;
  } catch (e) {
    return 0;
  }
}

// "Q" (for quick) + 5 random characters: 32^5 = 33 million codes, so two matches never share a room.
function roomCode() {
  let code = "Q";
  for (const b of crypto.getRandomValues(new Uint8Array(5))) code += CODE_CHARS[b % CODE_CHARS.length];
  return code;
}

// ---------------------------------------------------------------------------------------------
// Tournaments
// ---------------------------------------------------------------------------------------------
//
// One object per KEY. A team (a headset's persistent 16-hex pid plus its name) says hello, joins
// the filling lobby of 4 or of 8, and when that is full the tournament starts: single elimination,
// every match a fresh "T....." room code on the relay. Both headsets report each match afterwards;
// the server checks the two reports against each other, replays draws and disagreements in a new
// room, and moves the winner on. Everything it knows is in storage ("s": teams and lobbies,
// "t:<tid>": one tournament each), so hibernation or a restart loses nothing; a socket only
// carries its pid (the attachment). README.md has the messages and the rules.

const SIZES = [4, 8];
const MAX_TEAMS = 400; // per KEY; past this, teams with nothing going on are forgotten, oldest first
const MAX_TOURNEYS = 50; // per KEY; a finished one still on show gives way to a new one
const MAX_SOCKETS = 600; // per KEY; only stops a flood
const MAX_PER_TEAM = 4; // a menu socket, plus reconnects whose dead socket has not timed out
const MAX_TEXT = 2048; // the game's messages are far smaller
const TIMES = {
  report: 3 * 60000, // one side's report stands alone this long, then it is taken
  noshow: 5 * 60000, // from a match being ready until "they never came" may be claimed
  grace: 90000, // a team in a filling lobby with no socket keeps its place this long
  stale: 60000, // a lobby socket silent this long is a headset gone without a close
  keep: 30 * 60000, // a finished tournament stays on show this long, then its teams are free
  // An unfinished one with no report and no result this long is abandoned (both teams of some
  // match gone for good): dropped, so its teams can play again and it stops counting to the cap.
  idle: 60 * 60000,
};
// `wrangler dev --var TEST_FAST:1` plus ?fast=1 on the URL: the same rules in seconds, for
// test-tourney.mjs. Production never has TEST_FAST, so ?fast=1 does nothing there.
const FAST_TIMES = { report: 2500, noshow: 1000, grace: 1500, stale: 3000, keep: 4000, idle: 8000 };

export class Tourneys {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.s = { players: {}, lobbies: { 4: [], 8: [] }, fast: false }; // lobbies: pids, in join order
    this.tourneys = {}; // tid -> tournament (with its pids, which never go out: a pid is the login)
    this.dirty = new Set(); // tids to write with the next save
    this.armedAt = null; // the alarm, as far as this instance knows (nothing after a wake-up)
    // Hibernation drops everything on `this`: it all comes back from storage before any event.
    state.blockConcurrencyWhile(async () => {
      const s = await state.storage.get("s");
      if (s) this.s = JSON.parse(s);
      for (const [, text] of await state.storage.list({ prefix: "t:" })) {
        const t = JSON.parse(text);
        if (typeof t.lastAt !== "number") t.lastAt = Date.now(); // one stored before lastAt existed
        this.tourneys[t.tid] = t;
      }
    });
    // The keepalive is answered without waking the object; its time is still there (activity()).
    try {
      state.setWebSocketAutoResponse(new WebSocketRequestResponsePair('{"t":"ping"}', '{"t":"pong"}'));
    } catch (e) {}
  }

  fast() {
    return !!this.env.TEST_FAST && this.s.fast;
  }

  times() {
    return this.fast() ? FAST_TIMES : TIMES;
  }

  async fetch(request) {
    if (this.state.getWebSockets().length >= MAX_SOCKETS) {
      return new Response("tournaments full\n", { status: 503 });
    }
    const now = Date.now();
    const fast = !!this.env.TEST_FAST && new URL(request.url).searchParams.get("fast") === "1";
    if (fast !== this.s.fast) {
      this.s.fast = fast;
      this.save();
      this.arm(now);
    }
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.state.acceptWebSocket(server);
    server.serializeAttachment({ pid: null, since: now, seen: now }); // the team comes with its hello
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage(ws, message) {
    if (typeof message !== "string" || message.length > MAX_TEXT) return;
    let m;
    try {
      m = JSON.parse(message);
    } catch (e) {
      return;
    }
    if (!m || typeof m !== "object" || typeof m.t !== "string") return;
    const now = Date.now();
    const a = attachment(ws);
    a.seen = now;
    ws.serializeAttachment(a);
    if (m.t === "ping") {
      send(ws, '{"t":"pong"}'); // only a ping not written exactly {"t":"ping"} gets this far
      return;
    }
    let changed = this.sweep(now);
    if (m.t === "hello") {
      changed = this.hello(ws, a, m, now) || changed;
    } else if (!a.pid) {
      if (["join", "leave", "result", "noshow"].includes(m.t)) refuse(ws, "say hello first");
    } else if (m.t === "join") {
      changed = this.join(ws, a.pid, m) || changed;
    } else if (m.t === "leave") {
      changed = this.leave(ws, a.pid) || changed;
    } else if (m.t === "result") {
      changed = this.result(ws, a.pid, m, now) || changed;
    } else if (m.t === "noshow") {
      changed = this.noshow(ws, a.pid, m, now) || changed;
    }
    if (changed) {
      this.commit(now); // everyone's state, this socket's included
    } else if (m.t === "hello" && a.pid) {
      send(ws, this.stateText(a.pid)); // a hello always gets its state
    }
  }

  webSocketClose(ws, code, reason, wasClean) {
    try {
      ws.close(1000, "bye");
    } catch (e) {}
    this.closed(ws);
  }

  webSocketError(ws, error) {
    try {
      ws.close(1011, "error");
    } catch (e) {}
    this.closed(ws);
  }

  // The alarm is the earliest deadline (a lobby place running out, a lobby socket gone silent, a
  // lone report standing, a finished or abandoned tournament going); whatever is due runs, and the
  // next one is set.
  async alarm() {
    this.armedAt = null;
    const now = Date.now();
    if (this.sweep(now)) {
      this.save();
      this.broadcast();
    }
    this.arm(now);
  }

  // A team whose last socket closed keeps its lobby place for the grace time (sweep marks it gone),
  // in case it is only reconnecting. In a tournament nothing happens: the headsets leave this menu
  // to play their match in a room.
  closed(ws) {
    const pid = attachment(ws).pid;
    const p = pid && this.s.players[pid];
    if (!p) return;
    const now = Date.now();
    p.seen = now;
    const changed = this.sweep(now, ws);
    this.save();
    this.arm(now);
    if (changed) this.broadcast();
  }

  hello(ws, a, m, now) {
    const pid = typeof m.pid === "string" ? m.pid.toLowerCase() : "";
    if (!/^[0-9a-f]{16}$/.test(pid)) return refuse(ws, "pid must be 16 hex characters");
    if (a.pid && a.pid !== pid) return refuse(ws, "this connection belongs to another team");
    let p = this.s.players[pid];
    let changed = false;
    if (!p) {
      if (!this.roomForTeam()) {
        refuse(ws, "too many teams here, try again later");
        try {
          ws.close(1013, "full");
        } catch (e) {}
        return false;
      }
      p = this.s.players[pid] = { name: "", star: -1, lobby: null, tid: null, seen: now, gone: null };
      changed = true;
    }
    // Name and star (the captain shown as the team's logo) are fixed for a whole tournament: the
    // bracket shows them.
    if (!p.name || (typeof m.name === "string" && !this.playing(p))) {
      const name = cleanName(m.name, pid);
      if (name !== p.name) {
        p.name = name;
        changed = true;
      }
    }
    if (m.star !== undefined && !this.playing(p)) {
      const star = Number.isInteger(m.star) && m.star >= -1 && m.star <= 7 ? m.star : -1;
      if (star !== p.star) {
        p.star = star;
        changed = true;
      }
    }
    if (p.gone !== null) {
      p.gone = null; // back in time to keep its lobby place
      changed = true;
    }
    p.seen = now;
    if (!a.pid) {
      a.pid = pid;
      ws.serializeAttachment(a);
      // A headset that reconnects often leaves dead sockets behind for a while: the oldest go.
      const mine = (this.liveByPid().get(pid) || []).sort((x, y) => attachment(x).since - attachment(y).since);
      while (mine.length > MAX_PER_TEAM) {
        try {
          mine.shift().close(4000, "replaced");
        } catch (e) {}
      }
    }
    return changed;
  }

  join(ws, pid, m) {
    const p = this.s.players[pid];
    const size = Number(m.size);
    if (!SIZES.includes(size)) return refuse(ws, "size must be 4 or 8");
    if (p.lobby) return refuse(ws, `already in the ${p.lobby}-team lobby`);
    if (this.playing(p)) return refuse(ws, "already in a tournament");
    const lobby = this.s.lobbies[size];
    if (lobby.length + 1 >= size && !this.roomForTourney()) {
      return refuse(ws, "too many tournaments running, try again later");
    }
    lobby.push(pid);
    p.lobby = size;
    if (lobby.length >= size) this.start(size, Date.now());
    return true;
  }

  leave(ws, pid) {
    const p = this.s.players[pid];
    if (!p.lobby) return refuse(ws, "not in a lobby");
    const lobby = this.s.lobbies[p.lobby];
    lobby.splice(lobby.indexOf(pid), 1);
    p.lobby = null;
    p.gone = null;
    return true;
  }

  // A full lobby becomes a tournament: the teams shuffled into the bracket, every first-round
  // match ready in its own room, and the lobby empty for the next teams.
  start(size, now) {
    const pids = this.s.lobbies[size].splice(0, size);
    shuffle(pids);
    const tid = randomHex(8);
    const t = {
      tid,
      size,
      pids,
      names: pids.map((pid) => this.s.players[pid].name),
      stars: pids.map((pid) => starOf(this.s.players[pid])),
      rounds: [],
      champion: null,
      finishedAt: null,
      lastAt: now,
    };
    for (let n = size / 2; n >= 1; n /= 2) {
      const r = t.rounds.length;
      const round = [];
      for (let i = 0; i < n; i++) {
        round.push({
          mid: `${tid}.${r}.${i}`, // with the tid, so a report left over from another tournament never fits
          a: r === 0 ? 2 * i : null,
          b: r === 0 ? 2 * i + 1 : null,
          code: null,
          status: "waiting",
          readyAt: null,
          winner: null,
          score: null,
          reports: { a: null, b: null },
          replays: 0,
        });
      }
      t.rounds.push(round);
    }
    this.tourneys[tid] = t;
    for (const m of t.rounds[0]) this.ready(t, m, now);
    for (const pid of pids) {
      const p = this.s.players[pid];
      p.lobby = null;
      p.gone = null;
      p.tid = tid;
    }
  }

  // Both teams known: a fresh room, and the no-show clock starts.
  ready(t, m, now) {
    m.status = "ready";
    m.code = tourneyCode();
    m.readyAt = now;
    m.reports = { a: null, b: null };
    t.lastAt = now;
    this.dirty.add(t.tid);
  }

  // The same match again in a new room: a draw, or reports that do not fit together.
  replay(t, m, now) {
    m.replays = (m.replays || 0) + 1;
    this.ready(t, m, now);
  }

  decide(t, r, i, side, score, now) {
    const m = t.rounds[r][i];
    m.status = "done";
    m.winner = m[side];
    m.score = score;
    if (r + 1 < t.rounds.length) {
      const next = t.rounds[r + 1][i >> 1];
      next[i % 2 === 0 ? "a" : "b"] = m.winner;
      if (next.a !== null && next.b !== null) this.ready(t, next, now);
    } else {
      t.champion = m.winner;
      t.finishedAt = now;
    }
    t.lastAt = now;
    this.dirty.add(t.tid);
  }

  // The player's own match `mid`, ready to report: { t, m, r, i, side }, or null (refused).
  matchFor(ws, pid, mid) {
    const p = this.s.players[pid];
    const t = p.tid ? this.tourneys[p.tid] : null;
    if (!t || t.finishedAt !== null) return refuse(ws, "not in a running tournament") || null;
    const me = t.pids.indexOf(pid);
    for (let r = 0; r < t.rounds.length; r++) {
      for (let i = 0; i < t.rounds[r].length; i++) {
        const m = t.rounds[r][i];
        if (m.mid !== mid) continue;
        const side = m.a === me ? "a" : m.b === me ? "b" : null;
        if (!side) return refuse(ws, "not your match") || null;
        if (m.status !== "ready") return refuse(ws, m.status === "done" ? "that match is over" : "that match is not ready") || null;
        return { t, m, r, i, side };
      }
    }
    return refuse(ws, "no such match") || null;
  }

  result(ws, pid, msg, now) {
    const at = this.matchFor(ws, pid, msg.mid);
    if (!at) return false;
    const { t, m, r, i, side } = at;
    // Optional: a report for a room since replayed (a late resend) must not count for the new one.
    if (typeof msg.code === "string" && msg.code !== m.code) return refuse(ws, "that match was replayed under a new code");
    const goals = (x) => Number.isInteger(x) && x >= 0 && x <= 99;
    if (!goals(msg.my) || !goals(msg.their) || (msg.how !== "played" && msg.how !== "left")) {
      return refuse(ws, "result needs my and their (0-99) and how (played or left)");
    }
    m.reports[side] = { my: msg.my, their: msg.their, how: msg.how, at: now };
    t.lastAt = now;
    this.dirty.add(t.tid);
    const A = m.reports.a;
    const B = m.reports.b;
    if (!A || !B) return true; // the other side has the report wait to answer (sweep)
    // Both in. The scores must agree, and at least one side must have seen the match end: two
    // "left" means a broken link (each thinks the other went). A draw is played again.
    const agree = A.my === B.their && A.their === B.my;
    if (agree && (A.how === "played" || B.how === "played") && A.my !== A.their) {
      this.decide(t, r, i, A.my > A.their ? "a" : "b", [A.my, A.their], now);
    } else {
      this.replay(t, m, now);
    }
    return true;
  }

  noshow(ws, pid, msg, now) {
    const at = this.matchFor(ws, pid, msg.mid);
    if (!at) return false;
    const { t, m, r, i, side } = at;
    if (m.reports[side === "a" ? "b" : "a"]) return refuse(ws, "your opponent has reported this match");
    const wait = m.readyAt + this.times().noshow - now;
    if (wait > 0) return refuse(ws, `too early: ${Math.ceil(wait / 1000)} s to go`);
    this.decide(t, r, i, side, null, now);
    return true;
  }

  // Everything that is due: lobby places running out, dead lobby sockets, lone reports standing,
  // finished tournaments going. `gone` is a socket closing right now. True if anything changed.
  sweep(now, gone) {
    const T = this.times();
    const live = this.liveByPid(gone);
    let changed = false;
    for (const size of SIZES) {
      const lobby = this.s.lobbies[size];
      for (const pid of lobby.slice()) {
        const p = this.s.players[pid];
        const socks = live.get(pid) || [];
        for (const ws of socks.slice()) {
          if (now - this.activity(ws) < T.stale) continue;
          try {
            ws.close(4001, "stale");
          } catch (e) {}
          socks.splice(socks.indexOf(ws), 1);
        }
        // No socket (also after a restart, which drops every socket without a close event).
        if (socks.length === 0 && p.gone === null) {
          p.gone = now;
          changed = true;
        }
        if (p.gone !== null && now - p.gone >= T.grace) {
          lobby.splice(lobby.indexOf(pid), 1);
          p.lobby = null;
          p.gone = null;
          changed = true;
        }
      }
    }
    for (const t of Object.values(this.tourneys)) {
      if (t.finishedAt !== null) {
        if (now - t.finishedAt >= T.keep) {
          this.drop(t);
          changed = true;
        }
        continue;
      }
      t.rounds.forEach((round, r) =>
        round.forEach((m, i) => {
          const side = lone(m);
          if (!side || now - m.reports[side].at < T.report) return;
          // Nothing from the other side for the whole wait (a crash, or it never got going):
          // the one report stands. "left" = its opponent quit, so it wins.
          const rep = m.reports[side];
          const score = side === "a" ? [rep.my, rep.their] : [rep.their, rep.my];
          if (rep.how === "left") this.decide(t, r, i, side, score, now);
          else if (rep.my !== rep.their) this.decide(t, r, i, rep.my > rep.their ? side : side === "a" ? "b" : "a", score, now);
          else this.replay(t, m, now); // a draw, as when both report one
          changed = true;
        })
      );
      // After the lone reports: one that just stood is news, not abandonment (a restart can make
      // both overdue at once).
      if (t.finishedAt === null && now - t.lastAt >= T.idle) {
        this.drop(t); // abandoned
        changed = true;
      }
    }
    return changed;
  }

  nextDeadline() {
    const T = this.times();
    const live = this.liveByPid();
    let next = Infinity;
    for (const size of SIZES) {
      for (const pid of this.s.lobbies[size]) {
        const p = this.s.players[pid];
        if (p.gone !== null) next = Math.min(next, p.gone + T.grace);
        for (const ws of live.get(pid) || []) next = Math.min(next, this.activity(ws) + T.stale);
      }
    }
    for (const t of Object.values(this.tourneys)) {
      if (t.finishedAt !== null) {
        next = Math.min(next, t.finishedAt + T.keep);
        continue;
      }
      next = Math.min(next, t.lastAt + T.idle);
      for (const round of t.rounds) {
        for (const m of round) {
          const side = lone(m);
          if (side) next = Math.min(next, m.reports[side].at + T.report);
        }
      }
    }
    return next === Infinity ? null : next;
  }

  // One alarm, at the earliest deadline. One set earlier than needed is harmless (it finds
  // nothing due and sets the next), so it only moves when something is due sooner.
  arm(now) {
    const at = this.nextDeadline();
    if (at === null || !Number.isFinite(at)) {
      if (this.armedAt !== null) this.state.storage.deleteAlarm();
      this.armedAt = null;
    } else if (this.armedAt === null || at < this.armedAt || this.armedAt <= now) {
      this.state.storage.setAlarm(at);
      this.armedAt = at;
    }
  }

  // Not awaited: the runtime holds outgoing messages until the write is safe.
  save() {
    const put = { s: JSON.stringify(this.s) };
    for (const tid of this.dirty) if (this.tourneys[tid]) put["t:" + tid] = JSON.stringify(this.tourneys[tid]);
    this.dirty.clear();
    this.state.storage.put(put);
  }

  commit(now) {
    this.save();
    this.arm(now);
    this.broadcast();
  }

  // A tournament's time on show is over: gone, and its teams free (unless already in a newer one).
  drop(t) {
    delete this.tourneys[t.tid];
    this.dirty.delete(t.tid);
    this.state.storage.delete("t:" + t.tid);
    for (const pid of t.pids) {
      const p = this.s.players[pid];
      if (p && p.tid === t.tid) p.tid = null;
    }
  }

  playing(p) {
    const t = p.tid ? this.tourneys[p.tid] : null;
    return !!t && t.finishedAt === null;
  }

  roomForTeam() {
    const pids = Object.keys(this.s.players);
    if (pids.length < MAX_TEAMS) return true;
    const live = this.liveByPid();
    let oldest = null;
    for (const pid of pids) {
      const p = this.s.players[pid];
      if (p.lobby || p.tid || live.has(pid)) continue;
      if (oldest === null || p.seen < this.s.players[oldest].seen) oldest = pid;
    }
    if (oldest === null) return false;
    delete this.s.players[oldest];
    return true;
  }

  roomForTourney() {
    const all = Object.values(this.tourneys);
    if (all.length < MAX_TOURNEYS) return true;
    const done = all.filter((t) => t.finishedAt !== null).sort((x, y) => x.finishedAt - y.finishedAt);
    if (done.length === 0) return false;
    this.drop(done[0]);
    return true;
  }

  // pid -> its open sockets (bound by a hello), leaving out `except`.
  liveByPid(except) {
    const live = new Map();
    for (const ws of this.state.getWebSockets()) {
      if (ws === except || ws.readyState !== WebSocket.OPEN) continue;
      const pid = attachment(ws).pid;
      if (!pid) continue;
      if (!live.has(pid)) live.set(pid, []);
      live.get(pid).push(ws);
    }
    return live;
  }

  // Last heard from: a message, or a ping Cloudflare answered on the object's behalf.
  activity(ws) {
    const a = attachment(ws);
    let t = a.seen || a.since || 0;
    try {
      const d = this.state.getWebSocketAutoResponseTimestamp(ws);
      if (d) t = Math.max(t, d.getTime());
    } catch (e) {}
    return t;
  }

  lobbiesView() {
    const view = {};
    for (const size of SIZES) {
      const lobby = this.s.lobbies[size].map((pid) => this.s.players[pid]);
      view[size] = { size, names: lobby.map((p) => p.name), stars: lobby.map(starOf) };
    }
    return view;
  }

  stateText(pid, lobbies = this.lobbiesView()) {
    const p = this.s.players[pid];
    if (!p) return null;
    const t = p.tid ? this.tourneys[p.tid] : null;
    const msg = {
      t: "state",
      lobbies,
      me: { pid, name: p.name, star: starOf(p), lobby: p.lobby, tid: p.tid },
      // Everything but the pids: a pid is all it takes to act as that team.
      tourney: t
        ? { tid: t.tid, size: t.size, names: t.names, stars: t.stars, rounds: t.rounds, champion: t.champion, finishedAt: t.finishedAt }
        : null,
      you: t ? t.pids.indexOf(pid) : -1,
    };
    if (this.fast()) msg.fast = true; // test runs only: tells test-tourney.mjs the short times apply
    return JSON.stringify(msg);
  }

  broadcast() {
    const lobbies = this.lobbiesView();
    for (const ws of this.state.getWebSockets()) {
      if (ws.readyState !== WebSocket.OPEN) continue;
      const pid = attachment(ws).pid;
      const text = pid ? this.stateText(pid, lobbies) : null;
      if (text) send(ws, text);
    }
  }
}

// ---------------------------------------------------------------------------------------------
// Leagues (r100)
// ---------------------------------------------------------------------------------------------
//
// One object per league code: an online franchise league for 2-8 headsets (Lance: "UP TO 8 player
// online ... leagues"). The game (src/platform/franchise.cpp, xr_league.cpp) runs the league: this only
// keeps what they share and hands out match rooms.
//
// Headset -> server (JSON text):
//   {"t":"hello","pid":"<16 hex>","name":"...","proto":N}   who this socket is; answered with a state
//   {"t":"create","club":C,"career":"<text>","max":M}       a new league here (none yet): you own it, on club C
//   {"t":"claim","club":C}                                  join on club C (a free one), or move to it
//   {"t":"leave"}                                           out of the league (your club goes back to the CPU)
//   {"t":"get"}                                             the career: {"t":"career","v":V,"text":"..."}
//   {"t":"put","base":V,"text":"...","op":"..."}            a new career made from version V: "put_ok" with its
//                                                           version, or "put_conflict" (V is old: get, redo, put)
//   {"t":"ready","game":G,"home":H,"away":A}               ready to play the league's game G (clubs H v A, both
//                                                           a member's); both members ready: "go" to each
//   {"t":"unready"}
// Server -> headset:
//   {"t":"state","exists":B,"code":"...","max":M,"v":V,"me":{"club":C,"owner":B},"members":[{"name","club",
//     "online","ready"}]}   after every change
//   {"t":"career","v":V,"text":"..."}  {"t":"put_ok","v":V}  {"t":"put_conflict","v":V}
//   {"t":"go","game":G,"room":"L.....","host":B}   your match's relay room; host = the home club's member
//   {"t":"error","msg":"..."}
// Storage: "s" (members, owner, version, ready), "career" (the text).

const LEAGUE_MAX_MEMBERS = 8;
const LEAGUE_MAX_SOCKETS = 64; // 8 members' menus and their reconnects
const LEAGUE_MAX_TEXT = 1000000; // a career is ~20-60 KB
const LEAGUE_READY_MS = 15 * 60000; // a "ready" with its socket gone this long is dropped

export class League {
  constructor(state, env) {
    this.state = state;
    this.env = env;
    this.s = { exists: false, owner: null, max: LEAGUE_MAX_MEMBERS, members: {}, v: 0, ready: {}, created: 0 };
    this.career = "";
    state.blockConcurrencyWhile(async () => {
      const s = await state.storage.get("s");
      if (s) this.s = JSON.parse(s);
      const c = await state.storage.get("career");
      if (typeof c === "string") this.career = c;
    });
    try {
      state.setWebSocketAutoResponse(new WebSocketRequestResponsePair('{"t":"ping"}', '{"t":"pong"}'));
    } catch (e) {}
  }

  async fetch(request) {
    if (this.state.getWebSockets().length >= LEAGUE_MAX_SOCKETS) {
      return new Response("league full\n", { status: 503 });
    }
    const code = (new URL(request.url).pathname.match(/^\/league\/([A-Z0-9]{4,8})$/) || [])[1] || "";
    const pair = new WebSocketPair();
    const [client, server] = Object.values(pair);
    this.state.acceptWebSocket(server);
    server.serializeAttachment({ pid: null, since: Date.now(), seen: Date.now(), code });
    return new Response(null, { status: 101, webSocket: client });
  }

  webSocketMessage(ws, message) {
    if (typeof message !== "string" || message.length > LEAGUE_MAX_TEXT) return;
    let m;
    try {
      m = JSON.parse(message);
    } catch (e) {
      return;
    }
    if (!m || typeof m !== "object" || typeof m.t !== "string") return;
    const a = attachment(ws);
    a.seen = Date.now();
    ws.serializeAttachment(a);
    if (m.t === "ping") {
      send(ws, '{"t":"pong"}');
      return;
    }
    if (m.t === "hello") {
      const pid = typeof m.pid === "string" ? m.pid.toLowerCase() : "";
      if (!/^[0-9a-f]{16}$/.test(pid)) return void refuse(ws, "pid must be 16 hex characters");
      a.pid = pid;
      a.name = cleanName(m.name, pid);
      ws.serializeAttachment(a);
      const me = this.s.members[pid];
      if (me && me.name !== a.name) {
        me.name = a.name;
        this.save();
      }
      this.broadcast(); // its state, and everyone sees it online
      return;
    }
    if (!a.pid) return void refuse(ws, "say hello first");
    const pid = a.pid;
    const me = this.s.members[pid];
    if (m.t === "create") {
      if (this.s.exists) return void refuse(ws, "this league already exists");
      const club = Number(m.club);
      const text = typeof m.career === "string" ? m.career : "";
      if (!Number.isInteger(club) || club < 0 || club > 63 || text.length < 16) return void refuse(ws, "bad league");
      const max = Number.isInteger(m.max) && m.max >= 2 && m.max <= LEAGUE_MAX_MEMBERS ? m.max : LEAGUE_MAX_MEMBERS;
      this.s = { exists: true, owner: pid, max, members: {}, v: 1, ready: {}, created: Date.now() };
      this.s.members[pid] = { name: a.name || cleanName("", pid), club, joined: Date.now() };
      this.career = text;
      this.save(true);
      this.broadcast();
      return;
    }
    if (!this.s.exists) return void refuse(ws, "no league with this code");
    if (m.t === "claim") {
      const club = Number(m.club);
      if (!Number.isInteger(club) || club < 0 || club > 63) return void refuse(ws, "bad club");
      for (const [p, mm] of Object.entries(this.s.members)) {
        if (p !== pid && mm.club === club) return void refuse(ws, "that club is taken");
      }
      if (!me && Object.keys(this.s.members).length >= this.s.max) return void refuse(ws, "the league is full");
      if (me) me.club = club;
      else this.s.members[pid] = { name: a.name || cleanName("", pid), club, joined: Date.now() };
      delete this.s.ready[pid];
      this.save();
      this.broadcast();
      return;
    }
    if (m.t === "get") {
      send(ws, JSON.stringify({ t: "career", v: this.s.v, text: this.career }));
      return;
    }
    if (!me) return void refuse(ws, "not a member of this league");
    if (m.t === "leave") {
      delete this.s.members[pid];
      delete this.s.ready[pid];
      if (this.s.owner === pid) {
        // the league goes to the longest-standing member left (none: it stays, ownerless, as it is)
        const next = Object.entries(this.s.members).sort((x, y) => x[1].joined - y[1].joined)[0];
        this.s.owner = next ? next[0] : null;
      }
      this.save();
      this.broadcast();
      return;
    }
    if (m.t === "put") {
      const text = typeof m.text === "string" ? m.text : "";
      if (text.length < 16) return void refuse(ws, "bad career");
      if (m.base !== this.s.v) {
        send(ws, JSON.stringify({ t: "put_conflict", v: this.s.v }));
        return;
      }
      this.s.v += 1;
      this.career = text;
      this.save(true);
      send(ws, JSON.stringify({ t: "put_ok", v: this.s.v }));
      this.broadcast(); // everyone's state carries the new version: they get it
      return;
    }
    if (m.t === "ready") {
      const game = Number(m.game), home = Number(m.home), away = Number(m.away);
      if (![game, home, away].every((x) => Number.isInteger(x) && x >= 0)) return void refuse(ws, "bad game");
      if (me.club !== home && me.club !== away) return void refuse(ws, "not your game");
      this.s.ready[pid] = { game, home, away, at: Date.now() };
      // the other club's member, ready for the same game: the match
      const otherClub = me.club === home ? away : home;
      const other = Object.entries(this.s.members).find(([p, mm]) => mm.club === otherClub);
      const or = other ? this.s.ready[other[0]] : null;
      if (other && or && or.game === game) {
        const room = leagueRoom();
        const live = this.liveByPid();
        for (const [p, club] of [[pid, me.club], [other[0], otherClub]]) {
          for (const sock of live.get(p) || []) send(sock, JSON.stringify({ t: "go", game, room, host: club === home }));
          delete this.s.ready[p];
        }
      }
      this.save();
      this.broadcast();
      return;
    }
    if (m.t === "unready") {
      if (this.s.ready[pid]) {
        delete this.s.ready[pid];
        this.save();
        this.broadcast();
      }
      return;
    }
  }

  webSocketClose(ws, code, reason, wasClean) {
    try {
      ws.close(1000, "bye");
    } catch (e) {}
    this.closed(ws);
  }

  webSocketError(ws, error) {
    try {
      ws.close(1011, "error");
    } catch (e) {}
    this.closed(ws);
  }

  // A member whose last socket closed is offline: its "ready" goes too.
  closed(ws) {
    const pid = attachment(ws).pid;
    if (!pid) return;
    const still = (this.liveByPid(ws).get(pid) || []).length > 0;
    if (!still && this.s.ready[pid]) {
      delete this.s.ready[pid];
      this.save();
    }
    this.broadcast(ws);
  }

  save(withCareer = false) {
    const put = { s: JSON.stringify(this.s) };
    if (withCareer) put.career = this.career;
    this.state.storage.put(put);
  }

  liveByPid(except) {
    const live = new Map();
    for (const ws of this.state.getWebSockets()) {
      if (ws === except || ws.readyState !== WebSocket.OPEN) continue;
      const pid = attachment(ws).pid;
      if (!pid) continue;
      if (!live.has(pid)) live.set(pid, []);
      live.get(pid).push(ws);
    }
    return live;
  }

  broadcast(except) {
    const live = this.liveByPid(except);
    const now = Date.now();
    for (const [p, r] of Object.entries(this.s.ready)) {
      if (!live.has(p) && now - r.at > LEAGUE_READY_MS) delete this.s.ready[p];
    }
    const members = Object.entries(this.s.members)
      .sort((x, y) => x[1].joined - y[1].joined)
      .map(([p, mm]) => ({
        name: mm.name,
        club: mm.club,
        online: live.has(p),
        owner: p === this.s.owner,
        ready: this.s.ready[p] ? this.s.ready[p].game : -1,
      }));
    for (const ws of this.state.getWebSockets()) {
      if (ws === except || ws.readyState !== WebSocket.OPEN) continue;
      const a = attachment(ws);
      if (!a.pid) continue;
      const me = this.s.members[a.pid];
      send(ws, JSON.stringify({
        t: "state",
        exists: this.s.exists,
        code: a.code || "",
        max: this.s.max,
        v: this.s.v,
        me: { club: me ? me.club : -1, owner: a.pid === this.s.owner },
        members,
      }));
    }
  }
}

// "L" (for league) + 5 random characters: a league match's relay room.
function leagueRoom() {
  let code = "L";
  for (const b of crypto.getRandomValues(new Uint8Array(5))) code += CODE_CHARS[b % CODE_CHARS.length];
  return code;
}

function attachment(ws) {
  try {
    return ws.deserializeAttachment() || {};
  } catch (e) {
    return {};
  }
}

function send(ws, text) {
  try {
    ws.send(text);
  } catch (e) {}
}

function refuse(ws, msg) {
  send(ws, JSON.stringify({ t: "error", msg }));
  return false;
}

// The side whose report is the only one in ("a" or "b"), or null.
function lone(m) {
  if (m.status !== "ready") return null;
  if (m.reports.a && !m.reports.b) return "a";
  if (m.reports.b && !m.reports.a) return "b";
  return null;
}

// The game's eTeamID of the team's captain, 0-7 (Daisy, Donkey Kong, Luigi, Mario, Peach, Waluigi,
// Wario, Yoshi), or -1 for none.
function starOf(p) {
  return Number.isInteger(p.star) ? p.star : -1;
}

// Letters, digits, space and ' . - only (the game's font, and nothing to trip a menu), at most 16.
function cleanName(name, pid) {
  const s = String(name ?? "")
    .replace(/[^A-Za-z0-9 '.\-]/g, "")
    .replace(/ +/g, " ")
    .trim()
    .slice(0, 16)
    .trim();
  return s || "Team " + pid.slice(0, 4).toUpperCase();
}

// "T" (for tournament) + 5 random characters, like the quick-match codes.
function tourneyCode() {
  let code = "T";
  for (const b of crypto.getRandomValues(new Uint8Array(5))) code += CODE_CHARS[b % CODE_CHARS.length];
  return code;
}

function randomHex(n) {
  let s = "";
  for (const b of crypto.getRandomValues(new Uint8Array(n / 2))) s += b.toString(16).padStart(2, "0");
  return s;
}

// Fisher-Yates; a 32-bit value mod at most 8 is as good as fair.
function shuffle(a) {
  const r = crypto.getRandomValues(new Uint32Array(a.length));
  for (let i = a.length - 1; i > 0; i--) {
    const j = r[i] % (i + 1);
    [a[i], a[j]] = [a[j], a[i]];
  }
  return a;
}
