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
        this->address_mask = PC2Interface::address_mask_t::beoport;
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

    if (testMode) {
        BOOST_LOG_TRIVIAL(info) << "PC2 Demo Application Starting in TEST MODE (hardware connected, no telegrams sent)...";
    } else {
        BOOST_LOG_TRIVIAL(info) << "PC2 Demo Application Starting...";
    }

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
    // Parameters: source_id, our_node_address, requesting_node_address
    pc2.source_request_callback = [&pc2, testMode](uint8_t source_id, uint8_t our_node, uint8_t from_node) {
        BOOST_LOG_TRIVIAL(info) << "Source 0x" << std::hex << (int)source_id
                                << " requested via Masterlink"
                                << " (to node 0x" << (int)our_node
                                << " from node 0x" << (int)from_node << ")";

        // Check if this is a source we handle (e.g., A.MEM2 = 0x7A)
        if (source_id == Masterlink::source::a_mem2) {
            BOOST_LOG_TRIVIAL(info) << "Starting N.MUSIC source";

            if (testMode) {
                // In test mode, just log what we would send
                BOOST_LOG_TRIVIAL(info) << "[TEST] Would send DistributionRequest (src_node=0x"
                                        << std::hex << (int)our_node
                                        << ", dest_node=0x" << (int)from_node << ")";
                BOOST_LOG_TRIVIAL(info) << "[TEST] Would send TrackText8: 'N.MUSIC'";
                BOOST_LOG_TRIVIAL(info) << "[TEST] Would send StatusInfo";
                BOOST_LOG_TRIVIAL(info) << "[TEST] Would send TrackInfo (track 1)";
                BOOST_LOG_TRIVIAL(info) << "[TEST] Would send TrackText8: 'N.MUSIC' (again)";
                BOOST_LOG_TRIVIAL(info) << "[TEST] Would enable audio distribution";
            } else {
                // Normal mode - actually send telegrams
                // 1. Send distribution request (announces we're starting distribution)
                DecodedTelegram::DistributionRequest dist_req(source_id);
                dist_req.src_node = our_node;
                dist_req.dest_node = from_node; // Send to Audio Master (requester)
                pc2.beolink->send_telegram(dist_req);

                // 2. Send track text to display (first time)
                DecodedTelegram::TrackText8 text_msg1(source_id, "HELLO");
                text_msg1.src_node = our_node;
                pc2.beolink->send_telegram(text_msg1);

                // 3. Send status info
                DecodedTelegram::StatusInfo status(source_id);
                status.src_node = our_node;
                pc2.beolink->send_telegram(status);

                // 4. Send track info with track number
                DecodedTelegram::TrackInfo track_info(source_id, 1);
                track_info.src_node = our_node;
                track_info.dest_node = 0x83; // Broadcast to all
                pc2.beolink->send_telegram(track_info);

                // 5. Send track text to display (second time - for reliability)
                DecodedTelegram::TrackText8 text_msg2(source_id, "HELLO");
                text_msg2.src_node = our_node;
                pc2.beolink->send_telegram(text_msg2);

                // 6. Enable audio distribution to Masterlink
                BOOST_LOG_TRIVIAL(info) << "Enabling audio distribution";
                pc2.mixer->ml_distribute(true);
            }
        } else {
            // Different source requested - stop our distribution
            BOOST_LOG_TRIVIAL(info) << "Other source requested - stopping distribution";
            if (testMode) {
                BOOST_LOG_TRIVIAL(info) << "[TEST] Would disable audio distribution";
            } else {
                pc2.mixer->ml_distribute(false);
            }
        }
    };

    // Open the PC2 device
    if (!pc2.open()) {
        BOOST_LOG_TRIVIAL(error) << "Failed to open PC2 device!";
        return 1;
    }

    BOOST_LOG_TRIVIAL(info) << "PC2 device opened successfully";

    // Broadcast timestamp (optional - helps synchronize B&O devices)
    if (testMode) {
        BOOST_LOG_TRIVIAL(info) << "[TEST] Would broadcast timestamp";
    } else {
        pc2.beolink->broadcast_timestamp();
    }

    if (testMode) {
        BOOST_LOG_TRIVIAL(info) << "TEST MODE: PC2 hardware connected, Beo4 input working, but NO telegrams will be sent";
        BOOST_LOG_TRIVIAL(info) << "Press buttons on Beo4 remote or request source on Masterlink to see logs";
    }
    BOOST_LOG_TRIVIAL(info) << "Entering event loop. Press Ctrl+C to exit.";

    // Run the event loop - this processes all PC2/Masterlink messages
    pc2.event_loop(keepRunning);

    BOOST_LOG_TRIVIAL(info) << "Event loop exited. Shutting down...";

    // Cleanup - send shutdown to all devices
    if (testMode) {
        BOOST_LOG_TRIVIAL(info) << "[TEST] Would send shutdown to all devices";
    } else {
        pc2.beolink->send_shutdown_all();
    }

    BOOST_LOG_TRIVIAL(info) << "Demo application terminated.";

    return 0;
}
