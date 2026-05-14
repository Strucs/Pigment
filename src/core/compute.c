/**
 * Copyright 2025-2026 Angel-Leduc TA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "compute.h"

#include "internal.h"
#include "structs.h"

void pigment_cmd_dispatch(Pigment* pigment, PCommandBuffer* cmd, uint32_t group_count_x, uint32_t group_count_y, uint32_t group_count_z)
{
    if(pigment == NULL || cmd == NULL)
    {
        return;
    }

    vkCmdDispatch(cmd->buffer, group_count_x, group_count_y, group_count_z);
}

void pigment_cmd_dispatch_indirect(Pigment* pigment, PCommandBuffer* cmd, PBuffer* indirect_buffer, uint64_t indirect_offset)
{
    if(pigment == NULL || cmd == NULL || indirect_buffer == NULL)
    {
        return;
    }

    pigment_cmd_use_buffer(pigment, cmd, indirect_buffer);
    vkCmdDispatchIndirect(cmd->buffer, indirect_buffer->buffer, (VkDeviceSize) indirect_offset);
}
