# Revere User Guide

Complete guide to using Revere video surveillance system.

## Table of Contents

- [Getting Started](#getting-started)
- [First Time Setup](#first-time-setup)
- [Camera Management](#camera-management)
- [Recording Configuration](#recording-configuration)
- [Motion Detection](#motion-detection)
- [Viewing Recordings](#viewing-recordings)
- [System Configuration](#system-configuration)
- [Advanced Features](#advanced-features)
- [Maintenance](#maintenance)
- [API Reference](#api-reference)

## Getting Started

### Starting Revere

[How to start the application]

**Linux:**
```bash
./revere [options]
```

**Windows:**
```
revere.exe [options]
```

### Command Line Options

```
Usage: revere [options]
       vision [options]

Common options will be shown in the application's help menu or via --help flag.
```

### First Launch

On first launch, Revere will:
1. Create necessary configuration directories
2. Initialize the database for camera and recording metadata
3. Start the HTTP API server (port 10080)
4. Begin ONVIF camera discovery
5. Show the main interface with discovered cameras

### System Tray

Revere includes a system tray icon (Windows/Linux desktop) that allows:
- Quick access to the main window
- Exit application
- View system status
- The application can run in the background with the tray icon visible

### Main Interface Overview

[Description of the main UI with labeled screenshots]

#### Camera Panel

[Description]

#### Recording Status

[Description]

#### Settings Panel

[Description]

## First Time Setup

### Setting Storage Location

[How to configure where recordings are stored]

**Considerations:**
- Disk space requirements
- Performance implications
- Network storage options

### Network Configuration

[How to configure network settings]

### Initial System Settings

[Other important settings to configure on first run]

## Camera Management

### Discovering Cameras

#### Automatic Discovery (ONVIF)

Revere automatically discovers ONVIF-compliant cameras on your network:

1. Ensure cameras are powered on and connected to the same network
2. Launch Revere - discovery begins automatically
3. Discovered cameras appear in the "Discovered" list
4. Click on a discovered camera to view details
5. Click "Record" to begin configuration
6. Enter camera credentials (username/password)
7. Revere will test the connection and measure the stream bitrate
8. Choose storage file size based on retention estimates
9. Camera begins recording

**Supported Discovery Methods:**
- ONVIF WS-Discovery (multicast on port 3702)
- Automatic profile and stream URI detection

#### Manual Camera Addition

If a camera doesn't appear in discovery, you can add it manually:

**Required Information:**
- Camera IP address or hostname
- RTSP URL (e.g., rtsp://192.168.1.100:554/stream1)
- Username/password for authentication
- Friendly name for the camera

**Note:** Manual addition requires knowing the exact RTSP URL for your camera model. Check your camera's documentation or manufacturer's website.

### Camera Configuration

#### Basic Settings

[Description of basic camera settings]

- **Name:** [description]
- **Location:** [description]
- **Enable Recording:** [description]

#### Video Settings

[Description of video settings]

- **Resolution:** [description]
- **Frame Rate:** [description]
- **Bitrate:** [description]

#### ONVIF Settings

[ONVIF-specific settings]

- **Profile:** [description]
- **Stream URI:** [description]

#### PTZ Control (Pan/Tilt/Zoom)

[How to use PTZ features if camera supports them]

### Testing Camera Connection

[How to verify camera is working]

### Camera Status Indicators

[Explanation of status icons/colors]

- 🟢 Green: [meaning]
- 🟡 Yellow: [meaning]
- 🔴 Red: [meaning]

### Removing a Camera

[How to remove a camera]

### Camera Groups/Organization

[If applicable - how to organize cameras]

## Recording Configuration

### Recording Modes

#### Continuous Recording

[Description and configuration]

**Pros:**
[List advantages]

**Cons:**
[List disadvantages]

**Configuration:**
[How to enable and configure]

#### Motion-Based Recording

[Description and configuration]

**Pros:**
[List advantages]

**Cons:**
[List disadvantages]

**Configuration:**
[How to enable and configure]

#### Scheduled Recording

[Description and configuration]

### Retention Policies

[How to configure how long recordings are kept]

**Options:**
- By duration (keep N days)
- By disk space (keep until X% full)
- Never delete

**Configuration:**
[Steps to configure retention]

### Storage Management

#### Viewing Storage Usage

[How to see current storage usage]

#### Manual Cleanup

[How to manually delete old recordings]

#### Export Recordings

[How to export specific recordings]

### Multiple Storage Locations

[If supported - how to configure multiple storage paths]

### Network Storage

[How to use NAS or network storage]

**Considerations:**
[Performance, reliability notes]

## Motion Detection

### Enabling Motion Detection

[How to enable motion detection per camera]

### Motion Detection Settings

#### Sensitivity

[How to adjust sensitivity]

#### Zones

[How to configure motion detection zones]

**Creating Zones:**
[Step-by-step]

**Use Cases:**
[Examples of when to use zones]

#### Schedules

[How to schedule when motion detection is active]

### AI-Based Motion Detection

[Description of AI plugins]

#### Available Plugins

##### Person Detection (MobileNet)
[Description, accuracy, performance]

##### Person Detection (PicoDet)
[Description, accuracy, performance]

##### Person Detection (YOLOv8)
[Description, accuracy, performance]

#### Choosing a Plugin

[Guidance on which plugin to use]

| Plugin | Speed | Accuracy | Resource Usage |
|--------|-------|----------|----------------|
| MobileNet | Fast | Good | Low |
| PicoDet | Fast | Good | Low |
| YOLOv8 | Medium | Better | Medium |

#### Installing AI Models

[How to install required model files]

#### Configuring AI Detection

[Settings specific to AI detection]

- **Confidence Threshold:** [description]
- **Object Classes:** [description]

### Motion Alerts

[If implemented - how to configure alerts]

### Testing Motion Detection

[How to verify motion detection is working]

## Viewing Recordings

### Playback Interface

[Description of playback interface]

#### Timeline View

[How to navigate the timeline]

#### Camera Selection

[How to select which camera to view]

#### Date/Time Selection

[How to jump to specific date/time]

### Playback Controls

[Description of playback controls]

- Play/Pause
- Speed control
- Frame stepping
- [etc.]

### Multi-Camera Viewing

[How to view multiple cameras simultaneously]

### Searching Recordings

#### By Time Range

[How to search by time]

#### By Motion Events

[How to view only motion events]

#### By Camera

[How to filter by camera]

### Exporting Video Clips

[How to export specific portions of recordings]

**Supported Formats:**
[List of export formats]

**Steps:**
[Step-by-step export process]

### Snapshots

[How to capture still images from recordings]

## System Configuration

### General Settings

[Description of general application settings]

### Network Settings

[Network-related settings]

#### HTTP Server

[Configuration of built-in web server]

- Port
- Authentication
- HTTPS/TLS

#### RTSP Server

[If applicable - RTSP streaming settings]

### Performance Settings

[Settings that affect performance]

- **Thread Count:** [description]
- **Buffer Sizes:** [description]
- **Hardware Acceleration:** [description]

### Logging

[How to configure logging]

- Log level
- Log location
- Log rotation

### Backup and Restore

#### Backing Up Configuration

[How to backup system configuration]

#### Restoring Configuration

[How to restore from backup]

### Database Maintenance

[If applicable - database maintenance tasks]

## Advanced Features

### REST API

[Overview of REST API]

**Base URL:**
```
http://localhost:PORT/api
```

**Authentication:**
[How to authenticate]

**Key Endpoints:**
[List main endpoints or link to API reference]

### Integration with Home Automation

[Examples of integration with other systems]

#### Home Assistant

[Integration guide]

#### OpenHAB

[Integration guide]

#### Custom Integration

[Guide to integrating with custom systems]

### Custom Motion Detection Plugins

[How users can add custom plugins]

### Scripting

[If applicable - scripting capabilities]

### Command Line Tools

[Additional command line tools included]

## Maintenance

### Regular Maintenance Tasks

[Recommended maintenance schedule]

**Daily:**
- [Task]

**Weekly:**
- [Task]

**Monthly:**
- [Task]

### Monitoring System Health

[How to monitor system health]

#### Log Files

[Where to find logs and what to look for]

#### Performance Metrics

[Key metrics to monitor]

#### Disk Space

[Monitoring storage usage]

### Updating Revere

[How to update to a new version]

**Before Updating:**
[Preparation steps]

**Update Process:**
[Step-by-step]

**After Updating:**
[Post-update steps]

### Backing Up Recordings

[Best practices for backing up recordings]

### Disaster Recovery

[What to do if system fails]

## API Reference

[Quick reference of REST API endpoints]

### Camera Management API

#### List Cameras
```
GET /api/cameras
```

[Description, parameters, response]

#### Add Camera
```
POST /api/cameras
```

[Description, parameters, response]

#### Update Camera
```
PUT /api/cameras/{id}
```

[Description, parameters, response]

#### Delete Camera
```
DELETE /api/cameras/{id}
```

[Description, parameters, response]

### Recording API

#### Query Recordings
```
GET /api/recordings
```

[Description, parameters, response]

#### Download Recording
```
GET /api/recordings/{id}/download
```

[Description, parameters, response]

### System API

#### System Status
```
GET /api/status
```

[Description, parameters, response]

#### System Configuration
```
GET /api/config
PUT /api/config
```

[Description, parameters, response]

### [Additional API Sections]

[Continue pattern for other API categories]

## Appendix

### Glossary

[Definitions of technical terms used in this guide]

### FAQ

[Frequently asked questions]

### Keyboard Shortcuts

[If applicable - list of keyboard shortcuts]

### Supported Camera Models

[List of tested camera models]
