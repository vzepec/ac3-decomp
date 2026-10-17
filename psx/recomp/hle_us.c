/* GENERADO por tools/recomp/gen_hle.py --libre: enlace del codigo del juego con psx/ (version US).
 * Solo direcciones y nombres de funciones de libreria (dato de analisis) y envoltorios generados de
 * psx/include: sin bytes del juego, se versiona (etapa 7, docs/12-etapa7.md). */
#include <stdint.h>
#include "recomp_rt.h"
#include "psx_libapi.h"
#include "psx_libc.h"
#include "psx_libcard.h"
#include "psx_libcd.h"
#include "psx_libetc.h"
#include "psx_libgpu.h"
#include "psx_libgte.h"
#include "psx_libpad.h"
#include "psx_libpress.h"
#include "psx_libspu.h"
#include "psx_libc.h"

static void *rc_hptr(uint32_t a) { return a ? psx_ptr(a) : 0; }
static uint32_t rc_pptr(const void *p) { return p ? psx_addr(p) : 0; }
static uint32_t hle_arg(RCpu *c, int k) { return k < 4 ? c->r[4 + k] : rc_lw(c->r[29] + 16 + 4 * (k - 4), 0); }

static void h_80025394(RCpu *c) { c->r[2] = 0; }   /* __main */
static void h_8009a1b4(RCpu *c) { c->r[2] = (uint32_t)(PadEnableCom((int)hle_arg(c, 0))); }   /* PadEnableCom */
static void h_8009b5b4(RCpu *c) { c->r[2] = (uint32_t)(PadChkVsync()); }   /* PadChkVsync */
static void h_8009b5d4(RCpu *c) { PadStartCom(); c->r[2] = 0; }   /* PadStartCom */
static void h_8009b5f4(RCpu *c) { PadStopCom(); c->r[2] = 0; }   /* PadStopCom */
static void h_8009b660(RCpu *c) { c->r[2] = (uint32_t)(PadGetState((int)hle_arg(c, 0))); }   /* PadGetState */
static void h_8009b72c(RCpu *c) { c->r[2] = (uint32_t)(PadInfoMode((int)hle_arg(c, 0), (int)hle_arg(c, 1), (int)hle_arg(c, 2))); }   /* PadInfoMode */
static void h_8009b824(RCpu *c) { c->r[2] = (uint32_t)(PadInfoAct((int)hle_arg(c, 0), (int)hle_arg(c, 1), (int)hle_arg(c, 2))); }   /* PadInfoAct */
static void h_8009b9a0(RCpu *c) { c->r[2] = (uint32_t)(PadSetActAlign((int)hle_arg(c, 0), (const uint8_t *)rc_hptr(hle_arg(c, 1)))); }   /* PadSetActAlign */
static void h_8009b9d8(RCpu *c) { c->r[2] = (uint32_t)(PadSetMainMode((int)hle_arg(c, 0), (int)hle_arg(c, 1), (int)hle_arg(c, 2))); }   /* PadSetMainMode */
static void h_8009ba20(RCpu *c) { PadSetAct((int)hle_arg(c, 0), (uint8_t *)rc_hptr(hle_arg(c, 1)), (int)hle_arg(c, 2)); c->r[2] = 0; }   /* PadSetAct */
static void h_8009c674(RCpu *c) { PadInitDirect((uint8_t *)rc_hptr(hle_arg(c, 0)), (uint8_t *)rc_hptr(hle_arg(c, 1))); c->r[2] = 0; }   /* PadInitDirect */
static void h_8009d254(RCpu *c) { { int64_t a = (int64_t)((uint64_t)c->r[5] << 32 | c->r[4]), b = (int64_t)((uint64_t)c->r[7] << 32 | c->r[6]); uint64_t q = (uint64_t)psx_divdi3(a, b); c->r[2] = (uint32_t)q; c->r[3] = (uint32_t)(q >> 32); } }   /* __divdi3 */
static void h_8009d2ec(RCpu *c) { { uint64_t a = (uint64_t)c->r[5] << 32 | c->r[4], b = (uint64_t)c->r[7] << 32 | c->r[6]; uint64_t q = psx_udivdi3(a, b); c->r[2] = (uint32_t)q; c->r[3] = (uint32_t)(q >> 32); } }   /* __udivdi3 */
static void h_8009da04(RCpu *c) { SpuStart(); c->r[2] = 0; }   /* SpuStart */
static void h_8009e5ec(RCpu *c) { SpuInit(); c->r[2] = 0; }   /* SsUtReverbOff */
static void h_8009e60c(RCpu *c) { SpuQuit(); c->r[2] = 0; }   /* SpuQuit */
static void h_8009e68c(RCpu *c) { c->r[2] = (uint32_t)(SpuSetReverb((int32_t)hle_arg(c, 0))); }   /* SpuSetReverb */
static void h_8009e86c(RCpu *c) { c->r[2] = (uint32_t)(SpuSetReverbModeParam((const SpuReverbAttr *)rc_hptr(hle_arg(c, 0)))); }   /* SpuSetReverbModeParam */
static void h_8009f21c(RCpu *c) { c->r[2] = (uint32_t)(SpuSetReverbDepth((const SpuReverbAttr *)rc_hptr(hle_arg(c, 0)))); }   /* SpuSetReverbDepth */
static void h_8009f29c(RCpu *c) { c->r[2] = (uint32_t)(SpuSetReverbVoice((int32_t)hle_arg(c, 0), (uint32_t)hle_arg(c, 1))); }   /* SpuSetReverbVoice */
static void h_8009f58c(RCpu *c) { c->r[2] = (uint32_t)(SpuClearReverbWorkArea((int32_t)hle_arg(c, 0))); }   /* SpuClearReverbWorkArea */
static void h_8009f72c(RCpu *c) { c->r[2] = (uint32_t)(SpuReadDecodedData((SpuDecodedData *)rc_hptr(hle_arg(c, 0)), (int32_t)hle_arg(c, 1))); }   /* SpuReadDecodedData */
static void h_8009f79c(RCpu *c) { SpuSetKey((int32_t)hle_arg(c, 0), (uint32_t)hle_arg(c, 1)); c->r[2] = 0; }   /* SpuSetKey */
static void h_8009f95c(RCpu *c) { c->r[2] = (uint32_t)(SpuWrite((const uint8_t *)rc_hptr(hle_arg(c, 0)), (uint32_t)hle_arg(c, 1))); }   /* SpuRead */
static void h_8009f9bc(RCpu *c) { c->r[2] = (uint32_t)(SpuWrite0((uint32_t)hle_arg(c, 0))); }   /* SpuWrite0 */
static void h_8009fb1c(RCpu *c) { c->r[2] = (uint32_t)(SpuSetTransferStartAddr((uint32_t)hle_arg(c, 0))); }   /* SpuSetTransferStartAddr */
static void h_8009fb7c(RCpu *c) { c->r[2] = (uint32_t)(SpuSetTransferMode((int32_t)hle_arg(c, 0))); }   /* SpuSetTransferMode */
static void h_8009fbac(RCpu *c) { c->r[2] = (uint32_t)(SpuIsTransferCompleted((int32_t)hle_arg(c, 0))); }   /* SpuIsTransferCompleted */
static void h_8009fc5c(RCpu *c) { SpuSetCommonAttr((const SpuCommonAttr *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SpuSetCommonAttr */
static void h_800a00b0(RCpu *c) { SpuGetAllKeysStatus((char *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SpuGetAllKeysStatus */
static void h_800a013c(RCpu *c) { SpuSetVoiceAttr((const SpuVoiceAttr *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SpuSetVoiceAttr */
static void h_800a093c(RCpu *c) { c->r[2] = (uint32_t)VSync((int)hle_arg(c, 0)); rc_vsync_hook(); }   /* VSync */
static void h_800a0b4c(RCpu *c) { c->r[2] = (uint32_t)(ResetCallback()); }   /* ResetCallback */
static void h_800a0bdc(RCpu *c) { c->r[2] = (uint32_t)(VSyncCallback((uint32_t)hle_arg(c, 0))); }   /* VSyncCallback */
static void h_800a0c40(RCpu *c) { c->r[2] = (uint32_t)(StopCallback()); }   /* StopCallback */
static void h_800a15cc(RCpu *c) { c->r[2] = (uint32_t)(SetVideoMode((int)hle_arg(c, 0))); }   /* SetVideoMode */
static void h_800a15fc(RCpu *c) { c->r[2] = (uint32_t)(CdInit()); }   /* CdInit */
static void h_800a1788(RCpu *c) { c->r[2] = (uint32_t)(CdReset((int)hle_arg(c, 0))); }   /* CdReset */
static void h_800a17f4(RCpu *c) { CdFlush(); c->r[2] = 0; }   /* CdFlush */
static void h_800a1814(RCpu *c) { c->r[2] = (uint32_t)(CdSetDebug((int)hle_arg(c, 0))); }   /* CdSetDebug */
static void h_800a1890(RCpu *c) { c->r[2] = (uint32_t)(CdSync((int)hle_arg(c, 0), (uint8_t *)rc_hptr(hle_arg(c, 1)))); }   /* CdSync */
static void h_800a18d0(RCpu *c) { c->r[2] = (uint32_t)(CdSyncCallback((CdlCB)hle_arg(c, 0))); }   /* CdSyncCallback */
static void h_800a18e4(RCpu *c) { c->r[2] = (uint32_t)(CdReadyCallback((CdlCB)hle_arg(c, 0))); }   /* CdReadyCallback */
static void h_800a18f8(RCpu *c) { c->r[2] = (uint32_t)(CdControl((uint8_t)hle_arg(c, 0), (const uint8_t *)rc_hptr(hle_arg(c, 1)), (uint8_t *)rc_hptr(hle_arg(c, 2)))); }   /* CdControl */
static void h_800a1a34(RCpu *c) { c->r[2] = (uint32_t)(CdControlF((uint8_t)hle_arg(c, 0), (const uint8_t *)rc_hptr(hle_arg(c, 1)))); }   /* CdControlF */
static void h_800a1b68(RCpu *c) { c->r[2] = (uint32_t)(CdControlB((uint8_t)hle_arg(c, 0), (const uint8_t *)rc_hptr(hle_arg(c, 1)), (uint8_t *)rc_hptr(hle_arg(c, 2)))); }   /* CdControlB */
static void h_800a1cb4(RCpu *c) { c->r[2] = (uint32_t)(CdMix((const CdlATV *)rc_hptr(hle_arg(c, 0)))); }   /* CdMix */
static void h_800a1cd4(RCpu *c) { c->r[2] = (uint32_t)(CdGetSector((void *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1))); }   /* CdGetSector */
static void h_800a1cf4(RCpu *c) { c->r[2] = (uint32_t)(CdGetSector2((void *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1))); }   /* CdGetSector2 */
static void h_800a1d14(RCpu *c) { c->r[2] = (uint32_t)(CdDataCallback((CdlCB)hle_arg(c, 0))); }   /* CdDataCallback */
static void h_800a34cc(RCpu *c) { c->r[2] = rc_pptr(CdSearchFile((CdlFILE *)rc_hptr(hle_arg(c, 0)), (const char *)rc_hptr(hle_arg(c, 1)))); }   /* CdSearchFile */
static void h_800a453c(RCpu *c) { c->r[2] = (uint32_t)(CdRead((int)hle_arg(c, 0), (uint32_t *)rc_hptr(hle_arg(c, 1)), (int)hle_arg(c, 2))); }   /* CdRead */
static void h_800a46d8(RCpu *c) { c->r[2] = (uint32_t)(CdReadSync((int)hle_arg(c, 0), (uint8_t *)rc_hptr(hle_arg(c, 1)))); }   /* CdReadSync */
static void h_800a480c(RCpu *c) { c->r[2] = rc_pptr(CdIntToPos((int)hle_arg(c, 0), (CdlLOC *)rc_hptr(hle_arg(c, 1)))); }   /* CdIntToPos */
static void h_800a4910(RCpu *c) { c->r[2] = (uint32_t)(CdPosToInt((const CdlLOC *)rc_hptr(hle_arg(c, 0)))); }   /* CdPosToInt */
static void h_800a499c(RCpu *c) { c->r[2] = (uint32_t)(rsin((int)hle_arg(c, 0))); }   /* rsin */
static void h_800a4a6c(RCpu *c) { c->r[2] = (uint32_t)(rcos((int)hle_arg(c, 0))); }   /* rcos */
static void h_800a4b14(RCpu *c) { InitGeom(); c->r[2] = 0; }   /* InitGeom */
static void h_800a4b9c(RCpu *c) { c->r[2] = (uint32_t)(SquareRoot0((long)hle_arg(c, 0))); }   /* SquareRoot0 */
static void h_800a4cfc(RCpu *c) { c->r[2] = (uint32_t)(VectorNormalSS((SVECTOR *)rc_hptr(hle_arg(c, 0)), (SVECTOR *)rc_hptr(hle_arg(c, 1)))); }   /* VectorNormalSS */
static void h_800a4edc(RCpu *c) { c->r[2] = rc_pptr(MulMatrix0((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)), (MATRIX *)rc_hptr(hle_arg(c, 2)))); }   /* MulMatrix0 */
static void h_800a4fec(RCpu *c) { c->r[2] = rc_pptr(CompMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)), (MATRIX *)rc_hptr(hle_arg(c, 2)))); }   /* CompMatrix */
static void h_800a514c(RCpu *c) { c->r[2] = rc_pptr(ApplyMatrixLV((MATRIX *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)), (VECTOR *)rc_hptr(hle_arg(c, 2)))); }   /* ApplyMatrixLV */
static void h_800a52ac(RCpu *c) { c->r[2] = rc_pptr(SetMulMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* SetMulMatrix */
static void h_800a53bc(RCpu *c) { c->r[2] = rc_pptr(ApplyRotMatrixLV((VECTOR *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)))); }   /* ApplyRotMatrixLV */
static void h_800a54fc(RCpu *c) { c->r[2] = rc_pptr(MulMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* MulMatrix */
static void h_800a560c(RCpu *c) { c->r[2] = rc_pptr(MulMatrix2((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* MulMatrix2 */
static void h_800a571c(RCpu *c) { c->r[2] = rc_pptr(ApplyMatrixSV((MATRIX *)rc_hptr(hle_arg(c, 0)), (SVECTOR *)rc_hptr(hle_arg(c, 1)), (SVECTOR *)rc_hptr(hle_arg(c, 2)))); }   /* ApplyMatrixSV */
static void h_800a577c(RCpu *c) { c->r[2] = rc_pptr(TransMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)))); }   /* TransMatrix */
static void h_800a57ac(RCpu *c) { c->r[2] = rc_pptr(ScaleMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)))); }   /* ScaleMatrix */
static void h_800a58ec(RCpu *c) { SetRotMatrix((MATRIX *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SetRotMatrix */
static void h_800a591c(RCpu *c) { SetColorMatrix((MATRIX *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SetColorMatrix */
static void h_800a594c(RCpu *c) { SetTransMatrix((MATRIX *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SetTransMatrix */
static void h_800a596c(RCpu *c) { ReadGeomOffset((int32_t *)rc_hptr(hle_arg(c, 0)), (int32_t *)rc_hptr(hle_arg(c, 1))); c->r[2] = 0; }   /* ReadGeomOffset */
static void h_800a598c(RCpu *c) { c->r[2] = (uint32_t)(ReadGeomScreen()); }   /* ReadGeomScreen */
static void h_800a599c(RCpu *c) { SetBackColor((int32_t)hle_arg(c, 0), (int32_t)hle_arg(c, 1), (int32_t)hle_arg(c, 2)); c->r[2] = 0; }   /* SetBackColor */
static void h_800a59bc(RCpu *c) { SetGeomOffset((int32_t)hle_arg(c, 0), (int32_t)hle_arg(c, 1)); c->r[2] = 0; }   /* SetGeomOffset */
static void h_800a59dc(RCpu *c) { SetGeomScreen((int32_t)hle_arg(c, 0)); c->r[2] = 0; }   /* SetGeomScreen */
static void h_800a5b88(RCpu *c) { OuterProduct0((VECTOR *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)), (VECTOR *)rc_hptr(hle_arg(c, 2))); c->r[2] = 0; }   /* OuterProduct0 */
static void h_800a5bfc(RCpu *c) { c->r[2] = (uint32_t)(RotTransPers((SVECTOR *)rc_hptr(hle_arg(c, 0)), (int32_t *)rc_hptr(hle_arg(c, 1)), (int32_t *)rc_hptr(hle_arg(c, 2)), (int32_t *)rc_hptr(hle_arg(c, 3)))); }   /* RotTransPers */
static void h_800a5c2c(RCpu *c) { c->r[2] = (uint32_t)(RotTransPers3((SVECTOR *)rc_hptr(hle_arg(c, 0)), (SVECTOR *)rc_hptr(hle_arg(c, 1)), (SVECTOR *)rc_hptr(hle_arg(c, 2)), (int32_t *)rc_hptr(hle_arg(c, 3)), (int32_t *)rc_hptr(hle_arg(c, 4)), (int32_t *)rc_hptr(hle_arg(c, 5)), (int32_t *)rc_hptr(hle_arg(c, 6)), (int32_t *)rc_hptr(hle_arg(c, 7)))); }   /* RotTransPers3 */
static void h_800a5c8c(RCpu *c) { RotTrans((SVECTOR *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)), (int32_t *)rc_hptr(hle_arg(c, 2))); c->r[2] = 0; }   /* RotTrans */
static void h_800a5cbc(RCpu *c) { c->r[2] = (uint32_t)(NormalClip((int32_t)hle_arg(c, 0), (int32_t)hle_arg(c, 1), (int32_t)hle_arg(c, 2))); }   /* NormalClip */
static void h_800a5cec(RCpu *c) { c->r[2] = rc_pptr(TransposeMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* TransposeMatrix */
static void h_800a5d2c(RCpu *c) { c->r[2] = rc_pptr(RotMatrix((SVECTOR *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* RotMatrix */
static void h_800a5fbc(RCpu *c) { c->r[2] = rc_pptr(RotMatrixYXZ((SVECTOR *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* RotMatrixYXZ */
static void h_800a624c(RCpu *c) { c->r[2] = rc_pptr(RotMatrix_gte((SVECTOR *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* RotMatrix_gte */
static void h_800a645c(RCpu *c) { c->r[2] = (uint32_t)(ratan2((long)hle_arg(c, 0), (long)hle_arg(c, 1))); }   /* ratan2 */
static void h_800a66bc(RCpu *c) { c->r[2] = (uint32_t)(ResetGraph((int)hle_arg(c, 0))); }   /* ResetGraph */
static void h_800a69a0(RCpu *c) { c->r[2] = (uint32_t)(SetDispMask((int)hle_arg(c, 0))); }   /* SetDispMask */
static void h_800a6a38(RCpu *c) { c->r[2] = (uint32_t)(DrawSync((int)hle_arg(c, 0))); }   /* DrawSync */
static void h_800a6bbc(RCpu *c) { c->r[2] = (uint32_t)(ClearImage((RECT *)rc_hptr(hle_arg(c, 0)), (uint8_t)hle_arg(c, 1), (uint8_t)hle_arg(c, 2), (uint8_t)hle_arg(c, 3))); }   /* ClearImage */
static void h_800a6c4c(RCpu *c) { c->r[2] = (uint32_t)(ClearImage2((RECT *)rc_hptr(hle_arg(c, 0)), (uint8_t)hle_arg(c, 1), (uint8_t)hle_arg(c, 2), (uint8_t)hle_arg(c, 3))); }   /* ClearImage2 */
static void h_800a6ce4(RCpu *c) { c->r[2] = (uint32_t)(LoadImage((RECT *)rc_hptr(hle_arg(c, 0)), (uint32_t *)rc_hptr(hle_arg(c, 1)))); }   /* LoadImage */
static void h_800a6d44(RCpu *c) { c->r[2] = (uint32_t)(StoreImage((RECT *)rc_hptr(hle_arg(c, 0)), (uint32_t *)rc_hptr(hle_arg(c, 1)))); }   /* StoreImage */
static void h_800a6da4(RCpu *c) { c->r[2] = (uint32_t)(MoveImage((RECT *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2))); }   /* MoveImage */
static void h_800a6f24(RCpu *c) { c->r[2] = rc_pptr(ClearOTagR((uint32_t *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1))); }   /* ClearOTagR */
static void h_800a702c(RCpu *c) { DrawOTag((uint32_t *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* DrawOTag */
static void h_800a709c(RCpu *c) { c->r[2] = rc_pptr(PutDrawEnv((DRAWENV *)rc_hptr(hle_arg(c, 0)))); }   /* PutDrawEnv */
static void h_800a7268(RCpu *c) { c->r[2] = rc_pptr(PutDispEnv((DISPENV *)rc_hptr(hle_arg(c, 0)))); }   /* PutDispEnv */
static void h_800a7794(RCpu *c) { c->r[2] = (uint32_t)(GetODE()); }   /* GetODE */
static void h_800a77c4(RCpu *c) { SetDrawArea((DR_AREA *)rc_hptr(hle_arg(c, 0)), (RECT *)rc_hptr(hle_arg(c, 1))); c->r[2] = 0; }   /* SetDrawArea */
static void h_800a9270(RCpu *c) { c->r[2] = (uint32_t)(StoreImage2((RECT *)rc_hptr(hle_arg(c, 0)), (uint32_t *)rc_hptr(hle_arg(c, 1)))); }   /* StoreImage2 */
static void h_800a95ec(RCpu *c) { c->r[2] = (uint32_t)(OpenTIM((uint32_t *)rc_hptr(hle_arg(c, 0)))); }   /* OpenTIM */
static void h_800a95fc(RCpu *c) { c->r[2] = rc_pptr(ReadTIM((TIM_IMAGE *)rc_hptr(hle_arg(c, 0)))); }   /* ReadTIM */
static void h_800aae6c(RCpu *c) { c->r[2] = rc_pptr(SetDefDrawEnv((DRAWENV *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3), (int)hle_arg(c, 4))); }   /* SetDefDrawEnv */
static void h_800aaf2c(RCpu *c) { c->r[2] = rc_pptr(SetDefDispEnv((DISPENV *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3), (int)hle_arg(c, 4))); }   /* SetDefDispEnv */
static void h_800aaf6c(RCpu *c) { c->r[2] = (uint32_t)(GetTPage((int)hle_arg(c, 0), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3))); }   /* GetTPage */
static void h_800aafac(RCpu *c) { CatPrim((void *)rc_hptr(hle_arg(c, 0)), (void *)rc_hptr(hle_arg(c, 1))); c->r[2] = 0; }   /* CatPrim */
static void h_800aafdc(RCpu *c) { SetDrawTPage((DR_TPAGE *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3)); c->r[2] = 0; }   /* SetDrawTPage */
static void h_800ab00c(RCpu *c) { SetDrawMode((DR_MODE *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3), (RECT *)rc_hptr(hle_arg(c, 4))); c->r[2] = 0; }   /* SetDrawMode */
static void h_800ab0ac(RCpu *c) { DecDCTReset((int)hle_arg(c, 0)); c->r[2] = 0; }   /* DecDCTReset */
static void h_800ab204(RCpu *c) { DecDCTin((uint32_t *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1)); c->r[2] = 0; }   /* DecDCTin */
static void h_800ab280(RCpu *c) { DecDCTout((uint32_t *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1)); c->r[2] = 0; }   /* DecDCTout */
static void h_800ab2dc(RCpu *c) { c->r[2] = (uint32_t)(DecDCToutSync((int)hle_arg(c, 0))); }   /* DecDCToutSync */
static void h_800ab348(RCpu *c) { c->r[2] = (uint32_t)(DecDCToutCallback((uint32_t)hle_arg(c, 0))); }   /* DecDCToutCallback */
static void h_800ab73c(RCpu *c) { c->r[2] = (uint32_t)(DecDCTvlcSize2((int32_t)hle_arg(c, 0))); }   /* DecDCTvlcSize2 */
static void h_800ab76c(RCpu *c) { c->r[2] = (uint32_t)(DecDCTvlc2((uint32_t *)rc_hptr(hle_arg(c, 0)), (uint32_t *)rc_hptr(hle_arg(c, 1)), (uint16_t *)rc_hptr(hle_arg(c, 2)))); }   /* DecDCTvlc2 */
static void h_800ababc(RCpu *c) { DecDCTvlcBuild((uint16_t *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* DecDCTvlcBuild */
static void h_800abbac(RCpu *c) { _bu_init(); c->r[2] = 0; }   /* _bu_init */
static void h_800abbbc(RCpu *c) { c->r[2] = (uint32_t)(_card_info((int32_t)hle_arg(c, 0))); }   /* _card_info */
static void h_800abbcc(RCpu *c) { c->r[2] = (uint32_t)(_card_load((int32_t)hle_arg(c, 0))); }   /* _card_load */
static void h_800abbdc(RCpu *c) { c->r[2] = (uint32_t)(_card_clear((int32_t)hle_arg(c, 0))); }   /* _card_clear */
static void h_800abc3c(RCpu *c) { InitCARD((int32_t)hle_arg(c, 0)); c->r[2] = 0; }   /* InitCARD */
static void h_800abccc(RCpu *c) { c->r[2] = (uint32_t)(StartCARD()); }   /* StartCARD */
static void h_800abd14(RCpu *c) { c->r[2] = (uint32_t)(StopCARD()); }   /* StopCARD */
static void h_800ac00c(RCpu *c) { c->r[2] = (uint32_t)(_card_format((int32_t)hle_arg(c, 0))); }   /* _card_format */
static void h_800ac48c(RCpu *c) { { uint32_t i; c->r[2] = 0; for (i = 0; i < c->r[6]; i++) if (rc_lb(c->r[4] + i, 0) == (c->r[5] & 0xFF)) { c->r[2] = c->r[4] + i; break; } } }   /* memchr */
static void h_800ac51c(RCpu *c) { { uint32_t i; for (i = 0; i < c->r[6]; i++) rc_sb(c->r[4] + i, c->r[5], 0); c->r[2] = c->r[4]; } }   /* memset */
static void h_800ac54c(RCpu *c) { c->r[2] = (uint32_t)(psx_rand()); }   /* rand */
static void h_800ac57c(RCpu *c) { psx_srand((unsigned int)hle_arg(c, 0)); c->r[2] = 0; }   /* srand */
static void h_800ac58c(RCpu *c) { { uint32_t d = c->r[4], i = 0; uint8_t b; while (rc_lb(d, 0)) d++; do { b = (uint8_t)rc_lb(c->r[5] + i, 0); rc_sb(d + i, b, 0); i++; } while (b); c->r[2] = c->r[4]; } }   /* strcat */
static void h_800ac6ac(RCpu *c) { { uint32_t i = 0; uint8_t b; do { b = (uint8_t)rc_lb(c->r[5] + i, 0); rc_sb(c->r[4] + i, b, 0); i++; } while (b); c->r[2] = c->r[4]; } }   /* strcpy */
static void h_800ac6fc(RCpu *c) { { uint32_t n = 0; while (rc_lb(c->r[4] + n, 0)) n++; c->r[2] = n; } }   /* strlen */
static void h_800ac7bc(RCpu *c) { { uint32_t a[24]; int k; for (k = 0; k < 24; k++) a[k] = hle_arg(c, 1 + k); c->r[2] = (uint32_t)psx_printf((const char *)rc_hptr(c->r[4]), a); } }   /* printf */
static void h_800ad0fc(RCpu *c) { { uint32_t a[24]; int k; char buf[2048]; for (k = 0; k < 24; k++) a[k] = hle_arg(c, 2 + k); int n = psx_sformat(buf, sizeof buf, (const char *)rc_hptr(c->r[5]), a); for (k = 0; k <= n && k < (int)sizeof buf; k++) rc_sb(c->r[4] + k, k < n ? (uint8_t)buf[k] : 0, 0); c->r[2] = (uint32_t)n; } }   /* sprintf */
static void h_800adb2c(RCpu *c) { c->r[2] = (uint32_t)(OpenEvent((uint32_t)hle_arg(c, 0), (uint32_t)hle_arg(c, 1), (int32_t)hle_arg(c, 2), (uint32_t)hle_arg(c, 3))); }   /* OpenEvent */
static void h_800adb3c(RCpu *c) { c->r[2] = (uint32_t)(CloseEvent((uint32_t)hle_arg(c, 0))); }   /* CloseEvent */
static void h_800adb5c(RCpu *c) { c->r[2] = (uint32_t)(TestEvent((uint32_t)hle_arg(c, 0))); }   /* TestEvent */
static void h_800adb6c(RCpu *c) { c->r[2] = (uint32_t)(EnableEvent((uint32_t)hle_arg(c, 0))); }   /* EnableEvent */
static void h_800adb7c(RCpu *c) { c->r[2] = (uint32_t)(DisableEvent((uint32_t)hle_arg(c, 0))); }   /* DisableEvent */
static void h_800adbbc(RCpu *c) { EnterCriticalSection(); c->r[2] = 0; }   /* EnterCriticalSection */
static void h_800adbcc(RCpu *c) { ExitCriticalSection(); c->r[2] = 0; }   /* ExitCriticalSection */
static void h_800adbdc(RCpu *c) { c->r[2] = (uint32_t)(bios_open((const char *)rc_hptr(hle_arg(c, 0)), (int32_t)hle_arg(c, 1))); }   /* open */
static void h_800adbec(RCpu *c) { c->r[2] = (uint32_t)(bios_read((int32_t)hle_arg(c, 0), (void *)rc_hptr(hle_arg(c, 1)), (int32_t)hle_arg(c, 2))); }   /* read */
static void h_800adbfc(RCpu *c) { c->r[2] = (uint32_t)(bios_write((int32_t)hle_arg(c, 0), (const void *)rc_hptr(hle_arg(c, 1)), (int32_t)hle_arg(c, 2))); }   /* write */
static void h_800adc0c(RCpu *c) { c->r[2] = (uint32_t)(bios_close((int32_t)hle_arg(c, 0))); }   /* close */
static void h_800adc1c(RCpu *c) { c->r[2] = rc_pptr(bios_nextfile((DIRENTRY *)rc_hptr(hle_arg(c, 0)))); }   /* nextfile */
static void h_800adc6c(RCpu *c) { c->r[2] = (uint32_t)(SetRCnt((uint32_t)hle_arg(c, 0), (uint16_t)hle_arg(c, 1), (int32_t)hle_arg(c, 2))); }   /* SetRCnt */
static void h_800add08(RCpu *c) { c->r[2] = (uint32_t)(GetRCnt((uint32_t)hle_arg(c, 0))); }   /* GetRCnt */
static void h_800add40(RCpu *c) { c->r[2] = (uint32_t)(StartRCnt((uint32_t)hle_arg(c, 0))); }   /* StartRCnt */
static void h_800add70(RCpu *c) { c->r[2] = (uint32_t)(StopRCnt((uint32_t)hle_arg(c, 0))); }   /* StopRCnt */
static void h_800adda4(RCpu *c) { c->r[2] = (uint32_t)(ResetRCnt((uint32_t)hle_arg(c, 0))); }   /* ResetRCnt */
static void h_800adddc(RCpu *c) { c->r[2] = rc_pptr(bios_firstfile((const char *)rc_hptr(hle_arg(c, 0)), (DIRENTRY *)rc_hptr(hle_arg(c, 1)))); }   /* firstfile */

const RcFunc hle_table_us[] = {
    {0x80025394u, h_80025394},
    {0x8009a1b4u, h_8009a1b4},
    {0x8009b5b4u, h_8009b5b4},
    {0x8009b5d4u, h_8009b5d4},
    {0x8009b5f4u, h_8009b5f4},
    {0x8009b660u, h_8009b660},
    {0x8009b72cu, h_8009b72c},
    {0x8009b824u, h_8009b824},
    {0x8009b9a0u, h_8009b9a0},
    {0x8009b9d8u, h_8009b9d8},
    {0x8009ba20u, h_8009ba20},
    {0x8009c674u, h_8009c674},
    {0x8009d254u, h_8009d254},
    {0x8009d2ecu, h_8009d2ec},
    {0x8009da04u, h_8009da04},
    {0x8009e5ecu, h_8009e5ec},
    {0x8009e60cu, h_8009e60c},
    {0x8009e68cu, h_8009e68c},
    {0x8009e86cu, h_8009e86c},
    {0x8009f21cu, h_8009f21c},
    {0x8009f29cu, h_8009f29c},
    {0x8009f58cu, h_8009f58c},
    {0x8009f72cu, h_8009f72c},
    {0x8009f79cu, h_8009f79c},
    {0x8009f95cu, h_8009f95c},
    {0x8009f9bcu, h_8009f9bc},
    {0x8009fb1cu, h_8009fb1c},
    {0x8009fb7cu, h_8009fb7c},
    {0x8009fbacu, h_8009fbac},
    {0x8009fc5cu, h_8009fc5c},
    {0x800a00b0u, h_800a00b0},
    {0x800a013cu, h_800a013c},
    {0x800a093cu, h_800a093c},
    {0x800a0b4cu, h_800a0b4c},
    {0x800a0bdcu, h_800a0bdc},
    {0x800a0c40u, h_800a0c40},
    {0x800a15ccu, h_800a15cc},
    {0x800a15fcu, h_800a15fc},
    {0x800a1788u, h_800a1788},
    {0x800a17f4u, h_800a17f4},
    {0x800a1814u, h_800a1814},
    {0x800a1890u, h_800a1890},
    {0x800a18d0u, h_800a18d0},
    {0x800a18e4u, h_800a18e4},
    {0x800a18f8u, h_800a18f8},
    {0x800a1a34u, h_800a1a34},
    {0x800a1b68u, h_800a1b68},
    {0x800a1cb4u, h_800a1cb4},
    {0x800a1cd4u, h_800a1cd4},
    {0x800a1cf4u, h_800a1cf4},
    {0x800a1d14u, h_800a1d14},
    {0x800a34ccu, h_800a34cc},
    {0x800a453cu, h_800a453c},
    {0x800a46d8u, h_800a46d8},
    {0x800a480cu, h_800a480c},
    {0x800a4910u, h_800a4910},
    {0x800a499cu, h_800a499c},
    {0x800a4a6cu, h_800a4a6c},
    {0x800a4b14u, h_800a4b14},
    {0x800a4b9cu, h_800a4b9c},
    {0x800a4cfcu, h_800a4cfc},
    {0x800a4edcu, h_800a4edc},
    {0x800a4fecu, h_800a4fec},
    {0x800a514cu, h_800a514c},
    {0x800a52acu, h_800a52ac},
    {0x800a53bcu, h_800a53bc},
    {0x800a54fcu, h_800a54fc},
    {0x800a560cu, h_800a560c},
    {0x800a571cu, h_800a571c},
    {0x800a577cu, h_800a577c},
    {0x800a57acu, h_800a57ac},
    {0x800a58ecu, h_800a58ec},
    {0x800a591cu, h_800a591c},
    {0x800a594cu, h_800a594c},
    {0x800a596cu, h_800a596c},
    {0x800a598cu, h_800a598c},
    {0x800a599cu, h_800a599c},
    {0x800a59bcu, h_800a59bc},
    {0x800a59dcu, h_800a59dc},
    {0x800a5b88u, h_800a5b88},
    {0x800a5bfcu, h_800a5bfc},
    {0x800a5c2cu, h_800a5c2c},
    {0x800a5c8cu, h_800a5c8c},
    {0x800a5cbcu, h_800a5cbc},
    {0x800a5cecu, h_800a5cec},
    {0x800a5d2cu, h_800a5d2c},
    {0x800a5fbcu, h_800a5fbc},
    {0x800a624cu, h_800a624c},
    {0x800a645cu, h_800a645c},
    {0x800a66bcu, h_800a66bc},
    {0x800a69a0u, h_800a69a0},
    {0x800a6a38u, h_800a6a38},
    {0x800a6bbcu, h_800a6bbc},
    {0x800a6c4cu, h_800a6c4c},
    {0x800a6ce4u, h_800a6ce4},
    {0x800a6d44u, h_800a6d44},
    {0x800a6da4u, h_800a6da4},
    {0x800a6f24u, h_800a6f24},
    {0x800a702cu, h_800a702c},
    {0x800a709cu, h_800a709c},
    {0x800a7268u, h_800a7268},
    {0x800a7794u, h_800a7794},
    {0x800a77c4u, h_800a77c4},
    {0x800a9270u, h_800a9270},
    {0x800a95ecu, h_800a95ec},
    {0x800a95fcu, h_800a95fc},
    {0x800aae6cu, h_800aae6c},
    {0x800aaf2cu, h_800aaf2c},
    {0x800aaf6cu, h_800aaf6c},
    {0x800aafacu, h_800aafac},
    {0x800aafdcu, h_800aafdc},
    {0x800ab00cu, h_800ab00c},
    {0x800ab0acu, h_800ab0ac},
    {0x800ab204u, h_800ab204},
    {0x800ab280u, h_800ab280},
    {0x800ab2dcu, h_800ab2dc},
    {0x800ab348u, h_800ab348},
    {0x800ab73cu, h_800ab73c},
    {0x800ab76cu, h_800ab76c},
    {0x800ababcu, h_800ababc},
    {0x800abbacu, h_800abbac},
    {0x800abbbcu, h_800abbbc},
    {0x800abbccu, h_800abbcc},
    {0x800abbdcu, h_800abbdc},
    {0x800abc3cu, h_800abc3c},
    {0x800abcccu, h_800abccc},
    {0x800abd14u, h_800abd14},
    {0x800ac00cu, h_800ac00c},
    {0x800ac48cu, h_800ac48c},
    {0x800ac51cu, h_800ac51c},
    {0x800ac54cu, h_800ac54c},
    {0x800ac57cu, h_800ac57c},
    {0x800ac58cu, h_800ac58c},
    {0x800ac6acu, h_800ac6ac},
    {0x800ac6fcu, h_800ac6fc},
    {0x800ac7bcu, h_800ac7bc},
    {0x800ad0fcu, h_800ad0fc},
    {0x800adb2cu, h_800adb2c},
    {0x800adb3cu, h_800adb3c},
    {0x800adb5cu, h_800adb5c},
    {0x800adb6cu, h_800adb6c},
    {0x800adb7cu, h_800adb7c},
    {0x800adbbcu, h_800adbbc},
    {0x800adbccu, h_800adbcc},
    {0x800adbdcu, h_800adbdc},
    {0x800adbecu, h_800adbec},
    {0x800adbfcu, h_800adbfc},
    {0x800adc0cu, h_800adc0c},
    {0x800adc1cu, h_800adc1c},
    {0x800adc6cu, h_800adc6c},
    {0x800add08u, h_800add08},
    {0x800add40u, h_800add40},
    {0x800add70u, h_800add70},
    {0x800adda4u, h_800adda4},
    {0x800adddcu, h_800adddc},
};
const unsigned hle_count_us = 155;

