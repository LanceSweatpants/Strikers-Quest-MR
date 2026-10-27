// r100: three fake headsets in one league: create, join, claim, career put / conflict / get, ready -> go.
//   node test-league.mjs [base]      (base e.g. ws://127.0.0.1:8787 for npx wrangler dev; ALWAYS a local one
//   while testing: this makes a league with a random code)
const base = (process.argv[2] || "ws://127.0.0.1:8787").replace(/\/+$/, "") + "/league/";
const code = "Z" + Math.random().toString(36).slice(2, 7).toUpperCase().replace(/[^A-Z0-9]/g, "X");

function open(pid, name) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(base + code);
    ws.got = [];
    ws.onopen = () => {
      ws.send(JSON.stringify({ t: "hello", pid, name, proto: 15 }));
      resolve(ws);
    };
    ws.onerror = (e) => reject(new Error(name + ": " + (e.message || "error")));
    ws.onmessage = (e) => ws.got.push(JSON.parse(e.data));
  });
}
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
const last = (ws, t) => [...ws.got].reverse().find((m) => m.t === t);
let fails = 0;
const check = (ok, what) => {
  console.log((ok ? "ok   " : "FAIL ") + what);
  if (!ok) fails++;
};

const a = await open("aaaaaaaaaaaaaaaa", "Lance");
const b = await open("bbbbbbbbbbbbbbbb", "Friend B");
const c = await open("cccccccccccccccc", "Friend C");
await wait(400);
check(last(a, "state") && last(a, "state").exists === false, "no league yet");
a.send(JSON.stringify({ t: "create", club: 5, career: "SSL1\nseed 1\n" + "x".repeat(30000), max: 8 }));
await wait(400);
check(last(b, "state").exists === true && last(b, "state").v === 1, "created, version 1, B sees it");
check(last(a, "state").me.owner === true && last(a, "state").me.club === 5, "A owns it, on club 5");
b.send(JSON.stringify({ t: "claim", club: 5 }));
await wait(300);
check(last(b, "error") && /taken/.test(last(b, "error").msg), "B can't take A's club");
b.send(JSON.stringify({ t: "claim", club: 9 }));
c.send(JSON.stringify({ t: "claim", club: 12 }));
await wait(400);
const st = last(a, "state");
check(st.members.length === 3 && st.members.every((m) => m.online), "3 members, all online");
b.send(JSON.stringify({ t: "get" }));
await wait(400);
check(last(b, "career") && last(b, "career").text.length > 30000 && last(b, "career").v === 1, "B got the 30 KB career");
b.send(JSON.stringify({ t: "put", base: 1, text: "SSL1\nseed 1\nB's week\n", op: "result" }));
c.send(JSON.stringify({ t: "put", base: 1, text: "SSL1\nseed 1\nC's week\n", op: "result" }));
await wait(500);
// (sent together: whichever reaches the server first lands - on the real network that can be either)
const okB = last(b, "put_ok"), okC = last(c, "put_ok");
const winner = okB ? b : c, loser = okB ? c : b;
check((okB ? 1 : 0) + (okC ? 1 : 0) === 1 && last(winner, "put_ok").v === 2, "one put lands (version 2)");
check(last(loser, "put_conflict") && last(loser, "put_conflict").v === 2, "the other put on version 1 conflicts");
check(last(a, "state").v === 2, "A sees version 2");
// A (club 5) v B (club 9), game 17
a.send(JSON.stringify({ t: "ready", game: 17, home: 5, away: 9 }));
await wait(300);
check(last(b, "state").members.find((m) => m.club === 5).ready === 17, "B sees A ready for game 17");
b.send(JSON.stringify({ t: "ready", game: 17, home: 5, away: 9 }));
await wait(400);
const ga = last(a, "go"), gb = last(b, "go");
check(ga && gb && ga.room === gb.room && /^L[A-Z0-9]{5}$/.test(ga.room), "both got the same L room " + (ga ? ga.room : ""));
check(ga && ga.host === true && gb && gb.host === false, "home club's member hosts");
c.close();
await wait(500);
check(last(a, "state").members.find((m) => m.club === 12).online === false, "C shows offline after closing");
a.close();
b.close();
console.log(fails ? `${fails} FAILED` : "all passed", "(league " + code + ")");
process.exit(fails ? 1 : 0);
