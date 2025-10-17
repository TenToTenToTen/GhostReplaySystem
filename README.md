# 🌀 Ghost Replay System

**Ghost Replay System** is a lightweight, multiplayer-ready plugin that lets you record and replay actor animation, transforms, and materials — just like ghost cars in racing games or bloodstains in Soulslikes.

It supports both Static and Skeletal Meshes, full pose fidelity (including IK, cloth, and groom), and offers optional compression and quantization to keep replay data lightweight — perfect for multiplayer synchronization without overloading the network.

Designed for ease of use, it works out of the box with existing projects and includes fully-Blueprint controls for spawning, playback, and customization.

Whether you're building death replays, tutorial ghosts, or story moments, Ghost Replay System makes immersive, in-world playback easy.

---

## 📦 Store Page
🔗 [View on Unreal Fab](https://www.fab.com/listings/2eb83102-6ea6-448a-a56e-64b905cc1651)

---

## 📚 Documentation
📖 [Read the Docs](https://tentotentoten.github.io/GhostReplay_Docs/)

---

## 🎥 Trailer Video
[▶ Watch on YouTube](https://www.youtube.com/watch?v=LHjubAv9SbY)

---

## 📞 Contact Us
- 🎮 **Discord**: [Join our Discord](https://discord.gg/Ab2hA5Eb9Q)
- ▶ **YouTube**: [Watch our Trailer Video](https://www.youtube.com/watch?v=LHjubAv9SbY)
- 📧 **Email**: techlab.ttt@gmail.com

---

> **Disclaimer**: All assets used in the trailer video and screenshots are not included in this plugin.  
> We do not claim any rights to the assets shown in promotional materials.

---

## ✨ Key Features

### 🎥 Instant Replay for Any Actor
- Record and replay actors with Static or Skeletal Meshes
- Captures full pose (including IK, bone copy, etc.) and component transforms
- Supports advanced simulation components like Groom Hair and Chaos Cloth

### 🎮 Multiplayer-Ready by Design
- Built-in support for dedicated and listen servers
- Smart data chunking system avoids reliable buffer overflow
- Lightweight replay files via quantization and compression, ideal for online play

### 👥 Record Multiple Actors at Once
- Grouped or simultaneous multi-actor recording and playback
- Each actor’s pose, animation, and materials are restored accurately

### 🧱 Drop-In Integration
- Works seamlessly with existing projects like Lyra, Valley of the Ancient, and more
- Add bloodstains, replay triggers, and ghost actors with minimal setup

### 🎨 Visual Fidelity
- Materials are dynamically re-instanced and restored per actor
- Custom playback options: loop, reverse, play rate, visibility control

---

## 🕹 Use Cases

### 🩸 Bloodstain Replays
> Let players touch a bloodstain and witness how another player died — just like Soulslike games.

### 🏁 Ghost Car Challenges
> Replay your best lap and challenge your past self or another player’s ghost in real time.

### 📚 In-World Tutorials or Story Moments
- Embed recorded scenes or actions into the game world as ambient narrative or guidance tools.

---

## 🔍 What Sets It Apart
- ✅ **Multiplayer-aware from the ground up** — Efficiently synchronizes replay data without flooding the network.
- ✅ **Fully modular** — No need to modify your existing animation systems or blueprints; plug and play.
- ✅ **Replay accuracy with performance in mind** — Uses `FAnimInstanceProxy` for threaded animation evaluation and quantized transform storage for minimal bandwidth impact.
- ✅ **Visual separation with orchestration** — Server and client replay actors are clearly separated between data orchestrators and visual ghosts, reducing replication load and improving clarity.
