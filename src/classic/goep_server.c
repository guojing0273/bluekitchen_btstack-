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

#define __BTSTACK_FILE__ "goep_server.c"
 
#include "btstack_config.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hci_cmd.h"
#include "btstack_run_loop.h"
#include "btstack_debug.h"
#include "btstack_defines.h"
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
#include "classic/goep_server.h"

static btstack_linked_list_t goep_server_connections = NULL;
static btstack_linked_list_t goep_server_services = NULL;
static uint16_t goep_cid_counter = 0;

static goep_server_service_t * goep_server_get_service_for_rfcomm_channel(uint8_t rfcomm_channel){
    btstack_linked_item_t *it;
    for (it = (btstack_linked_item_t *) goep_server_services; it ; it = it->next){
        goep_server_service_t * service = ((goep_server_service_t *) it);
        if (service->rfcomm_channel == rfcomm_channel){
            return service;
        };
    }
    return NULL;
}

static goep_server_service_t * goep_server_get_service_for_l2cap_psm(uint16_t l2cap_psm){
    btstack_linked_item_t *it;
    for (it = (btstack_linked_item_t *) goep_server_services; it ; it = it->next){
        goep_server_service_t * service = ((goep_server_service_t *) it);
        if (service->l2cap_psm == l2cap_psm){
            return service;
        };
    }
    return NULL;
}

static goep_server_connection_t * goep_server_get_connection_for_rfcomm_cid(uint16_t bearer_cid){
    btstack_linked_item_t *it;
    for (it = (btstack_linked_item_t *) goep_server_connections; it ; it = it->next){
        goep_server_connection_t * connection = ((goep_server_connection_t *) it);
        if (connection->type != GOEP_RFCOMM_CONNECTION) continue;
        if (connection->bearer_cid == bearer_cid){
            return connection;
        };
    }
    return NULL;
}

static goep_server_connection_t * goep_server_get_connection_for_l2cap_cid(uint16_t bearer_cid){
    btstack_linked_item_t *it;
    for (it = (btstack_linked_item_t *) goep_server_connections; it ; it = it->next){
        goep_server_connection_t * connection = ((goep_server_connection_t *) it);
        if (connection->type != GOEP_L2CAP_CONNECTION) continue;
        if (connection->bearer_cid == bearer_cid){
            return connection;
        };
    }
    return NULL;
}

static uint16_t goep_server_get_next_goep_cid(void){
    goep_cid_counter++;
    if (goep_cid_counter == 0){
        goep_cid_counter = 1;
    }
    return goep_cid_counter;
}

static inline void goep_server_emit_connection_opened_event(goep_server_connection_t * connection, bd_addr_t bd_addr, hci_con_handle_t con_handle){
    if (!connection || !connection->service || !connection->service->callback) return;
    uint8_t event[15];
    int pos = 0;
    event[pos++] = HCI_EVENT_GOEP_META;
    pos++;  // skip len
    event[pos++] = GOEP_SUBEVENT_CONNECTION_OPENED;
    little_endian_store_16(event,pos,connection->goep_cid);
    pos+=2;
    event[pos++] = ERROR_CODE_SUCCESS;
    memcpy(&event[pos], bd_addr, 6);
    pos += 6;
    little_endian_store_16(event,pos,con_handle);
    pos += 2;
    event[pos++] = 1;
    event[1] = pos - 2;
    if (pos != sizeof(event)) log_error("goep_server_emit_connection_opened_event size %u", pos);
    connection->service->callback(HCI_EVENT_PACKET, connection->goep_cid, &event[0], pos);
}   

static inline void goep_server_emit_connection_closed_event(goep_server_connection_t * connection){
    if (!connection || !connection->service || !connection->service->callback) return;
    
    uint8_t event[5];
    int pos = 0;
    event[pos++] = HCI_EVENT_GOEP_META;
    pos++;  // skip len
    event[pos++] = GOEP_SUBEVENT_CONNECTION_CLOSED;
    little_endian_store_16(event,pos,connection->goep_cid);
    pos+=2;
    event[1] = pos - 2;
    if (pos != sizeof(event)) log_error("goep_server_emit_connection_closed_event size %u", pos);
    connection->service->callback(HCI_EVENT_PACKET, connection->goep_cid, &event[0], pos);
} 

static void goep_server_packet_handler_l2cap(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel); 
    UNUSED(size);    
    printf("packet_type 0x%02x, event type 0x%02x, subevent 0x%02x\n", packet_type, hci_event_packet_get_type(packet), hci_event_goep_meta_get_subevent_code(packet));
    // on connection opened create  goep_server_connection_t
}

static void goep_server_packet_handler_rfcomm(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel); 
    UNUSED(size);    
    // printf("goep packet_type 0x%02x, event type 0x%02x, subevent 0x%02x\n", packet_type, hci_event_packet_get_type(packet), hci_event_goep_meta_get_subevent_code(packet));
    // on connection opened create  goep_server_connection_t
    // forward goep message to 

    bd_addr_t event_addr;
    uint8_t status;
    uint8_t  rfcomm_channel;
    uint16_t rfcomm_cid;
    goep_server_service_t    * goep_service = NULL;
    goep_server_connection_t * goep_connection = NULL;
    
    log_debug("GOEP server packet_handler type %u, event type %x, size %u", packet_type, hci_event_packet_get_type(packet), size);
    
    switch (packet_type){
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case RFCOMM_EVENT_INCOMING_CONNECTION:
                    // data: event (8), len(8), address(48), channel (8), rfcomm_cid (16)
                    rfcomm_channel = rfcomm_event_incoming_connection_get_server_channel(packet); 
                    rfcomm_cid = rfcomm_event_incoming_connection_get_rfcomm_cid(packet);

                    goep_service = goep_server_get_service_for_rfcomm_channel(rfcomm_channel);
                    if (!goep_service){
                        // reject
                        log_info("goep: no service for rfcomm channel 0x%02x - decline", rfcomm_channel);
                        rfcomm_decline_connection(rfcomm_cid);
                        return;
                    }
                    
                    // alloc structure
                    goep_connection = btstack_memory_goep_server_connection_get();
                    if (!goep_connection){
                        log_info("goep: no memory to create goep connection - decline");
                        rfcomm_decline_connection(rfcomm_cid);
                        return;   
                    }
                    // printf("Accept incoming connection for RFCOMM Channel ID 0x%02X\n", rfcomm_cid);
                    goep_connection->bearer_cid = rfcomm_cid;
                    goep_connection->service = goep_service;
                    goep_connection->type = GOEP_RFCOMM_CONNECTION;
                    goep_connection->state = GOEP_SERVER_W4_RFCOMM_CONNECTED;
                    btstack_linked_list_add(&goep_server_connections, (btstack_linked_item_t *) goep_connection);
                    rfcomm_accept_connection(rfcomm_cid);
                    break;

                case RFCOMM_EVENT_CHANNEL_OPENED:
                    // data: event(8), len(8), status (8), address (48), handle(16), server channel(8), rfcomm_cid(16), max frame size(16)
                    rfcomm_cid = rfcomm_event_channel_opened_get_rfcomm_cid(packet); 
                    goep_connection = goep_server_get_connection_for_rfcomm_cid(rfcomm_cid);

                    if (!goep_connection){
                        log_info("RFCOMM channel open failed. No connection for RFCOMM Channel IDRFCOMM Channel ID  0x%02x", rfcomm_cid);
                        return;
                    }
                    if (goep_connection->state != GOEP_SERVER_W4_RFCOMM_CONNECTED) {
                        log_info("RFCOMM channel open failed. Connection in wrong state %d", goep_connection->state);
                        return;
                    }

                    status = rfcomm_event_channel_opened_get_status(packet);          
                    if (status != ERROR_CODE_SUCCESS) {
                        printf("RFCOMM channel open failed. RFCOMM Channel ID 0x%02x, status 0x%02x\n", rfcomm_cid, status);
                        btstack_linked_list_remove(&goep_server_connections, (btstack_linked_item_t *) goep_connection);
                        btstack_memory_goep_server_connection_free(goep_connection);
                        return;
                    }

                    goep_connection->goep_cid = goep_server_get_next_goep_cid();
                    goep_connection->state = GOEP_SERVER_RFCOMM_CONNECTED;
                    log_info("RFCOMM channel open succeeded. GOEP Connection %p, RFCOMM Channel ID 0x%02x, GOEP CID 0x%02x", goep_connection, rfcomm_cid, goep_connection->goep_cid);
                    
                    rfcomm_event_channel_opened_get_bd_addr(event_addr, packet);
                    goep_server_emit_connection_opened_event(goep_connection, event_addr, rfcomm_event_channel_opened_get_con_handle(packet));
                    break;

                case RFCOMM_EVENT_CHANNEL_CLOSED:
                    rfcomm_cid = little_endian_read_16(packet,2);
                    goep_connection = goep_server_get_connection_for_rfcomm_cid(rfcomm_cid);
                    if (!goep_connection) break;

                    log_info("RFCOMM channel closed. RFCOMM Channel ID 0x%02x, GOEP CID 0x%02x", rfcomm_cid, goep_connection->goep_cid);
                    goep_server_emit_connection_closed_event(goep_connection);
                    btstack_linked_list_remove(&goep_server_connections, (btstack_linked_item_t *) goep_connection);
                    btstack_memory_goep_server_connection_free(goep_connection);
                    break;
                 default:
                    break;
            }
            break;
        case RFCOMM_DATA_PACKET:
            goep_connection = goep_server_get_connection_for_rfcomm_cid(channel);
            if (!goep_connection || !goep_connection->service || !goep_connection->service->callback) break;
            goep_connection->service->callback(GOEP_DATA_PACKET, goep_connection->goep_cid, packet, size);
            break;

        default:
            break;
    }
}

uint8_t goep_server_register_service(btstack_packet_handler_t callback, uint8_t rfcomm_channel, uint16_t rfcomm_max_frame_size,
                uint16_t l2cap_psm, uint16_t l2cap_mtu, gap_security_level_t security_level){

    log_info("rfcomm_channel 0x%02x rfcomm_max_frame_size %u l2cap_psm 0x%02x l2cap_mtu %u",
             rfcomm_channel, rfcomm_max_frame_size, l2cap_psm, l2cap_mtu);

    // check if service is already registered
    goep_server_service_t * service = goep_server_get_service_for_rfcomm_channel(rfcomm_channel);
    if (service) return RFCOMM_CHANNEL_ALREADY_REGISTERED;

    if (l2cap_psm){
        service = goep_server_get_service_for_l2cap_psm(l2cap_psm);
        if (service) return L2CAP_SERVICE_ALREADY_REGISTERED;
    } 

    // alloc structure
    service = btstack_memory_goep_server_service_get();
    if (!service) return BTSTACK_MEMORY_ALLOC_FAILED;

    // fill in 
    service->callback = callback;
    service->rfcomm_channel = rfcomm_channel;
    service->l2cap_psm = l2cap_psm;

    // register with RFCOMM 
    rfcomm_register_service(goep_server_packet_handler_rfcomm, rfcomm_channel, rfcomm_max_frame_size);
    if (l2cap_psm){
        l2cap_register_service(goep_server_packet_handler_l2cap, l2cap_psm, l2cap_mtu, security_level);
    }
    // add to services list
    btstack_linked_list_add(&goep_server_services, (btstack_linked_item_t *) service);
    return ERROR_CODE_SUCCESS;
}

void goep_server_init(void){

}
