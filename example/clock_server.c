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

#define BTSTACK_FILE__ "clock_server.c"


#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "clock_server.h"
#include "btstack.h"

static const char authorization_password[] = "Toblerone";
static const uint32_t pairing_pin = 112233;

/* LISTING_START(MainConfiguration): Init L2CAP SM ATT Server and start heartbeat timer */
static btstack_packet_callback_registration_t hci_event_callback_registration;
static bool authorized = false;
static char clock_value[100];
static uint16_t clock_value_len;

static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static uint16_t att_read_callback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size);
static int att_write_callback(hci_con_handle_t con_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size);

const uint8_t adv_data[] = {
    // Flags general discoverable, BR/EDR not supported
    2, BLUETOOTH_DATA_TYPE_FLAGS, 0x06,
    // Name
    13, BLUETOOTH_DATA_TYPE_COMPLETE_LOCAL_NAME, 'D', 'E', 'F', 'C', 'O', 'N', ' ', 'C', 'l', 'o', 'c', 'k',
    // Incomplete List of 16-bit Service Class UUIDs -- FF10 - only valid for testing!
    3, BLUETOOTH_DATA_TYPE_INCOMPLETE_LIST_OF_16_BIT_SERVICE_CLASS_UUIDS, 0x10, 0xff,
};
const uint8_t adv_data_len = sizeof(adv_data);

static void clock_setup(void){

    l2cap_init();

    // setup le device db
    le_device_db_init();

    // setup SM: Display only
    sm_init();

    // setup ATT server
    att_server_init(profile_data, att_read_callback, att_write_callback);

    // LE Legacy Pairing, Passkey entry initiator enter, responder (us) displays
    sm_set_io_capabilities(IO_CAPABILITY_DISPLAY_ONLY);
    sm_set_authentication_requirements(SM_AUTHREQ_MITM_PROTECTION);
    sm_use_fixed_passkey_in_display_role(pairing_pin);

    // setup advertisements
    uint16_t adv_int_min = 0x0030;
    uint16_t adv_int_max = 0x0030;
    uint8_t adv_type = 0;
    bd_addr_t null_addr;
    memset(null_addr, 0, 6);
    gap_advertisements_set_params(adv_int_min, adv_int_max, adv_type, 0, null_addr, 0x07, 0x00);
    gap_advertisements_set_data(adv_data_len, (uint8_t*) adv_data);
    gap_advertisements_enable(1);

    // register for HCI events
    hci_event_callback_registration.callback = &packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // register for ATT event
    att_server_register_packet_handler(packet_handler);

    const char initial_text[] = "12:32 Uhr";
    strcpy(clock_value, initial_text);
    clock_value_len = strlen(initial_text);
}
/* LISTING_END */

/* 
 * @section Packet Handler
 *
 * @text The packet handler is used to:
 *        - stop the counter after a disconnect
 *        - send a notification when the requested ATT_EVENT_CAN_SEND_NOW is received
 */

/* LISTING_START(packetHandler): Packet Handler */
static void packet_handler (uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size){
    UNUSED(channel);
    UNUSED(size);

    switch (packet_type) {
        case HCI_EVENT_PACKET:
            switch (hci_event_packet_get_type(packet)) {
                case HCI_EVENT_DISCONNECTION_COMPLETE:
                    authorized = 0;
                    break;
            }
            break;
    }
}
/* LISTING_END */

/*
 * @section ATT Read
 *
 * @text The ATT Server handles all reads to constant data. For dynamic data like the custom characteristic, the registered
 * att_read_callback is called. To handle long characteristics and long reads, the att_read_callback is first called
 * with buffer == NULL, to request the total value length. Then it will be called again requesting a chunk of the value.
 * See Listing attRead.
 */

/* LISTING_START(attRead): ATT Read */

// ATT Client Read Callback for Dynamic Data
// - if buffer == NULL, don't copy data, just return size of value
// - if buffer != NULL, copy data and return number bytes copied
// @param offset defines start of attribute value
static uint16_t att_read_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t offset, uint8_t * buffer, uint16_t buffer_size){
    UNUSED(connection_handle);

    if (att_handle == ATT_CHARACTERISTIC_0000FF11_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE) {
        if (authorized){
            static const char * auth_message = "Authorized!";
            return att_read_callback_handle_blob((uint8_t *) auth_message, strlen(auth_message), offset, buffer, buffer_size);
        } else {
            static const char * auth_message = "Not Authorized, need password!";
            return att_read_callback_handle_blob((uint8_t *)auth_message, strlen(auth_message), offset, buffer, buffer_size);
        }
    }
    if (att_handle == ATT_CHARACTERISTIC_0000FF12_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE) {
        return att_read_callback_handle_blob((uint8_t *)clock_value, clock_value_len, offset, buffer, buffer_size);
    }
    return 0;
}
/* LISTING_END */


/*
 * @section ATT Write
 *
 * @text The only valid ATT write in this example is to the Client Characteristic Configuration, which configures notification
 * and indication. If the ATT handle matches the client configuration handle, the new configuration value is stored and used
 * in the heartbeat handler to decide if a new value should be sent. See Listing attWrite.
 */

/* LISTING_START(attWrite): ATT Write */
static int att_write_callback(hci_con_handle_t connection_handle, uint16_t att_handle, uint16_t transaction_mode, uint16_t offset, uint8_t *buffer, uint16_t buffer_size){
    UNUSED(connection_handle);
    UNUSED(transaction_mode);
    UNUSED(offset);
    
    if (att_handle == ATT_CHARACTERISTIC_0000FF11_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE){
        if (memcmp(buffer, authorization_password, strlen(authorization_password)) == 0){
            authorized = true;
        }
        return 0;
    }
    if (att_handle == ATT_CHARACTERISTIC_0000FF12_0000_1000_8000_00805F9B34FB_01_VALUE_HANDLE) {
        if (authorized){
            memcpy(clock_value, buffer, buffer_size);
            clock_value_len = buffer_size;
            clock_value[clock_value_len] = 0;
            //
            printf("New value: %s\n", clock_value);
        }
    }
    return 0;
}
/* LISTING_END */

int btstack_main(void);
int btstack_main(void)
{
    clock_setup();

    // turn on!
	hci_power_control(HCI_POWER_ON);
	    
    return 0;
}
/* EXAMPLE_END */
