package io.github.newcoke.strikers;

import android.os.SystemClock;
import android.util.Base64;
import android.util.Log;

import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.security.SecureRandom;
import java.util.ArrayList;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

import javax.net.ssl.SSLParameters;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;

/**
 * strikers-quest r39: the online room's connection, a WebSocket over TLS (wss://) to the game's own
 * room server (server/ in this repository, a Cloudflare Worker): one binary WebSocket message per game
 * message, which the server hands to everyone in the room, the sender included. Driven from the
 * native network thread (src/platform/xr_net.cpp) through JNI; Android supplies the TLS and the
 * certificate checks.
 *
 * <p>A reader thread per connection turns frames into whole messages on a queue, so the native side
 * polls with a timeout and never sees a half frame. Every call returns rather than throws.
 */
final class NetWs {
    private static final String TAG = "strikers_net";
    // r39b: the size of xr_net.cpp's receive buffer. r100: 4 MiB, a league's career text (xr_league.cpp's
    // buffer; the relay's game messages stay small, and a bigger one there still ends the link in recv).
    private static final int MAX_MESSAGE = 4 * 1024 * 1024;
    // strikers-quest r39b: how long one server address gets to take the connection before the next
    // one is tried; the last address tried gets whatever is left of the caller's timeout.
    private static final int CONNECT_EACH_MS = 2500;
    private static final ConcurrentHashMap<Integer, Conn> CONNS = new ConcurrentHashMap<>();
    private static final AtomicInteger NEXT = new AtomicInteger(1);
    private static final SecureRandom RANDOM = new SecureRandom();

    private static final class Conn {
        final SSLSocket socket;
        final InputStream in;
        final OutputStream out;
        final LinkedBlockingQueue<byte[]> inbox = new LinkedBlockingQueue<>();
        volatile boolean closed;

        Conn(SSLSocket socket) throws IOException {
            this.socket = socket;
            this.in = new BufferedInputStream(socket.getInputStream(), 16384);
            this.out = socket.getOutputStream();
        }
    }

    private NetWs() {}

    /** Connects to wss://host:port/path and upgrades. A handle, or -1. */
    static int open(String host, int port, String path, int timeoutMs) {
        Socket plain = null;
        SSLSocket s = null;
        try {
            final int wait = timeoutMs > 0 ? timeoutMs : 5000;  // r39b: 0 would mean wait forever
            plain = connectAny(host, port, wait);
            plain.setTcpNoDelay(true);
            SSLSocketFactory factory = (SSLSocketFactory) SSLSocketFactory.getDefault();
            s = (SSLSocket) factory.createSocket(plain, host, port, true);
            SSLParameters params = s.getSSLParameters();
            params.setEndpointIdentificationAlgorithm("HTTPS");  // the certificate must name the host
            s.setSSLParameters(params);
            s.setSoTimeout(wait);
            s.startHandshake();
            final Conn c = new Conn(s);

            byte[] keyBytes = new byte[16];
            RANDOM.nextBytes(keyBytes);
            final String key = Base64.encodeToString(keyBytes, Base64.NO_WRAP);
            final String request = "GET " + path + " HTTP/1.1\r\n"
                    + "Host: " + host + "\r\n"
                    + "Upgrade: websocket\r\n"
                    + "Connection: Upgrade\r\n"
                    + "Sec-WebSocket-Key: " + key + "\r\n"
                    + "Sec-WebSocket-Version: 13\r\n"
                    + "User-Agent: StrikersQuest\r\n\r\n";
            c.out.write(request.getBytes(StandardCharsets.US_ASCII));
            c.out.flush();
            final String status = readLine(c.in);
            if (status == null || !status.startsWith("HTTP/1.1 101")) {
                Log.w(TAG, "room server refused the upgrade: " + status);
                closeQuietly(s);
                return -1;
            }
            for (String line = readLine(c.in); line != null && !line.isEmpty(); line = readLine(c.in)) {
                // headers: nothing we need
            }
            s.setSoTimeout(0);  // the reader thread blocks; liveness is the game's business
            final int id = NEXT.getAndIncrement();
            Thread reader = new Thread(() -> readLoop(id, c), "strikers-ws");
            reader.setDaemon(true);
            // strikers-quest r39b: started before the handle is listed, so a reader that cannot
            // start leaves no half-open entry behind (the catch below closes the socket).
            reader.start();
            CONNS.put(id, c);
            return id;
        } catch (Throwable e) {
            // strikers-quest r39b: anything, not only an IOException; nothing may reach the native side.
            Log.w(TAG, "room server connect to " + host + " failed: " + e);
            closeQuietly(s);
            closeQuietly(plain);
            return -1;
        }
    }

    /**
     * strikers-quest r39b: a TCP connection to the first of the host's addresses that takes one, an
     * IPv4 address first, then IPv6, then the next IPv4, and so on. Some home networks hand out IPv6
     * but do not route it, and the resolver then lists the IPv6 address first; trying only that one
     * never connected there. Taking turns means a family that is dead on this network costs one try,
     * not the whole timeout, so the other family still gets its turn.
     */
    private static Socket connectAny(String host, int port, int timeoutMs) throws IOException {
        final InetAddress[] all = InetAddress.getAllByName(host);
        final ArrayList<InetAddress> v4 = new ArrayList<>();
        final ArrayList<InetAddress> v6 = new ArrayList<>();
        for (InetAddress a : all) (a instanceof Inet4Address ? v4 : v6).add(a);
        final ArrayList<InetAddress> order = new ArrayList<>();
        for (int i = 0; i < Math.max(v4.size(), v6.size()); i++) {
            if (i < v4.size()) order.add(v4.get(i));
            if (i < v6.size()) order.add(v6.get(i));
        }
        final long deadline = SystemClock.elapsedRealtime() + timeoutMs;
        IOException last = null;
        for (int i = 0; i < order.size(); i++) {
            final long left = deadline - SystemClock.elapsedRealtime();
            if (left <= 0) break;
            final long wait = i == order.size() - 1 ? left : Math.min(CONNECT_EACH_MS, left);
            final InetAddress address = order.get(i);
            final Socket plain = new Socket();
            boolean connected = false;
            try {
                plain.connect(new InetSocketAddress(address, port), (int) wait);
                connected = true;
                return plain;
            } catch (IOException e) {
                Log.w(TAG, "room server address " + address.getHostAddress() + " failed: " + e);
                last = e;
            } finally {
                if (!connected) closeQuietly(plain);
            }
        }
        throw last != null ? last : new IOException("no address of " + host + " could be tried");
    }

    /** One binary message. False when the connection is gone. */
    static boolean send(int id, byte[] data) {
        try {
            final Conn c = CONNS.get(id);
            if (c == null || c.closed || data == null) return false;
            return sendFrame(c, 0x2, data);
        } catch (Throwable e) {
            Log.w(TAG, "room server send failed: " + e);
            return false;
        }
    }

    /**
     * strikers-quest r44: one text message (the quick-match queue's "ping", LobbyActivity). False when
     * the connection is gone.
     */
    static boolean sendText(int id, String text) {
        try {
            final Conn c = CONNS.get(id);
            if (c == null || c.closed || text == null) return false;
            return sendFrame(c, 0x1, text.getBytes(StandardCharsets.UTF_8));
        } catch (Throwable e) {
            Log.w(TAG, "room server send failed: " + e);
            return false;
        }
    }

    /**
     * The next whole message into `buf`: its length (>= 0), -2 when none arrived within timeoutMs,
     * -1 when the connection is gone and nothing is left. r44: text messages come the same way, as
     * their UTF-8 bytes.
     */
    static int recv(int id, byte[] buf, int timeoutMs) {
        try {
            final Conn c = CONNS.get(id);
            if (c == null || buf == null) return -1;
            byte[] m;
            try {
                m = timeoutMs <= 0 ? c.inbox.poll() : c.inbox.poll(timeoutMs, TimeUnit.MILLISECONDS);
            } catch (InterruptedException e) {
                m = null;
            }
            if (m == null) {
                if (!c.closed) return -2;
                // strikers-quest r39b: the reader queues its last message before it marks the
                // connection closed, so one more look keeps a message that landed after the wait.
                m = c.inbox.poll();
                if (m == null) return -1;
            }
            if (m.length > buf.length) {
                // strikers-quest r39b: cut short it would read as a different message; end the link.
                Log.w(TAG, "room server sent a " + m.length + "-byte message, more than the game takes");
                close(id);
                return -1;
            }
            System.arraycopy(m, 0, buf, 0, m.length);
            return m.length;
        } catch (Throwable e) {
            Log.w(TAG, "room server receive failed: " + e);
            return -1;
        }
    }

    /** Ends the connection. Safe from any thread; a second call for the same handle does nothing. */
    static void close(int id) {
        try {
            final Conn c = CONNS.remove(id);  // only one caller gets it
            if (c == null) return;
            try {
                if (!c.closed) sendFrame(c, 0x8, new byte[] {0x03, (byte) 0xE8});  // 1000, normal
            } finally {
                c.closed = true;
                closeQuietly(c.socket);
            }
        } catch (Throwable e) {
            Log.w(TAG, "room server close failed: " + e);
        }
    }

    private static boolean sendFrame(Conn c, int opcode, byte[] payload) {
        try {
            final int n = payload.length;
            final byte[] mask = new byte[4];
            RANDOM.nextBytes(mask);
            final byte[] frame = new byte[(n < 126 ? 2 : n < 65536 ? 4 : 10) + 4 + n];
            int at = 0;
            frame[at++] = (byte) (0x80 | opcode);  // FIN
            if (n < 126) {
                frame[at++] = (byte) (0x80 | n);  // client frames are masked
            } else if (n < 65536) {
                frame[at++] = (byte) (0x80 | 126);
                frame[at++] = (byte) (n >> 8);
                frame[at++] = (byte) n;
            } else {
                frame[at++] = (byte) (0x80 | 127);
                for (int i = 7; i >= 0; i--) frame[at++] = (byte) (i < 4 ? (n >> (8 * i)) : 0);
            }
            System.arraycopy(mask, 0, frame, at, 4);
            at += 4;
            for (int i = 0; i < n; i++) frame[at + i] = (byte) (payload[i] ^ mask[i & 3]);
            synchronized (c) {
                c.out.write(frame);
                c.out.flush();
                return true;
            }
        } catch (Throwable e) {
            // strikers-quest r39b: anything, not only an IOException. Part of a frame may be out, so
            // the connection is done either way.
            Log.w(TAG, "room server write failed: " + e);
            c.closed = true;
            return false;
        }
    }

    /**
     * strikers-quest r39b: the server broke the WebSocket rules (RFC 6455): a close frame with `code`,
     * then the connection is closed, so recv() reports it gone and the native side reconnects.
     */
    private static void fail(Conn c, int code, String why) {
        Log.w(TAG, "room server sent " + why + ", closing");
        sendFrame(c, 0x8, new byte[] {(byte) (code >> 8), (byte) code});
        c.closed = true;
    }

    private static void readLoop(int id, Conn c) {
        byte[] partial = null;
        try {
            while (!c.closed) {
                final int b0 = c.in.read();
                final int b1 = c.in.read();
                if (b0 < 0 || b1 < 0) break;
                final boolean fin = (b0 & 0x80) != 0;
                final int opcode = b0 & 0x0F;
                long len = b1 & 0x7F;
                if (len == 126) {
                    len = ((long) readByte(c.in) << 8) | readByte(c.in);
                } else if (len == 127) {
                    len = 0;
                    for (int i = 0; i < 8; i++) len = (len << 8) | readByte(c.in);
                }
                // strikers-quest r39b: checked before anything is allocated. A 64-bit length with its
                // top bit set reads as negative here, and a control frame is at most 125 bytes, whole.
                if (len < 0) { fail(c, 1002, "a negative frame length"); break; }
                if (len > MAX_MESSAGE) { fail(c, 1009, "a " + len + "-byte frame"); break; }
                if (opcode >= 0x8 && (len > 125 || !fin)) { fail(c, 1002, "a bad control frame"); break; }
                final boolean masked = (b1 & 0x80) != 0;
                final byte[] mask = new byte[4];
                if (masked) readFully(c.in, mask);
                final byte[] payload = new byte[(int) len];
                readFully(c.in, payload);
                if (masked) for (int i = 0; i < payload.length; i++) payload[i] ^= mask[i & 3];

                if (opcode == 0x8) break;                         // close
                if (opcode == 0x9) { sendFrame(c, 0xA, payload); continue; }  // ping -> pong
                if (opcode == 0xA) continue;                      // pong
                byte[] whole;
                if (opcode == 0x0) {                              // continuation
                    if (partial == null) continue;
                    if ((long) partial.length + payload.length > MAX_MESSAGE) {
                        fail(c, 1009, "a message of more than " + MAX_MESSAGE + " bytes");
                        break;
                    }
                    whole = concat(partial, payload);
                } else {
                    whole = payload;
                }
                if (fin) {
                    partial = null;
                    c.inbox.offer(whole);
                } else {
                    partial = whole;
                }
            }
        } catch (IOException e) {
            if (!c.closed) Log.w(TAG, "room server read failed: " + e);
        } catch (Throwable e) {
            // strikers-quest r39b: anything else (out of memory, a bug) must neither kill the app nor
            // stop this thread quietly with recv() waiting on it forever.
            Log.w(TAG, "room server reader stopped: " + e);
        } finally {
            c.closed = true;
        }
    }

    private static int readByte(InputStream in) throws IOException {
        final int b = in.read();
        if (b < 0) throw new IOException("closed");
        return b;
    }

    private static void readFully(InputStream in, byte[] out) throws IOException {
        int at = 0;
        while (at < out.length) {
            final int n = in.read(out, at, out.length - at);
            if (n < 0) throw new IOException("closed");
            at += n;
        }
    }

    private static byte[] concat(byte[] a, byte[] b) {
        final byte[] r = new byte[a.length + b.length];
        System.arraycopy(a, 0, r, 0, a.length);
        System.arraycopy(b, 0, r, a.length, b.length);
        return r;
    }

    private static void closeQuietly(Socket s) {
        if (s == null) return;
        try {
            s.close();
        } catch (Throwable ignored) {
        }
    }

    private static String readLine(InputStream in) throws IOException {
        final StringBuilder sb = new StringBuilder();
        for (;;) {
            final int ch = in.read();
            if (ch < 0) return sb.length() == 0 ? null : sb.toString();
            if (ch == '\n') break;
            if (ch != '\r') sb.append((char) ch);
        }
        return sb.toString();
    }
}
