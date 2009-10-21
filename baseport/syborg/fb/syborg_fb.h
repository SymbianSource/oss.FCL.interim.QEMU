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
* Description: Minimalistic frame buffer driver
*
*/

#ifndef _SYBORG_FB_H
#define _SYBORG_FB_H

#include <videodriver.h>
#include <nkern.h>
#include <kernel.h>
#include <kpower.h>
#include <system.h>



// The definition of __portrait_display__ in the MMP file affects
// both the display and the pointer component, so both must be
// rebuilt if this setting is changed.


_LIT(KLitLcd,"SYBORG_FB");


#ifdef __PORTRAIT_DISPLAY__ // portrait display selected

#	ifdef __DISPLAY_WVGA__

#pragma comment(layout,"Portrait display enabled")
const TUint KConfigLcdWidth            	= 854; // This must be left at 640, even with only 360 used by S60
const TUint	KConfigLcdHeight	       	= 854;
const TInt	KConfigLcdHeightInTwips     = 12860;
const TInt	KConfigLcdWidthInTwips    	= 12860;
const TUint	KConfigMouseWidth	       	= 480; // Usable width for the mouse driver
const TUint	KConfigMouseMin			   	= 0;

#	else //__DISPLAY_WVGA__

#pragma comment(layout,"Portrait display enabled")
const TUint KConfigLcdWidth            = 640; // This must be left at 640, even with only 360 used by S60
const TUint	KConfigLcdHeight	       = 640;
const TInt	KConfigLcdHeightInTwips     = 3550;
const TInt	KConfigLcdWidthInTwips    = 3550;
const TUint	KConfigMouseWidth	       = 360; // Usable width for the mouse driver

const TUint	KConfigMouseMin			   = 0;
#	endif //__DISPLAY_WVGA__




#else // __PORTRAIT_DISPLAY__  Landscape display selected

#pragma comment(layout,"Landscape display enabled")
const TUint  KConfigLcdWidth            = 640;
const TUint	KConfigLcdHeight	       = 480;
const TInt	KConfigLcdWidthInTwips     = 3550;
const TInt	KConfigLcdHeightInTwips    = 2670;
const TUint	KConfigMouseWidth	       = 640; // Usable width for the mouse driver

const TUint	KConfigMouseMin			   = 120; // mouse range is 120 - 480, not 0 - 360
#endif // __PORTRAIT_DISPLAY__


const TBool KConfigIsMono              = 0;
const TBool KConfigIsPalettized        = 0;

const TInt  KConfigOffsetToFirstPixel  = 0;

const TBool KConfigPixelOrderRGB       = 0;
const TBool KConfigPixelOrderLandscape = 1;
const TInt  KConfigLcdDisplayMode       = 2;
//const TInt  KConfigLcdDisplayMode       = 1;
const TInt  KConfigLcdNumberOfDisplayModes = 3;


const TInt  KConfigBitsPerPixel        = 24;

#ifdef __DISPLAY_WVGA__
const TInt  KConfigOffsetBetweenLines  = 3416;
#else //__DISPLAY_WVGA__
const TInt  KConfigOffsetBetweenLines  = 2560;
#endif //__DISPLAY_WVGA__


class DLcdPowerHandler : public DPowerHandler
{
public: // from DPowerHandler
  void PowerDown(TPowerState);
  void PowerUp();
public:	// to prevent a race condition with WServer trying to power up/down at the same time
  void PowerUpDfc();
  void PowerDownDfc();
public:
  DLcdPowerHandler();
  TInt Create();
  void DisplayOn();
  void DisplayOff();
  TInt HalFunction(TInt aFunction, TAny* a1, TAny* a2);

  void PowerUpLcd(TBool aSecure);
  void PowerDownLcd();

  void ScreenInfo(TScreenInfoV01& aInfo);
  void WsSwitchOnScreen();
  void WsSwitchOffScreen();
  void HandleMsg(TMessageBase* aMsg);
  void SwitchDisplay(TBool aSecure);

private:
  TInt GetCurrentDisplayModeInfo(TVideoInfoV01& aInfo, TBool aSecure);
  TInt GetSpecifiedDisplayModeInfo(TInt aMode, TVideoInfoV01& aInfo);
  TInt SetDisplayMode(TInt aMode);
  TInt AllocateFrameBuffer();

  TBool iDisplayOn;
  DPlatChunkHw* iChunk;
  DPlatChunkHw* iSecureChunk;
  TBool iWsSwitchOnScreen;
  TBool iSecureDisplay;

public:
  TDfcQue* iDfcQ;
  TMessageQue iMsgQ;					// to prevent a race condition with Power Manager trying to power up/down at the same time
  TDfc iPowerUpDfc;
  TDfc iPowerDownDfc;

private:
  TVideoInfoV01 iVideoInfo;
  TVideoInfoV01 iSecureVideoInfo;
  NFastMutex iLock;
  TPhysAddr ivRamPhys;
  TPhysAddr iSecurevRamPhys;

public:
  TLinAddr iPortAddr;

enum {
    FB_ID               = 0,
    FB_BASE             = 1,
    FB_HEIGHT           = 2,
    FB_WIDTH            = 3,
    FB_ORIENTATION      = 4,
    FB_BLANK            = 5,
    FB_INT_MASK         = 6,
    /* begin new interface */
    FB_INTERRUPT_CAUSE  = 7,
    FB_BPP              = 8,
    FB_COLOR_ORDER      = 9,
    FB_BYTE_ORDER       = 10,
    FB_PIXEL_ORDER      = 11,
    FB_ROW_PITCH        = 12,
    FB_ENABLED          = 13,
    FB_PALETTE_START    = 0x400 >> 2,
    FB_PALETTE_END   = FB_PALETTE_START+256-1,
    /* end new interface */
};

#define FB_INT_VSYNC            (1U << 0)
#define FB_INT_BASE_UPDATE_DONE (1U << 1)

};

#endif
