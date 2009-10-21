/*
* Copyright (c) 2009 Nokia Corporation and/or its subsidiary(-ies).
* All rights reserved.
* This component and the accompanying materials are made available
* under the terms of the License "Eclipse Public License v1.0"
* which accompanies this distribution, and is available
* at the URL "http://www.eclipse.org/legal/epl-v10.html".
*
* Initial Contributors:
* Nokia Corporation - initial contribution.
*
* Contributors:
*
* Description:
*
*/

#include "shared_sound.h"
#include "variant_sound.h"

void TimerCallback(TAny* aData)
	{
	DDriverSyborgSoundScPdd * soundscpdd = (DDriverSyborgSoundScPdd*) aData;
		
	soundscpdd->Callback(soundscpdd->iTransferArray[0].iTransferID, KErrNone, soundscpdd->iTransferArray[0].iNumBytes);
	
	}


DDriverSyborgSoundScPdd::DDriverSyborgSoundScPdd() : iTimer(TimerCallback,this)
	{

	}

DDriverSyborgSoundScPdd::~DDriverSyborgSoundScPdd()
	{
	iTimer.Cancel();
	}


TInt DDriverSyborgSoundScPdd::DoCreate()
	{

	SetCaps();

	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::DoCreate TxPdd");
	
	return KErrNone;
	}

void DDriverSyborgSoundScPdd::GetChunkCreateInfo(TChunkCreateInfo& aChunkCreateInfo)
	{
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::GetChunkCreateInfo TxPdd");
	
	aChunkCreateInfo.iType = TChunkCreateInfo::ESharedKernelMultiple;
	aChunkCreateInfo.iMapAttr = EMapAttrFullyBlocking; 	// No caching
	aChunkCreateInfo.iOwnsMemory = ETrue; 				// Using RAM pages
	aChunkCreateInfo.iDestroyedDfc = NULL; 				// No chunk destroy DFC
	}

void DDriverSyborgSoundScPdd::Caps(TDes8& aCapsBuf) const
	{

	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::Caps TxPdd");
	
	// Fill the structure with zeros in case it is a newer version than we know about
	aCapsBuf.FillZ(aCapsBuf.MaxLength());

	// And copy the capabilities into the packaged structure
	TPtrC8 ptr((const TUint8*) &iCaps, sizeof(iCaps));
	aCapsBuf = ptr.Left(Min(ptr.Length(), aCapsBuf.MaxLength()));
	}

TInt DDriverSyborgSoundScPdd::SetConfig(const TDesC8& aConfigBuf)
	{

	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::SetConfig TxPdd");
	
	// Read the new configuration from the LDD
	TCurrentSoundFormatV02 config;
	TPtr8 ptr((TUint8*) &config, sizeof(config));
	Kern::InfoCopy(ptr, aConfigBuf);

	iConfig = config;
	
	return KErrNone;
	}


TInt DDriverSyborgSoundScPdd::SetVolume(TInt aVolume)
	{
	
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::Setvolume TxPdd");
	
	return KErrNone;
	}


TInt DDriverSyborgSoundScPdd::StartTransfer()
	{
	
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::starttransfer TxPdd");
	
	//Prepare for transfer
	return KErrNone;
	
	}

TInt DDriverSyborgSoundScPdd::CalculateBufferTime(TInt aNumBytes)
	{
	
	TUint samplerate=0;

	// Let the compiler perform an integer division of rates
	switch(iConfig.iRate)
		{
		case ESoundRate7350Hz: 	samplerate = 7350; break;
		case ESoundRate8000Hz: 	samplerate = 8000; break;
		case ESoundRate8820Hz: 	samplerate = 8820; break;
		case ESoundRate9600Hz: 	samplerate = 9600; break;
		case ESoundRate11025Hz: samplerate = 11025; break;
		case ESoundRate12000Hz: samplerate = 12000; break;
		case ESoundRate14700Hz:	samplerate = 14700; break;
		case ESoundRate16000Hz: samplerate = 16000; break;
		case ESoundRate22050Hz: samplerate = 22050; break;
		case ESoundRate24000Hz: samplerate = 24000; break;
		case ESoundRate29400Hz: samplerate = 29400; break;
		case ESoundRate32000Hz: samplerate = 32000; break;
		case ESoundRate44100Hz: samplerate = 44100; break;
		case ESoundRate48000Hz: samplerate = 48000; break;
		}


	// integer division by number of channels
	aNumBytes /= iConfig.iChannels;

	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::iChannels =%d", iConfig.iChannels);
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::iEncoding =%d", iConfig.iEncoding);
	
	// integer division by bytes per sample
	switch(iConfig.iEncoding)
		{
		case ESoundEncoding8BitPCM: break;
		case ESoundEncoding16BitPCM: aNumBytes /= 2; break;
		case ESoundEncoding24BitPCM: aNumBytes /= 3; break;
		}

	return (aNumBytes * 1000) / samplerate; //return time in milliseconds
	

	}

TInt DDriverSyborgSoundScPdd::TransferData(TUint aTransferID, TLinAddr aLinAddr, TPhysAddr /*aPhysAddr*/, TInt aNumBytes)
	{

	//function wil get called multiple times while transfer is in progress therefore keep fifo queue of requests
	TTransferArrayInfo transfer;	
		
	transfer.iTransferID = aTransferID;
	transfer.iLinAddr = aLinAddr;
	transfer.iNumBytes = aNumBytes;
	
	//calculate the amount of time required to play/record buffer
	TInt buffer_play_time = CalculateBufferTime(aNumBytes);
	TInt timerticks = NKern::TimerTicks(buffer_play_time);
	transfer.iPlayTime = timerticks;
	
	iTransferArray.Append(transfer);
	
	//Timer will callback when correct time has elapsed, will return KErrInUse if transfer
	//already active, this is ok becuase will be started again in callback
	TInt err = iTimer.OneShot(timerticks, ETrue);
	
	
	return KErrNone;
	}

void DDriverSyborgSoundScPdd::StopTransfer()
	{
	// Stop transfer
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::stoptransfer TxPdd");
	
	//If timer is currently active then cancel it and call back buffer
	if(iTimer.Cancel())
		{
		Callback(iTransferArray[0].iTransferID, KErrNone, iTransferArray[0].iNumBytes);
		}
		

	}


TInt DDriverSyborgSoundScPdd::PauseTransfer()
	{
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::pausetransfer TxPdd");
	//Pause Transfer
	
	return KErrNone;
	}


TInt DDriverSyborgSoundScPdd::ResumeTransfer()
	{
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::resumetransfer TxPdd");
	//Resume Transfer
	
	return KErrNone;
	}

TInt DDriverSyborgSoundScPdd::PowerUp()
	{
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::PowerUp TxPdd");
	return KErrNone;
	}

void DDriverSyborgSoundScPdd::PowerDown()
	{
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::Powerdown TxPdd");
	}

TInt DDriverSyborgSoundScPdd::CustomConfig(TInt /*aFunction*/,TAny* /*aParam*/)
	{
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::customconfig TxPdd");
	return KErrNotSupported;
	}


void DDriverSyborgSoundScPdd::Callback(TUint aTransferID, TInt aTransferResult, TInt aBytesTransferred)
	{
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::playcallback TxPdd");
	//Callback when Transfer completes or is stopped
	
	iTransferArray.Remove(0);
	
	if(iUnitType == KSoundScTxUnit0)
		{
		Ldd()->PlayCallback(aTransferID, aTransferResult, aBytesTransferred);
		}
	else if(iUnitType == KSoundScRxUnit0)
		{
		Ldd()->RecordCallback(aTransferID, aTransferResult, aBytesTransferred);
		}
	
	if(	iTransferArray.Count()>0)
		{
		iTimer.OneShot(iTransferArray[0].iPlayTime, ETrue);
		}
	
	}

TDfcQue*DDriverSyborgSoundScPdd::DfcQ()
	{
	return iPhysicalDevice->iDfcQ;
	}


TInt DDriverSyborgSoundScPdd::MaxTransferLen() const
	{
	
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::MaxTransferLen TxPdd");
	
	TInt maxlength = 200*1024;
	return maxlength;
	}


void DDriverSyborgSoundScPdd::SetCaps()
	{
	SYBORG_SOUND_DEBUG("DDriverSyborgSoundScPdd::SetCaps TxPdd");
	
	if(iUnitType == KSoundScTxUnit0)
		{
		// The data transfer direction for this unit is play
		iCaps.iDirection = ESoundDirPlayback;
		}
	else if(iUnitType == KSoundScTxUnit0)
		{
		// The data transfer direction for this unit is play
		iCaps.iDirection = ESoundDirRecord;
		}
	
	// This unit supports both mono and stereo
	iCaps.iChannels = (KSoundMonoChannel | KSoundStereoChannel);

	// This unit supports only some of the sample rates offered by Symbian OS
	iCaps.iRates = (KSoundRate8000Hz | KSoundRate11025Hz | KSoundRate12000Hz | KSoundRate16000Hz |
					KSoundRate22050Hz | KSoundRate24000Hz | KSoundRate32000Hz | KSoundRate44100Hz |
					KSoundRate48000Hz);

	// This unit only supports 16bit PCM encoding
	iCaps.iEncodings = KSoundEncoding16BitPCM;

	// This unit only supports interleaved data format when playing stereo;  that is, a PCM data
	// stream where the left and right channel samples are interleaved as L-R-L-R-L-R etc.
	iCaps.iDataFormats = KSoundDataFormatInterleaved;

	// The iRequestMinSize member is named badly.  It is actually the value of which the length samples
	// must be a multiple of.  ie.  The sample length % iRequestMinSize must == 0.  This value must always
	// be a power of 2
	iCaps.iRequestMinSize = 4;

	// The logarithm to base 2 of the alignment required for request arguments.  DMA requests must be
	// aligned to a 32 bit boundary
	iCaps.iRequestAlignment = 2;

	// This unit is not capable of detecting changes in hardware configuration
	iCaps.iHwConfigNotificationSupport = EFalse;
	}




