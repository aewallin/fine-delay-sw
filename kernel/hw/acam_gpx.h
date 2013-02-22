#ifndef __ACAM_GPX_H
#define __ACAM_GPX_H

#define AR0_ROsc (1<<0)
#define AR0_RiseEn0 (1<<1)
#define AR0_FallEn0 (1<<2)
#define AR0_RiseEn1 (1<<3)
#define AR0_FallEn1 (1<<4)
#define AR0_RiseEn2 (1<<5)
#define AR0_FallEn2 (1<<6)
#define AR0_HQSel (1<<7)
#define AR0_TRiseEn(port) (1<<(10+port))
#define AR0_TFallEn(port) (1<<(19+port))

#define AR1_Adj(chan, value) (((value) & 0xf) << (chan * 4))

#define AR2_GMode (1<<0)
#define AR2_IMode (1<<1)
#define AR2_RMode (1<<2)
#define AR2_Disable(chan) (1<<(3+chan))
#define AR2_Adj(chan, value) (((value)&0xf)<<(12+4*(chan-7)))

#define AR2_DelRise1(value) (((value)&0x3)<<(20))
#define AR2_DelFall1(value) (((value)&0x3)<<(22))
#define AR2_DelRise2(value) (((value)&0x3)<<(24))
#define AR2_DelFall2(value) (((value)&0x3)<<(26))

#define AR3_DelTx(chan, value) (((value)&0x3)<<(5 + (chan -1 ) * 2))
#define AR3_RaSpeed(chan, value) (((value)&0x3)<<(21 + (chan ) * 2))

#define AR4_RaSpeed(chan, value) (((value)&0x3)<<(10 + (chan-3) * 2))

#define AR3_Zero (0) // nothing interesting for the Fine Delay

#define AR4_StartTimer(value) ((value) & 0xff)
#define AR4_Quiet (1<<8)
#define AR4_MMode (1<<9)
#define AR4_MasterReset (1<<22)
#define AR4_PartialReset (1<<23)
#define AR4_AluTrigSoft (1<<24)
#define AR4_EFlagHiZN (1<<25)
#define AR4_MTimerStart (1<<26)
#define AR4_MTimerStop (1<<27)

#define AR5_StartOff1(value) ((value)&0x3ffff)
#define AR5_StopDisStart (1<<21)
#define AR5_StartDisStart (1<<22)
#define AR5_MasterAluTrig (1<<23)
#define AR5_PartialAluTrig (1<<24)
#define AR5_MasterOenTrig (1<<25)
#define AR5_PartialOenTrig (1<<26)
#define AR5_StartRetrig (1<<27)

#define AR6_Fill(value) ((value)&0xff)
#define AR6_StartOff2(value) (((value)&0x3ffff)<<8)
#define AR6_InSelECL   (1<<26)
#define AR6_PowerOnECL (1<<27)

#define AR7_HSDiv(value) ((value)&0xff)
#define AR7_RefClkDiv(value) (((value)&0x7)<<8)
#define AR7_ResAdj (1<<11)
#define AR7_NegPhase (1<<12)
#define AR7_Track (1<<13)
#define AR7_MTimer(value) (((value) & 0x1ff)<<15)

#define AR14_16BitMode (1<<4)

#define AR8I_IFIFO1(reg) ((reg) & 0x1ffff)
#define AR8I_Slope1(reg) ((reg) & (1<<17) ? 1 : 0)
#define AR8I_StartN1(reg) (((reg) >> 18) & 0xff)
#define AR8I_ChaCode1(reg) (((reg) >> 26) & 0x3)

#define AR9I_IFIFO2(reg) ((reg) & 0x1ffff)
#define AR9I_Slope2(reg) ((reg) & (1<<17) ? 1 : 0)
#define AR9I_StartN2(reg) (((reg) >> 18) & 0xff)
#define AR9I_ChaCode2(reg) (((reg) >> 26) & 0x3)

#define AR8R_IFIFO1(reg) ((reg) & 0x3fffff)
#define AR9R_IFIFO2(reg) ((reg) & 0x3fffff)

#define AR11_StopCounter0(num) ((num) & 0xff)
#define AR11_StopCounter1(num) (((num) & 0xff) << 8)
#define AR11_HFifoErrU(num) (1 << (num+16))
#define AR11_IFifoErrU(num) (1 << (num+24))
#define AR11_NotLockErrU (1 << 26)

#define AR12_HFifoE (1<<11)
#define AR12_NotLocked (1<<10)

#endif

