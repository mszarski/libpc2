#include <iostream>
#include <csignal>
#include <memory>
#include <chrono>
#include <thread>
#include <fstream>
#include <sstream>
#include <boost/log/trivial.hpp>
#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "pc2/pc2.hpp"
#include "pc2/pc2interface.hpp"
#include "pc2/beo4.hpp"
#include "masterlink/telegram.hpp"

using json = nlohmann::json;

// Global flag for graceful shutdown
volatile bool keepRunning = true;

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received. Shutting down...\n";
    keepRunning = false;
}

// Callback function for curl to write data to string
static size_t WriteCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Spotify HTTP API client with automatic token refresh
class SpotifyHTTP {
private:
    CURL* curl;
    std::string accessToken;
    std::string refreshToken;
    std::string clientId;
    std::string clientSecret;
    std::chrono::steady_clock::time_point tokenExpiry;
    const std::string apiBase = "https://api.spotify.com/v1";
    const std::string tokenUrl = "https://accounts.spotify.com/api/token";

    std::string makeRequest(const std::string& url, const std::string& method = "GET",
                           const std::string& body = "") {
        // Check if token needs refresh
        if (std::chrono::steady_clock::now() >= tokenExpiry) {
            BOOST_LOG_TRIVIAL(info) << "Access token expired, refreshing...";
            if (!refreshAccessToken()) {
                BOOST_LOG_TRIVIAL(error) << "Failed to refresh access token!";
                return "";
            }
        }

        std::string response;

        if (!curl) {
            BOOST_LOG_TRIVIAL(error) << "CURL not initialized";
            return "";
        }

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

        // Set authorization header
        struct curl_slist* headers = NULL;
        std::string authHeader = "Authorization: Bearer " + accessToken;
        headers = curl_slist_append(headers, authHeader.c_str());
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // Set HTTP method
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            if (!body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            }
        } else if (method == "PUT") {
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
            if (!body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            }
        } else {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }

        CURLcode res = curl_easy_perform(curl);

        // Check for 401 Unauthorized (token invalid)
        long http_code = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            BOOST_LOG_TRIVIAL(error) << "CURL request failed: " << curl_easy_strerror(res);
            return "";
        }

        // If 401, try refreshing token and retry once
        if (http_code == 401) {
            BOOST_LOG_TRIVIAL(warning) << "Got 401 Unauthorized, refreshing token...";
            if (refreshAccessToken()) {
                // Retry the request with new token
                return makeRequest(url, method, body);
            }
        }

        return response;
    }

    bool refreshAccessToken() {
        if (refreshToken.empty() || clientId.empty() || clientSecret.empty()) {
            BOOST_LOG_TRIVIAL(error) << "Missing refresh token or client credentials";
            return false;
        }

        CURL* refresh_curl = curl_easy_init();
        if (!refresh_curl) {
            return false;
        }

        // Prepare Basic auth header: base64(client_id:client_secret)
        std::string credentials = clientId + ":" + clientSecret;
        std::string credentials_b64;

        // Simple base64 encoding
        const std::string base64_chars =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "abcdefghijklmnopqrstuvwxyz"
            "0123456789+/";

        int val = 0, valb = -6;
        for (unsigned char c : credentials) {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0) {
                credentials_b64.push_back(base64_chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) credentials_b64.push_back(base64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (credentials_b64.size() % 4) credentials_b64.push_back('=');

        struct curl_slist* headers = NULL;
        std::string authHeader = "Authorization: Basic " + credentials_b64;
        headers = curl_slist_append(headers, authHeader.c_str());
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");

        std::string postData = "grant_type=refresh_token&refresh_token=" + refreshToken;
        std::string response;

        curl_easy_setopt(refresh_curl, CURLOPT_URL, tokenUrl.c_str());
        curl_easy_setopt(refresh_curl, CURLOPT_POST, 1L);
        curl_easy_setopt(refresh_curl, CURLOPT_POSTFIELDS, postData.c_str());
        curl_easy_setopt(refresh_curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(refresh_curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(refresh_curl, CURLOPT_WRITEDATA, &response);

        CURLcode res = curl_easy_perform(refresh_curl);
        curl_slist_free_all(headers);
        curl_easy_cleanup(refresh_curl);

        if (res != CURLE_OK) {
            BOOST_LOG_TRIVIAL(error) << "Token refresh failed: " << curl_easy_strerror(res);
            return false;
        }

        try {
            auto j = json::parse(response);
            if (j.contains("access_token")) {
                accessToken = j["access_token"];
                int expires_in = j.value("expires_in", 3600);
                tokenExpiry = std::chrono::steady_clock::now() +
                             std::chrono::seconds(expires_in - 60); // Refresh 60s early

                BOOST_LOG_TRIVIAL(info) << "Access token refreshed, expires in " << expires_in << " seconds";

                // Save updated token to file
                saveTokens(j);
                return true;
            }
        } catch (const json::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to parse token response: " << e.what();
        }

        return false;
    }

    void saveTokens(const json& tokens) {
        std::string tokenFile = std::string(getenv("HOME")) + "/.spotify_tokens";
        std::ofstream file(tokenFile);
        if (file.is_open()) {
            // Read existing tokens to preserve refresh_token if not in response
            std::ifstream existing(tokenFile);
            json existingTokens;
            if (existing.is_open()) {
                existing >> existingTokens;
                existing.close();
            }

            json toSave = existingTokens;
            toSave.update(tokens);

            file << toSave.dump(2);
            file.close();
        }
    }

public:
    SpotifyHTTP(const std::string& access, const std::string& refresh,
                const std::string& clientId, const std::string& clientSecret, int expiresIn)
        : accessToken(access), refreshToken(refresh), clientId(clientId), clientSecret(clientSecret) {

        curl = curl_easy_init();
        if (!curl) {
            BOOST_LOG_TRIVIAL(error) << "Failed to initialize CURL easy handle!";
            BOOST_LOG_TRIVIAL(error) << "This usually means curl_global_init() was not called or failed";
            throw std::runtime_error("CURL initialization failed");
        }

        // Set token expiry (refresh 60 seconds before actual expiry)
        tokenExpiry = std::chrono::steady_clock::now() +
                     std::chrono::seconds(expiresIn - 60);
    }

    ~SpotifyHTTP() {
        if (curl) {
            curl_easy_cleanup(curl);
        }
    }

    void play() {
        std::string url = apiBase + "/me/player/play";
        makeRequest(url, "PUT");
        BOOST_LOG_TRIVIAL(info) << "Sent Play command to Spotify";
    }

    void pause() {
        std::string url = apiBase + "/me/player/pause";
        makeRequest(url, "PUT");
        BOOST_LOG_TRIVIAL(info) << "Sent Pause command to Spotify";
    }

    void next() {
        std::string url = apiBase + "/me/player/next";
        makeRequest(url, "POST");
        BOOST_LOG_TRIVIAL(info) << "Sent Next command to Spotify";
    }

    void previous() {
        std::string url = apiBase + "/me/player/previous";
        makeRequest(url, "POST");
        BOOST_LOG_TRIVIAL(info) << "Sent Previous command to Spotify";
    }

    void playContext(const std::string& contextUri) {
        std::string url = apiBase + "/me/player/play";
        json body = {{"context_uri", contextUri}};
        makeRequest(url, "PUT", body.dump());
        BOOST_LOG_TRIVIAL(info) << "Playing context: " << contextUri;
    }

    std::string getCurrentTrack() {
        std::string url = apiBase + "/me/player/currently-playing";
        std::string response = makeRequest(url);

        if (response.empty()) {
            return "";
        }

        try {
            auto j = json::parse(response);
            if (j.contains("item") && j["item"].contains("name")) {
                return j["item"]["name"];
            }
        } catch (const json::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "JSON parse error: " << e.what();
        }

        return "";
    }

    std::string getCurrentArtist() {
        std::string url = apiBase + "/me/player/currently-playing";
        std::string response = makeRequest(url);

        if (response.empty()) {
            return "";
        }

        try {
            auto j = json::parse(response);
            if (j.contains("item") && j["item"].contains("artists") &&
                j["item"]["artists"].is_array() && !j["item"]["artists"].empty()) {
                return j["item"]["artists"][0]["name"];
            }
        } catch (const json::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "JSON parse error: " << e.what();
        }

        return "";
    }

    bool isPlaying() {
        std::string url = apiBase + "/me/player/currently-playing";
        std::string response = makeRequest(url);

        if (response.empty()) {
            return false;
        }

        try {
            auto j = json::parse(response);
            if (j.contains("is_playing")) {
                return j["is_playing"];
            }
        } catch (const json::exception& e) {
            BOOST_LOG_TRIVIAL(error) << "JSON parse error: " << e.what();
        }

        return false;
    }
};

// Interface implementation for Spotify control
class SpotifyInterface : public PC2Interface {
public:
    SpotifyInterface() {
        this->address_mask = PC2Interface::address_mask_t::beoport;
    }

    void beo4_press(Beo4::keycode keycode) override {
        BOOST_LOG_TRIVIAL(debug) << "Beo4 key received in interface: 0x"
                                  << std::hex << (int)keycode;
    }
};

// Keyboard input thread for testing without Beo4 remote
void keyboardInputThread(std::shared_ptr<SpotifyHTTP> spotify,
                         std::function<void()> triggerSourceCallback,
                         volatile bool& running) {
    std::cout << "\n=== Keyboard Controls ===\n";
    std::cout << "  p - Play\n";
    std::cout << "  s - Stop/Pause\n";
    std::cout << "  n - Next track\n";
    std::cout << "  b - Previous track (back)\n";
    std::cout << "  t - Trigger source selection (test Masterlink flow)\n";
    std::cout << "  q - Quit\n";
    std::cout << "=========================\n\n";

    while (running) {
        char input;
        std::cin >> input;

        switch(input) {
            case 'p':
                std::cout << "[TEST] Play command\n";
                spotify->play();
                break;
            case 's':
                std::cout << "[TEST] Stop/Pause command\n";
                spotify->pause();
                break;
            case 'n':
                std::cout << "[TEST] Next track\n";
                spotify->next();
                break;
            case 'b':
                std::cout << "[TEST] Previous track\n";
                spotify->previous();
                break;
            case 't':
                std::cout << "[TEST] Triggering source selection (A.MEM2 = 0x7A)\n";
                triggerSourceCallback();
                break;
            case 'q':
                std::cout << "[TEST] Quit requested\n";
                keepRunning = false;
                break;
            default:
                std::cout << "[TEST] Unknown command: " << input << "\n";
                break;
        }
    }
}

int main(int argc, char** argv) {
    // Check for test mode flag
    bool testMode = false;
    for (int i = 1; i < argc; i++) {
        if (std::string(argv[i]) == "--test-mode" || std::string(argv[i]) == "-t") {
            testMode = true;
            break;
        }
    }

    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);

    // Initialize CURL globally FIRST (must be done before any CURL operations)
    CURLcode curl_init_result = curl_global_init(CURL_GLOBAL_ALL);
    if (curl_init_result != CURLE_OK) {
        BOOST_LOG_TRIVIAL(error) << "Failed to initialize CURL globally: " << curl_easy_strerror(curl_init_result);
        return 1;
    }
    BOOST_LOG_TRIVIAL(debug) << "CURL initialized successfully";

    if (testMode) {
        BOOST_LOG_TRIVIAL(info) << "PC2 Spotify Application Starting in TEST MODE (no hardware required)...";
    } else {
        BOOST_LOG_TRIVIAL(info) << "PC2 Spotify Application Starting...";
    }

    // Read Spotify tokens and client credentials from JSON files
    std::string homeDir = std::string(getenv("HOME"));
    std::string tokenFile = homeDir + "/.spotify_tokens";
    std::string clientFile = homeDir + "/.spotify_client";

    // Read tokens
    std::ifstream tokensInput(tokenFile);
    if (!tokensInput.is_open()) {
        BOOST_LOG_TRIVIAL(error) << "Failed to open token file: " << tokenFile;
        BOOST_LOG_TRIVIAL(error) << "Please run spotify_auth.py to set up Spotify authorization.";
        return 1;
    }

    json tokens;
    try {
        tokensInput >> tokens;
        tokensInput.close();
    } catch (const json::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to parse tokens: " << e.what();
        return 1;
    }

    // Read client credentials
    std::ifstream clientInput(clientFile);
    if (!clientInput.is_open()) {
        BOOST_LOG_TRIVIAL(error) << "Failed to open client file: " << clientFile;
        BOOST_LOG_TRIVIAL(error) << "Please run spotify_auth.py to set up Spotify authorization.";
        return 1;
    }

    json clientCreds;
    try {
        clientInput >> clientCreds;
        clientInput.close();
    } catch (const json::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to parse client credentials: " << e.what();
        return 1;
    }

    // Extract required fields
    std::string accessToken = tokens.value("access_token", "");
    std::string refreshToken = tokens.value("refresh_token", "");
    std::string clientId = clientCreds.value("client_id", "");
    std::string clientSecret = clientCreds.value("client_secret", "");
    int expiresIn = tokens.value("expires_in", 3600);

    if (accessToken.empty() || refreshToken.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Missing access_token or refresh_token in " << tokenFile;
        BOOST_LOG_TRIVIAL(error) << "Please run spotify_auth.py to generate tokens.";
        return 1;
    }

    if (clientId.empty() || clientSecret.empty()) {
        BOOST_LOG_TRIVIAL(error) << "Missing client_id or client_secret in " << clientFile;
        BOOST_LOG_TRIVIAL(error) << "Please run spotify_auth.py to set up credentials.";
        return 1;
    }

    BOOST_LOG_TRIVIAL(info) << "Loaded Spotify credentials and tokens";

    // Create Spotify HTTP client with refresh capability
    std::shared_ptr<SpotifyHTTP> spotify;
    try {
        spotify = std::make_shared<SpotifyHTTP>(accessToken, refreshToken,
                                                 clientId, clientSecret, expiresIn);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to create Spotify HTTP client: " << e.what();
        curl_global_cleanup();
        return 1;
    }

    // Create the custom interface
    SpotifyInterface interface;

    // Create the PC2 instance with the interface (only if not in test mode)
    PC2* pc2 = nullptr;
    if (!testMode) {
        pc2 = new PC2(&interface);
    }

    // Track update thread flag and state
    std::atomic<bool> trackUpdateRunning{true};
    std::string lastTrackTitle;
    std::atomic<uint8_t> activeSource{0};  // Track current active source
    std::atomic<uint8_t> ourNodeAddress{0xC2};  // Track our node address

    // Lambda function to handle source selection (used by both PC2 callback and test mode)
    auto handleSourceRequest = [&](uint8_t source_id, uint8_t our_node, uint8_t from_node) {
        BOOST_LOG_TRIVIAL(info) << "Source 0x" << std::hex << (int)source_id
                                << " requested via Masterlink"
                                << " (to node 0x" << (int)our_node
                                << " from node 0x" << (int)from_node << ")";

        // Check if this is our Spotify source (A.MEM2 = 0x7A)
        if (source_id == Masterlink::source::a_mem2) {
            BOOST_LOG_TRIVIAL(info) << "Starting Spotify source";

            // Save the active source and our node address for the track update thread
            activeSource.store(source_id);
            ourNodeAddress = our_node;

            if (pc2 != nullptr) {

                // 0. First, send DISTRIBUTION_REQUEST response to audio master
                BOOST_LOG_TRIVIAL(debug) << "Sending DISTRIBUTION_REQUEST response";
                DecodedTelegram::DistributionRequest dist_req(source_id);
                dist_req.src_node = our_node;
                dist_req.dest_node = 0xc1;  // Send to audio master
                pc2->beolink->send_telegram(dist_req);

                // 1. Send initial track text (12 chars for network sources)
                DecodedTelegram::TrackText12 text_msg1(source_id, "SPOTIFY");
                text_msg1.src_node = our_node;
                pc2->beolink->send_telegram(text_msg1);

                // 2. Send STATUS_INFO (broadcast to all devices)
                DecodedTelegram::StatusInfo status(source_id);
                status.src_node = our_node;
                status.dest_node = 0x83;
                pc2->beolink->send_telegram(status);

                // 3. Send TRACK_INFO_LONG (to audio master)
                DecodedTelegram::TrackInfoLong track_info_long(source_id, 1);
                track_info_long.src_node = our_node;
                track_info_long.dest_node = 0xc1;  // Audio master
                pc2->beolink->send_telegram(track_info_long);

                // 5. Send metadata telegrams
                BOOST_LOG_TRIVIAL(debug) << "Sending initial metadata telegrams";

                DecodedTelegram::Metadata track_metadata(source_id, DecodedTelegram::Metadata::metadata_field_type::track, "Spotify");
                track_metadata.src_node = our_node;
                pc2->beolink->send_telegram(track_metadata);

                DecodedTelegram::Metadata artist_metadata(source_id, DecodedTelegram::Metadata::metadata_field_type::artist, "Spotify");
                artist_metadata.src_node = our_node;
                pc2->beolink->send_telegram(artist_metadata);

                // 6. Enable audio distribution to Masterlink
                BOOST_LOG_TRIVIAL(info) << "Enabling audio distribution";
                pc2->mixer->ml_distribute(true);
            } else {
                BOOST_LOG_TRIVIAL(info) << "[TEST MODE] Skipping Masterlink telegrams (no hardware)";
            }

            // 7. Start playing liked songs collection
            // Note: The collection URI might not work for all accounts
            // Alternative: use spotify->play() to resume current playback
            BOOST_LOG_TRIVIAL(info) << "Starting playback (resuming or playing liked songs)";
            spotify->play();  // Just resume playback instead of trying to play a specific collection
        } else {
            // Different source requested - stop our distribution
            BOOST_LOG_TRIVIAL(info) << "Other source requested - stopping distribution";
            activeSource.store(0);  // Clear active source
            if (pc2 != nullptr) {
                pc2->mixer->ml_distribute(false);
            }
            spotify->pause();
        }
    };

    // Register callbacks if in normal mode (with PC2 hardware)
    if (pc2 != nullptr) {
        // Register keystroke callback for Beo4 remote control
        pc2->keystroke_callback = [&](Beo4::keycode keycode) {
            BOOST_LOG_TRIVIAL(info) << "Beo4 key pressed: 0x" << std::hex << (int)keycode;

            switch(keycode) {
                case Beo4::keycode::play:
                    BOOST_LOG_TRIVIAL(info) << "Play button pressed";
                    spotify->play();
                    break;

                case Beo4::keycode::stop:
                    BOOST_LOG_TRIVIAL(info) << "Stop button pressed";
                    spotify->pause();
                    break;

                case Beo4::keycode::arrow_right:
                    BOOST_LOG_TRIVIAL(info) << "Next track (arrow right)";
                    spotify->next();
                    break;

                case Beo4::keycode::arrow_left:
                    BOOST_LOG_TRIVIAL(info) << "Previous track (arrow left)";
                    spotify->previous();
                    break;

                case Beo4::keycode::arrow_up:
                    BOOST_LOG_TRIVIAL(info) << "Arrow up pressed";
                    // Could add volume control here if Spotify API supports it
                    break;

                case Beo4::keycode::arrow_down:
                    BOOST_LOG_TRIVIAL(info) << "Arrow down pressed";
                    // Could add volume control here if Spotify API supports it
                    break;

                default:
                    BOOST_LOG_TRIVIAL(debug) << "Unhandled keycode: 0x" << std::hex << (int)keycode;
                    break;
            }
        };

        // Register distribution request callback
        pc2->distribution_request_callback = handleSourceRequest;

        // Register STATUS_INFO callback - called when we receive a STATUS_INFO telegram
        // This tells us which source the audio master has switched to
        pc2->status_info_callback = [&](uint8_t active_source_id) {
            BOOST_LOG_TRIVIAL(info) << "STATUS_INFO received: Active source = 0x"
                                    << std::hex << (int)active_source_id;

            uint8_t our_source = activeSource.load();
            // If the audio master switched to a different source, stop distributing
            if (our_source != 0 && active_source_id != our_source) {
                BOOST_LOG_TRIVIAL(info) << "Audio master switched away from our source (0x"
                                        << std::hex << (int)our_source
                                        << ") - stopping Spotify and distribution";
                activeSource.store(0);
                pc2->mixer->ml_distribute(false);
                spotify->pause();
            }
        };

        // Register RELEASE callback - called when standby/power button is pressed
        pc2->release_callback = [&]() {
            BOOST_LOG_TRIVIAL(info) << "RELEASE/Standby received - stopping Spotify and distribution";
            activeSource.store(0);
            pc2->mixer->ml_distribute(false);
            spotify->pause();
        };

        // Open the PC2 device
        if (!pc2->open()) {
            BOOST_LOG_TRIVIAL(error) << "Failed to open PC2 device!";
            curl_global_cleanup();
            delete pc2;
            return 1;
        }

        BOOST_LOG_TRIVIAL(info) << "PC2 device opened successfully";

        // Broadcast timestamp
        pc2->beolink->broadcast_timestamp();
    } else {
        BOOST_LOG_TRIVIAL(info) << "Test mode enabled - PC2 hardware not initialized";
        BOOST_LOG_TRIVIAL(info) << "Use keyboard commands to control playback";
    }

    // Start track metadata update thread with scrolling text
    // Note: This thread needs its own SpotifyHTTP instance because CURL handles are not thread-safe
    std::shared_ptr<SpotifyHTTP> spotifyTrackThread;
    try {
        spotifyTrackThread = std::make_shared<SpotifyHTTP>(accessToken, refreshToken,
                                                             clientId, clientSecret, expiresIn);
    } catch (const std::exception& e) {
        BOOST_LOG_TRIVIAL(error) << "Failed to create Spotify HTTP client for track thread: " << e.what();
        // Continue anyway, track updates just won't work
    }

    std::thread trackUpdateThread([&, spotifyTrackThread]() {
        std::string fullText;
        size_t scrollPosition = 0;
        auto lastScrollTime = std::chrono::steady_clock::now();
        auto lastMetadataFetch = std::chrono::steady_clock::now();
        const auto scrollInterval = std::chrono::milliseconds(500);  // Scroll every 500ms
        const auto metadataInterval = std::chrono::seconds(1);       // Fetch metadata every 1 second

        while (trackUpdateRunning) {
            auto now = std::chrono::steady_clock::now();

            // Only update metadata if we're the active source AND actually playing
            if (activeSource != 0 && spotifyTrackThread) {
                // Fetch metadata from Spotify API every 1 second
                if (now - lastMetadataFetch >= metadataInterval) {
                    lastMetadataFetch = now;


                    std::string currentTrack = spotifyTrackThread->getCurrentTrack();
                    std::string artist = spotifyTrackThread->getCurrentArtist();

                    // Check if track changed
                    if (!currentTrack.empty() && currentTrack != lastTrackTitle) {
                        lastTrackTitle = currentTrack;

                        // Build full text: "Artist - Track"
                        if (!artist.empty()) {
                            fullText = artist + " - " + currentTrack;
                        } else {
                            fullText = currentTrack;
                        }

                        // Add padding for smooth scrolling loop
                        fullText += "    ";  // 4 spaces between loop

                        BOOST_LOG_TRIVIAL(info) << "Track changed: " << artist << " - " << currentTrack;
                        scrollPosition = 0;  // Reset scroll on track change
                    }
                }

                // Send scrolling text every 500ms if we have text and an active source
                if (!fullText.empty() && activeSource != 0) {
                    // Update scroll position every scrollInterval
                    if (now - lastScrollTime >= scrollInterval) {
                        lastScrollTime = now;

                        // Extract 12 characters starting at scrollPosition
                        std::string displayText;
                        if (fullText.length() <= 12) {
                            // Text fits, no need to scroll
                            displayText = fullText;
                            // Pad to 12 characters
                            while (displayText.length() < 12) {
                                displayText += " ";
                            }
                        } else {
                            // Text needs scrolling
                            for (size_t i = 0; i < 12; i++) {
                                displayText += fullText[(scrollPosition + i) % fullText.length()];
                            }

                            // Advance scroll position
                            scrollPosition = (scrollPosition + 1) % fullText.length();
                        }

                        // Send updated track text to Masterlink using saved source (if hardware available)
                        if (pc2 != nullptr) {
                            DecodedTelegram::TrackText12 text_msg(activeSource, displayText);
                            text_msg.src_node = ourNodeAddress;
                            pc2->beolink->send_telegram(text_msg);
                        } else {
                            // In test mode, just log the scrolling text
                            BOOST_LOG_TRIVIAL(debug) << "[TEST] Display: " << displayText;
                        }
                    }
                }
            }

            // Sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    BOOST_LOG_TRIVIAL(info) << "Spotify Web API ready with automatic token refresh";

    // Start keyboard input thread in test mode
    std::thread* keyboardThread = nullptr;
    if (testMode) {
        // Create a lambda that calls handleSourceRequest with test parameters
        auto triggerSource = [&]() {
            handleSourceRequest(Masterlink::source::a_mem2, 0xC2, 0xC1);
        };

        keyboardThread = new std::thread(keyboardInputThread, spotify, triggerSource, std::ref(keepRunning));
    }

    if (pc2 != nullptr) {
        BOOST_LOG_TRIVIAL(info) << "Entering event loop. Press Ctrl+C to exit.";
        // Run the event loop
        pc2->event_loop(keepRunning);
    } else {
        BOOST_LOG_TRIVIAL(info) << "Test mode: Use keyboard commands. Press 'q' to quit.";
        // In test mode, just wait for keepRunning to become false
        while (keepRunning) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    BOOST_LOG_TRIVIAL(info) << "Event loop exited. Shutting down...";

    // Stop track update thread
    trackUpdateRunning = false;
    trackUpdateThread.join();

    // Stop keyboard thread if running
    if (keyboardThread != nullptr) {
        keyboardThread->join();
        delete keyboardThread;
    }

    // Cleanup
    if (pc2 != nullptr) {
        delete pc2;
    }
    curl_global_cleanup();

    BOOST_LOG_TRIVIAL(info) << "Spotify HTTP application terminated.";

    return 0;
}
