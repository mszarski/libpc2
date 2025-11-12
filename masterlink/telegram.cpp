#include <boost/format.hpp>
#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include <iostream>
#include "pc2/pc2.hpp"
#include "masterlink/telegram.hpp"

MasterlinkTelegram::MasterlinkTelegram() {
    this->data = std::vector<uint8_t>();
};

namespace DecodedTelegram {
    DecodedTelegram::DecodedTelegram(): MasterlinkTelegram{} {
    };

    MasterPresent MasterPresent::reply_from_request(const MasterPresent &request) {
        MasterPresent reply;

        reply.telegram_type = MasterlinkTelegram::telegram_types::status;
        reply.dest_node = request.src_node;
        // FIXME: I should be smarter about where this comes from
        reply.src_node = request.dest_node;
        reply.payload_version = 4;

        // no idea what these bytes signify
        reply.payload = std::vector<uint8_t> {0x01, 0x01, 0x01};
        return reply;
    }

    MasterPresent::MasterPresent() {
        this->payload_type = MasterlinkTelegram::payload_types::master_present;
    }

    GotoSource::GotoSource(MasterlinkTelegram & tgram): DecodedTelegram{tgram} {
        this->payload_type = MasterlinkTelegram::payload_types::goto_source;

        if(this->telegram_type == telegram_types::request) {
            if(this->payload.size() == 7 && this->payload_version == 1) {
                this->tgram_meaning = request_source;
                this->requested_source = this->payload[1];
            }
        }
    }

    TrackInfo::TrackInfo(uint8_t source_id, uint8_t track_number) {
        this->telegram_type = telegram_types::status;
        this->payload_type = MasterlinkTelegram::payload_types::track_info;
        this->payload_version = 5;
        this->payload = { 0x02, source_id, 0x00, 0x02, 0x01, 0x00, 0x00, track_number };
    }

    TrackInfoLong::TrackInfoLong(uint8_t source_id, uint8_t track_number) {
        this->telegram_type = telegram_types::status;
        this->payload_type = MasterlinkTelegram::payload_types::track_info_long;
        this->payload_version = 6;
        this->payload = { 0x06, source_id, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, track_number };
    }

    StatusInfo::StatusInfo(MasterlinkTelegram & tgram): DecodedTelegram{tgram} {
        this->payload_type = MasterlinkTelegram::payload_types::status_info;
        this->tgram_meaning = unknown;
        this->active_source = 0;

        // Decode STATUS_INFO telegram
        if(this->telegram_type == telegram_types::status) {
            if(this->payload.size() >= 1) {
                this->tgram_meaning = status_update;
                this->active_source = this->payload[0];  // First byte is the active source
            }
        }
    }

    StatusInfo::StatusInfo(uint8_t source_id) {
        this->telegram_type = telegram_types::status;
        this->dest_node = 0x83;
        this->payload_type = MasterlinkTelegram::payload_types::status_info;
        this->payload_version = 4;
        this->payload = { \
            source_id, 0x01, 0x00, 0x00, 0x1F, 0xBE, 0x01, 0x00, \
                0x00, 0x00, 0xFF, 0x02, 0x01, 0x00, 0x03, 0x01, \
                0x01, 0x01, 0x03, 0x00, 0x02, 0x00, 0x00, 0x00, \
                0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00};
    }

    TrackText8::TrackText8(uint8_t source_id, std::string text) {
        this->telegram_type = telegram_types::info;
        this->dest_node = 0x83;
        this->src_src = source_id;
        this->payload_type = MasterlinkTelegram::payload_types::display_data;
        this->payload_version = 0;
        this->payload = { 0x03, 0x01, 0x01, 0x00, 0x00 };

        // Append 8 bytes of text (pad with spaces if shorter, truncate if longer)
        for(int i = 0; i < 8; i++) {
            if(i < text.length()) {
                this->payload.push_back(text[i]);
            } else {
                this->payload.push_back(' ');
            }
        }
    }

    TrackText12::TrackText12(uint8_t source_id, std::string text) {
        this->telegram_type = telegram_types::info;
        this->dest_node = 0x83;
        this->src_src = source_id;
        this->payload_type = MasterlinkTelegram::payload_types::display_data;
        this->payload_version = 0;
        this->payload = { 0x03, 0x01, 0x01, 0x00, 0x00 };

        // Append 12 bytes of text (pad with spaces if shorter, truncate if longer)
        for(int i = 0; i < 12; i++) {
            if(i < text.length()) {
                this->payload.push_back(text[i]);
            } else {
                this->payload.push_back(' ');
            }
        }
    }

    DistributionRequest::DistributionRequest(MasterlinkTelegram & tgram): DecodedTelegram{tgram} {
        this->payload_type = MasterlinkTelegram::payload_types::distribution_request;
        this->tgram_meaning = unknown;
        this->requested_source = 0;

        // Decode DISTRIBUTION_REQUEST telegram
        // It comes as a REQUEST telegram (0x0B) with the source in the dest_src field
        if(this->telegram_type == telegram_types::request) {
            if(this->payload.size() >= 1 && this->payload_version == 1) {
                this->tgram_meaning = request_distribution;
                this->requested_source = this->dest_src;  // Source is in dest_src field (0x7A in your example)
            }
        }
    }

    Beo4Key::Beo4Key(MasterlinkTelegram & tgram): DecodedTelegram{tgram} {
        this->payload_type = MasterlinkTelegram::payload_types::beo4_key;
        this->tgram_meaning = unknown;
        this->source = 0;
        this->keycode = 0;

        // Decode BEO4_KEY telegram
        // It comes as a COMMAND telegram (0x0A) with payload containing source and keycode
        if(this->telegram_type == telegram_types::command) {
            if(this->payload.size() >= 2 && this->payload_version == 1) {
                this->tgram_meaning = key_press;
                this->source = this->payload[0];   // Source (0x7A in your examples)
                this->keycode = this->payload[1];  // Keycode (0x1E for UP, 0x1F for DOWN, etc.)
            }
        }
    }

    DistributionRequest::DistributionRequest(uint8_t source_id) {
        this->telegram_type = telegram_types::status;
        this->payload_type = MasterlinkTelegram::payload_types::distribution_request;
        this->src_src = source_id;
        this->payload_version = 8;
        this->payload = { 0x01 };
    }

    AudioBus::AudioBus(MasterlinkTelegram & tgram): DecodedTelegram{tgram} {
        this->payload_type = MasterlinkTelegram::payload_types::audio_bus;
        this->tgram_meaning = unknown;
        this->active_source = 0;

        if(this->telegram_type == telegram_types::request) {
            if(!this->payload.size() && (this->payload_version == 1)) {
                this->tgram_meaning = request_status;
            }
        } else if (this->telegram_type == telegram_types::status) {
            if(this->payload_version == 6) {
                this->tgram_meaning = status_distributing;
                // Extract the active source from payload byte 3
                if(this->payload.size() >= 4) {
                    this->active_source = this->payload[3];
                }
            } else if(!this->payload.size() && (this->payload_version == 4)) {
                this->tgram_meaning = status_not_distributing;
            }
        }
    }
    Metadata::Metadata(MasterlinkTelegram & tgram): DecodedTelegram{tgram} {
        this->key = metadata_field_type_label[tgram.payload[0]];
        this->value = std::string(tgram.payload.begin() + 14, tgram.payload.end());
        // FIXME: Is this an appropriate constant?
        this->src = Masterlink::source::a_mem2;
    }

    DecodedTelegram *DecodedTelegramFactory::make(MasterlinkTelegram & tgram) {
        switch (tgram.payload_type) {
            case MasterlinkTelegram::payload_types::metadata:
                return new Metadata(tgram);
            case MasterlinkTelegram::payload_types::display_data:
                return new DisplayData(tgram);
            case MasterlinkTelegram::payload_types::audio_bus:
                return new AudioBus(tgram);
            case MasterlinkTelegram::payload_types::beo4_key:
                return new Beo4Key(tgram);
            case MasterlinkTelegram::payload_types::goto_source:
                return new GotoSource(tgram);
            case MasterlinkTelegram::payload_types::status_info:
                return new StatusInfo(tgram);
            case MasterlinkTelegram::payload_types::track_info_long:
                return new TrackInfoLong(tgram);
            case MasterlinkTelegram::payload_types::distribution_request:
                return new DistributionRequest(tgram);
            case MasterlinkTelegram::payload_types::master_present:
                return new MasterPresent(tgram);
            default:
                return new UnknownTelegram(tgram);
        }
    }
}

std::ostream& operator <<(std::ostream& outputStream, DecodedTelegram::DecodedTelegram& m) {
    m.debug_repr(outputStream);
    return outputStream;
};

uint8_t MasterlinkTelegram::checksum(std::vector<uint8_t> data) {
    uint8_t checksum;

    for(auto byte: data) {
        checksum += byte;
    }

    return checksum;
};

PC2Message MasterlinkTelegram::serialize() {
    this->data = std::vector<uint8_t>();
    this->data.push_back(this->dest_node);
    this->data.push_back(this->src_node);
    this->data.push_back(0x01); // SOT
    this->data.push_back(this->telegram_type);
    this->data.push_back(this->dest_src);
    this->data.push_back(this->src_src);
    this->data.push_back(0x00); // Spare
    this->data.push_back(this->payload_type);
    this->data.push_back((uint8_t) this->payload.size());
    this->payload_size = this->payload.size();
    this->data.push_back((uint8_t) this->payload_version);
    this->data.insert(this->data.end(), this->payload.begin(), this->payload.end());
    this->data.push_back(checksum(this->data));
    this->data.push_back(0x00); // EOT
    return PC2Message(this->data);
}

MasterlinkTelegram::MasterlinkTelegram(const PC2Message & tgram) {
    assert(tgram[0] == 0x60); // A PC2 telegram always starts with 0x60
    //assert(tgram[2] == 0x00); // A Masterlink telegram always starts with 0x00

    std::vector<uint8_t>::const_iterator first = tgram.begin() + 2;
    std::vector<uint8_t>::const_iterator last = tgram.end() - 1;
    this->data = std::vector<uint8_t>(first, last);
    this->dest_node = this->data[1];
    this->src_node = this->data[2];
    this->telegram_type = (telegram_types)this->data[4];
    this->dest_src = this->data[5];  // Fixed: was reading data[7], should be data[5]
    this->src_src = this->data[6];
    this->payload_type = (payload_types)this->data[8];
    this->payload_size = this->data[9];
    this->payload_version = this->data[10];

    first = tgram.begin() + 13;
    this->payload = std::vector<uint8_t>(first, first + payload_size);
};

