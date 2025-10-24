# 🔒🎥 Secure SRTP Streaming

A minimal, low‑latency, bidirectional audio‑video streaming setup built with GStreamer and SRTP, implementing encrypted RTP/RTCP flows between a server and a client.

## ✨ Highlights

- **End‑to‑end SRTP** using AES‑256‑ICM with HMAC‑SHA1‑80 authentication for RTP and RTCP.
- **RTP sessioning via rtpbin** with explicit send/receive pads and synchronized AV pipelines.
- **H.264 video + Opus audio** encoded and packetized for real‑time transport.
- **Clear port mapping** for client→server and server→client media/RTCP flows.
- **Single‑binary apps**: server.cpp and client.cpp with straightforward CLI usage.

## 🧭 Topology

- Server and client each capture, encode, SRTP‑protect, and send their media while simultaneously receiving, decrypting, and rendering the peer stream.
- Media control runs over RTP/RTCP with matching SSRC/keying parameters on both sides for successful decryption and synchronization.

## ⚙️ Build

Compile with pkg‑config flags for GStreamer 1.0 and C++11 standard, no Makefile required.

```bash
# Server
g++ server.cpp -o server $(pkg-config --cflags --libs gstreamer-1.0)

# Client
g++ client.cpp -o client $(pkg-config --cflags --libs gstreamer-1.0)
```

Required components typically include core, base/good/bad/ugly plugins, libav, tools, and x264 support from your distro’s GStreamer packages.

## 🚀 Run

- Server expects the client’s IP to direct outbound media correctly.

```bash
./server <client_ip>
```

- Client expects the server’s IP to initiate the session in the opposite direction.

```bash
./client <server_ip>
```

Example pair on a LAN (server at 192.168.1.50, client at 192.168.1.100): run server with client IP, then client with server IP to establish bidirectional media.

## 🔌 Ports at a Glance

- Distinct UDP ports are used for RTP and RTCP for both audio and video in each direction to keep flows clear and firewall rules simple.
- Ensure the selected UDP ports are open on both peers and mapped correctly if NAT/firewalls are present to avoid one‑way media.

## 🔐 Security Notes

- The SRTP master key is currently hardcoded for both binaries; for anything beyond local testing, replace it with a securely generated value and introduce a proper key exchange.
- Both sides must use identical cipher/auth suites and the exact same 46‑byte master key layout: 32‑byte master key plus 14‑byte salt.
- Add endpoint authentication and narrow firewall rules to restrict access to known peers and required UDP ports only.

## 🛠️ Troubleshooting

- If you see silence or a black screen, verify device access, plugin availability, and that IP/port mappings match the peer configuration.
- Encryption or caps‑negotiation errors usually indicate mismatched SRTP parameters or keys between server and client.
- Increase visibility by running with GST_DEBUG logs to pinpoint pad linking, caps, or network issues quickly.

```bash
GST_DEBUG=3 ./server <client_ip>
GST_DEBUG=3 ./client <server_ip>
```

## 🧩 Roadmap

We plan to make the key exchange mechanism post‑quantum secure in future updates using modern PQC approaches.

## 🤝 Contributing

Issues and pull requests are welcome for fixes, portability improvements, and quality‑of‑life enhancements across platforms.
