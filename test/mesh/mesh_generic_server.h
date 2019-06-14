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

#ifndef __MESH_GENERIC_SERVER_H
#define __MESH_GENERIC_SERVER_H

#include <stdint.h>
#include "mesh_access.h"

#ifdef __cplusplus
extern "C"
{
#endif

#define MESH_GENERIC_ON_OFF_GET                     0x8201u    
#define MESH_GENERIC_ON_OFF_SET                     0x8202u
#define MESH_GENERIC_ON_OFF_SET_UNACKNOWLEDGED      0x8203u
#define MESH_GENERIC_ON_OFF_STATUS                  0x8204u

typedef enum {
    MESH_DEFAULT_TRANSITION_STEP_RESOLUTION_100ms = 0x00u,
    MESH_DEFAULT_TRANSITION_STEP_RESOLUTION_1s,
    MESH_DEFAULT_TRANSITION_STEP_RESOLUTION_10s,
    MESH_DEFAULT_TRANSITION_STEP_RESOLUTION_10min
} mesh_default_transition_step_resolution_t;

typedef struct {
    uint8_t  current_on_off_value;
    uint8_t  transaction_identifier;
    uint32_t transition_time_ms;      
    uint16_t delay_ms;                

    // transition data
    uint8_t  target_on_off_value;
    uint32_t remaining_time_ms;
} mesh_generic_on_off_state_t;

const mesh_operation_t * mesh_generic_on_off_server_get_operations(void);
/**
 * @brief Register packet handler
 * @param packet_handler
 */
void mesh_generic_on_off_server_register_packet_handler(btstack_packet_handler_t packet_handler);

/**
 * @brief Set ON/OFF value
 * @param generic_on_off_server_model
 * @param on_off_value
 * @param transition_time_ms
 * @param delay_ms
 */
void mesh_generic_on_off_server_update_value(mesh_model_t *generic_on_off_server_model, uint8_t on_off_value, uint32_t transition_time_ms, uint16_t delay_ms);

#ifdef __cplusplus
} /* end of extern "C" */
#endif

#endif