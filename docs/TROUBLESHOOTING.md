# Troubleshooting Guide

Common issues and solutions for Revere video surveillance system.

## Table of Contents

- [General Issues](#general-issues)
- [Build Issues](#build-issues)
- [Camera Issues](#camera-issues)
- [Recording Issues](#recording-issues)
- [Playback Issues](#playback-issues)
- [Performance Issues](#performance-issues)
- [Network Issues](#network-issues)
- [Storage Issues](#storage-issues)
- [Platform-Specific Issues](#platform-specific-issues)
- [Getting More Help](#getting-more-help)

## General Issues

### Application Won't Start

**Symptoms:**
[Description of symptoms]

**Possible Causes:**
1. [Cause 1]
2. [Cause 2]

**Solutions:**

**Check Dependencies:**
```bash
# Linux
ldd ./revere
# Look for any missing libraries

# Windows
# Use Dependency Walker or similar tool
```

**Check Logs:**
```bash
# Look for error messages in logs
[log location]
```

**Verify Configuration:**
[Steps to verify configuration]

---

### Application Crashes on Startup

**Symptoms:**
[Description]

**Solutions:**

**Run with Debug Logging:**
```bash
./revere --log-level=debug
```

**Check Core Dump:**
[Instructions for analyzing crash dumps]

**Common Causes:**
- Missing configuration file
- Corrupted database
- Incompatible OpenGL drivers

---

### High CPU Usage

**Symptoms:**
[Description]

**Diagnosis:**
```bash
# Check which threads are consuming CPU
top -H -p $(pgrep revere)
```

**Solutions:**
[Solutions]

---

### High Memory Usage

**Symptoms:**
[Description]

**Diagnosis:**
[How to diagnose]

**Solutions:**
[Solutions]

## Build Issues

### CMake Can't Find Dependencies

**Symptoms:**
```
CMake Error: Could not find OpenCV
```

**Solutions:**

**Linux:**
```bash
# Ensure pkg-config can find the library
pkg-config --modversion opencv4

# If not found, install the development package
sudo apt-get install libopencv-dev
```

**Windows:**
```powershell
# Verify environment variables are set
echo $env:OPENCV_TOP_DIR
echo $env:GST_TOP_DIR
echo $env:FFMPEG_TOP_DIR

# Set if missing (see BUILDING.md)
```

---

### Linking Errors

**Symptoms:**
```
undefined reference to `symbol`
```

**Causes:**
- Missing library in link command
- ABI mismatch
- Incorrect library order

**Solutions:**
[Platform-specific solutions]

---

### Compiler Version Issues

**Symptoms:**
```
error: 'feature' is not a member of 'std'
```

**Solution:**
Ensure you're using a C++17 compatible compiler:
```bash
# Check compiler version
g++ --version  # Should be 9.x or higher
clang++ --version  # Should be 10.x or higher
```

---

### Windows-Specific Build Issues

#### MSVC Version Mismatch

**Symptoms:**
[Description]

**Solution:**
[Solution]

#### Missing Windows SDK

**Symptoms:**
[Description]

**Solution:**
[Solution]

## Camera Issues

### Camera Not Discovered

**Symptoms:**
Camera doesn't appear in the discovery list

**Diagnosis Checklist:**
- [ ] Camera is powered on
- [ ] Camera is on the same network
- [ ] ONVIF is enabled on the camera
- [ ] Firewall isn't blocking discovery
- [ ] Camera firmware is up to date

**Solutions:**

**Test Network Connectivity:**
```bash
# Ping the camera
ping <camera-ip>

# Test ONVIF port (typically 80 or 8080)
telnet <camera-ip> 80
# or
nc -zv <camera-ip> 80
```

**Manual Discovery:**
[Try adding camera manually with known IP]

**Check ONVIF Settings:**
[Camera-specific instructions]

**Firewall Rules:**
```bash
# Linux - allow ONVIF discovery
sudo ufw allow from <camera-subnet> to any port 3702 proto udp

# Windows
[PowerShell firewall commands]
```

---

### Camera Connection Lost

**Symptoms:**
Camera was working but now shows disconnected

**Common Causes:**
- Network issues
- Camera reboot
- Credential change
- DHCP address change

**Solutions:**

**Check Camera Status:**
```bash
ping <camera-ip>
```

**Verify Credentials:**
Re-configure the camera in Revere with the correct username and password. If credentials have changed on the camera, you'll need to update them in Revere.

**Check for IP Change:**
If using DHCP, the camera's IP address may have changed. Either:
- Configure your router to assign a static DHCP lease
- Configure a static IP on the camera
- Re-discover the camera with its new IP address

**Review Logs:**
Check the Revere log output for connection errors. Logs show ONVIF communication attempts and failures.

---

### Authentication Failed

**Symptoms:**
```
Error: Authentication failed for camera <name>
```

**Solutions:**
- Verify username and password
- Check for special characters in password
- Try digest authentication vs basic
- Reset camera credentials if necessary

---

### Video Stream Not Working

**Symptoms:**
Camera connects but no video appears

**Diagnosis:**

**Test RTSP Stream Directly:**
```bash
# Using ffplay
ffplay rtsp://<username>:<password>@<camera-ip>/stream

# Using VLC
vlc rtsp://<username>:<password>@<camera-ip>/stream

# Using GStreamer
gst-launch-1.0 rtspsrc location=rtsp://<username>:<password>@<camera-ip>/stream ! decodebin ! autovideosink
```

**Check Stream URI:**
[How to verify correct RTSP URI]

**Codec Compatibility:**
[Check if codec is supported]

**Solutions:**
[Solutions based on diagnosis]

---

### Poor Video Quality

**Symptoms:**
Video is pixelated or low quality

**Solutions:**
- Adjust camera bitrate settings
- Check network bandwidth
- Verify resolution settings
- Adjust encoding quality in camera

---

### PTZ Not Working

**Symptoms:**
Pan/Tilt/Zoom controls don't work

**Solutions:**
- Verify camera supports PTZ
- Check ONVIF PTZ support
- Test PTZ from camera's web interface
- Review PTZ permissions

## Recording Issues

### Recording Not Starting

**Symptoms:**
[Description]

**Diagnosis Checklist:**
- [ ] Recording is enabled for the camera
- [ ] Storage path is configured
- [ ] Sufficient disk space available
- [ ] Write permissions on storage path

**Solutions:**

**Check Storage Path:**
```bash
# Verify path exists and is writable
ls -ld /path/to/storage
touch /path/to/storage/test_write && rm /path/to/storage/test_write
```

**Check Disk Space:**
```bash
df -h /path/to/storage
```

**Review Error Logs:**
[Log location and what to look for]

---

### Recording Stops Unexpectedly

**Symptoms:**
[Description]

**Common Causes:**
- Disk full
- Network interruption
- Camera stream interrupted
- Process crash

**Solutions:**
[Solutions]

---

### Gaps in Recording

**Symptoms:**
Timeline shows missing segments

**Causes:**
- Camera was offline
- Network issues
- Motion detection gaps (if using motion-based recording)
- Storage errors

**Diagnosis:**
[How to diagnose]

**Solutions:**
[Solutions]

---

### Corrupted Recordings

**Symptoms:**
Can't play back certain recordings

**Solutions:**
- Check disk health
- Verify not interrupted during write
- Try recovery tools
- Check filesystem errors

```bash
# Linux - check filesystem
sudo fsck /dev/sdX
```

## Playback Issues

### Can't Play Back Recordings

**Symptoms:**
[Description]

**Solutions:**

**Verify Recording Exists:**
```bash
ls -lh /path/to/storage/camera_id/date/
```

**Check File Permissions:**
```bash
ls -l [recording-file]
```

**Test File Directly:**
```bash
ffplay [recording-file]
```

---

### Playback Stuttering

**Symptoms:**
Video playback is choppy

**Causes:**
- High system load
- Slow storage (network storage)
- Codec issues
- Insufficient system resources

**Solutions:**
[Solutions]

---

### Timeline Not Loading

**Symptoms:**
[Description]

**Solutions:**
- Check database connectivity
- Verify index files
- Rebuild index if corrupted

---

### Wrong Time Zone

**Symptoms:**
Recordings appear at wrong time

**Solutions:**
- Verify system timezone
- Check camera timezone settings
- Verify NTP configuration

```bash
# Check system timezone
timedatectl  # Linux
tzutil /g    # Windows
```

## Performance Issues

### Slow Performance with Multiple Cameras

**Symptoms:**
[Description]

**Solutions:**

**Optimize Settings:**
- Reduce resolution
- Lower frame rate
- Adjust recording quality
- Enable hardware acceleration

**System Tuning:**
```bash
# Increase file descriptors (Linux)
ulimit -n 4096

# Adjust network buffers
[sysctl commands]
```

**Resource Monitoring:**
```bash
# Monitor resources
htop  # or top
iotop  # disk I/O
iftop  # network I/O
```

---

### High Disk I/O

**Symptoms:**
[Description]

**Solutions:**
- Use faster storage
- Adjust recording quality
- Enable write caching
- Consider RAID configuration

---

### Frame Drops

**Symptoms:**
[Description]

**Diagnosis:**
[How to identify frame drops]

**Solutions:**
[Solutions]

## Network Issues

### Multicast Issues

**Symptoms:**
[Description]

**Solutions:**

**Enable Multicast:**
```bash
# Linux - enable multicast on interface
sudo ip link set eth0 multicast on

# Verify
ip link show eth0 | grep MULTICAST
```

---

### Firewall Blocking

**Symptoms:**
[Description]

**Required Ports:**

| Port | Protocol | Purpose |
|------|----------|---------|
| 3702 | UDP | ONVIF Discovery |
| 80 | TCP | HTTP/ONVIF |
| 554 | TCP | RTSP |
| 8000 | TCP | Web Interface |
| [etc] | | |

**Solutions:**

**Linux (ufw):**
```bash
sudo ufw allow 554/tcp
sudo ufw allow 3702/udp
```

**Linux (firewalld):**
```bash
sudo firewall-cmd --permanent --add-port=554/tcp
sudo firewall-cmd --reload
```

**Windows:**
```powershell
New-NetFirewallRule -DisplayName "RTSP" -Direction Inbound -Protocol TCP -LocalPort 554 -Action Allow
```

---

### Network Bandwidth Issues

**Symptoms:**
Multiple cameras causing network congestion

**Solutions:**
- Use dedicated network for cameras
- Implement VLANs
- Adjust camera bitrates
- Use H.265 instead of H.264
- Consider local recording on cameras

## Storage Issues

### Disk Full

**Symptoms:**
```
Error: Cannot write to storage - disk full
```

**Solutions:**

**Check Disk Usage:**
```bash
df -h /path/to/storage
du -sh /path/to/storage/*
```

**Adjust Retention Policy:**
[How to configure retention]

**Manual Cleanup:**
[How to safely delete old recordings]

---

### Slow Storage Performance

**Symptoms:**
[Description]

**Diagnosis:**
```bash
# Test disk performance
sudo hdparm -t /dev/sdX  # Linux

# Or use dd
dd if=/dev/zero of=/path/to/storage/testfile bs=1M count=1024 oflag=direct
```

**Solutions:**
- Use SSD instead of HDD
- Check for disk errors
- Reduce number of concurrent recordings
- Use hardware RAID

---

### Permission Errors

**Symptoms:**
```
Error: Permission denied writing to [path]
```

**Solutions:**
```bash
# Linux - fix permissions
sudo chown -R $USER:$USER /path/to/storage
chmod -R u+rw /path/to/storage

# Verify
ls -ld /path/to/storage
```

---

### NAS/Network Storage Issues

**Symptoms:**
[Description]

**Solutions:**
- Verify NFS/SMB mount
- Check network latency
- Increase network timeouts
- Consider local storage with backup to NAS

```bash
# Test NFS performance
time dd if=/dev/zero of=/mnt/nas/testfile bs=1M count=1024
```

## Platform-Specific Issues

### Linux Issues

#### Missing Libraries

**Symptoms:**
```
error while loading shared libraries: libXXX.so
```

**Solution:**
```bash
# Find missing library
sudo apt-file search libXXX.so

# Install package
sudo apt-get install <package-name>

# Or update library cache
sudo ldconfig
```

---

#### GStreamer Plugin Issues

**Symptoms:**
[Description]

**Solution:**
```bash
# List available plugins
gst-inspect-1.0

# Install missing plugins
sudo apt-get install gstreamer1.0-plugins-bad gstreamer1.0-plugins-ugly
```

---

### Windows Issues

#### DLL Not Found

**Symptoms:**
```
The code execution cannot proceed because XXX.dll was not found
```

**Solutions:**
- Add dependency directories to PATH
- Copy required DLLs to executable directory
- Reinstall dependencies

**Required DLLs:**
[List of runtime DLLs needed]

---

#### Visual C++ Runtime

**Symptoms:**
Application requires specific VC++ runtime

**Solution:**
Install Visual C++ Redistributable:
[Link and version]

---

#### OpenGL Issues

**Symptoms:**
Graphics not rendering properly

**Solutions:**
- Update graphics drivers
- Check OpenGL version support
- Try software rendering fallback

## Getting More Help

### Collecting Diagnostic Information

When reporting issues, please provide:

**System Information:**
```bash
# Linux
uname -a
lsb_release -a
gcc --version
cmake --version

# Windows
systeminfo
[compiler version]
```

**Logs:**
```bash
# Copy relevant log sections
[log location]
```

**Configuration:**
[Relevant configuration]

### Log Levels

Increase log verbosity for debugging:
```bash
./revere --log-level=debug
# or
./revere --log-level=trace
```

### Debugging Tools

**Linux:**
```bash
# Run with debugger
gdb ./revere

# Capture system calls
strace -f -o trace.log ./revere

# Monitor file access
lsof -p $(pgrep revere)
```

**Windows:**
- Visual Studio Debugger
- Process Monitor
- Event Viewer

### Where to Get Help

1. **Documentation:**
   - [User Guide](USER_GUIDE.md)
   - [Building Guide](BUILDING.md)
   - [Architecture](ARCHITECTURE.md)

2. **Community:**
   - GitHub Issues: [link]
   - Discussions: [link]
   - [Other channels]

3. **Reporting Bugs:**
   See [CONTRIBUTING.md](../CONTRIBUTING.md) for bug report template

### Known Issues

[Link to GitHub issues with "known-issue" label]

### FAQ

#### Q: Does Revere support audio recording?
**A:** The focus is currently on video surveillance. Audio support may be added in future versions.

#### Q: Can I use Revere with non-ONVIF cameras?
**A:** If you can get the RTSP URL for your camera, you can add it manually. However, automatic discovery requires ONVIF support.

#### Q: What's the maximum number of cameras supported?
**A:** This depends on your system resources (CPU, network bandwidth, disk I/O). Most modern systems should handle several cameras, especially without AI detection enabled.

#### Q: Does Revere support cloud storage?
**A:** Revere stores recordings locally. You can use network storage (NAS) by pointing the storage path to a mounted network drive.

#### Q: Can I access Revere remotely?
**A:** The vision app connects to Revere's API (:10080) and the RTSP server (:10554). You can access these remotely if you configure port forwarding or VPN, but be mindful of security implications.

#### Q: What video codecs are supported?
**A:** Revere supports H.264 and H.265 (HEVC) video streams from cameras.

---

If your issue isn't covered here, please search existing [GitHub Issues](link) or create a new one following the guidelines in [CONTRIBUTING.md](../CONTRIBUTING.md).
