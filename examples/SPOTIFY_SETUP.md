# Spotify App Setup Guide

Complete setup guide for the PC2 Spotify controller with automatic OAuth token refresh.

## Quick Start

```bash
# 1. Run the authorization helper (one-time setup)
cd examples
./spotify_auth.py

# 2. Build and run the app
cd ../build
make spotify_http_app
./spotify_http_app
```

## Detailed Setup

### Step 1: Create Spotify Application

1. Go to https://developer.spotify.com/dashboard
2. Log in with your Spotify account
3. Click "Create app"
4. Fill in the details:
   - **App name**: PC2 Controller (or anything you like)
   - **App description**: B&O Masterlink Spotify controller
   - **Redirect URI**: `http://127.0.0.1:8888/callback`
     - ⚠️ **Important**: Use `127.0.0.1`, NOT `localhost` (Spotify requirement)
   - **APIs used**: Web API
5. Click "Save"
6. Note your **Client ID** and **Client Secret** (click "Show client secret")

### Step 2: Run Authorization Helper

The Python script handles the OAuth flow and stores your tokens:

```bash
cd examples
./spotify_auth.py
```

**What it does:**
1. Prompts for Client ID and Client Secret (saved to `~/.spotify_client`)
2. Opens your browser for Spotify login
3. You grant permissions to the app
4. Exchanges authorization code for tokens
5. Saves tokens to `~/.spotify_tokens`

**Required Python packages:**
```bash
pip3 install requests
```

### Step 3: Build the C++ Application

```bash
cd build
cmake ..
make spotify_http_app
```

**Dependencies:**
- libcurl (HTTP client)
- nlohmann-json (JSON parsing)
- Boost::log (logging)
- libusb (PC2 USB communication)

### Step 4: Run the Application

```bash
./spotify_http_app
```

The app will:
- Load tokens from `~/.spotify_tokens`
- Automatically refresh access tokens when they expire
- Connect to PC2 and listen for Beo4 commands

## File Structure

After setup, you'll have these files in your home directory:

### `~/.spotify_client`
```json
{
  "client_id": "abc123...",
  "client_secret": "xyz789..."
}
```
- **Permissions**: 0600 (read/write for owner only)
- Contains your Spotify app credentials
- Used to refresh access tokens

### `~/.spotify_tokens`
```json
{
  "access_token": "BQD...",
  "token_type": "Bearer",
  "expires_in": 3600,
  "refresh_token": "AQC...",
  "scope": "user-read-playback-state user-modify-playback-state..."
}
```
- **Permissions**: 0600 (read/write for owner only)
- Contains OAuth tokens
- Auto-updated when tokens refresh

## How Token Refresh Works

The C++ app automatically handles token expiration:

1. **Initial Load**: Reads `access_token` and `refresh_token` from `~/.spotify_tokens`
2. **Expiry Tracking**: Monitors token expiry time (refreshes 60 seconds early)
3. **Automatic Refresh**: When token expires:
   - Uses `refresh_token` + Client ID/Secret to get new `access_token`
   - Updates `~/.spotify_tokens` with new token
   - Continues operation seamlessly
4. **401 Handling**: If API returns 401 Unauthorized:
   - Immediately refreshes token
   - Retries the request
   - User never sees an error

### Token Lifetime

- **Access Token**: 1 hour (refreshed automatically)
- **Refresh Token**: Never expires (until revoked by user)

## Security Notes

### File Permissions

Both credential files are automatically set to `0600` (owner read/write only):
```bash
-rw------- 1 user user  123 Jan 10 12:00 .spotify_client
-rw------- 1 user user  456 Jan 10 12:00 .spotify_tokens
```

### Best Practices

1. **Never commit tokens to git**:
   ```bash
   # Add to .gitignore
   .spotify_client
   .spotify_tokens
   ```

2. **Revoke access** if tokens are compromised:
   - Go to https://www.spotify.com/account/apps/
   - Remove "PC2 Controller" from connected apps
   - Run `spotify_auth.py` again to re-authorize

3. **Client Secret protection**:
   - Don't share your Client Secret
   - If exposed, regenerate it in the Spotify Dashboard
   - Run `spotify_auth.py` again with new credentials

## Troubleshooting

### "Failed to open token file"

**Problem**: `~/.spotify_tokens` doesn't exist

**Solution**: Run `./spotify_auth.py` to generate tokens

### "Failed to refresh access token"

**Possible causes:**
1. **Invalid refresh token**: Run `spotify_auth.py` again
2. **Invalid Client Secret**: Check `~/.spotify_client` or regenerate in Dashboard
3. **App revoked**: User removed permissions at spotify.com/account/apps

**Solution**: Re-run authorization:
```bash
rm ~/.spotify_client ~/.spotify_tokens
./spotify_auth.py
```

### "Authorization failed" in browser

**Possible causes:**
1. **Wrong redirect URI**: Must be exactly `http://127.0.0.1:8888/callback`
2. **Port 8888 in use**: Close other apps using that port
3. **Firewall blocking**: Allow localhost connections

**Solution**:
- Check Spotify app settings match redirect URI exactly
- Try closing and re-running `spotify_auth.py`

### Tokens keep expiring immediately

**Problem**: System clock incorrect

**Solution**: Sync system time:
```bash
# macOS
sudo sntp -sS time.apple.com

# Linux
sudo ntpdate pool.ntp.org
```

## Manual Token Refresh

If you need to manually refresh tokens:

```bash
# Using curl
CLIENT_ID="your_client_id"
CLIENT_SECRET="your_client_secret"
REFRESH_TOKEN="your_refresh_token"

curl -X POST https://accounts.spotify.com/api/token \
  -H "Authorization: Basic $(echo -n "$CLIENT_ID:$CLIENT_SECRET" | base64)" \
  -d "grant_type=refresh_token&refresh_token=$REFRESH_TOKEN"
```

## Revoking Access

To completely remove Spotify authorization:

1. **Remove from Spotify**:
   - Visit https://www.spotify.com/account/apps/
   - Click "Remove Access" for PC2 Controller

2. **Delete local files**:
   ```bash
   rm ~/.spotify_client ~/.spotify_tokens
   ```

3. **Re-authorize** when needed:
   ```bash
   ./spotify_auth.py
   ```

## Advanced: Debugging Token Issues

Enable verbose logging to see token refresh in action:

```bash
# In the C++ app, you'll see logs like:
# [INFO] Loaded Spotify credentials and tokens
# [INFO] Access token expired, refreshing...
# [INFO] Access token refreshed, expires in 3600 seconds
```

Check token expiry manually:
```bash
# View token file
cat ~/.spotify_tokens | python3 -m json.tool

# Check expiry time (expires_in is in seconds)
```

## See Also

- [SPOTIFY_HTTP_APP.md](SPOTIFY_HTTP_APP.md) - Full application documentation
- [Spotify Web API Reference](https://developer.spotify.com/documentation/web-api)
- [OAuth 2.0 Authorization Code Flow](https://developer.spotify.com/documentation/web-api/tutorials/code-flow)
