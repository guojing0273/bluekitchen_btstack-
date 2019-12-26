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

#define BTSTACK_FILE__ "clock_client.c"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "btstack.h"

#ifdef HAVE_POSIX_TIME
#include <time.h>
#endif

static const char clock_server_name[] = "DEFCON Clock";
static const char authorization_password[] = "Toblerone";
static const uint32_t pairing_pin = 112233;

// prototypes
static void handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

typedef enum {
    TC_OFF,
    TC_IDLE,
    TC_W4_SCAN_RESULT,
    TC_W4_CONNECT,
    TC_W4_SERVICE_RESULT,
    TC_W4_CHARACTERISTIC_PASSWORD_RESULT,
    TC_W4_CHARACTERISTIC_TIME_RESULT,
    TC_W4_WRITE_PASSWORD_COMPLETE,
    TC_W4_WRITE_TIME_COMPLETE,
    TC_W4_DISCONNECTED,
} app_state_t;

// addr and type of device with correct name
static bd_addr_t      clock_server_addr;
static bd_addr_type_t clock_server_addr_type;

static hci_con_handle_t connection_handle;

// On the GATT Server, RX Characteristic is used for receive data via Write, and TX Characteristic is used to send data via Notifications
static uint8_t clock_service_uuid[16]  = {0x00, 0x00, 0xFF, 0x10, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB};
static uint8_t clock_password_uuid[16] = {0x00, 0x00, 0xFF, 0x11, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB};
static uint8_t clock_time_uuid[16] =     {0x00, 0x00, 0xFF, 0x12, 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0x80, 0x5F, 0x9B, 0x34, 0xFB};

static gatt_client_service_t clock_service;
static gatt_client_characteristic_t clock_password_characteristic;
static gatt_client_characteristic_t clock_time_characteristic;

static app_state_t state = TC_OFF;
static btstack_packet_callback_registration_t hci_event_callback_registration;
static btstack_packet_callback_registration_t sm_event_callback_registration;

static char time_value[20] = "12:42 Uhr";
static btstack_timer_source_t minute_timer;


// returns 1 if name is found in advertisement
static int advertisement_report_contains_name(const char * name, uint8_t * advertisement_report){
    // get advertisement from report event
    const uint8_t * adv_data = gap_event_advertising_report_get_data(advertisement_report);
    uint16_t        adv_len  = gap_event_advertising_report_get_data_length(advertisement_report);
    int             name_len = strlen(name);

    // iterate over advertisement data
    ad_context_t context;
    for (ad_iterator_init(&context, adv_len, adv_data) ; ad_iterator_has_more(&context) ; ad_iterator_next(&context)){
        uint8_t data_type    = ad_iterator_get_data_type(&context);
        uint8_t data_size    = ad_iterator_get_data_len(&context);
        const uint8_t * data = ad_iterator_get_data(&context);
        int i;
        switch (data_type){
            case BLUETOOTH_DATA_TYPE_SHORTENED_LOCAL_NAME:
            case BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME:
                // compare common prefix
                for (i=0; i<data_size && i<name_len;i++){
                    if (data[i] != name[i]) break;
                }
                // prefix match
                return 1;
            default:
                break;
        }
    }
    return 0;
}

static void handle_gatt_client_event(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(packet_type);
    UNUSED(channel);
    UNUSED(size);

    switch(state){
        case TC_W4_SERVICE_RESULT:
            switch(hci_event_packet_get_type(packet)){
                case GATT_EVENT_SERVICE_QUERY_RESULT:
                    // store service (we expect only one)
                    gatt_event_service_query_result_get_service(packet, &clock_service);
                    break;
                case GATT_EVENT_QUERY_COMPLETE:
                    if (packet[4] != 0){
                        printf("SERVICE_QUERY_RESULT - Error status %x.\n", packet[4]);
                        gap_disconnect(connection_handle);
                        break;  
                    } 
                    // service query complete, look for characteristic
                    state = TC_W4_CHARACTERISTIC_PASSWORD_RESULT;
                    printf("Search for characteristic A.\n");
                    gatt_client_discover_characteristics_for_service_by_uuid128(handle_gatt_client_event, connection_handle, &clock_service, clock_password_uuid);
                    break;
                default:
                    break;
            }
            break;
            
        case TC_W4_CHARACTERISTIC_PASSWORD_RESULT:
            switch(hci_event_packet_get_type(packet)){
                case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
                    gatt_event_characteristic_query_result_get_characteristic(packet, &clock_password_characteristic);
                    break;
                case GATT_EVENT_QUERY_COMPLETE:
                    if (packet[4] != 0){
                        printf("CHARACTERISTIC_QUERY_RESULT - Error status %x.\n", packet[4]);
                        gap_disconnect(connection_handle);
                        break;  
                    } 
                    // rx characteristiic found, look for tx characteristic
                    state = TC_W4_CHARACTERISTIC_TIME_RESULT;
                    printf("Search for characteristic B.\n");
                    gatt_client_discover_characteristics_for_service_by_uuid128(handle_gatt_client_event, connection_handle, &clock_service, clock_time_uuid);
                    break;
                default:
                    break;
            }
            break;

        case TC_W4_CHARACTERISTIC_TIME_RESULT:
            switch(hci_event_packet_get_type(packet)){
                case GATT_EVENT_CHARACTERISTIC_QUERY_RESULT:
                    gatt_event_characteristic_query_result_get_characteristic(packet, &clock_time_characteristic);
                    break;
                case GATT_EVENT_QUERY_COMPLETE:
                    if (packet[4] != 0){
                        printf("CHARACTERISTIC_QUERY_RESULT - Error status %x.\n", packet[4]);
                        gap_disconnect(connection_handle);
                        break;  
                    }
                    state = TC_W4_WRITE_PASSWORD_COMPLETE;
                    printf("Authorize!\n");
                    gatt_client_write_value_of_characteristic(handle_gatt_client_event, connection_handle, clock_password_characteristic.value_handle, strlen(authorization_password), (uint8_t *) authorization_password);
                    break;
                default:
                    break;
            }
            break;

        case TC_W4_WRITE_PASSWORD_COMPLETE:
            switch(hci_event_packet_get_type(packet)){
                case GATT_EVENT_QUERY_COMPLETE:
                    if (packet[4] != 0){
                        printf("CHARACTERISTIC_QUERY_RESULT - Error status %x.\n", packet[4]);
                        gap_disconnect(connection_handle);
                        break;
                    }
                    state = TC_W4_WRITE_TIME_COMPLETE;
                    printf("Set time\n");
#ifdef HAVE_POSIX_TIME
                    {
                        time_t rawtime;
                        struct tm *timeinfo;
                        time(&rawtime);
                        timeinfo = localtime(&rawtime);
                        sprintf(time_value, "%02d:%02d\n", timeinfo->tm_hour, timeinfo->tm_min);
                    }
#endif
                    gatt_client_write_value_of_characteristic(handle_gatt_client_event, connection_handle, clock_time_characteristic.value_handle, strlen(time_value), (uint8_t *) time_value);
                    break;
                default:
                    break;
            }
            break;

        case TC_W4_WRITE_TIME_COMPLETE:
            switch(hci_event_packet_get_type(packet)){
                case GATT_EVENT_QUERY_COMPLETE:
                    state = TC_W4_DISCONNECTED;
                    printf("Disconnect\n");
                    gap_disconnect(connection_handle);
                    break;
                default:
                    break;
            }
            break;

        default:
            printf("error\n");
            break;
    }
    
}

// Either connect to remote specified on command line or start scan for device with "LE Streamer" in advertisement
static void clock_client_start(btstack_timer_source_t * ts){
    UNUSED(ts);
    printf("Start scanning!\n");
    state = TC_W4_SCAN_RESULT;
    gap_set_scan_parameters(0,0x0030, 0x0030);
    gap_start_scan();
}

static void hci_event_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);

    if (packet_type != HCI_EVENT_PACKET) return;
    
    uint8_t event = hci_event_packet_get_type(packet);
    switch (event) {
        case BTSTACK_EVENT_STATE:
            // BTstack activated, get started
            if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
                clock_client_start(NULL);
            } else {
                state = TC_OFF;
            }
            break;
        case GAP_EVENT_ADVERTISING_REPORT:
            if (state != TC_W4_SCAN_RESULT) return;
            // check name in advertisement
            if (!advertisement_report_contains_name(clock_server_name, packet)) return;
            // store address and type
            gap_event_advertising_report_get_address(packet, clock_server_addr);
            clock_server_addr_type = gap_event_advertising_report_get_address_type(packet);
            // stop scanning, and connect to the device
            state = TC_W4_CONNECT;
            gap_stop_scan();
            printf("Stop scan. Connect to device with addr %s.\n", bd_addr_to_str(clock_server_addr));
            gap_connect(clock_server_addr, clock_server_addr_type);
            break;
        case HCI_EVENT_LE_META:
            // wait for connection complete
            if (hci_event_le_meta_get_subevent_code(packet) !=  HCI_SUBEVENT_LE_CONNECTION_COMPLETE) break;
            if (state != TC_W4_CONNECT) return;
            connection_handle = hci_subevent_le_connection_complete_get_connection_handle(packet);
            // initialize gatt client context with handle, and add it to the list of active clients
            // query primary services
            printf("Search for Clock service.\n");
            state = TC_W4_SERVICE_RESULT;
            gatt_client_discover_primary_services_by_uuid128(handle_gatt_client_event, connection_handle, clock_service_uuid);
            break;
        case HCI_EVENT_DISCONNECTION_COMPLETE:
            // unregister listener
            connection_handle = HCI_CON_HANDLE_INVALID;
            printf("Disconnected %s\n", bd_addr_to_str(clock_server_addr));
            if (state == TC_W4_DISCONNECTED) break;
            // set timer
            btstack_run_loop_set_timer_handler(&minute_timer, &clock_client_start);
            btstack_run_loop_set_timer(&minute_timer, 50000);
            btstack_run_loop_add_timer(&minute_timer);
            break;
        case SM_EVENT_JUST_WORKS_REQUEST:
            printf("Just works requested\n");
            sm_just_works_confirm(sm_event_just_works_request_get_handle(packet));
            break;
        case SM_EVENT_NUMERIC_COMPARISON_REQUEST:
            printf("Confirming numeric comparison: %"PRIu32"\n", sm_event_numeric_comparison_request_get_passkey(packet));
            sm_numeric_comparison_confirm(sm_event_passkey_display_number_get_handle(packet));
            break;
        case SM_EVENT_PASSKEY_DISPLAY_NUMBER:
            printf("Display Passkey: %"PRIu32"\n", sm_event_passkey_display_number_get_passkey(packet));
            break;
        case SM_EVENT_PASSKEY_INPUT_NUMBER:
            printf("Passkey Input requested\n");
            printf("Sending fixed passkey %"PRIu32"\n", pairing_pin);
            sm_passkey_input(sm_event_passkey_input_number_get_handle(packet), pairing_pin);
            break;
        default:
            break;
    }
}

int btstack_main(int argc, const char * argv[]);
int btstack_main(int argc, const char * argv[]){

    (void)argc;
    (void)argv;

    l2cap_init();

    sm_init();
    sm_set_io_capabilities(IO_CAPABILITY_KEYBOARD_ONLY);
    sm_set_authentication_requirements(SM_AUTHREQ_MITM_PROTECTION);

    // sm_init needed before gatt_client_init
    gatt_client_init();

    hci_event_callback_registration.callback = &hci_event_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    sm_event_callback_registration.callback = &hci_event_handler;
    sm_add_event_handler(&sm_event_callback_registration);

    // turn on!
    hci_power_control(HCI_POWER_ON);

    return 0;
}
/* EXAMPLE_END */
