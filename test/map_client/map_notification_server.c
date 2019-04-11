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

#define __BTSTACK_FILE__ "map_notification_server.c"
 
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
#include "classic/obex_message_builder.h"
#include "classic/goep_client.h"
#include "map_util.h"
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
    MAP_W2_SEND_CONNECTION_STATUS,
    MAP_CONNECTED
} map_state_t;

typedef struct map_server {
    map_state_t state;
    uint16_t  cid;
    bd_addr_t bd_addr;
    hci_con_handle_t con_handle;
    uint16_t  goep_cid;
    btstack_packet_handler_t callback;

    uint16_t maximum_obex_packet_length;
    uint8_t  flags;
} map_server_t;


static map_server_t _map_server;
static map_server_t * map_server = &_map_server;
static uint16_t maximum_obex_packet_length;

void map_notification_server_register_packet_handler(btstack_packet_handler_t callback){
    if (callback == NULL){
        log_error("map_server_register_packet_handler called with NULL callback");
        return;
    }
    map_server->callback = callback;
}

void map_notification_server_create_sdp_record(uint8_t * service, uint32_t service_record_handle, uint8_t instance_id,
    int channel_nr, uint16_t goep_l2cap_psm, map_message_type_t supported_message_types, uint32_t supported_features, const char * name){
    map_create_sdp_record(service, service_record_handle, BLUETOOTH_SERVICE_CLASS_MESSAGE_NOTIFICATION_SERVER, instance_id, channel_nr,
        goep_l2cap_psm, supported_message_types, supported_features, name);
}

static void map_server_emit_connected_event(map_server_t * context, uint8_t status){
    uint8_t event[16];
    int pos = 0;
    event[pos++] = HCI_EVENT_MAP_META;
    pos++;  // skip len
    event[pos++] = MAP_SUBEVENT_CONNECTION_OPENED;
    little_endian_store_16(event,pos,context->cid);
    pos+=2;
    event[pos++] = status;
    memcpy(&event[pos], context->bd_addr, 6);
    pos += 6;
    little_endian_store_16(event,pos,context->con_handle);
    pos += 2;
    event[pos++] = 1;
    event[pos++] = MAP_MESSAGE_NOTIFICATION_SERVICE;
    event[1] = pos - 2;
    if (pos != sizeof(event)) log_error("map_client_emit_connected_event size %u", pos);
    context->callback(HCI_EVENT_PACKET, context->cid, &event[0], pos);
}  

static void map_server_emit_connection_closed_event(map_server_t * context){
    uint8_t event[6];
    int pos = 0;
    event[pos++] = HCI_EVENT_MAP_META;
    pos++;  // skip len
    event[pos++] = MAP_SUBEVENT_CONNECTION_CLOSED;
    little_endian_store_16(event,pos,context->cid);
    pos+=2;
    event[pos++] = MAP_MESSAGE_NOTIFICATION_SERVICE;
    event[1] = pos - 2;
    if (pos != sizeof(event)) log_error("map_client_emit_connection_closed_event size %u", pos);
    context->callback(HCI_EVENT_PACKET, context->cid, &event[0], pos);
}   

static void obex_server_success_response(uint16_t rfcomm_cid){
    uint8_t event[30];
    obex_message_builder_response_create_connect(event, sizeof(event), OBEX_VERSION, 0, map_server->maximum_obex_packet_length , 0x1234);
    obex_message_builder_header_add_who(event, sizeof(event), map_client_notification_service_uuid);
    rfcomm_send(rfcomm_cid, event, obex_message_builder_get_message_length(event));
}

static uint8_t goep_data_packet_get_opcode(uint8_t *packet){
    return packet[0];
}

static void map_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel); 
    UNUSED(size);    
    // printf("map_packet_handler: packet_type 0x%02x, event type 0x%02x, subevent 0x%02x\n", packet_type, hci_event_packet_get_type(packet), hci_event_goep_meta_get_subevent_code(packet));
    int i;
    uint8_t status;

    switch (packet_type){
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case HCI_EVENT_GOEP_META:
                    switch (hci_event_goep_meta_get_subevent_code(packet)){
                        case GOEP_SUBEVENT_CONNECTION_OPENED:
                            if (map_server->state != MAP_INIT) return;
                            status = goep_subevent_connection_opened_get_status(packet);
                            map_server->con_handle = goep_subevent_connection_opened_get_con_handle(packet);
                            goep_subevent_connection_opened_get_bd_addr(packet, map_server->bd_addr); 
                            if (status){
                                log_info("MAP notification server: connection failed %u", status);
                                map_server->state = MAP_INIT;
                                map_server_emit_connected_event(map_server, status);
                                break;
                            } 
                            log_info("MAP notification server: connection established");
                            map_server->goep_cid = goep_subevent_connection_opened_get_goep_cid(packet);
                            map_server->state = MAP_CONNECTED;
                            map_server_emit_connected_event(map_server, status);
                            break;
                        case GOEP_SUBEVENT_CONNECTION_CLOSED:
                            if (map_server->state != MAP_CONNECTED) break;
                            log_info("MAP notification server: connection closed");
                            map_server->state = MAP_INIT;
                            map_server_emit_connection_closed_event(map_server);
                            break;
                        case GOEP_SUBEVENT_CAN_SEND_NOW:
                            switch (map_server->state){
                                case MAP_W2_SEND_CONNECTION_STATUS:
                                    map_server->state = MAP_CONNECTED;
                                    obex_server_success_response(rfcomm_channel_id);
                                    break;
                                default:
                                    break;
                            }
                            break;
                        default:
                            break;
                    }
                default:
                    break;
            }
            break;
        case GOEP_DATA_PACKET:
            if (map_server->state != MAP_CONNECTED) return;
            
            if (size < 3) break;
            switch (goep_data_packet_get_opcode(packet)){
                case OBEX_OPCODE_CONNECT:
                    map_server->state = MAP_W2_SEND_CONNECTION_STATUS;
                    map_server->flags = packet[2];
                    map_server->maximum_obex_packet_length = btstack_min(maximum_obex_packet_length, big_endian_read_16(packet, 3));
                    break;
                // case OBEX_OPCODE_ABORT:
                // case OBEX_OPCODE_DISCONNECT:
                // case OBEX_OPCODE_PUT:
                // case OBEX_OPCODE_GET:
                // case OBEX_OPCODE_SETPATH:
                default:
                    printf("MAP server: GOEP data packet'");
                    for (i=0;i<size;i++){
                        printf("%02x ", packet[i]);
                    }
                    printf("'\n"); 
                    return;
            }
            goep_server_request_can_send_now(channel, GOEP_RFCOMM_CONNECTION);
            break;
        
        default:
            break;
    }
}

void map_notification_server_init(uint16_t mtu){
    memset(map_server, 0, sizeof(map_server_t));
    map_server->state = MAP_INIT;
    map_server->cid = 1;
    maximum_obex_packet_length = mtu;
    goep_server_register_service(&map_packet_handler, rfcomm_channel_nr, 0xFFFF, 0, 0xFFFF, LEVEL_0);
}
