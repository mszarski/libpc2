# Spotify Application for PC2

A Spotify controller for Bang & Olufsen PC2/Masterlink systems that integrates spotifyd with Beo4 remote control.

## Features

- **Beo4 Remote Control Support**:
  - Play/Pause: Use the Play and Pause/Stop buttons
  - Next Track: Step Up or Wind buttons
  - Previous Track: Step Down or Rewind buttons

- **Masterlink Integration**:
  - Shows up as source A.MEM2 (N.MUSIC) on Masterlink
  - Displays current track name on compatible B&O displays
  - Automatic playback of Liked Songs when source is selected
  - Real-time track metadata updates every 2 seconds

- **D-Bus Control**:
  - Full MPRIS2 integration with spotifyd
  - Automatic playback transfer to spotifyd
  - Track metadata fetching (title, artist)

## Prerequisites

### System Requirements

1. **spotifyd** - Lightweight Spotify client
   ```bash
   # Install spotifyd (Arch Linux example)
   sudo pacman -S spotifyd

   # Or build from source
   git clone https://github.com/Spotifyd/spotifyd.git
   cd spotifyd
   cargo build --release
   ```

2. **sdbus-c++** - D-Bus C++ library
   ```bash
   # Arch Linux
   sudo pacman -S sdbus-cpp

   # Ubuntu/Debian
   sudo apt install libsdbus-c++-dev
   ```

3. **Spotify Premium Account** - Required for spotifyd

### spotifyd Configuration

Create `~/.config/spotifyd/spotifyd.conf`:

```ini
[global]
username = "your_spotify_username"
password = "your_spotify_password"
# Or use password_cmd for better security:
# password_cmd = "pass spotify"

backend = "alsa"
device = "default"  # or your ALSA device name

# IMPORTANT: Enable MPRIS for D-Bus control
use_mpris = true

# Device name that appears in Spotify Connect
device_name = "PC2_Spotify"

# Bitrate settings
bitrate = 320

# Volume control
volume_normalisation = true
normalisation_pregain = -10

# Cache
cache_path = "/tmp/spotifyd"
```

## Building

```bash
cd /Users/mszarski/source/libpc2
mkdir -p build && cd build
cmake ..
make spotify_app
```

## Running

### 1. Start spotifyd

```bash
# Run spotifyd in the background
spotifyd --no-daemon &

# Or run as a systemd service
systemctl --user start spotifyd
systemctl --user enable spotifyd  # Auto-start on boot
```

Verify spotifyd is running:
```bash
# Check if spotifyd appears on D-Bus
dbus-send --session --print-reply --dest=org.freedesktop.DBus \
  /org/freedesktop/DBus org.freedesktop.DBus.ListNames | grep spotifyd
```

### 2. Run spotify_app

```bash
./spotify_app
```

### 3. Select Source on Masterlink

Use your Beo4 remote to:
1. Press A.MEM (or the source button for A.MEM2)
2. The display should show "SPOTIFY"
3. Playback will automatically start with your Liked Songs

## Usage

### Beo4 Remote Control

| Button | Action |
|--------|--------|
| Play | Start playback |
| Stop/Pause | Pause playback |
| Step Up / Wind | Next track |
| Step Down / Rewind | Previous track |

### Track Display

The current track and artist will be displayed on the Masterlink display, truncated to 8 characters:
- Format: `ARTIST-TITLE`
- Updates every 2 seconds when the track changes
- Example: `Daft-Get`

## Troubleshooting

### spotifyd not found on D-Bus

**Problem**: Application shows "Spotifyd not found on D-Bus"

**Solutions**:
1. Make sure spotifyd is running:
   ```bash
   ps aux | grep spotifyd
   ```

2. Verify `use_mpris = true` is set in spotifyd.conf

3. Check D-Bus for spotifyd:
   ```bash
   dbus-send --session --print-reply --dest=org.freedesktop.DBus \
     /org/freedesktop/DBus org.freedesktop.DBus.ListNames | grep spotifyd
   ```

4. Restart spotifyd:
   ```bash
   killall spotifyd
   spotifyd --no-daemon &
   ```

### No audio output

**Problem**: Spotify plays but no audio on PC2/Masterlink

**Solutions**:
1. Check spotifyd ALSA device configuration matches your PC2 audio output
2. Test ALSA output:
   ```bash
   speaker-test -D default -c 2
   ```
3. Verify audio routing in spotifyd.conf

### Track metadata not updating

**Problem**: Display shows "SPOTIFY" but not track names

**Solutions**:
1. Make sure a track is actually playing in Spotify
2. Check that spotifyd has MPRIS enabled
3. Test metadata manually:
   ```bash
   dest=org.mpris.MediaPlayer2.spotifyd.instance$(pidof spotifyd)
   dbus-send --print-reply --session --dest=$dest \
     /org/mpris/MediaPlayer2 org.freedesktop.DBus.Properties.Get \
     string:org.mpris.MediaPlayer2.Player string:Metadata
   ```

### Playback doesn't start on source selection

**Problem**: Selecting A.MEM2 doesn't start Spotify

**Solutions**:
1. Try manually starting playback with the Play button on Beo4
2. Check Spotify account is logged in to spotifyd
3. Verify internet connection
4. Check spotifyd logs:
   ```bash
   journalctl --user -u spotifyd -f
   ```

## Advanced Configuration

### Using Different Playlists

To use a different playlist instead of Liked Songs, modify [spotify_app.cpp:329](spotify_app.cpp#L329):

```cpp
// Replace this line:
spotify->openUri("spotify:user:spotify:collection");

// With your playlist URI (get from Spotify app -> Share -> Copy Playlist Link):
spotify->openUri("spotify:playlist:YOUR_PLAYLIST_ID");
```

### Changing the Source

By default, the app uses A.MEM2 (0x7A). To use a different Masterlink source, modify the source ID checks throughout the code.

### Adjusting Track Display Format

Modify the track display logic at [spotify_app.cpp:369-372](spotify_app.cpp#L369-L372) to change how track names are formatted.

## Technical Details

### D-Bus Communication

The application uses the MPRIS2 specification to communicate with spotifyd:

- **Interface**: `org.mpris.MediaPlayer2.Player`
- **Service**: `org.mpris.MediaPlayer2.spotifyd.instance<PID>`
- **Object Path**: `/org/mpris/MediaPlayer2`

### Masterlink Messages

When source is requested, the app sends:
1. DistributionRequest (0x6c) - Announce audio distribution
2. TrackText8 - Display text (8 chars)
3. StatusInfo - Playback status
4. TrackInfo - Track number
5. Enable ML audio distribution via mixer

### Thread Model

- Main thread: PC2 event loop processing Masterlink/Beo4 messages
- Background thread: Track metadata polling (2-second interval)

## Dependencies

- libpc2 (PC2 device library)
- sdbus-c++ (D-Bus C++ bindings)
- Boost.Log (logging)
- libusb (USB communication with PC2)

## License

Same as libpc2 main project.
