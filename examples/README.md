# PC2 Demo Application

This is an example application demonstrating how to use the libpc2 library.

## Building

From the project root directory:

```bash
mkdir -p build
cd build
cmake ..
make demo_app
```

The executable will be created at `build/demo_app`.

## Running

```bash
./build/demo_app
```

Press `Ctrl+C` to exit gracefully.

## What it does

The demo application:

1. **Initializes the PC2 interface** as an Audio Master on the Masterlink bus
2. **Registers callbacks** for:
   - **Beo4 remote control keypresses** (`keystroke_callback`) - handles buttons pressed on B&O remotes
   - **Source requests** (`source_request_callback`) - handles when another device requests a source from this device
3. **Enters an event loop** that processes all PC2/Masterlink messages
4. **Responds to events** by sending appropriate telegrams (track info, display text, etc.)

## Customizing

### Handling Remote Control Keys

Edit the `keystroke_callback` lambda to handle different Beo4 keys:

```cpp
pc2.keystroke_callback = [&pc2](Beo4::keycode keycode) {
    switch(keycode) {
        case Beo4::keycode::play:
            // Start playback
            break;
        case Beo4::keycode::stop:
            // Stop playback
            break;
        // Add more cases...
    }
};
```

Available keycodes are defined in `pc2/beo4.hpp`.

### Handling Source Requests

Edit the `source_request_callback` lambda to respond when a source is requested:

```cpp
pc2.source_request_callback = [&pc2](uint8_t source_id) {
    // Send display text
    DecodedTelegram::TrackText8 text_msg(source_id, "PLAYING");
    text_msg.src_node = 0xC1; // A_MASTER
    pc2.beolink->send_telegram(text_msg);

    // Send track info
    DecodedTelegram::TrackInfo track_info(source_id, track_number);
    track_info.src_node = 0xC1;
    track_info.dest_node = 0x83; // Broadcast
    pc2.beolink->send_telegram(track_info);

    // Start audio distribution
    // pc2.mixer->ml_distribute(true);
};
```

### Sending Telegrams

You can send various telegrams using the new classes:

```cpp
// Send 8-character display text
DecodedTelegram::TrackText8 text(source_id, "HELLO!");
text.src_node = 0xC1;
pc2.beolink->send_telegram(text);

// Send track info with track number
DecodedTelegram::TrackInfo track(source_id, track_number);
track.src_node = 0xC1;
track.dest_node = 0x83; // Broadcast to all
pc2.beolink->send_telegram(track);

// Send status info
DecodedTelegram::StatusInfo status(source_id);
status.src_node = 0xC1;
pc2.beolink->send_telegram(status);
```

## Interface Modes

The PC2 interface can operate in different modes (set in the constructor):

- `audio_master` - Acts as an audio master device (BeoSound, etc.)
- `beoport` - Acts as a BeoPort device
- `promisc` - Promiscuous mode (receives all Masterlink traffic)

Change this in `demo_app.cpp`:

```cpp
this->address_mask = PC2Interface::address_mask_t::promisc;
```

## Notes

- The device needs USB access to the PC2 interface hardware
- Make sure you have permissions to access USB devices
