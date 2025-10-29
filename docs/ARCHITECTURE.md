# Revere Architecture

This document describes the architecture and design of the Revere video surveillance system.

## Table of Contents

- [System Overview](#system-overview)
- [Architecture Diagram](#architecture-diagram)
- [Component Overview](#component-overview)
- [Core Libraries](#core-libraries)
- [Data Flow](#data-flow)
- [Storage Architecture](#storage-architecture)
- [Plugin System](#plugin-system)
- [Network Architecture](#network-architecture)
- [Threading Model](#threading-model)
- [Design Decisions](#design-decisions)

## System Overview

Revere is 2 applications: revere and vision. revere takes care of camera discovery, recording, exports, thumbnails, analytics, etc. vision is a viewing app that uses revere as its backend api.

The revere applications are built from 13 libraries that provide most of the functionality. These libraries range in functionality from video, networking, storage and analytics.
  

### Key Architectural Principles

I shoot for building lego blocks that are generally useful from which many applications can be built. Each of these lego blocks take the form of libraries that all should have their own independent tests.

## Architecture Diagram

[ASCII diagram or reference to image showing major components and their relationships]

```
[Camera] --RTSP--> [r_vss] --> [r_storage] --> [Disk]
                      |              |
                      v              v
                 [r_motion]      [r_http]
                      |              |
                      v              v
                 [AI Plugins]    [Web UI]
```

## Component Overview

### Applications

#### revere
The main surveillance application that handles camera management, recording, and streaming.

**Purpose:** Central hub for video surveillance operations

**Key responsibilities:**
- ONVIF camera discovery and configuration
- Video recording and storage management
- Motion detection coordination
- RTSP stream restreaming (default: rtsp://127.0.0.1:10554/{camera_name})
- HTTP API server (default port: 10080)
- Desktop tray integration
- Recording retention policy management

**UI Framework:** ImGui (immediate-mode GUI)

#### vision
The playback and analytics viewer application.

**Purpose:** Video playback, analysis, and export interface

**Key responsibilities:**
- Connect to revere backend via HTTP API
- Query and playback recordings
- Time-range video browsing
- Multi-camera layout management
- Video export functionality
- Live streaming visualization

**Relationship to revere:** Acts as a client to the revere HTTP API (connects to port 10080)

### Core Services

[Overview of the main service components]

## Core Libraries

### r_av (Audio/Video Processing)
**Location:** `libs/r_av/`

**Purpose:** FFmpeg-based video/audio decoding, encoding, format conversion, and manipulation

**Key Classes/Functions:**
- Video decoder and encoder wrappers
- Format conversion utilities
- Frame extraction and manipulation
- Codec parameter management

**Dependencies:**
- FFmpeg (libavcodec, libavformat, libavutil, libswscale)
- r_utils

**Used By:**
- r_vss (recording and stream processing)
- r_fakey (test stream generation)
- vision app (playback and export)

---

### r_db (Database)
**Location:** `libs/r_db/`

**Purpose:** SQLite database abstraction for storing camera metadata, configuration, and system state

**Key Classes/Functions:**
- Database connection management
- Query builders and helpers
- Transaction support
- Schema migration utilities

**Dependencies:**
- r_utils
- SQLite3 (embedded)

**Schema:** Stores camera configurations, recording metadata, event logs, and system settings

---

### r_disco (Device Discovery)
**Location:** `libs/r_disco/`

**Purpose:** High-level device and camera management, coordinating ONVIF discovery with pipeline management

**Key Classes/Functions:**
- `r_agent` - Manages camera agents and their lifecycle
- `r_devices` - Device registry and management
- `r_camera` - Camera abstraction with recording state

**Discovery Mechanisms:**
- ONVIF WS-Discovery for automatic camera detection
- Manual camera addition with RTSP URLs
- Integration with r_onvif for camera communication

**Dependencies:** r_onvif, r_pipeline, r_db, r_utils, GStreamer

---

### r_onvif (ONVIF Protocol)
**Location:** `libs/r_onvif/`

**Purpose:** ONVIF protocol implementation for camera discovery, configuration, and control

**Key Classes/Functions:**
- WS-Discovery client for camera detection
- SOAP-based ONVIF communication
- PTZ control interface
- Media profile management
- Authentication handling (digest/basic)

**Supported ONVIF Features:**
- Profile S (Streaming)
- Profile T (Advanced streaming)
- PTZ control (Pan/Tilt/Zoom)
- Media URI retrieval
- Camera configuration

**Dependencies:** r_http, r_utils, pugixml (XML parsing)

---

### r_http (HTTP Server/Client)
**Location:** `libs/r_http/`

**Purpose:** HTTP/HTTPS server and client with REST API support

**Key Classes/Functions:**
- HTTP server with routing
- HTTP client for external requests
- REST API endpoint management
- Request/response handling

**API Endpoints:**
- `/contents` - Query recording segments
- `/export` - Export video clips
- Additional endpoints for camera and system management

**Dependencies:** r_utils

---

### r_storage (Recording Storage)
**Location:** `libs/r_storage/`

**Purpose:** Video recording storage, indexing, and time-range based retrieval

**Key Classes/Functions:**
- Recording segment management
- Time-series index with nanots
- Storage allocation and retention policies
- Query interface for time-range lookups

**File Format:** Video segments with time-series indexing for efficient retrieval

**Indexing Strategy:** Time-series database (nanots) provides fast lookup by timestamp ranges

**Dependencies:** r_db, r_utils, nanots

---

### r_motion (Motion Detection)
**Location:** `libs/r_motion/`

**Purpose:** Advanced motion detection with background subtraction and adaptive learning

**Key Classes/Functions:**
- `r_motion_state` - Core motion detection algorithm
- Background subtraction with exponential moving average
- Adaptive threshold calculation
- Static region masking
- Motion frequency tracking

**Algorithms:**
- Exponential moving average background model
- Adaptive threshold (mean + k*σ)
- Illumination change detection
- Per-pixel motion frequency for static masking

**Tuning Parameters:**
- learning_rate: 0.002
- fast_learn_rate: 0.10
- adaptive_k: 2.0
- min_area_fraction: 0.003

**Dependencies:** r_utils, OpenCV, GStreamer

---

### r_vss (Video Surveillance Service)
**Location:** `libs/r_vss/`

**Purpose:** Core surveillance orchestration - coordinates recording, motion detection, streaming, and plugin management

**Key Classes/Functions:**
- `r_stream_keeper` - Manages active camera streams
- `r_motion_event_plugin_host` - Loads and manages motion detection plugins
- Recording coordinator
- RTSP restreaming server

**Responsibilities:**
- Coordinate r_pipeline for video ingestion
- Trigger motion detection via r_motion
- Manage r_storage for recording
- Load and dispatch events to AI plugins
- Provide RTSP streams for external viewers

**Motion Plugins:** Supports dynamic loading of AI-based detection plugins (YOLOv8, MobileNet, PicoDet)

**Dependencies:** r_pipeline, r_av, r_motion, r_disco, r_storage, r_utils, FFmpeg, OpenCV, GStreamer

---

### r_pipeline (Processing Pipeline)
**Location:** `libs/r_pipeline/`

**Purpose:** GStreamer pipeline abstraction for RTSP streaming and media processing

**Key Classes/Functions:**
- GStreamer pipeline builder and manager
- RTSP source integration
- Pipeline element configuration
- Buffer and frame callback management

**Pipeline Stages:**
- RTSP source connection
- Decoding (hardware-accelerated when available)
- Frame extraction for motion detection
- Recording sink

**Dependencies:** r_utils, GStreamer (gst-app, gst-rtsp, etc.)

---

### r_utils (Utilities)
**Location:** `libs/r_utils/`

**Purpose:** Common utilities used across all components

**Key Utilities:**
- `r_logger` - Logging framework with levels and formatting
- `r_socket` - Cross-platform socket abstraction
- `r_string_utils` - String manipulation utilities
- `r_time_utils` - Time and timestamp handling
- `r_uuid` - UUID generation
- `r_file` - File I/O utilities
- `r_exception` - Exception base classes
- `r_dynamic_library` - Plugin loading (dlopen/LoadLibrary wrapper)
- `r_thread_pool` - Thread pool for concurrent operations
- mbedTLS integration - Cryptography and TLS

**Dependencies:** uuid, mbedtls (embedded), platform-specific libraries (ws2_32 on Windows, dl on Linux)

---

### r_ui_utils (UI Utilities)
**Location:** `libs/r_ui_utils/`

**Purpose:** UI helper functions for ImGui-based interfaces

**Key Features:**
- ImGui integration utilities
- Texture loading and management (for video frames)
- Font handling
- OpenGL helpers
- Common UI widgets

**Dependencies:** imgui, r_utils, glfw, OpenGL

---

### r_fakey (Testing)
**Location:** `libs/r_fakey/`

**Purpose:** Fake camera and test stream generation for development and testing

**Features:**
- Generate synthetic video streams
- Simulate camera behavior
- Test data generation
- Mock components for unit testing

**Dependencies:** r_utils, r_av, FFmpeg, GStreamer

## Data Flow

### Camera Discovery Flow

```
1. User launches revere application
2. r_disco initiates ONVIF WS-Discovery via r_onvif
3. r_onvif sends multicast discovery messages (port 3702)
4. Cameras respond with their ONVIF endpoints
5. r_onvif queries camera capabilities and media profiles
6. Discovered cameras appear in UI
7. User selects camera and provides credentials
8. r_onvif retrieves RTSP stream URL
9. Camera added to r_disco device registry
10. Configuration saved to r_db
```

### Recording Flow

```
Camera RTSP Stream (H.264/H.265)
    ↓
r_pipeline (GStreamer RTSP source)
    ↓
Video Decoding (gstreamer decoders)
    ↓ (frames)
    ├──> r_motion (motion detection)
    │       ↓
    │    Motion Events → r_vss plugin host → AI Plugins
    ↓
r_av (FFmpeg encoding/muxing)
    ↓
r_storage (segment writing + nanots indexing)
    ↓
Disk (video segments + time-series index)
```

### Motion Detection Flow

```
1. r_pipeline extracts decoded frames from GStreamer
2. Frames sent to r_motion for analysis
3. r_motion applies background subtraction
4. Adaptive thresholding detects motion pixels
5. Motion region calculated (bounding box)
6. If motion exceeds threshold → Motion Event triggered
7. r_vss receives motion event
8. r_motion_event_plugin_host dispatches to loaded plugins
9. AI plugins (YOLOv8, etc.) receive frame + motion region
10. Plugins perform object detection (e.g., person detection)
11. Results can trigger alerts or enhanced logging
```

### Playback Flow

```
1. vision app sends HTTP request to revere (:10080)
2. Request: GET /contents?camera_id=X&start_time=Y&end_time=Z
3. r_http receives request
4. r_storage queries nanots for matching segments
5. Response returns list of available segments
6. vision requests specific segment for playback
7. r_av decodes video segment
8. Frames streamed back to vision
9. vision displays frames via ImGui/OpenGL
```

## Storage Architecture

### File Organization

[Description of how recordings are organized on disk]

```
storage_root/
├── camera_id_1/
│   ├── YYYY-MM-DD/
│   │   ├── segment_1.mp4
│   │   ├── segment_2.mp4
│   │   └── index.db
│   └── events/
└── camera_id_2/
    └── ...
```

### Segment Management

[How video segments are created, rotated, and pruned]

### Metadata Storage

[How metadata is stored and indexed]

### Storage Optimization

[Strategies for efficient storage usage]

## Plugin System

### Motion Detection Plugins

**Location:** `libs/r_vss/motion_plugins/`

**Available Plugins:**

#### yolov8_person_plugin
**Status:** Active (enabled by default when NCNN is available)
**Purpose:** Person detection using YOLOv8 neural network
**Performance:** Medium speed, high accuracy
**Use case:** Reliable person detection with good balance of speed and accuracy
**Requirements:** NCNN, model files in `models/yolov8_person_plugin/`

#### mobilenet_person_plugin
**Status:** Available but disabled by default
**Purpose:** Person detection using MobileNet SSD
**Performance:** Fast, moderate accuracy
**Use case:** Resource-constrained systems needing quick detection
**Requirements:** NCNN, model files

#### picodet_person_plugin
**Status:** Available but disabled by default
**Purpose:** Person detection using PicoDet lightweight detector
**Performance:** Very fast, good accuracy
**Use case:** Real-time detection on lower-end hardware
**Requirements:** NCNN, model files

#### test_plugin
**Status:** Always built (no NCNN required)
**Purpose:** Reference implementation and testing
**Use case:** Plugin development template and system testing

### Plugin Interface

The plugin system uses a C interface for maximum compatibility:

**Plugin Entry Points:**
- `load_plugin(host_handle)` - Initialize plugin with host
- `destroy_plugin(plugin_handle)` - Cleanup
- `post_motion_event(...)` - Receive motion events with frame data

**Motion Event Types:**
- `motion_event_start` (0) - Motion begins
- `motion_event_update` (1) - Motion continues
- `motion_event_end` (2) - Motion ends

**Frame Data Format:**
- BGR 24-bit format (3 bytes per pixel)
- Width and height provided
- Motion region bounding box included

**Creating a Custom Plugin:**

1. Implement the three required C functions
2. Link against r_utils and any detection libraries
3. Build as shared library (.so/.dll)
4. Place in `motion_plugins/` directory
5. Plugin automatically loaded by r_motion_event_plugin_host

**Plugin Lifecycle:**
1. r_vss startup → r_motion_event_plugin_host scans plugin directory
2. Plugins loaded via r_dynamic_library
3. `load_plugin()` called for each plugin
4. During recording: motion events trigger `post_motion_event()` calls
5. Plugins receive frame data and motion regions
6. On shutdown: `destroy_plugin()` called for cleanup

## Network Architecture

### RTSP Streaming

[How RTSP streams are handled]

### ONVIF Communication

[Network topology for ONVIF discovery and control]

### Web API

[REST API architecture]

### Port Usage

| Port | Protocol | Purpose |
|------|----------|---------|
| 3702 | UDP | ONVIF WS-Discovery (multicast) |
| 80/8080 | TCP | ONVIF HTTP communication with cameras |
| 554 | TCP | RTSP camera streams (inbound from cameras) |
| 10080 | TCP | Revere HTTP API server (for vision app) |
| 10554 | TCP | Revere RTSP restreaming server (outbound) |

## Threading Model

### Thread Pool Architecture

[Description of threading strategy]

### Per-Camera Threads

[How camera processing is threaded]

### Synchronization

[How data is synchronized across threads]

### Performance Considerations

[Threading best practices and performance notes]

## Design Decisions

### Why C++?

[Rationale for C++ choice]

### Why GStreamer AND FFmpeg?

[Explanation of why both are used]

### Storage Format Choice

[Why the chosen storage format]

### Stateful vs Stateless Components

[Architectural decisions around state management]

### Error Handling Strategy

[How errors are handled across the system]

### [Other Key Decisions]

[Continue pattern]

## Extension Points

### Adding New Camera Types

[Guide to extending camera support]

### Custom Motion Detection Algorithms

[Guide to adding new motion detection]

### Custom Storage Backends

[Guide to implementing alternative storage]

### API Extensions

[Guide to extending the REST API]

## Performance Characteristics

### Expected Performance

[Performance expectations for various configurations]

### Bottlenecks

[Known performance bottlenecks]

### Optimization Opportunities

[Areas for future optimization]

## Security Considerations

### Authentication

[How authentication is handled]

### Camera Credentials

[How camera credentials are stored/managed]

### Network Security

[Network security considerations]

### Data Protection

[How recorded data is protected]

## Future Architecture Plans

[Planned architectural changes or improvements]
