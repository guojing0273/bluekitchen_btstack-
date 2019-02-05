/*
 * Copyright (C) 2019 BlueKitchen GmbH
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the copyright holders nor the names of
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 * 4. Any redistribution, use, or modification is done solely for
 *    personal benefit and not for any commercial purpose or for
 *    monetary gain.
 *
 * THIS SOFTWARE IS PROVIDED BY BLUEKITCHEN GMBH AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL MATTHIAS
 * RINGWALD OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Please inquire about commercial licensing options at 
 * contact@bluekitchen-gmbh.com
 *
 */

#define __BTSTACK_FILE__ "headset_demo.c"

/*
 * headset_demo.c
 */

// *****************************************************************************
/* EXAMPLE_START(headset_demo): Receive audio stream and control its playback.
 *
 * @text The HFP Hands-Free protocol is used to receive 
 * an output from a remote HFP audio gateway (AG), and, 
 * if HAVE_BTSTACK_STDIN is defined, how to control the HFP AG. 
 *
 * @text This A2DP Sink example demonstrates how to use the A2DP Sink service to 
 * receive an audio data stream from a remote A2DP Source device. In addition,
 * the AVRCP Controller is used to get information on currently played media, 
 * such are title, artist and album, as well as to control the playback, 
 * i.e. to play, stop, repeat, etc.
 *
 * @test To test with a remote device, e.g. a mobile phone,
 * pair from the remote device with the demo, then start playing music on the remote device.
 * Alternatively, set the device_addr_string to the Bluetooth address of your 
 * remote device in the code, and call connect from the UI.
 * 
 * @test To control the playback, tap SPACE on the console to show the available 
 * AVRCP commands.
 */
// *****************************************************************************

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack.h"
#include "headset.h"
#include "classic/goep_client.h"
#include "classic/pbap_client.h"
#include "sco_demo_util.h"
#include "btstack_ring_buffer.h"
#include "btstack_tlv.h"

#ifdef HAVE_BTSTACK_STDIN
#include "btstack_stdin.h"
#endif

#ifdef HAVE_POSIX_FILE_IO
#include "wav_util.h"
#define STORE_SBC_TO_SBC_FILE 
#define STORE_SBC_TO_WAV_FILE 
#endif

#define GAP_TEST_LEGACY_PAIRING
#define ENABLE_A2DP
#define ENABLE_HFP

#define AVRCP_BROWSING_ENABLED 0

// define headset visibility
#define HEADSET_CONNECTABLE_WHEN_NOT_CONNECTED  1
#define HEADSET_DISCOVERABLE_WHEN_NOT_CONNECTED 1

#define HEADSET_AUTO_CONNECT_INTERVAL_MS 10000

// value in 0.625 ms units (8000 = 5 seconds)
#define LINK_SUPERVISION_TIMEOUT 8000

#define LAST_CONNECTED_DEVICE_TAG 0x41414141

static const char * headset_states[] = {
    "BTSTACK_HEADSET_IDLE",
    "BTSTACK_HEADSET_W4_CONNECTION_COMPLETE",
    "BTSTACK_HEADSET_W4_TIMER",
    "BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION",
    "BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER",
    "BTSTACK_HEADSET_INCOMING_AUTHENTICATION_REJECTED",
    "BTSTACK_HEADSET_CONNECTED",
    "BTSTACK_HEADSET_W4_LINK_SUPERVSION_TIMEOUT_UPDATE",
    "BTSTACK_HEADSET_LINK_SUPERVSION_TIMEOUT_UPDATE",
    "BTSTACK_HEADSET_W4_AUTHENTICATION",
    "BTSTACK_HEADSET_AUTHENTICATION_DONE",
    "BTSTACK_HEADSET_DONE",
    "BTSTACK_HEADSET_W4_DISCONNECT",
};

typedef enum {
    BTSTACK_HEADSET_IDLE = 0,
    BTSTACK_HEADSET_W4_CONNECTION_COMPLETE,
    BTSTACK_HEADSET_W4_TIMER,
    BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION,
    BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER,
    BTSTACK_HEADSET_INCOMING_AUTHENTICATION_REJECTED,
    BTSTACK_HEADSET_CONNECTED,
    BTSTACK_HEADSET_W4_LINK_SUPERVSION_TIMEOUT_UPDATE,
    BTSTACK_HEADSET_LINK_SUPERVSION_TIMEOUT_UPDATE,
    BTSTACK_HEADSET_W4_AUTHENTICATION,
    BTSTACK_HEADSET_AUTHENTICATION_DONE,
    BTSTACK_HEADSET_DONE,
    BTSTACK_HEADSET_W4_DISCONNECT,
} btstack_headset_state_t;

typedef enum {
    BTSTACK_HEADSET_RECONNECT_NOT_INITIALIZED = 0,
    BTSTACK_HEADSET_RECONNECT_LAST_USED_DEVICE,
    BTSTACK_HEADSET_RECONNECT_LINK_KEY_LIST_NEXT
} btstack_headset_reconnect_state_t;

typedef struct {
    bd_addr_t remote_device_addr;
    uint8_t   remote_addr_valid;

    hci_con_handle_t con_handle;
    btstack_headset_state_t state;

    // GAP connection: define visibility of headset interface
    uint8_t gap_headset_connectable;
    uint8_t gap_headset_discoverable;
    btstack_timer_source_t headset_auto_connect_timer;

    // connect first to last used device
    bd_addr_t last_connected_device;
    uint8_t   last_connected_device_valid;

    // if last used device is unavailable, go through list of Bluetooth addresses stored with link keys
    // store last used address permanently in TLV storage
    btstack_link_key_iterator_t link_key_iterator;
    btstack_headset_reconnect_state_t reconnect_state;
    
    // flags
    uint8_t connect;
    uint8_t disconnect;
    uint8_t pairing_mode_enabled;
} headset_connection_t;

static headset_connection_t headset;
static const btstack_tlv_t * btstack_tlv_impl;
static void                * btstack_tlv_context;

static void headset_run(void);

// --

#define NUM_CHANNELS 2
#define BYTES_PER_FRAME     (2*NUM_CHANNELS)
#define MAX_SBC_FRAME_SIZE 120

// SBC Decoder for WAV file or live playback
static btstack_sbc_decoder_state_t sbc_decoder_state;
static btstack_sbc_mode_t sbc_mode = SBC_MODE_STANDARD;

// ring buffer for SBC Frames
// below 30: add samples, 30-40: fine, above 40: drop samples
#define OPTIMAL_FRAMES_MIN 30
#define OPTIMAL_FRAMES_MAX 40
#define ADDITIONAL_FRAMES  10
static uint8_t sbc_frame_storage[(OPTIMAL_FRAMES_MAX + ADDITIONAL_FRAMES) * MAX_SBC_FRAME_SIZE];
static btstack_ring_buffer_t sbc_frame_ring_buffer;
static unsigned int sbc_frame_size;
static int sbc_samples_fix;

// rest buffer for not fully used sbc frames
static uint8_t decoded_audio_storage[(MAX_SBC_FRAME_SIZE+4) * BYTES_PER_FRAME];
static btstack_ring_buffer_t decoded_audio_ring_buffer;

// 
static int audio_stream_started;

// temp storage of lower-layer request
static int16_t * request_buffer;
static int       request_samples;

// WAV File
#ifdef STORE_SBC_TO_WAV_FILE    
static int frame_count = 0;
static char * wav_filename = "avdtp_sink.wav";
#endif

#ifdef STORE_SBC_TO_SBC_FILE    
static FILE * sbc_file;
static char * sbc_filename = "avdtp_sink.sbc";
#endif

typedef struct {
    // bitmaps
    uint8_t sampling_frequency_bitmap;
    uint8_t channel_mode_bitmap;
    uint8_t block_length_bitmap;
    uint8_t subbands_bitmap;
    uint8_t allocation_method_bitmap;
    uint8_t min_bitpool_value;
    uint8_t max_bitpool_value;
} adtvp_media_codec_information_sbc_t;

typedef struct {
    int reconfigure;
    int num_channels;
    int sampling_frequency;
    int channel_mode;
    int block_length;
    int subbands;
    int allocation_method;
    int min_bitpool_value;
    int max_bitpool_value;
    int frames_per_buffer;
} avdtp_media_codec_configuration_sbc_t;

#ifdef HAVE_BTSTACK_STDIN
static bd_addr_t device_addr;
//  iPhone 
static const char * device_addr_string = "6C:72:E7:10:22:EE";
//  iPad   static const char * device_addr_string = "80:BE:05:D5:28:48";

#endif

static uint8_t  sdp_avdtp_sink_service_buffer[150];
static avdtp_media_codec_configuration_sbc_t sbc_configuration;
static uint16_t a2dp_cid = 0;
static uint8_t  local_seid = 0;
static uint8_t  value[100];

static btstack_packet_callback_registration_t hci_event_callback_registration;

static int media_initialized = 0;

static uint16_t a2dp_sink_connected = 0;
static uint16_t avrcp_cid = 0;
static uint8_t  avrcp_connected = 0;
static uint8_t  sdp_avrcp_controller_service_buffer[200];

static uint8_t hfp_service_buffer[150];
static const uint8_t   rfcomm_channel_nr = 1;
static const char hfp_hf_service_name[] = "HFP HF Demo";

static hci_con_handle_t acl_handle = HCI_CON_HANDLE_INVALID;
static hci_con_handle_t sco_handle = HCI_CON_HANDLE_INVALID;
#ifdef ENABLE_HFP_WIDE_BAND_SPEECH
static uint8_t codecs[] = {HFP_CODEC_CVSD, HFP_CODEC_MSBC};
#else
static uint8_t codecs[] = {HFP_CODEC_CVSD};
#endif
static uint16_t indicators[1] = {0x01};
static uint8_t  negotiated_codec = HFP_CODEC_CVSD;
static btstack_packet_callback_registration_t hci_event_callback_registration;
static char cmd;

static const char * pb_name   = "pb";

static const char * phonebook_name;
static char phonebook_folder[30];
static char phonebook_path[30];
// static uint16_t pbap_cid;
static int sim1_selected;

static void main_state_summary(void){
    // log_info("Main state: %s", headset_states[headset.state]);
    // printf(" Main state: %s\n", headset_states[headset.state]);
}

static void log_summary(char * msg){
    if (headset.remote_addr_valid){
        log_info("Headset %s: %s", bd_addr_to_str(headset.remote_device_addr), msg);
        printf("Headset %s: %s\n", bd_addr_to_str(headset.remote_device_addr), msg);
    } else {
        log_info("Headset: %s", msg);
        printf("Headset: %s\n", msg);
    }
}

static void gap_summary(void){
    log_info("GAP Discoverable: Headset %2u", (int) headset.gap_headset_discoverable);
    log_info("GAP Connectable:  Headset %2u", (int) headset.gap_headset_connectable);
}

#ifdef HAVE_BTSTACK_STDIN
static void show_usage(void);

static void select_phonebook(const char * phonebook){
    phonebook_name = phonebook;
    sprintf(phonebook_path, "%s%s.vcf", sim1_selected ? "SIM1/telecom/" : "telecom/", phonebook);
    sprintf(phonebook_folder, "%s%s",   sim1_selected ? "SIM1/telecom/" : "telecom/", phonebook);
    printf("[-] Phonebook name   '%s'\n", phonebook_name);
    printf("[-] Phonebook folder '%s'\n", phonebook_folder);
    printf("[-] Phonebook path   '%s'\n", phonebook_path);
}
#endif

static void dump_supported_codecs(void){
    unsigned int i;
    int mSBC_skipped = 0;
    printf("Supported codecs: ");
    for (i = 0; i < sizeof(codecs); i++){
        switch(codecs[i]){
            case HFP_CODEC_CVSD:
                printf("CVSD");
                break;
            case HFP_CODEC_MSBC:
                if (hci_extended_sco_link_supported()){
                    printf(", mSBC");
                } else {
                    mSBC_skipped = 1;
                }
                break;
        }
    }
    printf("\n");
    if (mSBC_skipped){
        printf("mSBC codec disabled because eSCO not supported by local controller.\n");
    }
}


static uint8_t media_sbc_codec_capabilities[] = {
    0xFF,//(AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO,
    0xFF,//(AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2) | AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
    2, 53
}; 

static uint8_t media_sbc_codec_configuration[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO,
    (AVDTP_SBC_BLOCK_LENGTH_16 << 4) | (AVDTP_SBC_SUBBANDS_8 << 2) | AVDTP_SBC_ALLOCATION_METHOD_LOUDNESS,
    2, 53
}; 



static void hfp_hf_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t packet_size);
static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t packet_size);
static void avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void handle_l2cap_media_data_packet(uint8_t seid, uint8_t *packet, uint16_t size);

static void playback_handler(int16_t * buffer, uint16_t num_samples){
    
    // called from lower-layer but guaranteed to be on main thread

    // first fill from decoded_audio
    uint32_t bytes_read;
    btstack_ring_buffer_read(&decoded_audio_ring_buffer, (uint8_t *) buffer, num_samples * BYTES_PER_FRAME, &bytes_read);
    buffer          += bytes_read / NUM_CHANNELS;
    num_samples     -= bytes_read / BYTES_PER_FRAME;

    // then start decoding sbc frames using request_* globals
    request_buffer = buffer;
    request_samples = num_samples;
    while (request_samples && btstack_ring_buffer_bytes_available(&sbc_frame_ring_buffer) >= sbc_frame_size){
        // log_info("buffer %06u bytes -- need %d", btstack_ring_buffer_bytes_available(&sbc_frame_ring_buffer), request_samples);
        // decode frame
        uint8_t frame[MAX_SBC_FRAME_SIZE];
        btstack_ring_buffer_read(&sbc_frame_ring_buffer, frame, sbc_frame_size, &bytes_read);
        btstack_sbc_decoder_process_data(&sbc_decoder_state, 0, frame, sbc_frame_size);
    }
}

static void handle_pcm_data(int16_t * data, int num_samples, int num_channels, int sample_rate, void * context){
    UNUSED(sample_rate);
    UNUSED(context);
    UNUSED(num_channels);   // must be stereo == 2

#ifdef STORE_SBC_TO_WAV_FILE
    wav_writer_write_int16(num_samples * NUM_CHANNELS, data);
    frame_count++;
#endif

    int sbc_samples_fix_applied = 0;

    // drop audio frame to fix drift
    if (sbc_samples_fix < 0){
        num_samples--;
        data += NUM_CHANNELS;
        sbc_samples_fix_applied = 1;
    }

    // store data in btstack_audio buffer first
    if (request_samples){

        // add audio frame to fix drift
        if (!sbc_samples_fix_applied && sbc_samples_fix > 0){
            memcpy(request_buffer, data, BYTES_PER_FRAME);
            request_samples--;
            request_buffer += NUM_CHANNELS;
            sbc_samples_fix_applied = 1;
        }

        int samples_to_copy = btstack_min(num_samples, request_samples);
        memcpy(request_buffer, data, samples_to_copy * BYTES_PER_FRAME);
        num_samples     -= samples_to_copy;
        request_samples -= samples_to_copy;
        data            += samples_to_copy * NUM_CHANNELS;
        request_buffer  += samples_to_copy * NUM_CHANNELS;
    }

    // and rest in ring buffer
    if (num_samples){

        // add audio frame to fix drift
        if (!sbc_samples_fix_applied && sbc_samples_fix > 0){
            btstack_ring_buffer_write(&decoded_audio_ring_buffer, (uint8_t *) data, BYTES_PER_FRAME);
            sbc_samples_fix_applied = 1;
        }

        btstack_ring_buffer_write(&decoded_audio_ring_buffer, (uint8_t *) data, num_samples * BYTES_PER_FRAME);
    }
}

static int media_processing_init(avdtp_media_codec_configuration_sbc_t configuration){
    if (media_initialized) return 0;

    btstack_sbc_decoder_init(&sbc_decoder_state, sbc_mode, handle_pcm_data, NULL);

#ifdef STORE_SBC_TO_WAV_FILE
    wav_writer_open(wav_filename, configuration.num_channels, configuration.sampling_frequency);
#endif

#ifdef STORE_SBC_TO_SBC_FILE    
   sbc_file = fopen(sbc_filename, "wb"); 
#endif

    btstack_ring_buffer_init(&sbc_frame_ring_buffer, sbc_frame_storage, sizeof(sbc_frame_storage));
    btstack_ring_buffer_init(&decoded_audio_ring_buffer, decoded_audio_storage, sizeof(decoded_audio_storage));

    // setup audio playback
    const btstack_audio_t * audio = btstack_audio_get_instance();
    if (audio){
        audio->init(NUM_CHANNELS, configuration.sampling_frequency, &playback_handler, NULL);
    }

    audio_stream_started = 0;
    media_initialized = 1;
    return 0;
}

static void media_processing_close(void){

    if (!media_initialized) return;
    media_initialized = 0;
    audio_stream_started = 0;

#ifdef STORE_SBC_TO_WAV_FILE                  
    wav_writer_close();
    int total_frames_nr = sbc_decoder_state.good_frames_nr + sbc_decoder_state.bad_frames_nr + sbc_decoder_state.zero_frames_nr;

    printf("WAV Writer: Decoding done. Processed totaly %d frames:\n - %d good\n - %d bad\n", total_frames_nr, sbc_decoder_state.good_frames_nr, total_frames_nr - sbc_decoder_state.good_frames_nr);
    printf("WAV Writer: Written %d frames to wav file: %s\n", frame_count, wav_filename);
#endif

#ifdef STORE_SBC_TO_SBC_FILE
    fclose(sbc_file);
#endif     

    // stop audio playback
    const btstack_audio_t * audio = btstack_audio_get_instance();
    if (audio){
        audio->close();
    }
}

static int read_media_data_header(uint8_t * packet, int size, int * offset, avdtp_media_packet_header_t * media_header);
static int read_sbc_header(uint8_t * packet, int size, int * offset, avdtp_sbc_codec_header_t * sbc_header);

static void handle_l2cap_media_data_packet(uint8_t seid, uint8_t *packet, uint16_t size){
    UNUSED(seid);
    int pos = 0;
    
    avdtp_media_packet_header_t media_header;
    if (!read_media_data_header(packet, size, &pos, &media_header)) return;
    
    avdtp_sbc_codec_header_t sbc_header;
    if (!read_sbc_header(packet, size, &pos, &sbc_header)) return;

    const btstack_audio_t * audio = btstack_audio_get_instance();

    // process data right away if there's no audio implementation active, e.g. on posix systems to store as .wav
    if (!audio){
        btstack_sbc_decoder_process_data(&sbc_decoder_state, 0, packet+pos, size-pos);
        return;
    }

    // store sbc frame size for buffer management
    sbc_frame_size = (size-pos)/ sbc_header.num_frames;
        
    btstack_ring_buffer_write(&sbc_frame_ring_buffer, packet+pos, size-pos);

    // decide on audio sync drift based on number of sbc frames in queue
    int sbc_frames_in_buffer = btstack_ring_buffer_bytes_available(&sbc_frame_ring_buffer) / sbc_frame_size;
    if (sbc_frames_in_buffer < OPTIMAL_FRAMES_MIN){
    	sbc_samples_fix = 1;	// duplicate last sample
    } else if (sbc_frames_in_buffer <= OPTIMAL_FRAMES_MAX){
    	sbc_samples_fix = 0;	// nothing to do
    } else {
    	sbc_samples_fix = -1;	// drop last sample
    }

#ifdef STORE_SBC_TO_SBC_FILE
    fwrite(packet+pos, size-pos, 1, sbc_file);
#endif

    // start stream if enough frames buffered
    if (!audio_stream_started && sbc_frames_in_buffer >= (OPTIMAL_FRAMES_MAX+OPTIMAL_FRAMES_MIN)/2){
        audio_stream_started = 1;
        // setup audio playback
        if (audio){
            audio->start_stream();
        }
    }
}

static int read_sbc_header(uint8_t * packet, int size, int * offset, avdtp_sbc_codec_header_t * sbc_header){
    int sbc_header_len = 12; // without crc
    int pos = *offset;
    
    if (size - pos < sbc_header_len){
        printf("Not enough data to read SBC header, expected %d, received %d\n", sbc_header_len, size-pos);
        return 0;
    }

    sbc_header->fragmentation = get_bit16(packet[pos], 7);
    sbc_header->starting_packet = get_bit16(packet[pos], 6);
    sbc_header->last_packet = get_bit16(packet[pos], 5);
    sbc_header->num_frames = packet[pos] & 0x0f;
    pos++;
    // printf("SBC HEADER: num_frames %u, fragmented %u, start %u, stop %u\n", sbc_header.num_frames, sbc_header.fragmentation, sbc_header.starting_packet, sbc_header.last_packet);
    *offset = pos;
    return 1;
}

static int read_media_data_header(uint8_t *packet, int size, int *offset, avdtp_media_packet_header_t *media_header){
    int media_header_len = 12; // without crc
    int pos = *offset;
    
    if (size - pos < media_header_len){
        printf("Not enough data to read media packet header, expected %d, received %d\n", media_header_len, size-pos);
        return 0;
    }

    media_header->version = packet[pos] & 0x03;
    media_header->padding = get_bit16(packet[pos],2);
    media_header->extension = get_bit16(packet[pos],3);
    media_header->csrc_count = (packet[pos] >> 4) & 0x0F;
    pos++;

    media_header->marker = get_bit16(packet[pos],0);
    media_header->payload_type  = (packet[pos] >> 1) & 0x7F;
    pos++;

    media_header->sequence_number = big_endian_read_16(packet, pos);
    pos+=2;

    media_header->timestamp = big_endian_read_32(packet, pos);
    pos+=4;

    media_header->synchronization_source = big_endian_read_32(packet, pos);
    pos+=4;
    *offset = pos;
    return 1;
}

static void dump_sbc_configuration(avdtp_media_codec_configuration_sbc_t configuration){
    printf("Received SBC configuration:\n");
    printf("    - num_channels: %d\n", configuration.num_channels);
    printf("    - sampling_frequency: %d\n", configuration.sampling_frequency);
    printf("    - channel_mode: %d\n", configuration.channel_mode);
    printf("    - block_length: %d\n", configuration.block_length);
    printf("    - subbands: %d\n", configuration.subbands);
    printf("    - allocation_method: %d\n", configuration.allocation_method);
    printf("    - bitpool_value [%d, %d] \n", configuration.min_bitpool_value, configuration.max_bitpool_value);
    printf("\n");
}

static void avrcp_controller_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);
    uint16_t local_cid;
    uint8_t  status = 0xFF;
    bd_addr_t adress;
    
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_AVRCP_META) return;
    switch (hci_event_a2dp_meta_get_subevent_code(packet)){
        case AVRCP_SUBEVENT_CONNECTION_ESTABLISHED: {
            local_cid = avrcp_subevent_connection_established_get_avrcp_cid(packet);
            if (avrcp_cid != 0 && avrcp_cid != local_cid) {
                printf("Headsed AVRCP: Connection failed, expected 0x%02X l2cap cid, received 0x%02X\n", avrcp_cid, local_cid);
                return;
            }

            status = avrcp_subevent_connection_established_get_status(packet);
            if (status != ERROR_CODE_SUCCESS){
                printf("Headsed AVRCP: Connection failed: status 0x%02x\n", status);
                avrcp_cid = 0;
                return;
            }
            
            avrcp_cid = local_cid;
            avrcp_connected = 1;
            avrcp_subevent_connection_established_get_bd_addr(packet, adress);
            printf("Headsed AVRCP: Channel successfully opened: %s, avrcp_cid 0x%02x\n", bd_addr_to_str(adress), avrcp_cid);

            // automatically enable notifications
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_PLAYBACK_STATUS_CHANGED);
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_NOW_PLAYING_CONTENT_CHANGED);
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_VOLUME_CHANGED);
            avrcp_controller_enable_notification(avrcp_cid, AVRCP_NOTIFICATION_EVENT_TRACK_CHANGED);
            return;
        }
        case AVRCP_SUBEVENT_CONNECTION_RELEASED:
            printf("Headsed AVRCP: Channel released: avrcp_cid 0x%02x\n", avrcp_subevent_connection_released_get_avrcp_cid(packet));
            avrcp_cid = 0;
            avrcp_connected = 0;
            return;
        default:
            break;
    }

    status = packet[5];
    if (!avrcp_cid) return;

    // ignore INTERIM status
    if (status == AVRCP_CTYPE_RESPONSE_INTERIM){
        switch (hci_event_a2dp_meta_get_subevent_code(packet)){
            case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_POS_CHANGED:{
                uint32_t playback_position_ms = avrcp_subevent_notification_playback_pos_changed_get_playback_position_ms(packet);
                if (playback_position_ms == AVRCP_NO_TRACK_SELECTED_PLAYBACK_POSITION_CHANGED){
                    printf("Headsed AVRCP: notification, playback position changed, no track is selected\n");
                }  
                break;
            }
            default:
                printf("Headsed AVRCP:  INTERIM response \n"); 
                break;
        }
        return;
    } 
            
    printf("Headsed AVRCP: command status: %s, ", avrcp_ctype2str(status));
    switch (hci_event_a2dp_meta_get_subevent_code(packet)){
        case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_POS_CHANGED:
            printf("Headsed AVRCP: notification, playback position changed, position %d ms\n", (unsigned int) avrcp_subevent_notification_playback_pos_changed_get_playback_position_ms(packet));
            break;
        case AVRCP_SUBEVENT_NOTIFICATION_PLAYBACK_STATUS_CHANGED:
            printf("Headsed AVRCP: notification, playback status changed %s\n", avrcp_play_status2str(avrcp_subevent_notification_playback_status_changed_get_play_status(packet)));
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_NOW_PLAYING_CONTENT_CHANGED:
            printf("Headsed AVRCP: notification, playing content changed\n");
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_TRACK_CHANGED:
            printf("Headsed AVRCP: notification track changed\n");
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_VOLUME_CHANGED:
            printf("Headsed AVRCP: notification absolute volume changed %d\n", avrcp_subevent_notification_volume_changed_get_absolute_volume(packet));
            return;
        case AVRCP_SUBEVENT_NOTIFICATION_AVAILABLE_PLAYERS_CHANGED:
            printf("Headsed AVRCP: notification changed\n");
            return; 
        case AVRCP_SUBEVENT_SHUFFLE_AND_REPEAT_MODE:{
            uint8_t shuffle_mode = avrcp_subevent_shuffle_and_repeat_mode_get_shuffle_mode(packet);
            uint8_t repeat_mode  = avrcp_subevent_shuffle_and_repeat_mode_get_repeat_mode(packet);
            printf("%s, %s\n", avrcp_shuffle2str(shuffle_mode), avrcp_repeat2str(repeat_mode));
            break;
        }
        case AVRCP_SUBEVENT_NOW_PLAYING_TITLE_INFO:
            if (avrcp_subevent_now_playing_title_info_get_value_len(packet) > 0){
                memcpy(value, avrcp_subevent_now_playing_title_info_get_value(packet), avrcp_subevent_now_playing_title_info_get_value_len(packet));
                printf("    Title: %s\n", value);
            }  
            break;

        case AVRCP_SUBEVENT_NOW_PLAYING_ARTIST_INFO:
            if (avrcp_subevent_now_playing_artist_info_get_value_len(packet) > 0){
                memcpy(value, avrcp_subevent_now_playing_artist_info_get_value(packet), avrcp_subevent_now_playing_artist_info_get_value_len(packet));
                printf("    Artist: %s\n", value);
            }  
            break;
        
        case AVRCP_SUBEVENT_NOW_PLAYING_ALBUM_INFO:
            if (avrcp_subevent_now_playing_album_info_get_value_len(packet) > 0){
                memcpy(value, avrcp_subevent_now_playing_album_info_get_value(packet), avrcp_subevent_now_playing_album_info_get_value_len(packet));
                printf("    Album: %s\n", value);
            }  
            break;
        
        case AVRCP_SUBEVENT_NOW_PLAYING_GENRE_INFO:
            if (avrcp_subevent_now_playing_genre_info_get_value_len(packet) > 0){
                memcpy(value, avrcp_subevent_now_playing_genre_info_get_value(packet), avrcp_subevent_now_playing_genre_info_get_value_len(packet));
                printf("    Genre: %s\n", value);
            }  
            break;
        
        case AVRCP_SUBEVENT_PLAY_STATUS:
            printf("Headsed AVRCP: song length: %"PRIu32" ms, song position: %"PRIu32" ms, play status: %s\n", 
                avrcp_subevent_play_status_get_song_length(packet), 
                avrcp_subevent_play_status_get_song_position(packet),
                avrcp_play_status2str(avrcp_subevent_play_status_get_play_status(packet)));
            break;
        case AVRCP_SUBEVENT_OPERATION_COMPLETE:
            printf("Headsed AVRCP: operation done %s\n", avrcp_operation2str(avrcp_subevent_operation_complete_get_operation_id(packet)));
            break;
        case AVRCP_SUBEVENT_OPERATION_START:
            printf("Headsed AVRCP: operation start %s\n", avrcp_operation2str(avrcp_subevent_operation_complete_get_operation_id(packet)));
            break;
        case AVRCP_SUBEVENT_PLAYER_APPLICATION_VALUE_RESPONSE:
            // response to set shuffle and repeat mode
            printf("\n");
            break;
        default:
            printf("Headsed AVRCP: event is not parsed\n");
            break;
    }  
}

static void hfp_hf_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t * packet, uint16_t packet_size){
    UNUSED(channel);

    switch (packet_type){
        case HCI_SCO_DATA_PACKET:
            if (READ_SCO_CONNECTION_HANDLE(packet) != sco_handle) break;
            sco_demo_receive(packet, packet_size);
            break;

        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)){
                case HCI_EVENT_SCO_CAN_SEND_NOW:
                    sco_demo_send(sco_handle);
                    break;

                case HCI_EVENT_COMMAND_COMPLETE:
                    if (HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_read_local_supported_features)){
                        dump_supported_codecs();
                    }
                    break;
                
                case HCI_EVENT_HFP_META:
                    switch (hci_event_a2dp_meta_get_subevent_code(packet)) {   
                        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_ESTABLISHED:
                            acl_handle = hfp_subevent_service_level_connection_established_get_con_handle(packet);
                            hfp_subevent_service_level_connection_established_get_bd_addr(packet, device_addr);
                            printf("Headset HFP: Service level connection established %s.\n\n", bd_addr_to_str(device_addr));
                            break;
                        case HFP_SUBEVENT_SERVICE_LEVEL_CONNECTION_RELEASED:
                            acl_handle = HCI_CON_HANDLE_INVALID;
                            printf("Headset HFP:Service level connection released.\n\n");
                            break;
                        case HFP_SUBEVENT_AUDIO_CONNECTION_ESTABLISHED:
                            if (hfp_subevent_audio_connection_established_get_status(packet)){
                                printf("Headset HFP: Audio connection establishment failed with status %u\n", hfp_subevent_audio_connection_established_get_status(packet));
                            } else {
                                sco_handle = hfp_subevent_audio_connection_established_get_handle(packet);
                                printf("Headset HFP: Audio connection established with SCO handle 0x%04x.\n", sco_handle);
                                negotiated_codec = hfp_subevent_audio_connection_established_get_negotiated_codec(packet);
                                switch (negotiated_codec){
                                    case 0x01:
                                        printf("Headset HFP: Using CVSD codec.\n");
                                        break;
                                    case 0x02:
                                        printf("Headset HFP: Using mSBC codec.\n");
                                        break;
                                    default:
                                        printf("Headset HFP: Using unknown codec 0x%02x.\n", negotiated_codec);
                                        break;
                                }
                                sco_demo_set_codec(negotiated_codec);
                                hci_request_sco_can_send_now_event();
                            }
                            break;
                        case HFP_SUBEVENT_AUDIO_CONNECTION_RELEASED:
                            sco_handle = HCI_CON_HANDLE_INVALID;
                            printf("Headset HFP: Audio connection released\n");
                            sco_demo_close();
                            break;
                        case HFP_SUBEVENT_COMPLETE:
                            switch (cmd){
                                case 'd':
                                    printf("Headset HFP: HFP AG registration status update enabled.\n");
                                    break;
                                case 'e':
                                    printf("Headset HFP: HFP AG registration status update for individual indicators set.\n");
                                    break;
                                default:
                                    break;
                            }
                            break;
                        case HFP_SUBEVENT_AG_INDICATOR_STATUS_CHANGED:
                            printf("Headset HFP: AG_INDICATOR_STATUS_CHANGED, AG indicator (index: %d) to: %d of range [%d, %d], name '%s'\n", 
                                hfp_subevent_ag_indicator_status_changed_get_indicator_index(packet), 
                                hfp_subevent_ag_indicator_status_changed_get_indicator_status(packet),
                                hfp_subevent_ag_indicator_status_changed_get_indicator_min_range(packet),
                                hfp_subevent_ag_indicator_status_changed_get_indicator_max_range(packet),
                                (const char*) hfp_subevent_ag_indicator_status_changed_get_indicator_name(packet));
                            break;
                        case HFP_SUBEVENT_NETWORK_OPERATOR_CHANGED:
                            printf("Headset HFP: NETWORK_OPERATOR_CHANGED, operator mode: %d, format: %d, name: %s\n", 
                                hfp_subevent_network_operator_changed_get_network_operator_mode(packet), 
                                hfp_subevent_network_operator_changed_get_network_operator_format(packet), 
                                (char *) hfp_subevent_network_operator_changed_get_network_operator_name(packet));          
                            break;
                        case HFP_SUBEVENT_EXTENDED_AUDIO_GATEWAY_ERROR:
                            printf("Headset HFP: EXTENDED_AUDIO_GATEWAY_ERROR_REPORT, status : %d\n", 
                                hfp_subevent_extended_audio_gateway_error_get_error(packet));
                            break;
                        case HFP_SUBEVENT_RING:
                            printf("Headset HFP: ** Ring **\n");
                            break;
                        case HFP_SUBEVENT_NUMBER_FOR_VOICE_TAG:
                            printf("Headset HFP: Phone number for voice tag: %s\n", 
                                (const char *) hfp_subevent_number_for_voice_tag_get_number(packet));
                            break;
                        case HFP_SUBEVENT_SPEAKER_VOLUME:
                            printf("Headset HFP: Speaker volume: status %u, gain %u\n", 
                                hfp_subevent_speaker_volume_get_status(packet),
                                hfp_subevent_speaker_volume_get_gain(packet));
                            break;
                        case HFP_SUBEVENT_MICROPHONE_VOLUME:
                            printf("Headset HFP: Microphone volume: status %u, gain %u\n", 
                                hfp_subevent_microphone_volume_get_status(packet),
                                hfp_subevent_microphone_volume_get_gain(packet));
                            break;
                        case HFP_SUBEVENT_CALLING_LINE_IDENTIFICATION_NOTIFICATION:
                            printf("Headset HFP: Caller ID, number %s\n", hfp_subevent_calling_line_identification_notification_get_number(packet));
                            break;
                        default:
                            printf("Headset HFP: event not handled %u\n", hci_event_a2dp_meta_get_subevent_code(packet));
                            break;
                    }
                    break;

                default:
                    break;
            }
            break;

        default:
            break;
    }

}

static void a2dp_sink_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);
    uint16_t cid;
    bd_addr_t address;
    uint8_t status;

    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;

    switch (hci_event_a2dp_meta_get_subevent_code(packet)){
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_OTHER_CONFIGURATION:
            printf("Headset A2DP: received non SBC codec. not implemented.\n");
            break;
        case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:{
            printf("Headset A2DP: received SBC codec configuration.\n");
            sbc_configuration.reconfigure = a2dp_subevent_signaling_media_codec_sbc_configuration_get_reconfigure(packet);
            sbc_configuration.num_channels = a2dp_subevent_signaling_media_codec_sbc_configuration_get_num_channels(packet);
            sbc_configuration.sampling_frequency = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
            sbc_configuration.channel_mode = a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet);
            sbc_configuration.block_length = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet);
            sbc_configuration.subbands = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet);
            sbc_configuration.allocation_method = a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet);
            sbc_configuration.min_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_min_bitpool_value(packet);
            sbc_configuration.max_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet);
            sbc_configuration.frames_per_buffer = sbc_configuration.subbands * sbc_configuration.block_length;
            dump_sbc_configuration(sbc_configuration);

            if (sbc_configuration.reconfigure){
                media_processing_close();
            }
            // prepare media processing
            media_processing_init(sbc_configuration);
            break;
        }  
        case A2DP_SUBEVENT_STREAM_ESTABLISHED:
            a2dp_subevent_stream_established_get_bd_addr(packet, address);
            status = a2dp_subevent_stream_established_get_status(packet);
            cid = a2dp_subevent_stream_established_get_a2dp_cid(packet);
            printf("A2DP_SUBEVENT_STREAM_ESTABLISHED %d, %d \n", cid, a2dp_cid);
            if (!a2dp_cid){
                // incoming connection
                a2dp_cid = cid;
            } else if (cid != a2dp_cid) {
                break;
            }
            if (status){
                a2dp_sink_connected = 0;
                printf("Headset A2DP: streaming connection failed, status 0x%02x\n", status);
                break;
            }
            printf("Headset A2DP: streaming connection is established, address %s, a2dp cid 0x%02X, local_seid %d\n", bd_addr_to_str(address), a2dp_cid, local_seid);
            
            memcpy(device_addr, address, 6);

            local_seid = a2dp_subevent_stream_established_get_local_seid(packet);
            a2dp_sink_connected = 1;
            break;
        
        case A2DP_SUBEVENT_STREAM_STARTED:
            cid = a2dp_subevent_stream_started_get_a2dp_cid(packet);
            if (cid != a2dp_cid) break;
            local_seid = a2dp_subevent_stream_started_get_local_seid(packet);
            printf("Headset A2DP: stream started, a2dp cid 0x%02X, local_seid %d\n", a2dp_cid, local_seid);
            // started
            media_processing_init(sbc_configuration);
            break;
        
        case A2DP_SUBEVENT_STREAM_SUSPENDED:
            cid = a2dp_subevent_stream_suspended_get_a2dp_cid(packet);
            if (cid != a2dp_cid) break;
            local_seid = a2dp_subevent_stream_suspended_get_local_seid(packet);
            printf("Headset A2DP: stream paused, a2dp cid 0x%02X, local_seid %d\n", a2dp_cid, local_seid);
            media_processing_close();
            break;
        
        case A2DP_SUBEVENT_STREAM_RELEASED:
            local_seid = a2dp_subevent_stream_released_get_local_seid(packet);
            printf("Headset A2DP: stream released, a2dp cid 0x%02X, local_seid %d\n", a2dp_cid, local_seid);
            media_processing_close();
            break;
        case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
            cid = a2dp_subevent_signaling_connection_released_get_a2dp_cid(packet);
            a2dp_sink_connected = 0;
            printf("Headset A2DP: signaling connection released\n");
            media_processing_close();
            break;
        default:
            printf("Headset A2DP: not parsed 0x%02x\n", hci_event_a2dp_meta_get_subevent_code(packet));
            break; 
    }
}

static void headset_notify_connected_successfully(void){
    printf("Notification: Device connected successfully to %s\n", bd_addr_to_str(headset.remote_device_addr));
}

static void btstack_headset_outgoing_device_iterator_get_next(void){
    bd_addr_t  addr;
    link_key_t link_key;
    link_key_type_t type;

    headset.remote_addr_valid = 0;

    switch (headset.reconnect_state){
        case BTSTACK_HEADSET_RECONNECT_NOT_INITIALIZED:
            if (!gap_link_key_iterator_init(&headset.link_key_iterator)) {
                log_summary("Link key iterator failed.");
                break;
            }
            headset.reconnect_state = BTSTACK_HEADSET_RECONNECT_LAST_USED_DEVICE;

            /* fall through */

        case BTSTACK_HEADSET_RECONNECT_LAST_USED_DEVICE:
            if (headset.last_connected_device_valid){
                memcpy(headset.remote_device_addr, headset.last_connected_device, BD_ADDR_LEN);
                log_summary("Info on last used device is available.");
                headset.remote_addr_valid = 1;
                break;
            }
            headset.reconnect_state = BTSTACK_HEADSET_RECONNECT_LINK_KEY_LIST_NEXT;
            
            /* fall through */

        case BTSTACK_HEADSET_RECONNECT_LINK_KEY_LIST_NEXT:
            while (gap_link_key_iterator_get_next(&headset.link_key_iterator, addr, link_key, &type)){
                // skip last connected device
                if (memcmp(headset.last_connected_device, addr, BD_ADDR_LEN) == 0) continue;
                memcpy(headset.remote_device_addr, addr, BD_ADDR_LEN);
                headset.remote_addr_valid = 1;
                log_summary("Info on last used device is available.");
                return;
            } 
            gap_link_key_iterator_done(&headset.link_key_iterator);
            headset.reconnect_state = BTSTACK_HEADSET_RECONNECT_NOT_INITIALIZED;
            log_summary("There is no information on previously bounded device. Turn on pairing mode to enable incoming connection.");
            break;
        default:
            break;
    }
}

static void btstack_headset_outgoing_device_iterator_complete(void){
    switch (headset.reconnect_state){
        case BTSTACK_HEADSET_RECONNECT_LINK_KEY_LIST_NEXT:
            gap_link_key_iterator_done(&headset.link_key_iterator);
            break;
        default:
            break;
    }
    headset.reconnect_state = BTSTACK_HEADSET_RECONNECT_NOT_INITIALIZED; 
}

static void headset_auto_connect_timer_callback(btstack_timer_source_t * ts){
    UNUSED(ts);
    if (headset.state != BTSTACK_HEADSET_W4_TIMER) return;
    headset.state = BTSTACK_HEADSET_IDLE;
    headset.connect = 1;
    headset_run();
}

static void headset_auto_connect_timer_stop(void){
    log_summary("Stop auto-connect.");
    btstack_run_loop_remove_timer(&headset.headset_auto_connect_timer);
} 

static void headset_auto_connect_restart(void){
    if (headset.state == BTSTACK_HEADSET_W4_TIMER) return;

    btstack_headset_outgoing_device_iterator_get_next();

    if (!headset.remote_addr_valid) {
        headset.state = BTSTACK_HEADSET_IDLE;
        return;
    }

    headset.state = BTSTACK_HEADSET_W4_TIMER;
    log_summary("Trigger auto-connect procedure in 10 sec.");
    // disable incoming connection, as we will create an outoing connection
    headset.gap_headset_connectable  = 0;
    gap_connectable_control(headset.gap_headset_connectable);

    btstack_run_loop_set_timer_handler(&headset.headset_auto_connect_timer, headset_auto_connect_timer_callback);
    btstack_run_loop_set_timer(&headset.headset_auto_connect_timer, HEADSET_AUTO_CONNECT_INTERVAL_MS);
    btstack_run_loop_add_timer(&headset.headset_auto_connect_timer);
}

static void headset_init(void){
    memset(&headset, 0, sizeof(headset_connection_t));
    headset.con_handle = HCI_CON_HANDLE_INVALID;
     // Set remote device discoverable + connectable
    headset.gap_headset_connectable  = HEADSET_CONNECTABLE_WHEN_NOT_CONNECTED;
    headset.gap_headset_discoverable = HEADSET_DISCOVERABLE_WHEN_NOT_CONNECTED;
    gap_connectable_control(headset.gap_headset_connectable);
    gap_discoverable_control(headset.gap_headset_discoverable);

    main_state_summary();
    gap_summary();
}

static void headset_run(){
    if (hci_get_state() != HCI_STATE_WORKING) return;
    switch (headset.state){
        case BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER:
            if (!headset.pairing_mode_enabled) break;
            main_state_summary();
            break;

        case BTSTACK_HEADSET_IDLE:
            if (!headset.connect) break;
            if (!hci_can_send_command_packet_now()) break;
            headset.connect = 0;
            headset.state = BTSTACK_HEADSET_W4_CONNECTION_COMPLETE;
            log_summary("Auto-connect to device.");
            // HCI cmd, BD_ADDR, Packet_Type, Page_Scan_Repetition_Mode, Reserved, Clock_Offset, Allow_Role_Switch
            hci_send_cmd(&hci_create_connection, headset.remote_device_addr, hci_usable_acl_packet_types(), 0, 0, 0, 1);
            break;

        case BTSTACK_HEADSET_CONNECTED:
            if (!hci_can_send_command_packet_now()) break;
            headset.state = BTSTACK_HEADSET_W4_LINK_SUPERVSION_TIMEOUT_UPDATE;
            log_summary("Set link supervision timeout.");
            hci_send_cmd(&hci_write_link_supervision_timeout, headset.con_handle, LINK_SUPERVISION_TIMEOUT);
            break;

        case BTSTACK_HEADSET_LINK_SUPERVSION_TIMEOUT_UPDATE:
            if (gap_security_level(headset.con_handle) < LEVEL_2){
                headset.state = BTSTACK_HEADSET_W4_AUTHENTICATION;
                log_summary("Authenticate device.");
                gap_request_security_level(headset.con_handle, LEVEL_2);
                break;
            }
            log_summary("Already authenticated, skip authentication.");

            /*  fall through */

        case BTSTACK_HEADSET_AUTHENTICATION_DONE:
            headset_notify_connected_successfully();

            // main_state_summary();
            btstack_headset_outgoing_device_iterator_complete();
            memcpy(headset.last_connected_device, headset.remote_device_addr, BD_ADDR_LEN);
            headset.last_connected_device_valid = 1;
            headset.remote_addr_valid = 1;
            headset.pairing_mode_enabled = 0;

            // store last used in TLV
            if (btstack_tlv_impl){
                btstack_tlv_impl->store_tag(btstack_tlv_context, LAST_CONNECTED_DEVICE_TAG, (uint8_t*) &headset.last_connected_device, BD_ADDR_LEN);
            } else {
                log_summary("btstack_tlv_impl NULL!!!");
            }
            
             /*  fall through */

        case BTSTACK_HEADSET_DONE:
            headset.state = BTSTACK_HEADSET_DONE;
            
            if (headset.disconnect){
                headset.disconnect = 0;
                headset.state = BTSTACK_HEADSET_W4_DISCONNECT;
                gap_disconnect(headset.con_handle);
                break;
            }

            if (headset.pairing_mode_enabled){

                break;
            }
            break;
        default:
            break;
    }
}

static int is_bd_address_known(bd_addr_t event_addr){
    if (headset.last_connected_device_valid){
        if (memcmp(event_addr, headset.last_connected_device, BD_ADDR_LEN) == 0) return 1;
    }
                                
    int bd_addr_known = 0;
        // search TLV for list of known devices
    bd_addr_t  addr;
    link_key_t link_key;
    link_key_type_t type;
    btstack_link_key_iterator_t it;

    int ok = gap_link_key_iterator_init(&it);
    if (!ok) {
        return 0;
    }

    while (gap_link_key_iterator_get_next(&it, addr, link_key, &type)){
        if (memcmp(addr, event_addr, BD_ADDR_LEN) == 0) {
            bd_addr_known = 1;
            break;
        }
    }
    gap_link_key_iterator_done(&it);
    return bd_addr_known;
}


#ifdef HAVE_BTSTACK_STDIN
// packet handler for interactive console
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);

    bd_addr_t        event_addr;
    hci_con_handle_t con_handle;
    
    int i;
    uint8_t status;
    char buffer[32];
                                
    switch (packet_type){
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case BTSTACK_EVENT_STATE:
                    // BTstack activated, get started 
                    if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING){
                        btstack_tlv_get_instance(&btstack_tlv_impl, &btstack_tlv_context);
                        log_info("TLV impl %p, context %p", btstack_tlv_impl, btstack_tlv_context);
                        show_usage();
                        headset_auto_connect_restart();
                    }
                    break;

                case HCI_EVENT_CONNECTION_REQUEST:
                    main_state_summary();
                    hci_event_connection_request_get_bd_addr(packet, event_addr);
                    log_summary("Connection request.");

                    switch (headset.state){
                        case BTSTACK_HEADSET_IDLE:
                        case BTSTACK_HEADSET_W4_TIMER:
                            if (is_bd_address_known(event_addr) || headset.pairing_mode_enabled){
                                // on incoming connection, postpone our own connection attempts (hopefully done then)
                                memcpy(headset.remote_device_addr, event_addr, BD_ADDR_LEN);
                                headset.remote_addr_valid = 1;
                                log_summary("Device has been previously bounded, allow incoming connection.");
                            } 
                            break;
                        default:
                            break;
                    }
                    break;

                case HCI_EVENT_CONNECTION_COMPLETE:
                    hci_event_connection_complete_get_bd_addr(packet, event_addr);
                    con_handle = hci_event_connection_complete_get_connection_handle(packet);
                    status = hci_event_connection_complete_get_status(packet);

                    switch(status){
                        case ERROR_CODE_SUCCESS:
                            // disconnect, if not from a known address
                            if (!is_bd_address_known(event_addr) && !headset.pairing_mode_enabled){
                                log_summary("Unknown device is connected, but pairing mode is disabled - disconnect.");
                                headset.state = BTSTACK_HEADSET_W4_DISCONNECT;
                                gap_disconnect(con_handle);
                                break;
                            }
                            headset.con_handle = con_handle;
                            log_summary("Device connected.");
                            main_state_summary();

                            headset.gap_headset_connectable  = 0;
                            gap_connectable_control(headset.gap_headset_connectable);
                            headset.gap_headset_discoverable = 0;
                            gap_discoverable_control(headset.gap_headset_discoverable);

                            if (headset.pairing_mode_enabled) {
                                headset_auto_connect_timer_stop();
                                headset.state = BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION;
                                main_state_summary();
                                break;
                            }

                            switch (headset.state){
                                case BTSTACK_HEADSET_W4_TIMER:
                                case BTSTACK_HEADSET_W4_CONNECTION_COMPLETE:
                                    headset_auto_connect_timer_stop();
                                    headset.state = BTSTACK_HEADSET_CONNECTED;
                                    headset.remote_addr_valid = 1;
                                    break;
                                default:
                                    printf("start auto-connect? state %d\n", headset.state);
                                    break;

                            }
                            main_state_summary();
                            break;
                        case ERROR_CODE_PAGE_TIMEOUT:
                            if (headset.state != BTSTACK_HEADSET_W4_CONNECTION_COMPLETE) break;
                            log_summary("Connection failed with page timetout, retry.");
                            headset_auto_connect_restart();
                            break;
                        case ERROR_CODE_ACL_CONNECTION_ALREADY_EXISTS:
                            if (headset.state != BTSTACK_HEADSET_W4_CONNECTION_COMPLETE) break;
                            log_summary("Connection failed connection already exists, retry.");
                            headset_auto_connect_restart();
                            break;
                        default:
                            if (headset.state != BTSTACK_HEADSET_W4_CONNECTION_COMPLETE) break;
                            log_summary("Connection failed, retry.");
                            headset_auto_connect_restart();
                            break;
                    }
                    break;

                case HCI_EVENT_COMMAND_COMPLETE:
                    if (!HCI_EVENT_IS_COMMAND_COMPLETE(packet, hci_write_link_supervision_timeout)) break;
                    if (headset.state != BTSTACK_HEADSET_W4_LINK_SUPERVSION_TIMEOUT_UPDATE) break;
                    headset.state = BTSTACK_HEADSET_LINK_SUPERVSION_TIMEOUT_UPDATE;
                    break;

                case HCI_EVENT_PIN_CODE_REQUEST:
                    if (headset.state != BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION) break;
                    headset.state = BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER;
                    log_summary("Pin code request - using 0000. Wait for user confirmation...");
                    hci_event_pin_code_request_get_bd_addr(packet, headset.remote_device_addr);
                    break;

                case HCI_EVENT_USER_CONFIRMATION_REQUEST:
                    // inform about user confirmation request
                    if (headset.state != BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION) break;
                    // printf("GAP Headset: SSP User Confirmation Request with numeric value '%06"PRIu32"'\n", hci_event_user_confirmation_request_get_numeric_value(packet));
                    log_summary("SPP mode, numeric comparison. Wait for user confirmation");
                    headset.state = BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER;
                    break;

                case HCI_EVENT_AUTHENTICATION_COMPLETE_EVENT:
                    if (hci_event_authentication_complete_get_connection_handle(packet) != headset.con_handle) break;
                    status = hci_event_authentication_complete_get_status(packet);

                    // abort auto-connection on authentication failure
                    switch (status){
                        case ERROR_CODE_SUCCESS:
                            if (headset.state != BTSTACK_HEADSET_W4_AUTHENTICATION) break;
                            headset.state = BTSTACK_HEADSET_AUTHENTICATION_DONE;
                            break;
                        case ERROR_CODE_PIN_OR_KEY_MISSING:
                            log_summary("Device does not have link key, dropping stored link key, disconnect.");
                            gap_drop_link_key_for_bd_addr(headset.remote_device_addr);
                            // disconnect
                            headset.state = BTSTACK_HEADSET_W4_DISCONNECT;
                            gap_disconnect(headset.con_handle);
                            break;
                        default:
                            log_summary("Device authentication failed, disconnect");
                            headset.state = BTSTACK_HEADSET_W4_DISCONNECT;
                            gap_disconnect(headset.con_handle);
                            break;
                    }
                    main_state_summary();
                    break;

                case HCI_EVENT_DISCONNECTION_COMPLETE:
                    con_handle = hci_event_disconnection_complete_get_connection_handle(packet);
                    if (con_handle == HCI_CON_HANDLE_INVALID) break;
                    if (con_handle != headset.con_handle) break;
                    
                    headset.con_handle = HCI_CON_HANDLE_INVALID;
                    log_summary("Device disconnected.");

                    headset.state = BTSTACK_HEADSET_IDLE;
                    headset.gap_headset_connectable  = HEADSET_CONNECTABLE_WHEN_NOT_CONNECTED;
                    gap_connectable_control(headset.gap_headset_connectable);

                    if (headset.pairing_mode_enabled){
                        log_summary("Pairing mode is on.");
                        headset.gap_headset_discoverable = HEADSET_DISCOVERABLE_WHEN_NOT_CONNECTED;
                        gap_discoverable_control(headset.gap_headset_discoverable);
                        break;
                    }
                    headset_auto_connect_restart();
                    break;
                
                case HCI_EVENT_PBAP_META:
                    switch (hci_event_pbap_meta_get_subevent_code(packet)){
                        case PBAP_SUBEVENT_CONNECTION_OPENED:
                            status = pbap_subevent_connection_opened_get_status(packet);
                            if (status){
                                printf("[!] Connection failed, status 0x%02x\n", status);
                            } else {
                                printf("[+] Connected\n");
                            }
                            break;
                        case PBAP_SUBEVENT_CONNECTION_CLOSED:
                            printf("[+] Connection closed\n");
                            break;
                        case PBAP_SUBEVENT_OPERATION_COMPLETED:
                            printf("[+] Operation complete\n");
                            break;
                        case PBAP_SUBEVENT_AUTHENTICATION_REQUEST:
                            printf("[?] Authentication requested\n");
                            break;
                        case PBAP_SUBEVENT_PHONEBOOK_SIZE:
                            status = pbap_subevent_phonebook_size_get_status(packet);
                            if (status){
                                printf("[!] Get Phonebook size error: 0x%x\n", status);
                            } else {
                                printf("[+] Phonebook size: %u\n", pbap_subevent_phonebook_size_get_phoneboook_size(packet));
                            }
                            break;
                        case PBAP_SUBEVENT_CARD_RESULT:
                            memcpy(buffer, pbap_subevent_card_result_get_name(packet), pbap_subevent_card_result_get_name_len(packet));
                            buffer[pbap_subevent_card_result_get_name_len(packet)] = 0;
                            printf("[-] Name:   '%s'\n", buffer);
                            memcpy(buffer, pbap_subevent_card_result_get_handle(packet), pbap_subevent_card_result_get_handle_len(packet));
                            buffer[pbap_subevent_card_result_get_handle_len(packet)] = 0;
                            printf("[-] Handle: '%s'\n", buffer);
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            break;
        case PBAP_DATA_PACKET:
            for (i=0;i<size;i++){
                printf("%c", packet[i]);
            }
            break;
        default:
            break;
    }
    headset_run();
}

void headset_connect(bd_addr_t remote_device_addr){
    switch (headset.state){
        case BTSTACK_HEADSET_IDLE:
            memcpy(headset.remote_device_addr, remote_device_addr, BD_ADDR_LEN);
            headset.connect = 1;
            break;
        case BTSTACK_HEADSET_W4_TIMER:
            headset_auto_connect_timer_stop();
            headset.state = BTSTACK_HEADSET_IDLE;
            headset.gap_headset_connectable  = HEADSET_CONNECTABLE_WHEN_NOT_CONNECTED;
            gap_connectable_control(headset.gap_headset_connectable);
            headset.connect = 1;
            break;
        default:
            headset.connect = 0;
            return;
    }
    headset_run();
}

void headset_disconnect(void){
    switch (headset.state){
        case BTSTACK_HEADSET_IDLE:
        case BTSTACK_HEADSET_W4_DISCONNECT:
            return;
        case BTSTACK_HEADSET_W4_TIMER:
            headset_auto_connect_timer_stop();
            headset.state = BTSTACK_HEADSET_IDLE;
            break;
        default:
            if (headset.con_handle == HCI_CON_HANDLE_INVALID) return;
            headset.disconnect = 1;
            headset_run();
            break;
    }
}

/*
 * Shutdown all established services, and handles pairing on incoming connection. Currently, only display modus is supported. 
 */
void headset_start_pairing_mode(void){
    main_state_summary();
    switch (headset.state){
        case BTSTACK_HEADSET_W4_TIMER:
            headset_auto_connect_timer_stop();
            headset.state = BTSTACK_HEADSET_IDLE;
            
            /* fall through */

        case BTSTACK_HEADSET_IDLE:
            headset.gap_headset_connectable  = HEADSET_CONNECTABLE_WHEN_NOT_CONNECTED;
            gap_connectable_control(headset.gap_headset_connectable);
            headset.gap_headset_discoverable = HEADSET_DISCOVERABLE_WHEN_NOT_CONNECTED;
            gap_discoverable_control(headset.gap_headset_discoverable);
            break;
        
        case BTSTACK_HEADSET_W4_DISCONNECT:
            // wait for disconnect
            break;
        
        default:
            if (headset.con_handle == HCI_CON_HANDLE_INVALID) break;
            headset.disconnect = 1;
            break;
    }
    headset.pairing_mode_enabled = 1;
    headset_run();
}

/*
 * Stop pairing modus, and auto-reconnect to a nearby known device if not already connected 
 */
void headset_stop_pairing_mode(void){
    headset.pairing_mode_enabled = 0;
    headset.gap_headset_discoverable = 0;
    gap_discoverable_control(headset.gap_headset_discoverable);
}

/*
 * Accept pin code from remote device for legacy pairing. Only works in pairing mode. 
 */
void headset_legacy_pairing_accept(void){
    if (headset.state != BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER){
        printf("Headset in a wrong state, expected %d, current %d\n", BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER, headset.state);
        return;
    }
    headset.state = BTSTACK_HEADSET_CONNECTED;
    gap_pin_code_response(headset.remote_device_addr, "0000");
    // delete device_addr
}

/*
 * Reject pin code from remote device (for legacy pairing). Only works in pairing mode. 
 */
void headset_legacy_pairing_reject(void){
    if (headset.state != BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER){
        printf("Headset in a wrong state, expected %d, current %d\n", BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER, headset.state);
        return;
    }
    headset.state = BTSTACK_HEADSET_INCOMING_AUTHENTICATION_REJECTED;
    gap_pin_code_negative(headset.remote_device_addr);
}

/*
 * Accept pass-key from remote device (SSP modus). Only works in pairing mode. 
 */
void headset_ssp_accept(void){
    if (headset.state != BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER){
        printf("Headset in a wrong state, expected %d, current %d\n", BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER, headset.state);
        return;
    }
    headset.state = BTSTACK_HEADSET_CONNECTED;
    gap_ssp_confirmation_response(headset.remote_device_addr);
}

/*
 * Reject pass-key from remote device (SSP modus). Only works in pairing mode. 
 */
void headset_ssp_reject(void){
    if (headset.state != BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER){
        printf("Headset in a wrong state, expected %d, current %d\n", BTSTACK_HEADSET_INCOMING_W4_AUTHENTICATION_ANSWER, headset.state);
        return;
    }
    headset.state = BTSTACK_HEADSET_INCOMING_AUTHENTICATION_REJECTED;
    gap_ssp_passkey_negative(headset.remote_device_addr);
}

/*
 * Forget remote device with given Bluetooth address. It will be excluded from auto-reconnect, 
 * and the subsequent incoming connection from the device will be rejected if headset is not in the pairing mode.
 *
 * @param remote_device_address Bluetooth address of remote device, i.e {0x04,0x0C,0xCE,0xE4,0x85,0xD3}
 */
void headset_forget_device(bd_addr_t remote_device_address){
    gap_drop_link_key_for_bd_addr(remote_device_address);
    if (memcmp(headset.last_connected_device, remote_device_address, BD_ADDR_LEN) == 0){
        memset(headset.last_connected_device, 0, BD_ADDR_LEN);
        headset.last_connected_device_valid = 0;
    }
    if (memcmp(headset.remote_device_addr, remote_device_address, BD_ADDR_LEN) == 0){
        if (headset.state == BTSTACK_HEADSET_W4_TIMER){
            memset(headset.remote_device_addr, 0, BD_ADDR_LEN);
            headset.remote_addr_valid = 0;
        }
    }
    log_summary("Link-key deleted.");
}

/*
 * Forget all known remote devices, i.e there is no remote device in the list for the auto-reconnect, 
 * all incoming connections will be rejected unless pairing mode is ON. 
 */
void headset_forget_all_devices(void){
    gap_delete_all_link_keys();
    memset(headset.last_connected_device, 0, BD_ADDR_LEN);
    memset(headset.last_connected_device, 0, BD_ADDR_LEN);
    headset.last_connected_device_valid = 0;

    if (headset.state == BTSTACK_HEADSET_W4_TIMER){
        memset(headset.remote_device_addr, 0, BD_ADDR_LEN);
        headset.remote_addr_valid = 0;
    }
    log_summary("Link-keys deleted, last known device deleted.");
}

static void show_usage(void){
    bd_addr_t      iut_address;
    gap_local_bd_addr(iut_address);
    printf("\n--- Bluetooth Headset Test Console %s ---\n", bd_addr_to_str(iut_address));
    printf("c      - Connect to remote with address addr %s\n", bd_addr_to_str(device_addr));
    printf("C      - Disconnect from remote with address addr %s\n", bd_addr_to_str(device_addr));
    printf("d      - Forget remote device with address %s\n", bd_addr_to_str(headset.last_connected_device));
    printf("D      - Forget all known remote devices\n");
    printf("p      - Start pairing mode\n");
    printf("P      - Stop pairing mode\n");

    printf("a      - Accept pin code\n");
    printf("A      - Reject pin code\n");
    printf("b      - Accept pin code\n");
    printf("B      - Reject pin code\n");
    printf("\n");
    printf("---\n");
}

static void stdin_process(char c){
    cmd = c;
    uint8_t status = ERROR_CODE_SUCCESS;
    
    switch (cmd){
        case 'c':
            log_summary("Connect.");
            headset_connect(device_addr);
            break;
        case 'C':
            log_summary("Disconnect.");
            headset_disconnect();
            break;
        case 'd':
            log_summary("Forget remote device");
            headset_forget_device(headset.last_connected_device);
            break;
        case 'D':
            log_summary("Forget all known remote devices.");
            headset_forget_all_devices();
            break;
        case 'p':
            log_summary("Start pairing mode.");
            headset_start_pairing_mode();
            break;
        case 'P':
            log_summary("Stop pairing mode.");
            headset_stop_pairing_mode();
            break;
        case 'a':
            log_summary("Accept legacy paring (pin code).");
            headset_legacy_pairing_accept();
            break;
        case 'A':
            log_summary("Reject legacy paring (pin code).");
            headset_legacy_pairing_reject();
            break;
        case 'b':
            log_summary("Accept Secure Simple Pairing (passkey).");
            headset_ssp_accept();
            break;
        case 'B':
            log_summary("Reject Secure Simple Pairing (passkey).");
            headset_ssp_reject();
            break;

        case '\n':
        case '\r':
            break;
        default:
            show_usage();
            return;
    }

    if (status != ERROR_CODE_SUCCESS){
        printf("Could not perform command \'%c\', status 0x%2x\n", cmd, status);
    }
}
#endif
int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){
    UNUSED(argc);
    (void)argv;
    
    sco_demo_init();

    l2cap_init();
    
    // init RFCOMM
    rfcomm_init();

    // init GOEP Client
    goep_client_init();

    // init PBAP Client
    pbap_client_init();

    // initialize HFP HF
    uint16_t hf_supported_features          =
        (1<<HFP_HFSF_ESCO_S4)               |
        (1<<HFP_HFSF_CLI_PRESENTATION_CAPABILITY) |
        (1<<HFP_HFSF_HF_INDICATORS)         |
        (1<<HFP_HFSF_CODEC_NEGOTIATION)     |
        (1<<HFP_HFSF_ENHANCED_CALL_STATUS)  |
        (1<<HFP_HFSF_REMOTE_VOLUME_CONTROL);

    int wide_band_speech = 1;
    hfp_hf_init(rfcomm_channel_nr);
    hfp_hf_init_supported_features(hf_supported_features);
    hfp_hf_init_hf_indicators(sizeof(indicators)/sizeof(uint16_t), indicators);
    hfp_hf_init_codecs(sizeof(codecs), codecs);
    hci_register_sco_packet_handler(&hfp_hf_packet_handler);

    // register for HFP events
    hfp_hf_register_packet_handler(&hfp_hf_packet_handler);

    // Initialize AVDTP Sink
    a2dp_sink_init();
    a2dp_sink_register_packet_handler(&a2dp_sink_packet_handler);
    a2dp_sink_register_media_handler(&handle_l2cap_media_data_packet);

    uint8_t status = a2dp_sink_create_stream_endpoint(AVDTP_AUDIO, AVDTP_CODEC_SBC, media_sbc_codec_capabilities, sizeof(media_sbc_codec_capabilities), media_sbc_codec_configuration, sizeof(media_sbc_codec_configuration), &local_seid);
    if (status != ERROR_CODE_SUCCESS){
        printf("A2DP Sink: not enough memory to create local stream endpoint\n");
        return 1;
    }
    // Initialize AVRCP COntroller
    avrcp_controller_init();
    avrcp_controller_register_packet_handler(&avrcp_controller_packet_handler);
    
    // Initialize SDP 
    sdp_init();

#ifdef ENABLE_A2DP
    // setup AVDTP sink
    memset(sdp_avdtp_sink_service_buffer, 0, sizeof(sdp_avdtp_sink_service_buffer));
    a2dp_sink_create_sdp_record(sdp_avdtp_sink_service_buffer, 0x10001, 1, NULL, NULL);
    sdp_register_service(sdp_avdtp_sink_service_buffer);

    // setup AVRCP
    memset(sdp_avrcp_controller_service_buffer, 0, sizeof(sdp_avrcp_controller_service_buffer));
    avrcp_controller_create_sdp_record(sdp_avrcp_controller_service_buffer, 0x10002, AVRCP_BROWSING_ENABLED, 1, NULL, NULL);
    sdp_register_service(sdp_avrcp_controller_service_buffer);
#endif

#ifdef ENABLE_HFP
    // setup HFP HF
    memset(hfp_service_buffer, 0, sizeof(hfp_service_buffer));
    hfp_hf_create_sdp_record(hfp_service_buffer, 0x10003, rfcomm_channel_nr, hfp_hf_service_name, hf_supported_features, wide_band_speech);
    sdp_register_service(hfp_service_buffer);
#endif

    gap_set_local_name("Headset Demo 00:00:00:00:00:00");
    gap_discoverable_control(1);
    gap_set_class_of_device(0x200408);

    // setup Display only
    gap_ssp_set_io_capability(SSP_IO_CAPABILITY_DISPLAY_YES_NO);
    gap_ssp_set_auto_accept(0);

#ifdef GAP_TEST_LEGACY_PAIRING
    gap_ssp_set_enable(0);
#else
    gap_ssp_set_enable(1);
#endif

    /* Register for HCI events */
    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

#ifdef HAVE_BTSTACK_STDIN
    select_phonebook(pb_name);
    // parse human readable Bluetooth address
    sscanf_bd_addr(device_addr_string, device_addr);
    btstack_stdin_setup(stdin_process);
#endif
    headset_init();

    // turn on!
    printf("Starting BTstack ...\n");
    hci_power_control(HCI_POWER_ON);
    return 0;
}
