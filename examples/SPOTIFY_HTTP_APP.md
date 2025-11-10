# Spotify HTTP Application for PC2

A cross-platform Spotify controller for Bang & Olufsen PC2/Masterlink systems using the Spotify Web API. Works on both macOS and Linux!

## Features

- **Cross-platform**: Works on macOS and Linux (uses HTTP instead of D-Bus)
- **Beo4 Remote Control Support**:
  - Play/Pause: Use the Play and Pause/Stop buttons
  - Next Track: Step Up or Wind buttons
  - Previous Track: Step Down or Rewind buttons

- **Masterlink Integration**:
  - Shows up as source A.MEM2 (N.MUSIC) on Masterlink
  - Displays current track name with scrolling text
  - Automatic playback of Liked Songs when source is selected
  - Real-time track metadata updates

- **Spotify Web API**:
  - Full playback control (play, pause, next, previous)
  - Track metadata fetching (title, artist)
  - Works with any Spotify client (desktop, mobile, web player)

## Prerequisites

### System Requirements

1. **libcurl** - HTTP client library (usually pre-installed)
   ```bash
   # macOS (usually already installed)
   brew install curl

   # Linux
   sudo apt install libcurl4-openssl-dev
   ```

2. **nlohmann-json** - JSON parsing library
   ```bash
   # macOS
   brew install nlohmann-json

   # Ubuntu/Debian
   sudo apt install nlohmann-json3-dev

   # Arch Linux
   sudo pacman -S nlohmann-json
   ```

3. **Spotify Premium Account** - Required for Spotify Web API playback control

### Spotify API Setup

You need a Spotify access token with the following scopes:
- `user-read-currently-playing` - To read current track info
- `user-modify-playback-state` - To control playback
- `user-read-playback-state` - To read playback state

#### Quick Token Generation

1. Visit the [Spotify Web API Console](https://developer.spotify.com/console/get-users-currently-playing-track/)
2. Click "Get Token"
3. Select the required scopes:
   - user-read-currently-playing
   - user-modify-playback-state
   - user-read-playback-state
4. Copy the generated token
5. Save it to `~/.spotify_token`:
   ```bash
   echo "YOUR_ACCESS_TOKEN_HERE" > ~/.spotify_token
   chmod 600 ~/.spotify_token
   ```

**Note**: Tokens generated this way expire after 1 hour. For production use, implement proper OAuth 2.0 flow with token refresh.

#### Long-term Token Setup (Optional)

For a token that can be refreshed:

1. Create a Spotify App at [Spotify Developer Dashboard](https://developer.spotify.com/dashboard)
2. Get your Client ID and Client Secret
3. Implement OAuth 2.0 Authorization Code Flow with PKCE
4. Store the refresh token and automatically refresh the access token

See [Spotify Authorization Guide](https://developer.spotify.com/documentation/web-api/tutorials/code-flow) for details.

## Building

```bash
cd /Users/mszarski/source/libpc2
mkdir -p build && cd build
cmake ..
make spotify_http_app
```

## Running

### 1. Make sure you have a valid Spotify token

```bash
# Check that your token file exists
cat ~/.spotify_token
```

### 2. Start Spotify playback somewhere

The Web API controls existing Spotify playback. Make sure Spotify is playing on one of your devices:
- Desktop app
- Mobile app
- Web player
- Spotify Connect speaker

### 3. Run spotify_http_app

```bash
./spotify_http_app
```

### 4. Select Source on Masterlink

Use your Beo4 remote to:
1. Press A.MEM (or the source button for A.MEM2)
2. The display should show "SPOTIFY"
3. Playback will automatically start with your Liked Songs

## Usage

### Beo4 Remote Control

| Button | Action |
|--------|--------|
| Play | Start/resume playback |
| Stop/Pause | Pause playback |
| Step Up / Wind | Next track |
| Step Down / Rewind | Previous track |

### Track Display

The current track and artist will scroll on the Masterlink display:
- Format: "Artist - Track"
- Scrolls every 500ms for smooth reading
- Example: `Daft Punk - Get Lucky` displays as:
  ```
  Daft Pun → aft Punk → ft Punk  → t Punk - → ...
  ```

## Troubleshooting

### "No Spotify access token found"

**Problem**: Application can't find `~/.spotify_token`

**Solutions**:
1. Create the token file:
   ```bash
   echo "YOUR_TOKEN" > ~/.spotify_token
   ```
2. Make sure the file is in your home directory
3. Check file permissions: `chmod 600 ~/.spotify_token`

### "CURL request failed"

**Problem**: HTTP requests to Spotify API are failing

**Solutions**:
1. Check internet connection
2. Verify token is valid (tokens expire after 1 hour)
3. Regenerate token at [Spotify Console](https://developer.spotify.com/console/get-users-currently-playing-track/)
4. Check Spotify API status at [status.spotify.com](https://status.spotify.com)

### "No playback response" / Can't control playback

**Problem**: API calls succeed but nothing happens

**Solutions**:
1. **Start Spotify somewhere first**: The Web API can only control *existing* playback
   - Open Spotify on desktop, mobile, or web
   - Start playing any song
   - Then try controlling from PC2

2. Make sure you have Spotify Premium (required for Web API control)

3. Check token has correct scopes:
   ```bash
   # Your token should have been generated with these scopes:
   # - user-modify-playback-state
   # - user-read-currently-playing
   # - user-read-playback-state
   ```

### Track metadata not updating

**Problem**: Display shows "SPOTIFY" but not track names

**Solutions**:
1. Make sure a track is actually playing
2. Verify token has `user-read-currently-playing` scope
3. Check that Spotify client is connected and active
4. Try playing a track manually first

### Build errors: "nlohmann/json.hpp not found"

**Problem**: nlohmann-json library not installed

**Solutions**:
```bash
# macOS
brew install nlohmann-json

# Ubuntu/Debian
sudo apt install nlohmann-json3-dev

# Arch Linux
sudo pacman -S nlohmann-json
```

## Differences from D-Bus Version

| Feature | spotify_app (D-Bus) | spotify_http_app (Web API) |
|---------|--------------------|-----------------------------|
| **Platform** | Linux only | macOS + Linux |
| **Requirements** | spotifyd daemon | Any Spotify client |
| **Setup** | Install & run spotifyd | Get API token |
| **Token Expiry** | Never | 1 hour (console tokens) |
| **Playback Transfer** | Automatic to spotifyd | Controls active device |
| **Liked Songs Start** | Direct playback | Sends play command |

## Technical Details

### HTTP Endpoints Used

The application uses these Spotify Web API endpoints:

- **GET** `/v1/me/player/currently-playing` - Get current track info
- **PUT** `/v1/me/player/play` - Start/resume playback
- **PUT** `/v1/me/player/pause` - Pause playback
- **POST** `/v1/me/player/next` - Skip to next track
- **POST** `/v1/me/player/previous` - Skip to previous track

### Request Format

```
GET https://api.spotify.com/v1/me/player/currently-playing
Authorization: Bearer YOUR_ACCESS_TOKEN
Content-Type: application/json
```

### Masterlink Messages

Same as D-Bus version - sends standard Masterlink telegrams:
1. DistributionRequest (0x6c)
2. TrackText8 (scrolling display)
3. StatusInfo
4. TrackInfo
5. Enable ML audio distribution

### Thread Model

- **Main thread**: PC2 event loop (Masterlink/Beo4 messages)
- **Background thread**: Track metadata polling and scrolling text updates

## Security Notes

- **Token Storage**: Tokens are stored in plaintext in `~/.spotify_token`
  - Protect with `chmod 600 ~/.spotify_token`
  - Don't commit tokens to version control
  - Consider encrypting for production use

- **Token Expiry**: Console tokens expire after 1 hour
  - Implement OAuth refresh flow for production
  - App will fail gracefully when token expires

## Dependencies

- libpc2 (PC2 device library)
- libcurl (HTTP client)
- nlohmann-json (JSON parsing)
- Boost.Log (logging)
- libusb (USB communication with PC2)

## License

Same as libpc2 main project.
