/*
 * Copyright (C) 2021 Joseph Stephen vk7js
 *
 * Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer
 *    in the documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * 4. Use of this source code or binary releases for commercial purposes is strictly forbidden. This includes, without limitation,
 *    incorporation in a commercial product or incorporation into a product or project which allows commercial use.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */
#ifndef EDIT_HANDLER_H_INCLUDED
#define EDIT_HANDLER_H_INCLUDED

#include <ctype.h>
#include "uiUtilities.h"

typedef enum  {EDIT_TYPE_ALPHANUMERIC, EDIT_TYPE_NUMERIC, EDIT_TYPE_DTMF_CHARS} EditFieldTypes_t;

typedef  struct
{
	EditFieldTypes_t editFieldType;
	char* editBuffer;
	int maxLen;// including null terminator.
	int* cursorPos; // 0-based index of cursor in edit buffer (string).
	int xPixelOffset; // x pixel offset of first char of editable text (e.g. when preceeded by a prompt or edit is centered).
	int yPixelOffset; // y pixel offset to print cursor at.
	bool allowedToSpeakUpdate;
} EditStructParrams_t;

extern EditStructParrams_t editParams;

void editUpdateCursor(EditStructParrams_t* editParams, bool moved, bool render);
void moveCursorLeftInString(char *str, int *pos, bool delete);
void moveCursorRightInString(char *str, int *pos, int max);
bool HandleEditEvent(uiEvent_t *ev, EditStructParrams_t* editParams);
void RequeueEditBufferForAnnouncementOnSK1IfNeeded();

#endif //EDIT_HANDLER_H_INCLUDED
