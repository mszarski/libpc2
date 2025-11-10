#!/usr/bin/env python3
"""
Spotify OAuth Authorization Helper

This script handles the OAuth flow for Spotify API access and stores
the refresh token for use by the spotify_app C++ application.

Usage:
    1. Create a Spotify app at https://developer.spotify.com/dashboard
    2. Set redirect URI to http://127.0.0.1:8888/callback
    3. Run this script: python3 spotify_auth.py
    4. Follow the prompts to authorize
    5. Tokens will be saved to ~/.spotify_tokens

The C++ app will automatically refresh access tokens using the stored refresh token.
"""

import http.server
import socketserver
import urllib.parse
import webbrowser
import json
import base64
import requests
from pathlib import Path

# Spotify OAuth endpoints
AUTH_URL = "https://accounts.spotify.com/authorize"
TOKEN_URL = "https://accounts.spotify.com/api/token"
REDIRECT_URI = "http://127.0.0.1:8888/callback"  # Using explicit IPv4 loopback

# Required scopes for playback control
SCOPES = [
    "user-read-playback-state",
    "user-modify-playback-state",
    "user-read-currently-playing"
]

# Global variables to capture authorization code
auth_code = None
auth_state = None


class CallbackHandler(http.server.SimpleHTTPRequestHandler):
    """HTTP handler to receive OAuth callback"""

    def do_GET(self):
        global auth_code, auth_state

        # Parse query parameters
        query = urllib.parse.urlparse(self.path).query
        params = urllib.parse.parse_qs(query)

        if 'code' in params:
            auth_code = params['code'][0]
            if 'state' in params:
                auth_state = params['state'][0]

            # Send success response
            self.send_response(200)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            self.wfile.write(b"""
                <html>
                <body>
                    <h1>Authorization successful!</h1>
                    <p>You can close this window and return to the terminal.</p>
                </body>
                </html>
            """)
        else:
            # Send error response
            self.send_response(400)
            self.send_header('Content-type', 'text/html')
            self.end_headers()
            error = params.get('error', ['Unknown error'])[0]
            self.wfile.write(f"""
                <html>
                <body>
                    <h1>Authorization failed</h1>
                    <p>Error: {error}</p>
                </body>
                </html>
            """.encode())

    def log_message(self, format, *args):
        # Suppress log messages
        pass


def get_client_credentials():
    """Read or prompt for Client ID and Secret"""
    config_file = Path.home() / ".spotify_client"

    if config_file.exists():
        with open(config_file, 'r') as f:
            data = json.load(f)
            return data['client_id'], data['client_secret']

    print("\n=== Spotify App Credentials ===\n")
    print("You need to create a Spotify application to use this tool.")
    print("1. Go to https://developer.spotify.com/dashboard")
    print("2. Click 'Create app'")
    print("3. Set Redirect URI to: http://127.0.0.1:8888/callback")
    print("   (Note: Use 127.0.0.1, not localhost - Spotify requires explicit loopback)")
    print("4. Copy your Client ID and Client Secret\n")

    client_id = input("Enter your Client ID: ").strip()
    client_secret = input("Enter your Client Secret: ").strip()

    # Save for future use
    with open(config_file, 'w') as f:
        json.dump({
            'client_id': client_id,
            'client_secret': client_secret
        }, f)
    config_file.chmod(0o600)

    print(f"\nCredentials saved to {config_file}")

    return client_id, client_secret


def start_oauth_flow(client_id):
    """Start OAuth authorization flow"""
    import secrets

    state = secrets.token_urlsafe(16)

    params = {
        'client_id': client_id,
        'response_type': 'code',
        'redirect_uri': REDIRECT_URI,
        'scope': ' '.join(SCOPES),
        'state': state
    }

    auth_url = f"{AUTH_URL}?{urllib.parse.urlencode(params)}"

    print("\n=== Authorization Required ===\n")
    print("Opening browser for Spotify authorization...")
    print("If the browser doesn't open automatically, visit this URL:\n")
    print(f"  {auth_url}\n")

    webbrowser.open(auth_url)

    return state


def exchange_code_for_tokens(client_id, client_secret, code):
    """Exchange authorization code for access and refresh tokens"""

    # Prepare credentials
    credentials = f"{client_id}:{client_secret}"
    credentials_b64 = base64.b64encode(credentials.encode()).decode()

    headers = {
        'Authorization': f'Basic {credentials_b64}',
        'Content-Type': 'application/x-www-form-urlencoded'
    }

    data = {
        'grant_type': 'authorization_code',
        'code': code,
        'redirect_uri': REDIRECT_URI
    }

    response = requests.post(TOKEN_URL, headers=headers, data=data)

    if response.status_code != 200:
        print(f"\nError exchanging code for tokens:")
        print(f"Status: {response.status_code}")
        print(f"Response: {response.text}")
        return None

    return response.json()


def save_tokens(tokens):
    """Save tokens to file"""
    token_file = Path.home() / ".spotify_tokens"

    with open(token_file, 'w') as f:
        json.dump(tokens, f, indent=2)

    # Set restrictive permissions
    token_file.chmod(0o600)

    return token_file


def main():
    print("=== Spotify PC2 Authorization Helper ===\n")

    # Get credentials
    client_id, client_secret = get_client_credentials()

    # Start OAuth flow
    expected_state = start_oauth_flow(client_id)

    # Start local server to receive callback
    # Bind to 127.0.0.1 explicitly (Spotify requirement)
    print("Starting local server to receive authorization...")
    print("Listening on http://127.0.0.1:8888/callback\n")

    with socketserver.TCPServer(("127.0.0.1", 8888), CallbackHandler) as httpd:
        # Wait for one request (the callback)
        httpd.handle_request()

    if not auth_code:
        print("\nError: No authorization code received!")
        return 1

    if auth_state != expected_state:
        print("\nError: State mismatch! Possible CSRF attack.")
        return 1

    print("\nAuthorization code received!")
    print("Exchanging code for tokens...")

    # Exchange code for tokens
    tokens = exchange_code_for_tokens(client_id, client_secret, auth_code)

    if not tokens:
        print("\nFailed to get tokens!")
        return 1

    # Save tokens
    token_file = save_tokens(tokens)

    print(f"\n✓ Success! Tokens saved to {token_file}")
    print("\nToken information:")
    print(f"  Access Token:  {tokens['access_token'][:20]}...")
    print(f"  Refresh Token: {tokens['refresh_token'][:20]}...")
    print(f"  Expires in:    {tokens['expires_in']} seconds")
    print(f"  Token Type:    {tokens['token_type']}")
    print(f"  Scope:         {tokens['scope']}")

    print("\nYou can now run the spotify_app C++ application!")
    print("It will automatically refresh the access token when needed.")

    return 0


if __name__ == '__main__':
    import sys
    sys.exit(main())
