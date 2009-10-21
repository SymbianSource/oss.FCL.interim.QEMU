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
* Description: Minimalistic serial driver
*
* TODO: Handle multiple Units, hardcoded FIFO Size
*/

#include "syborg_serial.h"

//#define DPRINT(x) Kern::Printf(x)
//#define DPRINT2(x,y) Kern::Printf(x,y)

#define DPRINT(x)
#define DPRINT2(x,y)

// ---------------------------------------------------------------
// ---------------------------------------------------------------

DDriverSyborgComm::DDriverSyborgComm()
{
  iVersion=TVersion(KCommsMajorVersionNumber,KCommsMinorVersionNumber,KCommsBuildVersionNumber);
}

TInt DDriverSyborgComm::Install()
{
  DPRINT("DDriverSyborgComm::Install");
  return SetName(&KPddName);
}

void DDriverSyborgComm::GetCaps(TDes8 &aDes) const
{
  DPRINT("DDriverSyborgComm::GetCaps");

  TCommCaps3 capsBuf;
  TCommCapsV03 &c=capsBuf();
  c.iRate=KCapsBps110|KCapsBps150|KCapsBps300|KCapsBps600|KCapsBps1200|KCapsBps2400|KCapsBps4800|KCapsBps9600|KCapsBps19200|KCapsBps38400|KCapsBps57600|KCapsBps115200|KCapsBps230400;
  c.iDataBits=KCapsData5|KCapsData6|KCapsData7|KCapsData8;
  c.iStopBits=KCapsStop1|KCapsStop2;
  c.iParity=KCapsParityNone|KCapsParityEven|KCapsParityOdd;
  c.iHandshake=KCapsObeyXoffSupported|KCapsSendXoffSupported|KCapsObeyCTSSupported|KCapsFailCTSSupported|KCapsObeyDSRSupported|KCapsFailDSRSupported|KCapsObeyDCDSupported|KCapsFailDCDSupported|KCapsFreeRTSSupported|KCapsFreeDTRSupported;
  c.iSignals=KCapsSignalCTSSupported|KCapsSignalDSRSupported|KCapsSignalDCDSupported|KCapsSignalRTSSupported|KCapsSignalDTRSupported;
  c.iSIR=0;
  c.iNotificationCaps=KNotifyDataAvailableSupported|KNotifySignalsChangeSupported;
  c.iFifo=KCapsHasFifo;
  c.iRoleCaps=0;
  c.iFlowControlCaps=0;
  c.iBreakSupported=ETrue;
  aDes.FillZ(aDes.MaxLength());
  aDes=capsBuf.Left(Min(capsBuf.Length(),aDes.MaxLength()));
}

TInt DDriverSyborgComm::Create(DBase*& aChannel, TInt aUnit, const TDesC8* anInfo, const TVersion& aVer)
{
  DPRINT("DDriverSyborgComm::Create");

  DCommSyborgSoc* pD=new DCommSyborgSoc;
  aChannel=pD;
  TInt r=KErrNoMemory;
  if (pD)
	r=pD->DoCreate(aUnit,anInfo);
  return r;
}

TInt DDriverSyborgComm::Validate(TInt aUnit, const TDesC8* /*anInfo*/, const TVersion& aVer)
{
  DPRINT("DDriverSyborgComm::Validate");
  if ((!Kern::QueryVersionSupported(iVersion,aVer)) || 
	  (!Kern::QueryVersionSupported(aVer,TVersion(KMinimumLddMajorVersion,KMinimumLddMinorVersion,KMinimumLddBuild))))
	return KErrNotSupported;
  return KErrNone;
}

// ---------------------------------------------------------------
// ---------------------------------------------------------------

DCommSyborgSoc::DCommSyborgSoc()
{
}

DCommSyborgSoc::~DCommSyborgSoc()
{
  Interrupt::Unbind(iIrq);
}

TInt DCommSyborgSoc::DoCreate(TInt aUnit, const TDesC8* /*anInfo*/)
{
  DPRINT2("DCommSyborgSoc::DoCreate %d",aUnit);
  switch(aUnit)
	{
	case 0:
	  iPortAddr = KHwBaseUart0;
	  iIrq = EIntSerial0;
	  break;
	case 1:
	  iPortAddr = KHwBaseUart1;
	  iIrq = EIntSerial1;
	  break;
	case 2:
	  iPortAddr = KHwBaseUart2;
	  iIrq = EIntSerial2;
	  break;
	case 3:
	  iPortAddr = KHwBaseUart3;
	  iIrq = EIntSerial3;
	  break;
	default:
	  iPortAddr = KHwBaseUart0;
	  iIrq = EIntSerial0;
	  break;
	}

  Interrupt::Bind(EIntSerial0,Isr,this);

  return KErrNone;

}

TInt DCommSyborgSoc::Start()
{
  DPRINT("DCommSyborgSoc::Start");
  WriteReg(iPortAddr, SERIAL_INT_ENABLE, 0x1);
  Interrupt::Enable(iIrq);
  return KErrNone;
}

void DCommSyborgSoc::Stop(TStopMode aMode)
{
  DPRINT("DCommSyborgSoc::Stop");
  WriteReg(iPortAddr, SERIAL_INT_ENABLE, 0x0);
  Interrupt::Disable(iIrq);
}

void DCommSyborgSoc::Break(TBool aState)
{
  DPRINT("DCommSyborgSoc::Break");
}

void DCommSyborgSoc::EnableTransmit()
{
  DPRINT("DCommSyborgSoc::EnableTransmit");
  while (Kern::PowerGood())
	{
	  TInt r=TransmitIsr();
	  if (r<0)
		break;
	  WriteReg(iPortAddr, SERIAL_DATA, r);
	}

	iLdd->iTxError = KErrNone;
	iLdd->iTxCompleteDfc.Add();

}

TUint DCommSyborgSoc::Signals() const
{
  DPRINT("DCommSyborgSoc::Signals");
  return(0);
}
  
void DCommSyborgSoc::SetSignals(TUint aSetMask, TUint aClearMask)
{
  DPRINT("DCommSyborgSoc::SetSignals");
}

TInt DCommSyborgSoc::ValidateConfig(const TCommConfigV01 &aConfig) const
{
  DPRINT("DCommSyborgSoc::ValidateConfig");
  return KErrNone;
}

void DCommSyborgSoc::Configure(TCommConfigV01 &aConfig)
{
  DPRINT("DCommSyborgSoc::Configure");
}

void DCommSyborgSoc::Caps(TDes8 &aCaps) const
{
  DPRINT("DCommSyborgSoc::Caps");
  TCommCaps3 capsBuf;
  TCommCapsV03 &c=capsBuf();
  c.iRate=KCapsBps110|KCapsBps150|KCapsBps300|KCapsBps600|KCapsBps1200|KCapsBps2400|KCapsBps4800|KCapsBps9600|KCapsBps19200|KCapsBps38400|KCapsBps57600|KCapsBps115200|KCapsBps230400;
  c.iDataBits=KCapsData5|KCapsData6|KCapsData7|KCapsData8;
  c.iStopBits=KCapsStop1|KCapsStop2;
  c.iParity=KCapsParityNone|KCapsParityEven|KCapsParityOdd;
  c.iHandshake=KCapsObeyXoffSupported|KCapsSendXoffSupported|KCapsObeyCTSSupported|KCapsFailCTSSupported|KCapsObeyDSRSupported|KCapsFailDSRSupported|KCapsObeyDCDSupported|KCapsFailDCDSupported|KCapsFreeRTSSupported|KCapsFreeDTRSupported;
  c.iSignals=KCapsSignalCTSSupported|KCapsSignalDSRSupported|KCapsSignalDCDSupported|KCapsSignalRTSSupported|KCapsSignalDTRSupported;
  c.iSIR=0;
  c.iNotificationCaps=KNotifyDataAvailableSupported|KNotifySignalsChangeSupported;
  c.iFifo=KCapsHasFifo;
  c.iRoleCaps=0;
  c.iFlowControlCaps=0;
  c.iBreakSupported=ETrue;
  aCaps.FillZ(aCaps.MaxLength());
  aCaps=capsBuf.Left(Min(capsBuf.Length(),aCaps.MaxLength()));
}

TInt DCommSyborgSoc::DisableIrqs()
{
  DPRINT("DCommSyborgSoc::DisableIrqs");
  return NKern::DisableAllInterrupts();
}

void DCommSyborgSoc::RestoreIrqs(TInt aIrq)
{
  DPRINT("DCommSyborgSoc::RestoreIrqs");
  NKern::RestoreInterrupts(aIrq);
}

TDfcQue* DCommSyborgSoc::DfcQ(TInt /*aUnit*/)
{
  return Kern::DfcQue0();
}

void DCommSyborgSoc::CheckConfig(TCommConfigV01& aConfig)
{
  DPRINT("DCommSyborgSoc::CheckConfig");
}

void DCommSyborgSoc::Isr(TAny* aPtr)
{
  DCommSyborgSoc& d=*(DCommSyborgSoc*)aPtr;
  TUint rx[32];
  TInt rxi=0;

  DPRINT2("DCommSyborgSoc::Isr %x",d.iIrq);

  // Is now auto clear
  //  WriteReg(d.iPortAddr, SERIAL_CLEAR_INT, 0x0); // clear interrupts

  while(ReadReg(d.iPortAddr, SERIAL_FIFO_COUNT)!=0) {
	TUint ch = ReadReg(d.iPortAddr, SERIAL_DATA);
	rx[rxi++]=ch;
  }
  d.ReceiveIsr(rx,rxi,0);
}

// ---------------------------------------------------------------
// ---------------------------------------------------------------

DECLARE_STANDARD_PDD()
{
  DPRINT("DECLARE_STANDARD_PDD()");
  return new DDriverSyborgComm;
}

