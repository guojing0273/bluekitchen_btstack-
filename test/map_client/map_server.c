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

#define __BTSTACK_FILE__ "map_server.c"
 
#include "btstack_config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hci_cmd.h"
#include "btstack_run_loop.h"
#include "btstack_debug.h"
#include "hci.h"
#include "btstack_memory.h"
#include "hci_dump.h"
#include "l2cap.h"
#include "bluetooth_sdp.h"
#include "classic/sdp_client_rfcomm.h"
#include "btstack_event.h"
#include "classic/sdp_client.h"
#include "classic/sdp_util.h"

#include "classic/obex.h"
#include "classic/obex_iterator.h"
#include "classic/goep_client.h"
#include "goep_server.h"
#include "map_server.h"

#define MAP_MAX_NUM_ENTRIES 1024

static const uint8_t map_client_notification_service_uuid[] = {0xbb, 0x58, 0x2b, 0x41, 0x42, 0xc, 0x11, 0xdb, 0xb0, 0xde, 0x8, 0x0, 0x20, 0xc, 0x9a, 0x66};
static int rfcomm_channel_nr = 1;
// static bd_addr_t    remote_addr;
static uint16_t rfcomm_channel_id;   
// map access service bb582b40-420c-11db-b0de-0800200c9a66

typedef enum {
    MAP_INIT = 0,
    MAP_CONNECTED
} map_state_t;

typedef struct map_server {
    map_state_t state;
    uint16_t  cid;
    bd_addr_t bd_addr;
    hci_con_handle_t con_handle;
    uint16_t  goep_cid;
} map_server_t;


static map_server_t _map_server;
static map_server_t * map_server = &_map_server;

static void map_create_sdp_record(uint8_t * service, uint32_t service_record_handle, uint16_t service_uuid, uint8_t instance_id,
    int channel_nr, uint16_t goep_l2cap_psm, map_message_type_t supported_message_types, uint32_t supported_features, const char * name){
    UNUSED(goep_l2cap_psm);
    uint8_t* attribute;
    de_create_sequence(service);

    // 0x0000 "Service Record Handle"
    de_add_number(service, DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_SERVICE_RECORD_HANDLE);
    de_add_number(service, DE_UINT, DE_SIZE_32, service_record_handle);

    // 0x0001 "Service Class ID List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_SERVICE_CLASS_ID_LIST);
    attribute = de_push_sequence(service);
    {
        //  "UUID for Service"
        de_add_number(attribute, DE_UUID, DE_SIZE_16, service_uuid);
    }
    de_pop_sequence(service, attribute);

    // 0x0004 "Protocol Descriptor List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_PROTOCOL_DESCRIPTOR_LIST);
    attribute = de_push_sequence(service);
    {
        uint8_t* l2cpProtocol = de_push_sequence(attribute);
        {
            de_add_number(l2cpProtocol,  DE_UUID, DE_SIZE_16, BLUETOOTH_PROTOCOL_L2CAP);
        }
        de_pop_sequence(attribute, l2cpProtocol);
        
        uint8_t* rfcomm = de_push_sequence(attribute);
        {
            de_add_number(rfcomm,  DE_UUID, DE_SIZE_16, BLUETOOTH_PROTOCOL_RFCOMM);  // rfcomm_service
            de_add_number(rfcomm,  DE_UINT, DE_SIZE_8,  channel_nr);  // rfcomm channel
        }
        de_pop_sequence(attribute, rfcomm);

        uint8_t* obexProtocol = de_push_sequence(attribute);
        {
            de_add_number(obexProtocol,  DE_UUID, DE_SIZE_16, BLUETOOTH_PROTOCOL_OBEX);
        }
        de_pop_sequence(attribute, obexProtocol);
        
    }
    de_pop_sequence(service, attribute);

    
    // 0x0005 "Public Browse Group"
    de_add_number(service,  DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_BROWSE_GROUP_LIST); // public browse group
    attribute = de_push_sequence(service);
    {
        de_add_number(attribute,  DE_UUID, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_PUBLIC_BROWSE_ROOT);
    }
    de_pop_sequence(service, attribute);

    // 0x0009 "Bluetooth Profile Descriptor List"
    de_add_number(service,  DE_UINT, DE_SIZE_16, BLUETOOTH_ATTRIBUTE_BLUETOOTH_PROFILE_DESCRIPTOR_LIST);
    attribute = de_push_sequence(service);
    {
        uint8_t *profile = de_push_sequence(attribute);
        {
            de_add_number(profile,  DE_UUID, DE_SIZE_16, BLUETOOTH_SERVICE_CLASS_MESSAGE_ACCESS_PROFILE); 
            de_add_number(profile,  DE_UINT, DE_SIZE_16, 0x0103); // Verision 1.7
        }
        de_pop_sequence(attribute, profile);
    }
    de_pop_sequence(service, attribute);

    // "Service Name"
    de_add_number(service, DE_UINT, DE_SIZE_16, 0x0100);
    de_add_data(service,   DE_STRING, strlen(name), (uint8_t *) name);

#ifdef ENABLE_GOEP_L2CAP
    // 0x0200 "GoepL2CapPsm"
    de_add_number(service, DE_UINT, DE_SIZE_16, 0x0200);
    de_add_number(service, DE_UINT, DE_SIZE_16, goep_l2cap_psm);
#endif

    // 0x0315 "MASInstanceID"
    de_add_number(service, DE_UINT, DE_SIZE_16, 0x0315);
    de_add_number(service, DE_UINT, DE_SIZE_8, instance_id);
    
    // 0x0316 "SupportedMessageTypes"
    de_add_number(service, DE_UINT, DE_SIZE_16, 0x0316);
    de_add_number(service, DE_UINT, DE_SIZE_8, supported_message_types);
    
    // 0x0317 "MapSupportedFeatures"
    de_add_number(service, DE_UINT, DE_SIZE_16, 0x0317);
    de_add_number(service, DE_UINT, DE_SIZE_32, supported_features);
}

void map_message_access_service_create_sdp_record(uint8_t * service, uint32_t service_record_handle, uint8_t instance_id,
    int channel_nr, uint16_t goep_l2cap_psm, map_message_type_t supported_message_types, uint32_t supported_features, const char * name){
    map_create_sdp_record(service, service_record_handle, BLUETOOTH_SERVICE_CLASS_MESSAGE_ACCESS_SERVER, instance_id, channel_nr,
        goep_l2cap_psm, supported_message_types, supported_features, name);
}

void map_message_notification_service_create_sdp_record(uint8_t * service, uint32_t service_record_handle, uint8_t instance_id,
    int channel_nr, uint16_t goep_l2cap_psm, map_message_type_t supported_message_types, uint32_t supported_features, const char * name){
    map_create_sdp_record(service, service_record_handle, BLUETOOTH_SERVICE_CLASS_MESSAGE_NOTIFICATION_SERVER, instance_id, channel_nr,
        goep_l2cap_psm, supported_message_types, supported_features, name);
}

static void obex_server_success_response(uint16_t rfcomm_cid){
    uint8_t event[30];
    int pos = 0;
    event[pos++] = OBEX_RESP_SUCCESS;
    // store len
    pos += 2;
    // obex version num
    event[pos++] = OBEX_VERSION;
    // flags
    // Bit 0 should be used by the receiving client to decide how to multiplex operations 
    // to the server (should it desire to do so). If the bit is 0 the client should serialize 
    // the operations over a single TTP connection. If the bit is set the client is free to 
    // establish multiple TTP connections to the server and concurrently exchange its objects.
    event[pos++] = 0;
    
    // Maximum OBEX packet length
    big_endian_store_16(event, pos, 0x0400);
    pos += 2;
    
    event[pos++] = OBEX_HEADER_CONNECTION_ID;
    big_endian_store_32(event, pos, 0x1234); 
    pos += 4;

    event[pos++] = OBEX_HEADER_WHO;
    big_endian_store_16(event, pos, 16 + 3);
    pos += 2;
    memcpy(event+pos, map_client_notification_service_uuid, 16);
    pos += 16;

    big_endian_store_16(event, 1, pos);
    rfcomm_send(rfcomm_cid, event, pos);
}

static void map_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel); 
    UNUSED(size);    
    printf("map_packet_handler: packet_type 0x%02x, event type 0x%02x, subevent 0x%02x\n", packet_type, hci_event_packet_get_type(packet), hci_event_goep_meta_get_subevent_code(packet));
    int i;
    switch (packet_type){
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case HCI_EVENT_GOEP_META:
                    switch (hci_event_goep_meta_get_subevent_code(packet)){
                        case GOEP_SUBEVENT_CONNECTION_OPENED:
                            printf("connection opened\n");
                            break;
                        case GOEP_SUBEVENT_CONNECTION_CLOSED:
                            printf("connection closed\n");
                            break;
                        default:
                            break;
                    }
                default:
                    break;
            }
            break;
        case RFCOMM_DATA_PACKET:
            printf("MAP server - RFCOMM data packet: '");
            for (i=0;i<size;i++){
                printf("%02x ", packet[i]);
            }
            printf("'\n"); 
            obex_server_success_response(rfcomm_channel_id);
            break;

        default:
            break;
    }
}

void map_server_init(void){
    memset(map_server, 0, sizeof(map_server_t));
    map_server->state = MAP_INIT;
    map_server->cid = 1;

    goep_server_register_service(&map_packet_handler, rfcomm_channel_nr, 0xFFFF, 0, 0xFFFF, LEVEL_0);
}
