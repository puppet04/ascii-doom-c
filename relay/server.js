'use strict';

/*
 * Servidor Relay WebSocket (RFC 6455) para SECTOR 404 - Equipe 404 NF.
 * Zero dependencias externas (usa apenas modulos nativos http + crypto do Node.js).
 * Suporta dezenas de salas simultaneas identificadas por codigo (ex.: K7B2).
 *
 * Protocolo de quadros binarios (opcode 0x2):
 *   - Cabecalho de 4 bytes little-endian = peer_id (uint32) + payload bruto da struct C.
 *   - Convidado -> Servidor [0][payload]  ==> Servidor envia ao Host [guest.peerId][payload].
 *   - Host -> Servidor [targetId][payload]:
 *       * targetId == 0 : envia [0][payload] para todos os convidados da sala.
 *       * targetId > 0  : envia [0][payload] apenas para o convidado targetId.
 */

const http = require('http');
const crypto = require('crypto');

const PORT = parseInt(process.env.PORT || '8787', 10);
const WS_GUID = '258EAFA5-E914-47DA-95CA-C5AB0DC85B11'; /* GUID padrao do handshake WebSocket */
const MAX_FRAME_BYTES = 64 * 1024;
const MAX_GUESTS_PER_ROOM = 3; /* 1 host + 3 convidados = 4 players na sala */

/* Guarda as salas ativas em memoria indexadas pelo codigo (ex: K7B2) */
const rooms = new Map();

/* Limpa e valida o codigo da sala (maiusculas e numeros, 3 a 8 caracteres) */
function normalizeRoomCode(raw) {
  if (!raw) return '';
  const clean = String(raw).toUpperCase().replace(/[^A-Z0-9]/g, '');
  if (clean.length < 3 || clean.length > 8) return '';
  return clean;
}

/* Manda erro HTTP simples e fecha o socket se o handshake falhar */
function sendHttpError(socket, statusCode, message) {
  const body = Buffer.from(message, 'utf8');
  const head =
    `HTTP/1.1 ${statusCode} ${message}\r\n` +
    `Content-Type: text/plain; charset=utf-8\r\n` +
    `Content-Length: ${body.length}\r\n` +
    `Connection: close\r\n\r\n`;
  try {
    socket.end(Buffer.concat([Buffer.from(head, 'utf8'), body]));
  } catch (_) {
    try { socket.destroy(); } catch (__) {}
  }
}

/* Monta um quadro WebSocket pra mandar pro cliente (servidor nunca mascara payload) */
function encodeWsFrame(opcode, payloadBuf) {
  const payload = payloadBuf ? Buffer.from(payloadBuf) : Buffer.alloc(0);
  const len = payload.length;
  let header;
  if (len < 126) {
    header = Buffer.alloc(2);
    header[0] = 0x80 | (opcode & 0x0f);
    header[1] = len;
  } else if (len < 65536) {
    header = Buffer.alloc(4);
    header[0] = 0x80 | (opcode & 0x0f);
    header[1] = 126;
    header.writeUInt16BE(len, 2);
  } else {
    header = Buffer.alloc(10);
    header[0] = 0x80 | (opcode & 0x0f);
    header[1] = 127;
    header.writeBigUInt64BE(BigInt(len), 2);
  }
  return Buffer.concat([header, payload]);
}

/* Wrapper com estado do socket pra acumular chunks TCP e enviar quadros */
function makeWsClient(socket) {
  const client = {
    socket,
    closed: false,
    rxBuf: Buffer.alloc(0),
    fragOpcode: 0,
    fragChunks: [],
    lastActivity: Date.now(),
    sendFrame(opcode, payload) {
      if (this.closed || socket.destroyed || !socket.writable) return false;
      try {
        socket.write(encodeWsFrame(opcode, payload));
        return true;
      } catch (_) {
        return false;
      }
    },
    sendBinary(buf) {
      return this.sendFrame(0x2, buf);
    },
    close(code, reason) {
      if (this.closed) return;
      this.closed = true;
      try {
        if (!socket.destroyed && socket.writable) {
          const rbuf = Buffer.from(reason || '', 'utf8');
          const p = Buffer.alloc(2 + rbuf.length);
          p.writeUInt16BE(code || 1000, 0);
          rbuf.copy(p, 2);
          socket.end(encodeWsFrame(0x8, p));
        }
      } catch (_) {
        try { socket.destroy(); } catch (__) {}
      }
    }
  };
  return client;
}

/* Le todos os frames WebSocket prontos no rxBuf e tira a mascara do cliente */
function parseWsFrames(client, onBinary, onClose) {
  while (client.rxBuf.length >= 2) {
    const b0 = client.rxBuf[0];
    const b1 = client.rxBuf[1];
    const fin = (b0 & 0x80) !== 0;
    const opcode = b0 & 0x0f;
    const masked = (b1 & 0x80) !== 0;
    let payloadLen = b1 & 0x7f;
    let offset = 2;

    if (payloadLen === 126) {
      if (client.rxBuf.length < 4) return;
      payloadLen = client.rxBuf.readUInt16BE(2);
      offset = 4;
    } else if (payloadLen === 127) {
      if (client.rxBuf.length < 10) return;
      const bigLen = client.rxBuf.readBigUInt64BE(2);
      if (bigLen > BigInt(MAX_FRAME_BYTES)) {
        client.close(1009, 'Frame too large');
        return;
      }
      payloadLen = Number(bigLen);
      offset = 10;
    }

    if (payloadLen > MAX_FRAME_BYTES) {
      client.close(1009, 'Frame too large');
      return;
    }

    const maskOffset = offset;
    if (masked) offset += 4;
    if (client.rxBuf.length < offset + payloadLen) return;

    /* Tira a mascara XOR de 4 bytes que todo cliente WebSocket e obrigado a enviar */
    const frameData = Buffer.allocUnsafe(payloadLen);
    client.rxBuf.copy(frameData, 0, offset, offset + payloadLen);
    if (masked) {
      const m0 = client.rxBuf[maskOffset];
      const m1 = client.rxBuf[maskOffset + 1];
      const m2 = client.rxBuf[maskOffset + 2];
      const m3 = client.rxBuf[maskOffset + 3];
      for (let i = 0; i < payloadLen; i++) {
        const m = (i & 3) === 0 ? m0 : ((i & 3) === 1 ? m1 : ((i & 3) === 2 ? m2 : m3));
        frameData[i] ^= m;
      }
    }

    client.rxBuf = client.rxBuf.subarray(offset + payloadLen);
    client.lastActivity = Date.now();

    if (opcode === 0x8) {
      client.close(1000, 'Bye');
      onClose();
      return;
    } else if (opcode === 0x9) {
      /* Responde ping com pong na hora */
      client.sendFrame(0xA, frameData);
    } else if (opcode === 0xA) {
      // pong recebido (so precisa atualizar lastActivity acima)
    } else if (opcode === 0x0) {
      client.fragChunks.push(frameData);
      if (fin) {
        const full = Buffer.concat(client.fragChunks);
        client.fragChunks = [];
        if (client.fragOpcode === 0x2) onBinary(full);
      }
    } else if (opcode === 0x1 || opcode === 0x2) {
      if (fin) {
        if (opcode === 0x2) onBinary(frameData);
      } else {
        client.fragOpcode = opcode;
        client.fragChunks = [frameData];
      }
    }
  }
}

/* Servidor HTTP simples pro healthcheck do Render e listagem de salas em /rooms */
const server = http.createServer((req, res) => {
  if (req.url && req.url.startsWith('/rooms')) {
    const list = [];
    for (const [code, room] of rooms.entries()) {
      list.push({ code, players: 1 + room.guests.size });
    }
    res.writeHead(200, { 'Content-Type': 'application/json' });
    res.end(JSON.stringify({ ok: true, rooms: list }));
    return;
  }
  res.writeHead(200, { 'Content-Type': 'text/plain; charset=utf-8' });
  res.end(`SECTOR 404 Relay OK (salas ativas: ${rooms.size})\n`);
});

/* Trata conexoes WebSocket entrando em /ws?room=CODIGO&role=host|client */
server.on('upgrade', (req, socket, head) => {
  let url;
  try {
    url = new URL(req.url || '/', 'http://localhost');
  } catch (_) {
    sendHttpError(socket, 400, 'Bad Request');
    return;
  }

  if (url.pathname !== '/ws') {
    sendHttpError(socket, 404, 'Not Found');
    return;
  }

  const wsKey = req.headers['sec-websocket-key'];
  if (!wsKey || typeof wsKey !== 'string') {
    sendHttpError(socket, 400, 'Missing Sec-WebSocket-Key');
    return;
  }

  const roomCode = normalizeRoomCode(url.searchParams.get('room'));
  const role = (url.searchParams.get('role') || 'client').toLowerCase();
  if (!roomCode) {
    sendHttpError(socket, 400, 'Invalid room code');
    return;
  }

  if (role !== 'host' && role !== 'client') {
    sendHttpError(socket, 400, 'Invalid role');
    return;
  }

  let room = rooms.get(roomCode);
  if (role === 'client') {
    if (!room || !room.host || room.host.closed) {
      sendHttpError(socket, 404, 'Room Not Found');
      return;
    }
    if (room.guests.size >= MAX_GUESTS_PER_ROOM) {
      sendHttpError(socket, 409, 'Room Full');
      return;
    }
  }

  /* Calcula Sec-WebSocket-Accept com SHA-1 + Base64 pra fechar o handshake */
  const accept = crypto.createHash('sha1').update(wsKey + WS_GUID).digest('base64');
  const upgradeResp =
    'HTTP/1.1 101 Switching Protocols\r\n' +
    'Upgrade: websocket\r\n' +
    'Connection: Upgrade\r\n' +
    `Sec-WebSocket-Accept: ${accept}\r\n\r\n`;

  /* Desativa algoritmo de Nagle pra mandar os pacotes do jogo sem atraso */
  socket.setNoDelay(true);
  socket.write(upgradeResp);

  const client = makeWsClient(socket);
  if (head && head.length > 0) {
    client.rxBuf = Buffer.concat([client.rxBuf, head]);
  }

  let cleanupDone = false;
  let assignedPeerId = 0;

  if (role === 'host') {
    /* Se ja tinha outro host com esse codigo, derruba a sala antiga antes */
    if (room && room.host && !room.host.closed) {
      for (const g of room.guests.values()) {
        g.close(4001, 'Host replaced');
      }
      room.host.close(4001, 'Replaced');
    }
    room = {
      code: roomCode,
      host: client,
      guests: new Map(),
      nextPeerId: 1,
      createdAt: Date.now()
    };
    rooms.set(roomCode, room);
  } else {
    assignedPeerId = room.nextPeerId++;
    room.guests.set(assignedPeerId, client);
  }

  /* Se o host cair, derruba a sala inteira; se for convidado, so tira ele da lista */
  function cleanup() {
    if (cleanupDone) return;
    cleanupDone = true;
    if (role === 'host') {
      const cur = rooms.get(roomCode);
      if (cur === room) {
        rooms.delete(roomCode);
        for (const g of room.guests.values()) {
          g.close(4001, 'Host disconnected');
        }
        room.guests.clear();
      }
    } else {
      room.guests.delete(assignedPeerId);
    }
  }

  /* Repassa os pacotes binarios olhando os 4 primeiros bytes (target/peer_id) */
  function handleBinary(frameBuf) {
    if (!frameBuf || frameBuf.length < 5) return;
    const headerId = frameBuf.readUInt32LE(0);
    if (role === 'host') {
      if (headerId === 0) {
        /* targetId == 0 significa broadcast para todos os convidados da sala */
        const out = Buffer.from(frameBuf);
        out.writeUInt32LE(0, 0);
        for (const g of room.guests.values()) {
          g.sendBinary(out);
        }
      } else {
        const g = room.guests.get(headerId);
        if (g) {
          const out = Buffer.from(frameBuf);
          out.writeUInt32LE(0, 0);
          g.sendBinary(out);
        }
      }
    } else {
      /* Convidado mandando pro host: marca o peer_id dele nos 4 primeiros bytes */
      if (room.host && !room.host.closed) {
        const out = Buffer.from(frameBuf);
        out.writeUInt32LE(assignedPeerId >>> 0, 0);
        room.host.sendBinary(out);
      }
    }
  }

  socket.on('data', (chunk) => {
    client.rxBuf = Buffer.concat([client.rxBuf, chunk]);
    parseWsFrames(client, handleBinary, cleanup);
  });
  socket.on('close', cleanup);
  socket.on('error', cleanup);
  socket.on('end', cleanup);

  if (client.rxBuf.length > 0) {
    parseWsFrames(client, handleBinary, cleanup);
  }
});

/* Manda ping a cada 20s e fecha salas que ficarem mais de 90s sem atividade */
setInterval(() => {
  const now = Date.now();
  for (const [code, room] of rooms.entries()) {
    if (!room.host || room.host.closed || now - room.host.lastActivity > 90000) {
      if (room.host) room.host.close(4002, 'Idle timeout');
      for (const g of room.guests.values()) {
        g.close(4002, 'Room timeout');
      }
      rooms.delete(code);
      continue;
    }
    room.host.sendFrame(0x9, Buffer.alloc(0));
    for (const [pid, g] of room.guests.entries()) {
      if (g.closed || now - g.lastActivity > 90000) {
        g.close(4002, 'Idle timeout');
        room.guests.delete(pid);
      } else {
        g.sendFrame(0x9, Buffer.alloc(0));
      }
    }
  }
}, 20000).unref();

server.listen(PORT, '0.0.0.0', () => {
  console.log(`[SECTOR 404 Relay] WebSocket ativo na porta ${PORT}`);
});
