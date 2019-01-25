/*
 * Copyright (C) 2016 BlueKitchen GmbH
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

/*
 * headset.h
 * 
 */

#ifndef __HEADSET_H
#define __HEADSET_H

#include "btstack.h"

/*
 * Connect to remote device, set supervision timeout (LINK_SUPERVISION_TIMEOUT) and establish all services. 
 * On error, trigger auto-reconnect after HEADSET_AUTO_CONNECT_INTERVAL_MS milliseconds. 
 *
 * @param remote_device_address Bluetooth address of remote device, i.e {0x04,0x0C,0xCE,0xE4,0x85,0xD3}
 */
void headset_connect(bd_addr_t remote_device_address);

/*
 * Shutdown all established services and disconnect remote device.
 */
void headset_disconnect(void);

/*
 * Shutdown all established services, and handles pairing on incoming connection. Currently, only display modus is supported. 
 */
void headset_start_pairing_mode(void);

/*
 * Stop pairing modus, and auto-reconnect to a nearby known device if not already connected 
 */
void headset_stop_pairing_mode(void);

/*
 * Accept pin code from remote device. Only works in pairing mode. 
 */
void headset_accept_pin_code(void);

/*
 * Reject pin code from remote device. Only works in pairing mode. 
 */
void headset_reject_pin_code(void);


/*
 * Forget remote device with given Bluetooth address. It will be excluded from auto-reconnect, 
 * and the subsequent incoming connection from the device will be rejected if headset is not in the pairing mode.
 *
 * @param remote_device_address Bluetooth address of remote device, i.e {0x04,0x0C,0xCE,0xE4,0x85,0xD3}
 */
void headset_forget_device(bd_addr_t remote_device_address);

/*
 * Forget all known remote devices, i.e there is no remote device in the list for the auto-reconnect, 
 * all incoming connections will be rejected unless pairing mode is ON. 
 */
void headset_forget_all_devices(void);


/* TODO:
 */

#endif //__HEADSET_H
