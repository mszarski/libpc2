#include <iostream>
#include <csignal>
#include <memory>
#include <chrono>
#include <thread>
#include <boost/log/trivial.hpp>
#include <sdbus-c++/sdbus-c++.h>
#include "pc2/pc2.hpp"
#include "pc2/pc2interface.hpp"
#include "pc2/beo4.hpp"
#include "masterlink/telegram.hpp"

// Global flag for graceful shutdown
volatile bool keepRunning = true;

void signalHandler(int signum) {
    std::cout << "\nInterrupt signal (" << signum << ") received. Shutting down...\n";
    keepRunning = false;
}

// Spotify D-Bus interface wrapper
class SpotifyDBus {
private:
    std::unique_ptr<sdbus::IConnection> connection;
    std::unique_ptr<sdbus::IProxy> mprisProxy;
    std::unique_ptr<sdbus::IProxy> controlsProxy;
    std::string serviceName;
    bool isConnected = false;

    void setupProxies() {
        try {
            // Get spotifyd PID
            auto result = connection->createMethodCall(
                "org.freedesktop.DBus",
                "/org/freedesktop/DBus",
                "org.freedesktop.DBus",
                "ListNames"
            );

            std::vector<std::string> names;
            result.send().readReply(names);

            // Find spotifyd service name
            for (const auto& name : names) {
                if (name.find("org.mpris.MediaPlayer2.spotifyd") == 0) {
                    serviceName = name;
                    break;
                }
            }

            if (serviceName.empty()) {
                BOOST_LOG_TRIVIAL(warning) << "Spotifyd not found on D-Bus";
                isConnected = false;
                return;
            }

            BOOST_LOG_TRIVIAL(info) << "Found spotifyd service: " << serviceName;

            // Create proxies
            mprisProxy = sdbus::createProxy(*connection, serviceName, "/org/mpris/MediaPlayer2");

            // Also create proxy for rs.spotifyd.Controls (using modified service name)
            std::string controlsService = serviceName;
            size_t pos = controlsService.find("org.mpris.MediaPlayer2.spotifyd");
            if (pos != std::string::npos) {
                controlsService.replace(pos, 30, "rs.spotifyd");
            }
            controlsProxy = sdbus::createProxy(*connection, controlsService, "/rs/spotifyd/Controls");

            isConnected = true;
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "D-Bus error setting up proxies: " << e.what();
            isConnected = false;
        }
    }

public:
    SpotifyDBus() {
        connection = sdbus::createSessionBusConnection();
        setupProxies();
    }

    bool reconnect() {
        setupProxies();
        return isConnected;
    }

    void play() {
        if (!isConnected && !reconnect()) return;
        try {
            mprisProxy->callMethod("Play").onInterface("org.mpris.MediaPlayer2.Player");
            BOOST_LOG_TRIVIAL(info) << "Sent Play command to spotifyd";
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to play: " << e.what();
        }
    }

    void pause() {
        if (!isConnected && !reconnect()) return;
        try {
            mprisProxy->callMethod("Pause").onInterface("org.mpris.MediaPlayer2.Player");
            BOOST_LOG_TRIVIAL(info) << "Sent Pause command to spotifyd";
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to pause: " << e.what();
        }
    }

    void playPause() {
        if (!isConnected && !reconnect()) return;
        try {
            mprisProxy->callMethod("PlayPause").onInterface("org.mpris.MediaPlayer2.Player");
            BOOST_LOG_TRIVIAL(info) << "Sent PlayPause command to spotifyd";
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to play/pause: " << e.what();
        }
    }

    void next() {
        if (!isConnected && !reconnect()) return;
        try {
            mprisProxy->callMethod("Next").onInterface("org.mpris.MediaPlayer2.Player");
            BOOST_LOG_TRIVIAL(info) << "Sent Next command to spotifyd";
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to skip next: " << e.what();
        }
    }

    void previous() {
        if (!isConnected && !reconnect()) return;
        try {
            mprisProxy->callMethod("Previous").onInterface("org.mpris.MediaPlayer2.Player");
            BOOST_LOG_TRIVIAL(info) << "Sent Previous command to spotifyd";
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to skip previous: " << e.what();
        }
    }

    void openUri(const std::string& uri) {
        if (!isConnected && !reconnect()) return;
        try {
            mprisProxy->callMethod("OpenUri")
                .onInterface("org.mpris.MediaPlayer2.Player")
                .withArguments(uri);
            BOOST_LOG_TRIVIAL(info) << "Opening URI: " << uri;
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to open URI: " << e.what();
        }
    }

    void transferPlayback() {
        if (!isConnected && !reconnect()) return;
        try {
            controlsProxy->callMethod("TransferPlayback")
                .onInterface("rs.spotifyd.Controls");
            BOOST_LOG_TRIVIAL(info) << "Transferred playback to spotifyd";
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to transfer playback: " << e.what();
        }
    }

    std::string getTrackTitle() {
        if (!isConnected && !reconnect()) return "";
        try {
            auto reply = mprisProxy->getProperty("Metadata")
                .onInterface("org.mpris.MediaPlayer2.Player");

            std::map<std::string, sdbus::Variant> metadata;
            reply >> metadata;

            if (metadata.count("xesam:title")) {
                return metadata["xesam:title"].get<std::string>();
            }
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to get metadata: " << e.what();
        }
        return "";
    }

    std::string getArtist() {
        if (!isConnected && !reconnect()) return "";
        try {
            auto reply = mprisProxy->getProperty("Metadata")
                .onInterface("org.mpris.MediaPlayer2.Player");

            std::map<std::string, sdbus::Variant> metadata;
            reply >> metadata;

            if (metadata.count("xesam:artist")) {
                auto artists = metadata["xesam:artist"].get<std::vector<std::string>>();
                if (!artists.empty()) {
                    return artists[0];
                }
            }
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to get artist: " << e.what();
        }
        return "";
    }

    std::string getPlaybackStatus() {
        if (!isConnected && !reconnect()) return "Stopped";
        try {
            auto reply = mprisProxy->getProperty("PlaybackStatus")
                .onInterface("org.mpris.MediaPlayer2.Player");

            std::string status;
            reply >> status;
            return status;
        } catch (const sdbus::Error& e) {
            BOOST_LOG_TRIVIAL(error) << "Failed to get playback status: " << e.what();
        }
        return "Stopped";
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

int main(int argc, char** argv) {
    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);

    BOOST_LOG_TRIVIAL(info) << "PC2 Spotify Application Starting...";

    // Create Spotify D-Bus interface
    auto spotify = std::make_shared<SpotifyDBus>();

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
            case Beo4::keycode::pause:
                BOOST_LOG_TRIVIAL(info) << "Pause/Stop button pressed";
                spotify->pause();
                break;

            case Beo4::keycode::wind:
            case Beo4::keycode::step_up:
                BOOST_LOG_TRIVIAL(info) << "Next track button pressed";
                spotify->next();
                break;

            case Beo4::keycode::rewind:
            case Beo4::keycode::step_down:
                BOOST_LOG_TRIVIAL(info) << "Previous track button pressed";
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

            // 7. Start playing liked songs playlist
            BOOST_LOG_TRIVIAL(info) << "Starting liked songs playlist";
            spotify->transferPlayback();
            // Open the "Liked Songs" collection
            spotify->openUri("spotify:user:spotify:collection");
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            spotify->play();
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
        const auto scrollInterval = std::chrono::milliseconds(500);  // Scroll every 500ms

        while (trackUpdateRunning) {
            // Only update metadata if we're the active source
            if (activeSource != 0) {
                std::string currentTrack = spotify->getTrackTitle();
                std::string artist = spotify->getArtist();

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

                // Send scrolling text if we have text and an active source
                if (!fullText.empty() && activeSource != 0) {
                    auto now = std::chrono::steady_clock::now();

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
    BOOST_LOG_TRIVIAL(info) << "Make sure spotifyd is running with --use-mpris flag!";

    // Run the event loop
    pc2.event_loop(keepRunning);

    BOOST_LOG_TRIVIAL(info) << "Event loop exited. Shutting down...";

    // Stop track update thread
    trackUpdateRunning = false;
    trackUpdateThread.join();

    // Cleanup
    pc2.beolink->send_shutdown_all();

    BOOST_LOG_TRIVIAL(info) << "Spotify application terminated.";

    return 0;
}
