# Kerosene

> A browser built with the philosophy that modern software became unnecessarily heavy.

**Kerosene** is an open-source Windows browser focused on extreme performance, low memory usage, and direct hardware utilization.

The project follows a simple principle:

> If Apollo 11 reached the Moon with 4KB of RAM, a browser should not need gigabytes to render websites.

---

## Philosophy

Modern browsers are built on layers of abstractions.

Kerosene takes the opposite path.

* No Electron
* No unnecessary frameworks
* No telemetry
* No cloud dependency
* No AI integrations nobody asked for
* No crypto wallets
* No background services

Every byte of memory must justify its existence.

Every CPU cycle must have a purpose.

Every feature must earn its place.

---

# Design Goals

### Fast startup

Target:

```text
Cold start: 0.2s
New tab: 0.05s
Close tab: 0.01s
```

No profile-loading delays.

No waiting for background services.

No startup bloat.

---

### Memory Efficiency

Target:

```text
10 tabs < 150MB RAM
```

Shared resources:

* Shared font cache
* Shared CSS cache
* Shared image cache
* Compressed inactive tabs

Memory is treated as a limited resource, not an infinite one.

---

### GPU First

Rendering is designed around the GPU.

Not the CPU.

* Direct3D 11
* DirectWrite
* Hardware composition
* Texture-based scrolling
* Minimal redraws

The goal is smooth 144Hz scrolling without dropped frames.

---

# Architecture

```text
kerosene/
│
├── src/
│
├── cyclone/
│   ├── parser_html.c
│   ├── parser_css.c
│   ├── layout_engine.c
│   ├── flexbox.c
│   ├── grid.c
│   ├── paint.c
│
├── spark/
│   ├── runtime.c
│   ├── compiler.c
│   ├── jit.c
│   ├── gc.c
│
├── ignite/
│   ├── d3d11_renderer.c
│   ├── compositor.c
│   ├── text_atlas.c
│   ├── shader_compiler.c
│
├── net/
│   ├── http_client.c
│   ├── quic_handler.c
│   ├── cache.c
│
├── ui/
│   ├── main.rs
│   ├── window.rs
│   ├── tab_bar.rs
│   ├── omnibox.rs
│
├── platform/
│   ├── win32_window.c
│   ├── directwrite.c
│   ├── file_io.c
│
└── Makefile
```

---

# Core Components

## Cyclone

Custom HTML/CSS layout engine.

Supported:

* HTML5 subset
* Flexbox
* Grid
* Positioning
* Fonts
* Colors
* Borders
* Shadows

Excluded by design:

* Complex CSS animations
* Backdrop filters
* Container queries
* Heavy visual effects

Focus:

```text
Render pages fast.
```

---

## Spark

Lightweight JavaScript runtime.

Goals:

* Fast startup
* Low memory usage
* Small binary size
* Real-world web compatibility

Supported APIs:

* DOM
* Fetch
* Canvas
* WebGL
* WebAudio

Not planned:

* Web USB
* Web MIDI
* Web Bluetooth

---

## Ignite

Direct GPU renderer.

Built on:

* Direct3D 11
* DXGI Flip Model
* DirectWrite

Scrolling is implemented through viewport movement whenever possible instead of full-page redraws.

---

# Features

## Native Adblock

Built directly into the networking layer.

Benefits:

* Blocks requests before download
* Less bandwidth usage
* Less CPU usage
* Faster page loads

Sources:

* EasyList
* EasyPrivacy

---

## Turbo Mode

Predictive navigation.

While reading a page, Kerosene can preload visible links and likely navigation targets.

The result:

```text
Click → page already cached
```

---

## Reading Mode

Shortcut:

```text
F9
```

Removes:

* Ads
* Popups
* Sidebars
* Comment sections

Keeps:

* Content
* Images
* Headings

---

## Picture-in-Picture

Works with supported video sources.

Video remains active even if the browser is minimized.

---

# Privacy

Kerosene collects:

```text
Nothing.
```

No telemetry.

No usage analytics.

No tracking identifiers.

No cloud synchronization.

Your data remains on your machine.

---

# Installation

Current target:

```text
Single executable
```

Example:

```text
Kerosene.exe
```

No registry pollution.

No hidden services.

No scheduled tasks.

To uninstall:

```text
Delete the folder.
```

---

# Technology Stack

Core Engine:

* C

UI Layer:

* Rust

Networking:

* libcurl
* ngtcp2

Rendering:

* Direct3D 11
* DirectWrite

JavaScript:

* QuickJS (customized)

Media:

* FFmpeg

Database:

* SQLite

Compression:

* Brotli
* zlib
* LZ4

Security:

* BoringSSL

---

# Success Metrics

| Metric         | Modern Browser | Kerosene Target |
| -------------- | -------------- | --------------- |
| Startup Time   | Seconds        | 0.2s            |
| New Tab        | Hundreds of ms | 0.05s           |
| RAM Usage      | Gigabytes      | <150MB          |
| CPU Idle       | High           | Near Zero       |
| Scroll FPS     | Variable       | 144 FPS         |
| Installer Size | 100MB+         | ~15MB           |

---

# Open Source

Kerosene is open source because performance should be auditable.

If a feature makes the browser slower, it must justify itself.

If a dependency adds unnecessary complexity, it should be questioned.

Performance is not a marketing bullet.

It is the product.

---

# Contributing

We welcome contributions in:

* Rendering
* Networking
* JavaScript runtime
* UI
* Performance profiling
* Memory optimization

Every optimization matters.

Every millisecond counts.

---

# Motto

> Software should feel instant.
>
> Kerosene exists to prove that modern browsers do not have to be heavy.
