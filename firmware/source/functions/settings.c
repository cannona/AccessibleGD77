/*
 * Copyright (C) 2019      Kai Ludwig, DG4KLU
 * Copyright (C) 2019-2021 Roger Clark, VK3KYY / G4KYF
 *                         Daniel Caujolle-Bert, F1RMB
 * Joseph Stephen VK7JS
 * Jan Hegr OK1TE
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

#include "hardware/EEPROM.h"
#include "functions/settings.h"
#include "functions/sound.h"
#include "functions/trx.h"
#include "user_interface/menuSystem.h"
#include "user_interface/uiLocalisation.h"
#include "functions/ticks.h"
#include "functions/rxPowerSaving.h"

static const int STORAGE_BASE_ADDRESS 		= 0x6000;
// VK7JS updated on Nov 23 2021 after removing autoZone from settings.
static const int STORAGE_MAGIC_NUMBER 		= 0x2111; // NOTE: never use 0xDEADBEEF, it's reserved value

// Bit patterns for DMR Beep
const uint8_t BEEP_TX_NONE  = 0x00;
const uint8_t BEEP_TX_START = 0x01;
const uint8_t BEEP_TX_STOP  = 0x02;
const uint8_t BEEP_FM_TX_START = 0x04;
const uint8_t BEEP_FM_TX_STOP = 0x08;

#if defined(PLATFORM_RD5R)
static uint32_t dirtyTime = 0;
#endif

static bool settingsDirty = false;
static bool settingsVFODirty = false;
static int16_t ZoneChannelIndexMax=0;

settingsStruct_t nonVolatileSettings;
struct_codeplugChannel_t *currentChannelData;
struct_codeplugChannel_t channelScreenChannelData = { .rxFreq = 0 };
struct_codeplugContact_t contactListContactData;
struct_codeplugDTMFContact_t contactListDTMFContactData;
struct_codeplugChannel_t settingsVFOChannel[2];// VFO A and VFO B from the codeplug.
int settingsUsbMode = USB_MODE_CPS;

int *nextKeyBeepMelody = (int *)MELODY_KEY_BEEP;
struct_codeplugGeneralSettings_t settingsCodeplugGeneralSettings;

monitorModeSettingsStruct_t monitorModeData = { .isEnabled = false, .qsoInfoUpdated = true, .dmrIsValid = false };
const int ECO_LEVEL_MAX = 5;
const uint16_t NO_PRIORITY_CHANNEL=0xffffU;

bool settingsSaveSettings(bool includeVFOs)
{
	if (includeVFOs)
	{
		codeplugSetVFO_ChannelData(&settingsVFOChannel[CHANNEL_VFO_A], CHANNEL_VFO_A);
		codeplugSetVFO_ChannelData(&settingsVFOChannel[CHANNEL_VFO_B], CHANNEL_VFO_B);
		settingsVFODirty = false;
	}

	// Never reset this setting (as voicePromptsCacheInit() can change it if voice data are missing)
#if defined(PLATFORM_GD77S)
	nonVolatileSettings.audioPromptMode = AUDIO_PROMPT_MODE_VOICE_LEVEL_3;
#endif

	bool ret = EEPROM_Write(STORAGE_BASE_ADDRESS, (uint8_t *)&nonVolatileSettings, sizeof(settingsStruct_t));

	if (ret)
	{
		settingsDirty = false;
	}

	return ret;
}

bool settingsLoadSettings(void)
{
	bool hasRestoredDefaultsettings = false;
	if (!EEPROM_Read(STORAGE_BASE_ADDRESS, (uint8_t *)&nonVolatileSettings, sizeof(settingsStruct_t)))
	{
		nonVolatileSettings.magicNumber = 0;// flag settings could not be loaded
	}

	if (nonVolatileSettings.magicNumber != STORAGE_MAGIC_NUMBER)
	{
		settingsRestoreDefaultSettings();
		hasRestoredDefaultsettings = true;
	}

	// Force Hotspot mode to off for existing RD-5R users.
#if defined(PLATFORM_RD5R)
	nonVolatileSettings.hotspotType = HOTSPOT_TYPE_OFF;
#endif

	codeplugGetVFO_ChannelData(&settingsVFOChannel[CHANNEL_VFO_A], CHANNEL_VFO_A);
	codeplugGetVFO_ChannelData(&settingsVFOChannel[CHANNEL_VFO_B], CHANNEL_VFO_B);
	/* 2020.10.27  vk3kyy. This should not be necessary as the rest of the fimware e.g. on the VFO screen and in the contact lookup handles when Rx Group and / or Contact is set to none
	settingsInitVFOChannel(0);// clean up any problems with VFO data
	settingsInitVFOChannel(1);
	*/

	trxDMRID = uiDataGlobal.userDMRId = codeplugGetUserDMRID();
	struct_codeplugDeviceInfo_t tmpDeviceInfoBuffer;// Temporary buffer to load the data including the CPS user band limits
	if (codeplugGetDeviceInfo(&tmpDeviceInfoBuffer))
	{
		// Validate CPS band limit data
		if (	(tmpDeviceInfoBuffer.minVHFFreq < tmpDeviceInfoBuffer.maxVHFFreq) &&
				(tmpDeviceInfoBuffer.minUHFFreq > tmpDeviceInfoBuffer.maxVHFFreq) &&
				(tmpDeviceInfoBuffer.minUHFFreq < tmpDeviceInfoBuffer.maxUHFFreq) &&
				((tmpDeviceInfoBuffer.minVHFFreq * 100000) >= RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_VHF].minFreq) &&
				((tmpDeviceInfoBuffer.minVHFFreq * 100000) <= RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_VHF].maxFreq) &&

				((tmpDeviceInfoBuffer.maxVHFFreq * 100000) >= RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_VHF].minFreq) &&
				((tmpDeviceInfoBuffer.maxVHFFreq * 100000) <= RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_VHF].maxFreq) &&


				((tmpDeviceInfoBuffer.minUHFFreq * 100000) >= RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_UHF].minFreq) &&
				((tmpDeviceInfoBuffer.minUHFFreq * 100000) <= RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_UHF].maxFreq) &&

				((tmpDeviceInfoBuffer.maxUHFFreq * 100000) >= RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_UHF].minFreq) &&
				((tmpDeviceInfoBuffer.maxUHFFreq * 100000) <= RADIO_HARDWARE_FREQUENCY_BANDS[RADIO_BAND_UHF].maxFreq)
			)
		{
			// Only use it, if EVERYTHING is OK.
			USER_FREQUENCY_BANDS[RADIO_BAND_VHF].minFreq = tmpDeviceInfoBuffer.minVHFFreq * 100000;// value needs to be in 10s of Hz;
			USER_FREQUENCY_BANDS[RADIO_BAND_VHF].maxFreq = tmpDeviceInfoBuffer.maxVHFFreq * 100000;// value needs to be in 10s of Hz;
			USER_FREQUENCY_BANDS[RADIO_BAND_UHF].minFreq = tmpDeviceInfoBuffer.minUHFFreq * 100000;// value needs to be in 10s of Hz;
			USER_FREQUENCY_BANDS[RADIO_BAND_UHF].maxFreq = tmpDeviceInfoBuffer.maxUHFFreq * 100000;// value needs to be in 10s of Hz;
		}
	}
	//codeplugGetGeneralSettings(&settingsCodeplugGeneralSettings);

	if (nonVolatileSettings.languageIndex >= NUM_LANGUAGES)
	{
		nonVolatileSettings.languageIndex = 0U; // Reset to English
		settingsSetDirty();
	}
	else
	{
		settingsDirty = false;
	}

	currentLanguage = &languages[nonVolatileSettings.languageIndex];

	soundBeepVolumeDivider = nonVolatileSettings.beepVolumeDivider;

	trxSetAnalogFilterLevel(nonVolatileSettings.analogFilterLevel);

	codeplugInitChannelsPerZone();// Initialise the codeplug channels per zone
	ZoneChannelIndexMax=sizeof(nonVolatileSettings.zoneChannelIndices)/sizeof(int16_t);
	settingsVFODirty = false;

	rxPowerSavingSetLevel(nonVolatileSettings.ecoLevel);

	// Scan On Boot is enabled, but latest mode was VFO, switch back to Channel Mode
	if (settingsIsOptionBitSet(BIT_SCAN_ON_BOOT_ENABLED) && (nonVolatileSettings.initialMenuNumber != UI_CHANNEL_MODE))
	{
		settingsSet(nonVolatileSettings.initialMenuNumber, UI_CHANNEL_MODE);
	}
	if (nonVolatileSettings.sk2Latch > 10)
		nonVolatileSettings.sk2Latch = 0;
	else if (nonVolatileSettings.sk2Latch ==1) // convert old to new
		nonVolatileSettings.sk2Latch =6; // 3 seconds.
		// ensure dtmfLatch is set to a sensible value
	if (nonVolatileSettings.dtmfLatch > 6)
		nonVolatileSettings.dtmfLatch = 3; // defaults to 1.5 seconds.
	else if (nonVolatileSettings.dtmfLatch ==1)
		nonVolatileSettings.dtmfLatch =2; // 1 seconds.
	uiDataGlobal.priorityChannelIndex=nonVolatileSettings.priorityChannelIndex;
	if (nonVolatileSettings.vhfOffset==0 || nonVolatileSettings.vhfOffset > 1000)
		nonVolatileSettings.vhfOffset=600; // repeater offset for 2 m band kHz.
	if (nonVolatileSettings.uhfOffset==0 || nonVolatileSettings.uhfOffset > 9000) 
		nonVolatileSettings.uhfOffset=5000; // repeater offset for 70 cm band.
	if (nonVolatileSettings.voicePromptVolumePercent < 10 || nonVolatileSettings.voicePromptVolumePercent > 100)
		nonVolatileSettings.voicePromptVolumePercent=100; // max volume.
	if (nonVolatileSettings.voicePromptRate > 9)
		nonVolatileSettings.voicePromptRate=0; // default, no change, each increment of 1 increases by 10%

	return hasRestoredDefaultsettings;
}

void settingsInitVFOChannel(int vfoNumber)
{
	// temporary hack in case the code plug has no RxGroup selected
	// The TG needs to come from the RxGroupList
	if (settingsVFOChannel[vfoNumber].rxGroupList == 0)
	{
		settingsVFOChannel[vfoNumber].rxGroupList = 1;
	}

	if (settingsVFOChannel[vfoNumber].contact == 0)
	{
		settingsVFOChannel[vfoNumber].contact = 1;
	}
}

void settingsRestoreDefaultSettings(void)
{
	nonVolatileSettings.magicNumber = STORAGE_MAGIC_NUMBER;
	nonVolatileSettings.currentChannelIndexInZone = 0;
	nonVolatileSettings.currentChannelIndexInAllZone = 1;
	nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_CHANNEL_MODE] = 0;
	nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_A_MODE] = 0;
	nonVolatileSettings.currentIndexInTRxGroupList[SETTINGS_VFO_B_MODE] = 0;
	nonVolatileSettings.currentZone = 0;
	nonVolatileSettings.backlightMode =
#if defined(PLATFORM_GD77S)
			BACKLIGHT_MODE_NONE;
#else
			BACKLIGHT_MODE_AUTO;
#endif
	nonVolatileSettings.backLightTimeout = 0U;//0 = never timeout. 1 - 255 time in seconds
	nonVolatileSettings.displayContrast =
#if defined(PLATFORM_DM1801) || defined(PLATFORM_DM1801A)
			0x0e; // 14
#elif defined (PLATFORM_RD5R)
			0x06;
#else
			0x12; // 18
#endif
	nonVolatileSettings.initialMenuNumber =
#if defined(PLATFORM_GD77S)
			UI_CHANNEL_MODE;
#else
			UI_VFO_MODE;
#endif
	nonVolatileSettings.displayBacklightPercentage = 100;// 100% brightness
	nonVolatileSettings.displayBacklightPercentageOff = 0;// 0% brightness
	nonVolatileSettings.extendedInfosOnScreen = INFO_ON_SCREEN_OFF;
	nonVolatileSettings.txFreqLimited =
#if defined(PLATFORM_GD77S)
			BAND_LIMITS_NONE;//GD-77S is channelised, and there is no way to disable band limits from the UI, so disable limits by default.
#else
			BAND_LIMITS_ON_LEGACY_DEFAULT;// Limit Tx frequency to US Amateur bands
#endif
	nonVolatileSettings.txPowerLevel =
#if defined(PLATFORM_GD77S)
			3U; // 750mW
#else
			4U; // 1 W   3:750  2:500  1:250
#endif

	nonVolatileSettings.userPower = 4100U;// Max DAC value is 4095. 4100 is a hack to make the numbers more palatable.
	nonVolatileSettings.bitfieldOptions =
#if defined(PLATFORM_GD77S)
			0U;
#else
			BIT_SETTINGS_UPDATED; // we need to keep track if the user has been notified about settings update.
#endif
	nonVolatileSettings.overrideTG = 0U;// 0 = No override
	nonVolatileSettings.txTimeoutBeepX5Secs = 2U;
	nonVolatileSettings.beepVolumeDivider = 4U; //-6dB: Beeps are way too loud using the same setting as the official firmware
	nonVolatileSettings.micGainDMR = 11U; // Normal value used by the official firmware
	nonVolatileSettings.micGainFM = 17U; // Default (from all of my cals, datasheet default: 16)
	nonVolatileSettings.tsManualOverride = 0U; // No manual TS override using the Star key
	nonVolatileSettings.keypadTimerLong = 5U;
	nonVolatileSettings.keypadTimerRepeat = 3U;
	nonVolatileSettings.currentVFONumber = CHANNEL_VFO_A;
	nonVolatileSettings.dmrDestinationFilter =
#if defined(PLATFORM_GD77S)
	DMR_DESTINATION_FILTER_TG;
#else
	DMR_DESTINATION_FILTER_NONE;
#endif
	nonVolatileSettings.dmrCcTsFilter = DMR_CCTS_FILTER_CC_TS;

	nonVolatileSettings.dmrCaptureTimeout = 10U;// Default to holding 10 seconds after a call ends
	nonVolatileSettings.analogFilterLevel = ANALOG_FILTER_CSS;
	trxSetAnalogFilterLevel(nonVolatileSettings.analogFilterLevel);
	nonVolatileSettings.languageIndex = 0U;
	nonVolatileSettings.scanDelay = 5U;// 5 seconds
	nonVolatileSettings.scanStepTime = 0;// 30ms
	nonVolatileSettings.scanModePause = SCAN_MODE_HOLD;
	nonVolatileSettings.squelchDefaults[RADIO_BAND_VHF]		= 10U;// 1 - 21 = 0 - 100% , same as from the CPS variable squelch
	nonVolatileSettings.squelchDefaults[RADIO_BAND_220MHz]	= 10U;// 1 - 21 = 0 - 100% , same as from the CPS variable squelch
	nonVolatileSettings.squelchDefaults[RADIO_BAND_UHF]		= 10U;// 1 - 21 = 0 - 100% , same as from the CPS variable squelch
	nonVolatileSettings.hotspotType =
#if defined(PLATFORM_GD77S)
			HOTSPOT_TYPE_MMDVM;
#else
			HOTSPOT_TYPE_OFF;
#endif
	nonVolatileSettings.privateCalls =
#if defined(PLATFORM_GD77S)
			ALLOW_PRIVATE_CALLS_OFF;
#else
			ALLOW_PRIVATE_CALLS_ON;
#endif

    // Set all these value to zero to force the operator to set their own limits.
	nonVolatileSettings.vfoScanLow[CHANNEL_VFO_A] = 0U;
	nonVolatileSettings.vfoScanLow[CHANNEL_VFO_B] = 0U;
	nonVolatileSettings.vfoScanHigh[CHANNEL_VFO_A] = 0U;
	nonVolatileSettings.vfoScanHigh[CHANNEL_VFO_B] = 0U;

	nonVolatileSettings.contactDisplayPriority = CONTACT_DISPLAY_PRIO_CC_DB_TA;
	nonVolatileSettings.splitContact = SPLIT_CONTACT_SINGLE_LINE_ONLY;
	nonVolatileSettings.beepOptions = BEEP_TX_STOP | BEEP_TX_START;
	// VOX related
	nonVolatileSettings.voxThreshold = 20U;
	nonVolatileSettings.voxTailUnits = 4U; // 2 seconds tail

#if defined(PLATFORM_GD77S)
	nonVolatileSettings.audioPromptMode = AUDIO_PROMPT_MODE_VOICE_LEVEL_3;
#else
	if (voicePromptDataIsLoaded)
	{
		nonVolatileSettings.audioPromptMode = AUDIO_PROMPT_MODE_VOICE_LEVEL_3;
	}
	else
	{
		nonVolatileSettings.audioPromptMode = AUDIO_PROMPT_MODE_BEEP;
	}
#endif

	nonVolatileSettings.temperatureCalibration = 0;
	nonVolatileSettings.batteryCalibration = 5;
	nonVolatileSettings.priorityChannelIndex = NO_PRIORITY_CHANNEL;
	nonVolatileSettings.sk2Latch=0;
	nonVolatileSettings.dtmfLatch=3; // 1.5 second.

	nonVolatileSettings.ecoLevel = 1;

	nonVolatileSettings.vfoSweepSettings = ((((sizeof(VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE) / sizeof(VFO_SWEEP_SCAN_RANGE_SAMPLE_STEP_TABLE[0])) - 1) << 12) | (VFO_SWEEP_RSSI_NOISE_FLOOR_DEFAULT << 7) | VFO_SWEEP_GAIN_DEFAULT);

	nonVolatileSettings.vhfOffset=600; // repeater offset for 2 m band kHz.
	nonVolatileSettings.uhfOffset=5000; // repeater offset for 70 cm band.
	nonVolatileSettings.totMaster=0;
	nonVolatileSettings.autoZonesEnabled=0;
	memset(nonVolatileSettings.zoneChannelIndices, 0, sizeof(nonVolatileSettings.zoneChannelIndices));	
	currentChannelData = &settingsVFOChannel[nonVolatileSettings.currentVFONumber];// Set the current channel data to point to the VFO data since the default screen will be the VFO
	nonVolatileSettings.voicePromptVolumePercent=100; // max volume.
	nonVolatileSettings.voicePromptRate=0; // default, no change, each increment of 1 increases by 10%
	settingsDirty = true;

	settingsSaveSettings(false);
}

void enableVoicePromptsIfLoaded(bool enableFullPrompts)
{
	if (voicePromptDataIsLoaded)
	{
#if defined(PLATFORM_GD77S)
		nonVolatileSettings.audioPromptMode = AUDIO_PROMPT_MODE_VOICE_LEVEL_3;
#else

		nonVolatileSettings.audioPromptMode = enableFullPrompts?AUDIO_PROMPT_MODE_VOICE_LEVEL_3:AUDIO_PROMPT_MODE_VOICE_LEVEL_1;
#endif
		settingsDirty = true;
		settingsSaveSettings(false);
	}
}

void settingsEraseCustomContent(void)
{
	//Erase OpenGD77 custom content
	SPI_Flash_eraseSector(0);// The first sector (4k) contains the OpenGD77 custom codeplug content e.g. Boot melody and boot image.
}

// --- Helpers ---
void settingsSetBOOL(bool *s, bool v)
{
	*s = v;
	settingsSetDirty();
}

void settingsSetINT8(int8_t *s, int8_t v)
{
	*s = v;
	settingsSetDirty();
}

void settingsSetUINT8(uint8_t *s, uint8_t v)
{
	*s = v;
	settingsSetDirty();
}

void settingsSetINT16(int16_t *s, int16_t v)
{
	*s = v;
	settingsSetDirty();
}

void settingsSetUINT16(uint16_t *s, uint16_t v)
{
	*s = v;
	settingsSetDirty();
}

void settingsSetINT32(int32_t *s, int32_t v)
{
	*s = v;
	settingsSetDirty();
}

void settingsSetUINT32(uint32_t *s, uint32_t v)
{
	*s = v;
	settingsSetDirty();
}

void settingsIncINT8(int8_t *s, int8_t v)
{
	*s = *s + v;
	settingsSetDirty();
}

void settingsIncUINT8(uint8_t *s, uint8_t v)
{
	*s = *s + v;
	settingsSetDirty();
}

void settingsIncINT16(int16_t *s, int16_t v)
{
	*s = *s + v;
	settingsSetDirty();
}

void settingsIncUINT16(uint16_t *s, uint16_t v)
{
	*s = *s + v;
	settingsSetDirty();
}

void settingsIncINT32(int32_t *s, int32_t v)
{
	*s = *s + v;
	settingsSetDirty();
}

void settingsIncUINT32(uint32_t *s, uint32_t v)
{
	*s = *s + v;
	settingsSetDirty();
}

void settingsDecINT8(int8_t *s, int8_t v)
{
	*s = *s - v;
	settingsSetDirty();
}

void settingsDecUINT8(uint8_t *s, uint8_t v)
{
	*s = *s - v;
	settingsSetDirty();
}

void settingsDecINT16(int16_t *s, int16_t v)
{
	*s = *s - v;
	settingsSetDirty();
}

void settingsDecUINT16(uint16_t *s, uint16_t v)
{
	*s = *s - v;
	settingsSetDirty();
}

void settingsDecINT32(int32_t *s, int32_t v)
{
	*s = *s - v;
	settingsSetDirty();
}

void settingsDecUINT32(uint32_t *s, uint32_t v)
{
	*s = *s - v;
	settingsSetDirty();
}
// --- End of Helpers ---

void settingsSetOptionBit(bitfieldOptions_t bit, bool set)
{
	if (set)
	{
		nonVolatileSettings.bitfieldOptions |= bit;
	}
	else
	{
		nonVolatileSettings.bitfieldOptions &= ~bit;
	}
	settingsSetDirty();
}

bool settingsIsOptionBitSet(bitfieldOptions_t bit)
{
	return ((nonVolatileSettings.bitfieldOptions & bit) == (bit));
}

void settingsSetDirty(void)
{
	settingsDirty = true;

#if defined(PLATFORM_RD5R)
	dirtyTime = fw_millis();
#endif
}

void settingsSetVFODirty(void)
{
	settingsVFODirty = true;

#if defined(PLATFORM_RD5R)
	dirtyTime = fw_millis();
#endif
}

void settingsSaveIfNeeded(bool immediately)
{
#if defined(PLATFORM_RD5R)
	const int DIRTY_DURTION_MILLISECS = 500;

	if ((settingsDirty || settingsVFODirty) &&
			(immediately || (((fw_millis() - dirtyTime) > DIRTY_DURTION_MILLISECS) && // DIRTY_DURTION_ has passed since last change
					((uiDataGlobal.Scan.active == false) || // not scanning, or scanning anything but channels
							(menuSystemGetCurrentMenuNumber() != UI_CHANNEL_MODE)))))
	{
		settingsSaveSettings(settingsVFODirty);
	}
#endif
}

int settingsGetScanStepTimeMilliseconds(void)
{
	return TIMESLOT_DURATION + (nonVolatileSettings.scanStepTime * TIMESLOT_DURATION);
}

void settingsSetCurrentChannelIndexForZone(int16_t channelIndex, int16_t zoneIndex)
{
	settingsSet(nonVolatileSettings.currentChannelIndexInZone, channelIndex);
	if (zoneIndex >= ZoneChannelIndexMax)
		return; // can't save it.
	
	nonVolatileSettings.zoneChannelIndices[zoneIndex]=channelIndex;
	settingsSetDirty();
}

int16_t settingsGetCurrentChannelIndexForZone(int16_t zoneIndex)
{
	if (zoneIndex >= ZoneChannelIndexMax)
		return 0;
	
	return nonVolatileSettings.zoneChannelIndices[zoneIndex];
}
