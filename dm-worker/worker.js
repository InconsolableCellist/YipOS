// YipOS Private DM Worker — Cloudflare Workers + KV
//
// KV keys:
//   dm:pair:{code}          → { session_id, creator_id, creator_name, created_at }  TTL 5min
//   dm:session:{session_id} → { creator_id, creator_name, joiner_id, joiner_name,
//                                 creator_confirmed, joiner_confirmed, created_at }  TTL 30d
//   dm:messages:{session_id} → [ { from, from_name, text, date }, ... ]             TTL 30d
//   dm:rate:create:{ip}      → count   TTL 1d
//   dm:rate:join:{ip}        → count   TTL 5min
//   dm:rate:send:{session}   → count   TTL 1d

const PAIR_TTL = 300;            // 5 minutes
const SESSION_TTL = 2592000;     // 30 days
const MAX_MESSAGES = 20;         // per conversation (kept small — CRT chat, not Discord)
const MAX_MESSAGE_LEN = 140;     // tweet-length
const MAX_NAME_LEN = 20;
const MAX_ID_LEN = 64;
const MAX_BODY_BYTES = 4096;     // reject request bodies larger than this
const MAX_CREATES_PER_DAY = 5;
const MAX_JOINS_PER_WINDOW = 5;
const MAX_SENDS_PER_SESSION_DAY = 100;

const CORS_HEADERS = {
  "Access-Control-Allow-Origin": "*",
  "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
  "Access-Control-Allow-Headers": "Content-Type",
};

function jsonResp(data, status = 200) {
  return new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json", ...CORS_HEADERS },
  });
}

function errorResp(msg, status = 400) {
  return jsonResp({ error: msg }, status);
}

// Strip to printable ASCII (0x20-0x7E) only. Null bytes, control chars,
// unicode, emoji — all removed. This is what the CRT can display.
function sanitize(str, maxLen) {
  if (typeof str !== "string") return "";
  let out = "";
  for (let i = 0; i < str.length && out.length < maxLen; i++) {
    const c = str.charCodeAt(i);
    if (c >= 0x20 && c <= 0x7e) out += str[i];
  }
  return out;
}

// Validate that a string looks like a hex ID (alphanumeric + hyphens only)
function isValidId(str) {
  if (typeof str !== "string" || str.length === 0 || str.length > MAX_ID_LEN) return false;
  return /^[a-f0-9\-]+$/i.test(str);
}

// Validate 6-digit numeric code
function isValidCode(str) {
  return typeof str === "string" && /^\d{6}$/.test(str);
}

function generateCode() {
  const arr = new Uint32Array(1);
  crypto.getRandomValues(arr);
  return String(arr[0] % 1000000).padStart(6, "0");
}

function generateSessionId() {
  const arr = new Uint8Array(16);
  crypto.getRandomValues(arr);
  return Array.from(arr, (b) => b.toString(16).padStart(2, "0")).join("");
}

async function readBody(request) {
  const contentLength = request.headers.get("Content-Length");
  if (contentLength && parseInt(contentLength) > MAX_BODY_BYTES) return null;
  const text = await request.text();
  if (text.length > MAX_BODY_BYTES) return null;
  return JSON.parse(text);
}

export default {
  async fetch(request, env) {
    if (request.method === "OPTIONS") {
      return new Response(null, { headers: CORS_HEADERS });
    }

    const url = new URL(request.url);
    const path = url.pathname;
    const ip = request.headers.get("CF-Connecting-IP") || "unknown";
    const KV = env.DM_KV;

    try {
      // --- POST /dm/pair/create ---
      if (request.method === "POST" && path === "/dm/pair/create") {
        const rateKey = `dm:rate:create:${ip}`;
        const rateVal = await KV.get(rateKey);
        const count = rateVal ? parseInt(rateVal) : 0;
        if (count >= MAX_CREATES_PER_DAY) {
          return errorResp("Rate limit exceeded", 429);
        }

        const body = await readBody(request);
        if (!body) return errorResp("Invalid request body");

        const user_id = sanitize(body.user_id, MAX_ID_LEN);
        const display_name = sanitize(body.display_name, MAX_NAME_LEN) || "Unknown";
        if (!isValidId(user_id)) return errorResp("Invalid user_id");

        await KV.put(rateKey, String(count + 1), { expirationTtl: 86400 });

        const code = generateCode();
        const session_id = generateSessionId();

        await KV.put(
          `dm:pair:${code}`,
          JSON.stringify({
            session_id,
            creator_id: user_id,
            creator_name: display_name,
            created_at: Date.now(),
          }),
          { expirationTtl: PAIR_TTL }
        );

        await KV.put(
          `dm:session:${session_id}`,
          JSON.stringify({
            creator_id: user_id,
            creator_name: display_name,
            joiner_id: null,
            joiner_name: null,
            creator_confirmed: false,
            joiner_confirmed: false,
            created_at: Date.now(),
          }),
          { expirationTtl: SESSION_TTL }
        );

        await KV.put(`dm:messages:${session_id}`, "[]", {
          expirationTtl: SESSION_TTL,
        });

        return jsonResp({ code, session_id });
      }

      // --- POST /dm/pair/join ---
      if (request.method === "POST" && path === "/dm/pair/join") {
        const rateKey = `dm:rate:join:${ip}`;
        const rateVal = await KV.get(rateKey);
        const count = rateVal ? parseInt(rateVal) : 0;
        if (count >= MAX_JOINS_PER_WINDOW) {
          return errorResp("Rate limit exceeded", 429);
        }

        const body = await readBody(request);
        if (!body) return errorResp("Invalid request body");

        const code = sanitize(body.code, 6);
        const user_id = sanitize(body.user_id, MAX_ID_LEN);
        const display_name = sanitize(body.display_name, MAX_NAME_LEN) || "Unknown";

        if (!isValidCode(code)) return errorResp("Invalid code format");
        if (!isValidId(user_id)) return errorResp("Invalid user_id");

        await KV.put(rateKey, String(count + 1), { expirationTtl: 300 });

        const pairData = await KV.get(`dm:pair:${code}`);
        if (!pairData) return errorResp("Invalid or expired code", 404);

        const pair = JSON.parse(pairData);

        if (pair.creator_id === user_id) {
          return errorResp("Cannot pair with yourself");
        }

        const sessionData = await KV.get(`dm:session:${pair.session_id}`);
        if (!sessionData) return errorResp("Session not found", 404);

        const session = JSON.parse(sessionData);
        if (session.joiner_id && session.joiner_id !== user_id) {
          return errorResp("Session already has a peer");
        }

        session.joiner_id = user_id;
        session.joiner_name = display_name;
        session.joiner_confirmed = true;

        await KV.put(`dm:session:${pair.session_id}`, JSON.stringify(session), {
          expirationTtl: SESSION_TTL,
        });

        await KV.delete(`dm:pair:${code}`);

        return jsonResp({
          ok: true,
          session_id: pair.session_id,
          peer_name: pair.creator_name,
        });
      }

      // --- GET /dm/pair/status ---
      if (request.method === "GET" && path === "/dm/pair/status") {
        const session_id = sanitize(url.searchParams.get("session_id") || "", MAX_ID_LEN);
        const user_id = sanitize(url.searchParams.get("user_id") || "", MAX_ID_LEN);
        if (!isValidId(session_id) || !isValidId(user_id)) {
          return errorResp("Invalid parameters");
        }

        const sessionData = await KV.get(`dm:session:${session_id}`);
        if (!sessionData) return errorResp("Session not found", 404);

        const session = JSON.parse(sessionData);

        let status = "waiting";
        let peer_name = "";

        if (user_id === session.creator_id) {
          if (session.joiner_id) {
            status = session.creator_confirmed && session.joiner_confirmed
              ? "confirmed" : "joined";
            peer_name = session.joiner_name;
          }
        } else if (user_id === session.joiner_id) {
          status = session.creator_confirmed && session.joiner_confirmed
            ? "confirmed" : "joined";
          peer_name = session.creator_name;
        } else {
          return errorResp("Not a member of this session", 403);
        }

        return jsonResp({ status, peer_name });
      }

      // --- POST /dm/pair/confirm ---
      if (request.method === "POST" && path === "/dm/pair/confirm") {
        const body = await readBody(request);
        if (!body) return errorResp("Invalid request body");

        const session_id = sanitize(body.session_id, MAX_ID_LEN);
        const user_id = sanitize(body.user_id, MAX_ID_LEN);
        if (!isValidId(session_id) || !isValidId(user_id)) {
          return errorResp("Invalid parameters");
        }

        const sessionData = await KV.get(`dm:session:${session_id}`);
        if (!sessionData) return errorResp("Session not found", 404);

        const session = JSON.parse(sessionData);

        if (user_id === session.creator_id) {
          session.creator_confirmed = true;
        } else if (user_id === session.joiner_id) {
          session.joiner_confirmed = true;
        } else {
          return errorResp("Not a member of this session", 403);
        }

        await KV.put(`dm:session:${session_id}`, JSON.stringify(session), {
          expirationTtl: SESSION_TTL,
        });

        return jsonResp({ ok: true });
      }

      // --- GET /dm/messages ---
      if (request.method === "GET" && path === "/dm/messages") {
        const session_id = sanitize(url.searchParams.get("session_id") || "", MAX_ID_LEN);
        const user_id = sanitize(url.searchParams.get("user_id") || "", MAX_ID_LEN);
        if (!isValidId(session_id)) return errorResp("Invalid session_id");

        // Auth check: must be a member of this session
        if (!isValidId(user_id)) return errorResp("user_id required");
        const sessionData = await KV.get(`dm:session:${session_id}`);
        if (!sessionData) return errorResp("Session not found", 404);
        const session = JSON.parse(sessionData);
        if (user_id !== session.creator_id && user_id !== session.joiner_id) {
          return errorResp("Not a member of this session", 403);
        }

        const since = parseInt(url.searchParams.get("since") || "0") || 0;

        const msgData = await KV.get(`dm:messages:${session_id}`);
        if (!msgData) return jsonResp([]);

        let messages = JSON.parse(msgData);
        if (since > 0) {
          messages = messages.filter((m) => m.date > since);
        }

        return jsonResp(messages);
      }

      // --- POST /dm/send ---
      if (request.method === "POST" && path === "/dm/send") {
        const body = await readBody(request);
        if (!body) return errorResp("Invalid request body");

        const session_id = sanitize(body.session_id, MAX_ID_LEN);
        const user_id = sanitize(body.user_id, MAX_ID_LEN);
        const text = sanitize(body.text, MAX_MESSAGE_LEN);

        if (!isValidId(session_id) || !isValidId(user_id)) {
          return errorResp("Invalid parameters");
        }
        if (text.length === 0) return errorResp("Message text required");

        // Per-session send rate limit
        const sendRateKey = `dm:rate:send:${session_id}`;
        const sendRateVal = await KV.get(sendRateKey);
        const sendCount = sendRateVal ? parseInt(sendRateVal) : 0;
        if (sendCount >= MAX_SENDS_PER_SESSION_DAY) {
          return errorResp("Message limit reached for today", 429);
        }

        // Verify user is part of session
        const sessionData = await KV.get(`dm:session:${session_id}`);
        if (!sessionData) return errorResp("Session not found", 404);

        const session = JSON.parse(sessionData);
        if (user_id !== session.creator_id && user_id !== session.joiner_id) {
          return errorResp("Not a member of this session", 403);
        }

        if (!session.creator_confirmed || !session.joiner_confirmed) {
          return errorResp("Session not fully confirmed");
        }

        await KV.put(sendRateKey, String(sendCount + 1), { expirationTtl: 86400 });

        const from_name = user_id === session.creator_id
          ? session.creator_name : session.joiner_name;

        const msgData = await KV.get(`dm:messages:${session_id}`);
        const messages = msgData ? JSON.parse(msgData) : [];

        messages.push({
          from: user_id,
          from_name,
          text,
          date: Math.floor(Date.now() / 1000),
        });

        while (messages.length > MAX_MESSAGES) {
          messages.shift();
        }

        await KV.put(`dm:messages:${session_id}`, JSON.stringify(messages), {
          expirationTtl: SESSION_TTL,
        });

        return jsonResp({ ok: true });
      }

      return errorResp("Not found", 404);
    } catch (e) {
      return errorResp("Internal error", 500);
    }
  },
};
