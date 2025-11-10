#include <iostream>
#include <csignal>
#include <boost/log/trivial.hpp>
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

// Simple interface implementation - just needs to implement beo4_press
class DemoInterface : public PC2Interface {
public:
    DemoInterface() {
        // Set the address mask (audio_master, beoport, or promisc)
        this->address_mask = PC2Interface::address_mask_t::audio_master;
    }

    // This gets called by the library when a Beo4 key is received
    // We can forward it to the keystroke_callback or handle it directly
    void beo4_press(Beo4::keycode keycode) override {
        // The PC2 library already calls keystroke_callback internally,
        // so we can just log here if needed
        BOOST_LOG_TRIVIAL(debug) << "Beo4 key received in interface: 0x"
                                  << std::hex << (int)keycode;
    }
};

int main(int argc, char** argv) {
    // Set up signal handler for graceful shutdown
    signal(SIGINT, signalHandler);

    BOOST_LOG_TRIVIAL(info) << "PC2 Demo Application Starting...";

    // Create the custom interface
    DemoInterface interface;

    // Create the PC2 instance with the interface
    PC2 pc2(&interface);

    // Register keystroke callback - this is where you handle Beo4 remote keys
    pc2.keystroke_callback = [&pc2](Beo4::keycode keycode) {
        BOOST_LOG_TRIVIAL(info) << "Beo4 key pressed: 0x" << std::hex << (int)keycode;

        // Handle specific keys
        switch(keycode) {
            case Beo4::keycode::play:
                BOOST_LOG_TRIVIAL(info) << "Play button pressed!";
                // Call pc2 functions here
                break;

            case Beo4::keycode::stop:
                BOOST_LOG_TRIVIAL(info) << "Stop button pressed!";
                break;

            case Beo4::keycode::vol_up:
                BOOST_LOG_TRIVIAL(info) << "Volume up pressed!";
                break;

            case Beo4::keycode::vol_down:
                BOOST_LOG_TRIVIAL(info) << "Volume down pressed!";
                break;

            default:
                BOOST_LOG_TRIVIAL(debug) << "Unhandled keycode: 0x" << std::hex << (int)keycode;
                break;
        }
    };

    // Register source request callback - this is called when another Masterlink device
    // requests a source from this device
    pc2.source_request_callback = [&pc2](uint8_t source_id) {
        BOOST_LOG_TRIVIAL(info) << "Source 0x" << std::hex << (int)source_id
                                << " requested via Masterlink";

        // Handle the source request:
        // - Start audio distribution
        // - Load the requested source
        // - Send display data/track info

        // Example: Send track text to display
        DecodedTelegram::TrackText8 text_msg(source_id, "HELLO!");
        text_msg.src_node = 0xC1; // A_MASTER
        pc2.beolink->send_telegram(text_msg);

        // Example: Send track info with track number
        DecodedTelegram::TrackInfo track_info(source_id, 1);
        track_info.src_node = 0xC1;
        track_info.dest_node = 0x83; // Broadcast
        pc2.beolink->send_telegram(track_info);

        // Enable audio distribution
        BOOST_LOG_TRIVIAL(info) << "Starting audio distribution for source 0x"
                                << std::hex << (int)source_id;
        // pc2.mixer->ml_distribute(true);
    };

    // Open the PC2 device
    if (!pc2.open()) {
        BOOST_LOG_TRIVIAL(error) << "Failed to open PC2 device!";
        return 1;
    }

    BOOST_LOG_TRIVIAL(info) << "PC2 device opened successfully";

    // Broadcast timestamp (optional - helps synchronize B&O devices)
    pc2.beolink->broadcast_timestamp();

    BOOST_LOG_TRIVIAL(info) << "Entering event loop. Press Ctrl+C to exit.";

    // Run the event loop - this processes all PC2/Masterlink messages
    pc2.event_loop(keepRunning);

    BOOST_LOG_TRIVIAL(info) << "Event loop exited. Shutting down...";

    // Cleanup - send shutdown to all devices
    pc2.beolink->send_shutdown_all();

    BOOST_LOG_TRIVIAL(info) << "Demo application terminated.";

    return 0;
}
