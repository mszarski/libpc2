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

// Spotify HTTP API client using libcurl
class SpotifyHTTP {
private:
    CURL* curl;
    std::string accessToken;
    const std::string apiBase = "https://api.spotify.com/v1";

    std::string makeRequest(const std::string& url, const std::string& method = "GET",
                           const std::string& body = "") {
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
        curl_slist_free_all(headers);

        if (res != CURLE_OK) {
            BOOST_LOG_TRIVIAL(error) << "CURL request failed: " << curl_easy_strerror(res);
            return "";
        }

        return response;
    }

public:
    SpotifyHTTP(const std::string& token) : accessToken(token) {
        curl = curl_easy_init();
        if (!curl) {
            BOOST_LOG_TRIVIAL(error) << "Failed to initialize CURL";
        }
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

// Read Spotify access token from file
std::string readTokenFromFile(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        BOOST_LOG_TRIVIAL(error) << "Failed to open token file: " << filename;
        return "";
    }

    std::string token;
    std::getline(file, token);
    file.close();

    // Trim whitespace
    token.erase(0, token.find_first_not_of(" \t\n\r"));
    token.erase(token.find_last_not_of(" \t\n\r") + 1);

    return token;
}

int main(int argc, char** argv) {
    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);

    BOOST_LOG_TRIVIAL(info) << "PC2 Spotify HTTP Application Starting...";

    // Read Spotify access token from file
    std::string tokenFile = std::string(getenv("HOME")) + "/.spotify_token";
    std::string accessToken = readTokenFromFile(tokenFile);

    if (accessToken.empty()) {
        BOOST_LOG_TRIVIAL(error) << "No Spotify access token found!";
        BOOST_LOG_TRIVIAL(error) << "Please create " << tokenFile << " with your Spotify access token";
        BOOST_LOG_TRIVIAL(error) << "Visit https://developer.spotify.com/console/get-users-currently-playing-track/";
        BOOST_LOG_TRIVIAL(error) << "to generate a token with the required scopes.";
        return 1;
    }

    // Initialize CURL globally
    curl_global_init(CURL_GLOBAL_DEFAULT);

    // Create Spotify HTTP client
    auto spotify = std::make_shared<SpotifyHTTP>(accessToken);

    // Create the custom interface
    SpotifyInterface interface;

    // Create the PC2 instance with the interface
    PC2 pc2(&interface);

    // Track update thread flag and state
    std::atomic<bool> trackUpdateRunning{true};
    std::string lastTrackTitle;
    std::atomic<uint8_t> activeSource{0};  // Track current active source
    std::atomic<uint8_t> ourNodeAddress{0xC2};  // Track our node address

    // Register keystroke callback for Beo4 remote control
    pc2.keystroke_callback = [&](Beo4::keycode keycode) {
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

            default:
                BOOST_LOG_TRIVIAL(debug) << "Unhandled keycode: 0x" << std::hex << (int)keycode;
                break;
        }
    };

    // Register source request callback
    pc2.source_request_callback = [&](uint8_t source_id, uint8_t our_node, uint8_t from_node) {
        BOOST_LOG_TRIVIAL(info) << "Source 0x" << std::hex << (int)source_id
                                << " requested via Masterlink"
                                << " (to node 0x" << (int)our_node
                                << " from node 0x" << (int)from_node << ")";

        // Check if this is our Spotify source (A.MEM2 = 0x7A)
        if (source_id == Masterlink::source::a_mem2) {
            BOOST_LOG_TRIVIAL(info) << "Starting Spotify source";

            // Save the active source and our node address for the track update thread
            activeSource = source_id;
            ourNodeAddress = our_node;

            // 1. Send distribution request
            DecodedTelegram::DistributionRequest dist_req(source_id);
            dist_req.src_node = our_node;
            dist_req.dest_node = from_node;
            pc2.beolink->send_telegram(dist_req);

            // 2. Send initial track text
            DecodedTelegram::TrackText8 text_msg1(source_id, "SPOTIFY");
            text_msg1.src_node = our_node;
            pc2.beolink->send_telegram(text_msg1);

            // 3. Send status info
            DecodedTelegram::StatusInfo status(source_id);
            status.src_node = our_node;
            pc2.beolink->send_telegram(status);

            // 4. Send track info
            DecodedTelegram::TrackInfo track_info(source_id, 1);
            track_info.src_node = our_node;
            track_info.dest_node = 0x83;
            pc2.beolink->send_telegram(track_info);

            // 5. Send track text again for reliability
            DecodedTelegram::TrackText8 text_msg2(source_id, "SPOTIFY");
            text_msg2.src_node = our_node;
            pc2.beolink->send_telegram(text_msg2);

            // 6. Enable audio distribution to Masterlink
            BOOST_LOG_TRIVIAL(info) << "Enabling audio distribution";
            pc2.mixer->ml_distribute(true);

            // 7. Start playing liked songs collection
            BOOST_LOG_TRIVIAL(info) << "Starting liked songs collection";
            spotify->playContext("spotify:user:spotify:collection");
        } else {
            // Different source requested - stop our distribution
            BOOST_LOG_TRIVIAL(info) << "Other source requested - stopping distribution";
            activeSource = 0;  // Clear active source
            pc2.mixer->ml_distribute(false);
            spotify->pause();
        }
    };

    // Open the PC2 device
    if (!pc2.open()) {
        BOOST_LOG_TRIVIAL(error) << "Failed to open PC2 device!";
        curl_global_cleanup();
        return 1;
    }

    BOOST_LOG_TRIVIAL(info) << "PC2 device opened successfully";

    // Broadcast timestamp
    pc2.beolink->broadcast_timestamp();

    // Start track metadata update thread with scrolling text
    std::thread trackUpdateThread([&]() {
        std::string fullText;
        size_t scrollPosition = 0;
        auto lastScrollTime = std::chrono::steady_clock::now();
        auto lastMetadataFetch = std::chrono::steady_clock::now();
        const auto scrollInterval = std::chrono::milliseconds(500);  // Scroll every 500ms
        const auto metadataInterval = std::chrono::seconds(1);       // Fetch metadata every 1 second

        while (trackUpdateRunning) {
            auto now = std::chrono::steady_clock::now();

            // Only update metadata if we're the active source
            if (activeSource != 0) {
                // Fetch metadata from Spotify API every 1 second
                if (now - lastMetadataFetch >= metadataInterval) {
                    lastMetadataFetch = now;

                    std::string currentTrack = spotify->getCurrentTrack();
                    std::string artist = spotify->getCurrentArtist();

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

                        // Extract 8 characters starting at scrollPosition
                        std::string displayText;
                        if (fullText.length() <= 8) {
                            // Text fits, no need to scroll
                            displayText = fullText;
                            // Pad to 8 characters
                            while (displayText.length() < 8) {
                                displayText += " ";
                            }
                        } else {
                            // Text needs scrolling
                            for (size_t i = 0; i < 8; i++) {
                                displayText += fullText[(scrollPosition + i) % fullText.length()];
                            }

                            // Advance scroll position
                            scrollPosition = (scrollPosition + 1) % fullText.length();
                        }

                        // Send updated track text to Masterlink using saved source
                        DecodedTelegram::TrackText8 text_msg(activeSource, displayText);
                        text_msg.src_node = ourNodeAddress;
                        pc2.beolink->send_telegram(text_msg);
                    }
                }
            }

            // Sleep briefly to avoid busy-waiting
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });

    BOOST_LOG_TRIVIAL(info) << "Entering event loop. Press Ctrl+C to exit.";
    BOOST_LOG_TRIVIAL(info) << "Using Spotify Web API with access token from " << tokenFile;

    // Run the event loop
    pc2.event_loop(keepRunning);

    BOOST_LOG_TRIVIAL(info) << "Event loop exited. Shutting down...";

    // Stop track update thread
    trackUpdateRunning = false;
    trackUpdateThread.join();

    // Cleanup
    pc2.beolink->send_shutdown_all();
    curl_global_cleanup();

    BOOST_LOG_TRIVIAL(info) << "Spotify HTTP application terminated.";

    return 0;
}
