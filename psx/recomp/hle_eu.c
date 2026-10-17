/* GENERADO por tools/recomp/gen_hle.py --libre: enlace del codigo del juego con psx/ (version EU).
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

static void h_80025214(RCpu *c) { c->r[2] = 0; }   /* __main */
static void h_80099de0(RCpu *c) { c->r[2] = (uint32_t)(PadEnableCom((int)hle_arg(c, 0))); }   /* PadEnableCom */
static void h_8009b1e0(RCpu *c) { c->r[2] = (uint32_t)(PadChkVsync()); }   /* PadChkVsync */
static void h_8009b200(RCpu *c) { PadStartCom(); c->r[2] = 0; }   /* PadStartCom */
static void h_8009b220(RCpu *c) { PadStopCom(); c->r[2] = 0; }   /* PadStopCom */
static void h_8009b28c(RCpu *c) { c->r[2] = (uint32_t)(PadGetState((int)hle_arg(c, 0))); }   /* PadGetState */
static void h_8009b358(RCpu *c) { c->r[2] = (uint32_t)(PadInfoMode((int)hle_arg(c, 0), (int)hle_arg(c, 1), (int)hle_arg(c, 2))); }   /* PadInfoMode */
static void h_8009b450(RCpu *c) { c->r[2] = (uint32_t)(PadInfoAct((int)hle_arg(c, 0), (int)hle_arg(c, 1), (int)hle_arg(c, 2))); }   /* PadInfoAct */
static void h_8009b5cc(RCpu *c) { c->r[2] = (uint32_t)(PadSetActAlign((int)hle_arg(c, 0), (const uint8_t *)rc_hptr(hle_arg(c, 1)))); }   /* PadSetActAlign */
static void h_8009b604(RCpu *c) { c->r[2] = (uint32_t)(PadSetMainMode((int)hle_arg(c, 0), (int)hle_arg(c, 1), (int)hle_arg(c, 2))); }   /* PadSetMainMode */
static void h_8009b64c(RCpu *c) { PadSetAct((int)hle_arg(c, 0), (uint8_t *)rc_hptr(hle_arg(c, 1)), (int)hle_arg(c, 2)); c->r[2] = 0; }   /* PadSetAct */
static void h_8009c2a0(RCpu *c) { PadInitDirect((uint8_t *)rc_hptr(hle_arg(c, 0)), (uint8_t *)rc_hptr(hle_arg(c, 1))); c->r[2] = 0; }   /* PadInitDirect */
static void h_8009ce80(RCpu *c) { { int64_t a = (int64_t)((uint64_t)c->r[5] << 32 | c->r[4]), b = (int64_t)((uint64_t)c->r[7] << 32 | c->r[6]); uint64_t q = (uint64_t)psx_divdi3(a, b); c->r[2] = (uint32_t)q; c->r[3] = (uint32_t)(q >> 32); } }   /* __divdi3 */
static void h_8009cf18(RCpu *c) { { uint64_t a = (uint64_t)c->r[5] << 32 | c->r[4], b = (uint64_t)c->r[7] << 32 | c->r[6]; uint64_t q = psx_udivdi3(a, b); c->r[2] = (uint32_t)q; c->r[3] = (uint32_t)(q >> 32); } }   /* __udivdi3 */
static void h_8009d630(RCpu *c) { SpuStart(); c->r[2] = 0; }   /* SpuStart */
static void h_8009e218(RCpu *c) { SpuInit(); c->r[2] = 0; }   /* SsUtReverbOff */
static void h_8009e238(RCpu *c) { SpuQuit(); c->r[2] = 0; }   /* SpuQuit */
static void h_8009e2b8(RCpu *c) { c->r[2] = (uint32_t)(SpuSetReverb((int32_t)hle_arg(c, 0))); }   /* SpuSetReverb */
static void h_8009e498(RCpu *c) { c->r[2] = (uint32_t)(SpuSetReverbModeParam((const SpuReverbAttr *)rc_hptr(hle_arg(c, 0)))); }   /* SpuSetReverbModeParam */
static void h_8009ee48(RCpu *c) { c->r[2] = (uint32_t)(SpuSetReverbDepth((const SpuReverbAttr *)rc_hptr(hle_arg(c, 0)))); }   /* SpuSetReverbDepth */
static void h_8009eec8(RCpu *c) { c->r[2] = (uint32_t)(SpuSetReverbVoice((int32_t)hle_arg(c, 0), (uint32_t)hle_arg(c, 1))); }   /* SpuSetReverbVoice */
static void h_8009f1b8(RCpu *c) { c->r[2] = (uint32_t)(SpuClearReverbWorkArea((int32_t)hle_arg(c, 0))); }   /* SpuClearReverbWorkArea */
static void h_8009f358(RCpu *c) { c->r[2] = (uint32_t)(SpuReadDecodedData((SpuDecodedData *)rc_hptr(hle_arg(c, 0)), (int32_t)hle_arg(c, 1))); }   /* SpuReadDecodedData */
static void h_8009f3c8(RCpu *c) { SpuSetKey((int32_t)hle_arg(c, 0), (uint32_t)hle_arg(c, 1)); c->r[2] = 0; }   /* SpuSetKey */
static void h_8009f588(RCpu *c) { c->r[2] = (uint32_t)(SpuWrite((const uint8_t *)rc_hptr(hle_arg(c, 0)), (uint32_t)hle_arg(c, 1))); }   /* SpuRead */
static void h_8009f5e8(RCpu *c) { c->r[2] = (uint32_t)(SpuWrite0((uint32_t)hle_arg(c, 0))); }   /* SpuWrite0 */
static void h_8009f748(RCpu *c) { c->r[2] = (uint32_t)(SpuSetTransferStartAddr((uint32_t)hle_arg(c, 0))); }   /* SpuSetTransferStartAddr */
static void h_8009f7a8(RCpu *c) { c->r[2] = (uint32_t)(SpuSetTransferMode((int32_t)hle_arg(c, 0))); }   /* SpuSetTransferMode */
static void h_8009f7d8(RCpu *c) { c->r[2] = (uint32_t)(SpuIsTransferCompleted((int32_t)hle_arg(c, 0))); }   /* SpuIsTransferCompleted */
static void h_8009f888(RCpu *c) { SpuSetCommonAttr((const SpuCommonAttr *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SpuSetCommonAttr */
static void h_8009fcdc(RCpu *c) { SpuGetAllKeysStatus((char *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SpuGetAllKeysStatus */
static void h_8009fd68(RCpu *c) { SpuSetVoiceAttr((const SpuVoiceAttr *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SpuSetVoiceAttr */
static void h_800a0568(RCpu *c) { c->r[2] = (uint32_t)VSync((int)hle_arg(c, 0)); rc_vsync_hook(); }   /* VSync */
static void h_800a0778(RCpu *c) { c->r[2] = (uint32_t)(ResetCallback()); }   /* ResetCallback */
static void h_800a0808(RCpu *c) { c->r[2] = (uint32_t)(VSyncCallback((uint32_t)hle_arg(c, 0))); }   /* VSyncCallback */
static void h_800a086c(RCpu *c) { c->r[2] = (uint32_t)(StopCallback()); }   /* StopCallback */
static void h_800a11f8(RCpu *c) { c->r[2] = (uint32_t)(SetVideoMode((int)hle_arg(c, 0))); }   /* SetVideoMode */
static void h_800a1228(RCpu *c) { c->r[2] = (uint32_t)(CdInit()); }   /* CdInit */
static void h_800a13b4(RCpu *c) { c->r[2] = (uint32_t)(CdReset((int)hle_arg(c, 0))); }   /* CdReset */
static void h_800a1420(RCpu *c) { CdFlush(); c->r[2] = 0; }   /* CdFlush */
static void h_800a1440(RCpu *c) { c->r[2] = (uint32_t)(CdSetDebug((int)hle_arg(c, 0))); }   /* CdSetDebug */
static void h_800a14bc(RCpu *c) { c->r[2] = (uint32_t)(CdSync((int)hle_arg(c, 0), (uint8_t *)rc_hptr(hle_arg(c, 1)))); }   /* CdSync */
static void h_800a14fc(RCpu *c) { c->r[2] = (uint32_t)(CdSyncCallback((CdlCB)hle_arg(c, 0))); }   /* CdSyncCallback */
static void h_800a1510(RCpu *c) { c->r[2] = (uint32_t)(CdReadyCallback((CdlCB)hle_arg(c, 0))); }   /* CdReadyCallback */
static void h_800a1524(RCpu *c) { c->r[2] = (uint32_t)(CdControl((uint8_t)hle_arg(c, 0), (const uint8_t *)rc_hptr(hle_arg(c, 1)), (uint8_t *)rc_hptr(hle_arg(c, 2)))); }   /* CdControl */
static void h_800a1660(RCpu *c) { c->r[2] = (uint32_t)(CdControlF((uint8_t)hle_arg(c, 0), (const uint8_t *)rc_hptr(hle_arg(c, 1)))); }   /* CdControlF */
static void h_800a1794(RCpu *c) { c->r[2] = (uint32_t)(CdControlB((uint8_t)hle_arg(c, 0), (const uint8_t *)rc_hptr(hle_arg(c, 1)), (uint8_t *)rc_hptr(hle_arg(c, 2)))); }   /* CdControlB */
static void h_800a18e0(RCpu *c) { c->r[2] = (uint32_t)(CdMix((const CdlATV *)rc_hptr(hle_arg(c, 0)))); }   /* CdMix */
static void h_800a1900(RCpu *c) { c->r[2] = (uint32_t)(CdGetSector((void *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1))); }   /* CdGetSector */
static void h_800a1920(RCpu *c) { c->r[2] = (uint32_t)(CdGetSector2((void *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1))); }   /* CdGetSector2 */
static void h_800a1940(RCpu *c) { c->r[2] = (uint32_t)(CdDataCallback((CdlCB)hle_arg(c, 0))); }   /* CdDataCallback */
static void h_800a30f8(RCpu *c) { c->r[2] = rc_pptr(CdSearchFile((CdlFILE *)rc_hptr(hle_arg(c, 0)), (const char *)rc_hptr(hle_arg(c, 1)))); }   /* CdSearchFile */
static void h_800a4168(RCpu *c) { c->r[2] = (uint32_t)(CdRead((int)hle_arg(c, 0), (uint32_t *)rc_hptr(hle_arg(c, 1)), (int)hle_arg(c, 2))); }   /* CdRead */
static void h_800a4304(RCpu *c) { c->r[2] = (uint32_t)(CdReadSync((int)hle_arg(c, 0), (uint8_t *)rc_hptr(hle_arg(c, 1)))); }   /* CdReadSync */
static void h_800a4438(RCpu *c) { c->r[2] = rc_pptr(CdIntToPos((int)hle_arg(c, 0), (CdlLOC *)rc_hptr(hle_arg(c, 1)))); }   /* CdIntToPos */
static void h_800a453c(RCpu *c) { c->r[2] = (uint32_t)(CdPosToInt((const CdlLOC *)rc_hptr(hle_arg(c, 0)))); }   /* CdPosToInt */
static void h_800a45c8(RCpu *c) { c->r[2] = (uint32_t)(rsin((int)hle_arg(c, 0))); }   /* rsin */
static void h_800a4698(RCpu *c) { c->r[2] = (uint32_t)(rcos((int)hle_arg(c, 0))); }   /* rcos */
static void h_800a4740(RCpu *c) { InitGeom(); c->r[2] = 0; }   /* InitGeom */
static void h_800a47c8(RCpu *c) { c->r[2] = (uint32_t)(SquareRoot0((long)hle_arg(c, 0))); }   /* SquareRoot0 */
static void h_800a4928(RCpu *c) { c->r[2] = (uint32_t)(VectorNormalSS((SVECTOR *)rc_hptr(hle_arg(c, 0)), (SVECTOR *)rc_hptr(hle_arg(c, 1)))); }   /* VectorNormalSS */
static void h_800a4b08(RCpu *c) { c->r[2] = rc_pptr(MulMatrix0((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)), (MATRIX *)rc_hptr(hle_arg(c, 2)))); }   /* MulMatrix0 */
static void h_800a4c18(RCpu *c) { c->r[2] = rc_pptr(CompMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)), (MATRIX *)rc_hptr(hle_arg(c, 2)))); }   /* CompMatrix */
static void h_800a4d78(RCpu *c) { c->r[2] = rc_pptr(ApplyMatrixLV((MATRIX *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)), (VECTOR *)rc_hptr(hle_arg(c, 2)))); }   /* ApplyMatrixLV */
static void h_800a4ed8(RCpu *c) { c->r[2] = rc_pptr(SetMulMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* SetMulMatrix */
static void h_800a4fe8(RCpu *c) { c->r[2] = rc_pptr(ApplyRotMatrixLV((VECTOR *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)))); }   /* ApplyRotMatrixLV */
static void h_800a5128(RCpu *c) { c->r[2] = rc_pptr(MulMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* MulMatrix */
static void h_800a5238(RCpu *c) { c->r[2] = rc_pptr(MulMatrix2((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* MulMatrix2 */
static void h_800a5348(RCpu *c) { c->r[2] = rc_pptr(ApplyMatrixSV((MATRIX *)rc_hptr(hle_arg(c, 0)), (SVECTOR *)rc_hptr(hle_arg(c, 1)), (SVECTOR *)rc_hptr(hle_arg(c, 2)))); }   /* ApplyMatrixSV */
static void h_800a53a8(RCpu *c) { c->r[2] = rc_pptr(TransMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)))); }   /* TransMatrix */
static void h_800a53d8(RCpu *c) { c->r[2] = rc_pptr(ScaleMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)))); }   /* ScaleMatrix */
static void h_800a5518(RCpu *c) { SetRotMatrix((MATRIX *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SetRotMatrix */
static void h_800a5548(RCpu *c) { SetColorMatrix((MATRIX *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SetColorMatrix */
static void h_800a5578(RCpu *c) { SetTransMatrix((MATRIX *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* SetTransMatrix */
static void h_800a5598(RCpu *c) { ReadGeomOffset((int32_t *)rc_hptr(hle_arg(c, 0)), (int32_t *)rc_hptr(hle_arg(c, 1))); c->r[2] = 0; }   /* ReadGeomOffset */
static void h_800a55b8(RCpu *c) { c->r[2] = (uint32_t)(ReadGeomScreen()); }   /* ReadGeomScreen */
static void h_800a55c8(RCpu *c) { SetBackColor((int32_t)hle_arg(c, 0), (int32_t)hle_arg(c, 1), (int32_t)hle_arg(c, 2)); c->r[2] = 0; }   /* SetBackColor */
static void h_800a55e8(RCpu *c) { SetGeomOffset((int32_t)hle_arg(c, 0), (int32_t)hle_arg(c, 1)); c->r[2] = 0; }   /* SetGeomOffset */
static void h_800a5608(RCpu *c) { SetGeomScreen((int32_t)hle_arg(c, 0)); c->r[2] = 0; }   /* SetGeomScreen */
static void h_800a57b4(RCpu *c) { OuterProduct0((VECTOR *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)), (VECTOR *)rc_hptr(hle_arg(c, 2))); c->r[2] = 0; }   /* OuterProduct0 */
static void h_800a5828(RCpu *c) { c->r[2] = (uint32_t)(RotTransPers((SVECTOR *)rc_hptr(hle_arg(c, 0)), (int32_t *)rc_hptr(hle_arg(c, 1)), (int32_t *)rc_hptr(hle_arg(c, 2)), (int32_t *)rc_hptr(hle_arg(c, 3)))); }   /* RotTransPers */
static void h_800a5858(RCpu *c) { c->r[2] = (uint32_t)(RotTransPers3((SVECTOR *)rc_hptr(hle_arg(c, 0)), (SVECTOR *)rc_hptr(hle_arg(c, 1)), (SVECTOR *)rc_hptr(hle_arg(c, 2)), (int32_t *)rc_hptr(hle_arg(c, 3)), (int32_t *)rc_hptr(hle_arg(c, 4)), (int32_t *)rc_hptr(hle_arg(c, 5)), (int32_t *)rc_hptr(hle_arg(c, 6)), (int32_t *)rc_hptr(hle_arg(c, 7)))); }   /* RotTransPers3 */
static void h_800a58b8(RCpu *c) { RotTrans((SVECTOR *)rc_hptr(hle_arg(c, 0)), (VECTOR *)rc_hptr(hle_arg(c, 1)), (int32_t *)rc_hptr(hle_arg(c, 2))); c->r[2] = 0; }   /* RotTrans */
static void h_800a58e8(RCpu *c) { c->r[2] = (uint32_t)(NormalClip((int32_t)hle_arg(c, 0), (int32_t)hle_arg(c, 1), (int32_t)hle_arg(c, 2))); }   /* NormalClip */
static void h_800a5918(RCpu *c) { c->r[2] = rc_pptr(TransposeMatrix((MATRIX *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* TransposeMatrix */
static void h_800a5958(RCpu *c) { c->r[2] = rc_pptr(RotMatrix((SVECTOR *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* RotMatrix */
static void h_800a5be8(RCpu *c) { c->r[2] = rc_pptr(RotMatrixYXZ((SVECTOR *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* RotMatrixYXZ */
static void h_800a5e78(RCpu *c) { c->r[2] = rc_pptr(RotMatrix_gte((SVECTOR *)rc_hptr(hle_arg(c, 0)), (MATRIX *)rc_hptr(hle_arg(c, 1)))); }   /* RotMatrix_gte */
static void h_800a6088(RCpu *c) { c->r[2] = (uint32_t)(ratan2((long)hle_arg(c, 0), (long)hle_arg(c, 1))); }   /* ratan2 */
static void h_800a62e8(RCpu *c) { c->r[2] = (uint32_t)(ResetGraph((int)hle_arg(c, 0))); }   /* ResetGraph */
static void h_800a65cc(RCpu *c) { c->r[2] = (uint32_t)(SetDispMask((int)hle_arg(c, 0))); }   /* SetDispMask */
static void h_800a6664(RCpu *c) { c->r[2] = (uint32_t)(DrawSync((int)hle_arg(c, 0))); }   /* DrawSync */
static void h_800a67e8(RCpu *c) { c->r[2] = (uint32_t)(ClearImage((RECT *)rc_hptr(hle_arg(c, 0)), (uint8_t)hle_arg(c, 1), (uint8_t)hle_arg(c, 2), (uint8_t)hle_arg(c, 3))); }   /* ClearImage */
static void h_800a6878(RCpu *c) { c->r[2] = (uint32_t)(ClearImage2((RECT *)rc_hptr(hle_arg(c, 0)), (uint8_t)hle_arg(c, 1), (uint8_t)hle_arg(c, 2), (uint8_t)hle_arg(c, 3))); }   /* ClearImage2 */
static void h_800a6910(RCpu *c) { c->r[2] = (uint32_t)(LoadImage((RECT *)rc_hptr(hle_arg(c, 0)), (uint32_t *)rc_hptr(hle_arg(c, 1)))); }   /* LoadImage */
static void h_800a6970(RCpu *c) { c->r[2] = (uint32_t)(StoreImage((RECT *)rc_hptr(hle_arg(c, 0)), (uint32_t *)rc_hptr(hle_arg(c, 1)))); }   /* StoreImage */
static void h_800a69d0(RCpu *c) { c->r[2] = (uint32_t)(MoveImage((RECT *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2))); }   /* MoveImage */
static void h_800a6b50(RCpu *c) { c->r[2] = rc_pptr(ClearOTagR((uint32_t *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1))); }   /* ClearOTagR */
static void h_800a6c58(RCpu *c) { DrawOTag((uint32_t *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* DrawOTag */
static void h_800a6cc8(RCpu *c) { c->r[2] = rc_pptr(PutDrawEnv((DRAWENV *)rc_hptr(hle_arg(c, 0)))); }   /* PutDrawEnv */
static void h_800a6e94(RCpu *c) { c->r[2] = rc_pptr(PutDispEnv((DISPENV *)rc_hptr(hle_arg(c, 0)))); }   /* PutDispEnv */
static void h_800a73c0(RCpu *c) { c->r[2] = (uint32_t)(GetODE()); }   /* GetODE */
static void h_800a73f0(RCpu *c) { SetDrawArea((DR_AREA *)rc_hptr(hle_arg(c, 0)), (RECT *)rc_hptr(hle_arg(c, 1))); c->r[2] = 0; }   /* SetDrawArea */
static void h_800a8e9c(RCpu *c) { c->r[2] = (uint32_t)(StoreImage2((RECT *)rc_hptr(hle_arg(c, 0)), (uint32_t *)rc_hptr(hle_arg(c, 1)))); }   /* StoreImage2 */
static void h_800a9218(RCpu *c) { c->r[2] = (uint32_t)(OpenTIM((uint32_t *)rc_hptr(hle_arg(c, 0)))); }   /* OpenTIM */
static void h_800a9228(RCpu *c) { c->r[2] = rc_pptr(ReadTIM((TIM_IMAGE *)rc_hptr(hle_arg(c, 0)))); }   /* ReadTIM */
static void h_800aaa98(RCpu *c) { c->r[2] = rc_pptr(SetDefDrawEnv((DRAWENV *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3), (int)hle_arg(c, 4))); }   /* SetDefDrawEnv */
static void h_800aab58(RCpu *c) { c->r[2] = rc_pptr(SetDefDispEnv((DISPENV *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3), (int)hle_arg(c, 4))); }   /* SetDefDispEnv */
static void h_800aab98(RCpu *c) { c->r[2] = (uint32_t)(GetTPage((int)hle_arg(c, 0), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3))); }   /* GetTPage */
static void h_800aabd8(RCpu *c) { CatPrim((void *)rc_hptr(hle_arg(c, 0)), (void *)rc_hptr(hle_arg(c, 1))); c->r[2] = 0; }   /* CatPrim */
static void h_800aac08(RCpu *c) { SetDrawTPage((DR_TPAGE *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3)); c->r[2] = 0; }   /* SetDrawTPage */
static void h_800aac38(RCpu *c) { SetDrawMode((DR_MODE *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1), (int)hle_arg(c, 2), (int)hle_arg(c, 3), (RECT *)rc_hptr(hle_arg(c, 4))); c->r[2] = 0; }   /* SetDrawMode */
static void h_800aacd8(RCpu *c) { DecDCTReset((int)hle_arg(c, 0)); c->r[2] = 0; }   /* DecDCTReset */
static void h_800aae30(RCpu *c) { DecDCTin((uint32_t *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1)); c->r[2] = 0; }   /* DecDCTin */
static void h_800aaeac(RCpu *c) { DecDCTout((uint32_t *)rc_hptr(hle_arg(c, 0)), (int)hle_arg(c, 1)); c->r[2] = 0; }   /* DecDCTout */
static void h_800aaf08(RCpu *c) { c->r[2] = (uint32_t)(DecDCToutSync((int)hle_arg(c, 0))); }   /* DecDCToutSync */
static void h_800aaf74(RCpu *c) { c->r[2] = (uint32_t)(DecDCToutCallback((uint32_t)hle_arg(c, 0))); }   /* DecDCToutCallback */
static void h_800ab368(RCpu *c) { c->r[2] = (uint32_t)(DecDCTvlcSize2((int32_t)hle_arg(c, 0))); }   /* DecDCTvlcSize2 */
static void h_800ab398(RCpu *c) { c->r[2] = (uint32_t)(DecDCTvlc2((uint32_t *)rc_hptr(hle_arg(c, 0)), (uint32_t *)rc_hptr(hle_arg(c, 1)), (uint16_t *)rc_hptr(hle_arg(c, 2)))); }   /* DecDCTvlc2 */
static void h_800ab6e8(RCpu *c) { DecDCTvlcBuild((uint16_t *)rc_hptr(hle_arg(c, 0))); c->r[2] = 0; }   /* DecDCTvlcBuild */
static void h_800ab7d8(RCpu *c) { _bu_init(); c->r[2] = 0; }   /* _bu_init */
static void h_800ab7e8(RCpu *c) { c->r[2] = (uint32_t)(_card_info((int32_t)hle_arg(c, 0))); }   /* _card_info */
static void h_800ab7f8(RCpu *c) { c->r[2] = (uint32_t)(_card_load((int32_t)hle_arg(c, 0))); }   /* _card_load */
static void h_800ab808(RCpu *c) { c->r[2] = (uint32_t)(_card_clear((int32_t)hle_arg(c, 0))); }   /* _card_clear */
static void h_800ab868(RCpu *c) { InitCARD((int32_t)hle_arg(c, 0)); c->r[2] = 0; }   /* InitCARD */
static void h_800ab8f8(RCpu *c) { c->r[2] = (uint32_t)(StartCARD()); }   /* StartCARD */
static void h_800ab940(RCpu *c) { c->r[2] = (uint32_t)(StopCARD()); }   /* StopCARD */
static void h_800abc38(RCpu *c) { c->r[2] = (uint32_t)(_card_format((int32_t)hle_arg(c, 0))); }   /* _card_format */
static void h_800ac0b8(RCpu *c) { { uint32_t i; c->r[2] = 0; for (i = 0; i < c->r[6]; i++) if (rc_lb(c->r[4] + i, 0) == (c->r[5] & 0xFF)) { c->r[2] = c->r[4] + i; break; } } }   /* memchr */
static void h_800ac148(RCpu *c) { { uint32_t i; for (i = 0; i < c->r[6]; i++) rc_sb(c->r[4] + i, c->r[5], 0); c->r[2] = c->r[4]; } }   /* memset */
static void h_800ac178(RCpu *c) { c->r[2] = (uint32_t)(psx_rand()); }   /* rand */
static void h_800ac1a8(RCpu *c) { psx_srand((unsigned int)hle_arg(c, 0)); c->r[2] = 0; }   /* srand */
static void h_800ac1b8(RCpu *c) { { uint32_t d = c->r[4], i = 0; uint8_t b; while (rc_lb(d, 0)) d++; do { b = (uint8_t)rc_lb(c->r[5] + i, 0); rc_sb(d + i, b, 0); i++; } while (b); c->r[2] = c->r[4]; } }   /* strcat */
static void h_800ac2d8(RCpu *c) { { uint32_t i = 0; uint8_t b; do { b = (uint8_t)rc_lb(c->r[5] + i, 0); rc_sb(c->r[4] + i, b, 0); i++; } while (b); c->r[2] = c->r[4]; } }   /* strcpy */
static void h_800ac328(RCpu *c) { { uint32_t n = 0; while (rc_lb(c->r[4] + n, 0)) n++; c->r[2] = n; } }   /* strlen */
static void h_800ac3e8(RCpu *c) { { uint32_t a[24]; int k; for (k = 0; k < 24; k++) a[k] = hle_arg(c, 1 + k); c->r[2] = (uint32_t)psx_printf((const char *)rc_hptr(c->r[4]), a); } }   /* printf */
static void h_800acd28(RCpu *c) { { uint32_t a[24]; int k; char buf[2048]; for (k = 0; k < 24; k++) a[k] = hle_arg(c, 2 + k); int n = psx_sformat(buf, sizeof buf, (const char *)rc_hptr(c->r[5]), a); for (k = 0; k <= n && k < (int)sizeof buf; k++) rc_sb(c->r[4] + k, k < n ? (uint8_t)buf[k] : 0, 0); c->r[2] = (uint32_t)n; } }   /* sprintf */
static void h_800ad758(RCpu *c) { c->r[2] = (uint32_t)(OpenEvent((uint32_t)hle_arg(c, 0), (uint32_t)hle_arg(c, 1), (int32_t)hle_arg(c, 2), (uint32_t)hle_arg(c, 3))); }   /* OpenEvent */
static void h_800ad768(RCpu *c) { c->r[2] = (uint32_t)(CloseEvent((uint32_t)hle_arg(c, 0))); }   /* CloseEvent */
static void h_800ad788(RCpu *c) { c->r[2] = (uint32_t)(TestEvent((uint32_t)hle_arg(c, 0))); }   /* TestEvent */
static void h_800ad798(RCpu *c) { c->r[2] = (uint32_t)(EnableEvent((uint32_t)hle_arg(c, 0))); }   /* EnableEvent */
static void h_800ad7a8(RCpu *c) { c->r[2] = (uint32_t)(DisableEvent((uint32_t)hle_arg(c, 0))); }   /* DisableEvent */
static void h_800ad7e8(RCpu *c) { EnterCriticalSection(); c->r[2] = 0; }   /* EnterCriticalSection */
static void h_800ad7f8(RCpu *c) { ExitCriticalSection(); c->r[2] = 0; }   /* ExitCriticalSection */
static void h_800ad808(RCpu *c) { c->r[2] = (uint32_t)(bios_open((const char *)rc_hptr(hle_arg(c, 0)), (int32_t)hle_arg(c, 1))); }   /* open */
static void h_800ad818(RCpu *c) { c->r[2] = (uint32_t)(bios_read((int32_t)hle_arg(c, 0), (void *)rc_hptr(hle_arg(c, 1)), (int32_t)hle_arg(c, 2))); }   /* read */
static void h_800ad828(RCpu *c) { c->r[2] = (uint32_t)(bios_write((int32_t)hle_arg(c, 0), (const void *)rc_hptr(hle_arg(c, 1)), (int32_t)hle_arg(c, 2))); }   /* write */
static void h_800ad838(RCpu *c) { c->r[2] = (uint32_t)(bios_close((int32_t)hle_arg(c, 0))); }   /* close */
static void h_800ad848(RCpu *c) { c->r[2] = rc_pptr(bios_nextfile((DIRENTRY *)rc_hptr(hle_arg(c, 0)))); }   /* nextfile */
static void h_800ad898(RCpu *c) { c->r[2] = (uint32_t)(SetRCnt((uint32_t)hle_arg(c, 0), (uint16_t)hle_arg(c, 1), (int32_t)hle_arg(c, 2))); }   /* SetRCnt */
static void h_800ad934(RCpu *c) { c->r[2] = (uint32_t)(GetRCnt((uint32_t)hle_arg(c, 0))); }   /* GetRCnt */
static void h_800ad96c(RCpu *c) { c->r[2] = (uint32_t)(StartRCnt((uint32_t)hle_arg(c, 0))); }   /* StartRCnt */
static void h_800ad99c(RCpu *c) { c->r[2] = (uint32_t)(StopRCnt((uint32_t)hle_arg(c, 0))); }   /* StopRCnt */
static void h_800ad9d0(RCpu *c) { c->r[2] = (uint32_t)(ResetRCnt((uint32_t)hle_arg(c, 0))); }   /* ResetRCnt */
static void h_800ada08(RCpu *c) { c->r[2] = rc_pptr(bios_firstfile((const char *)rc_hptr(hle_arg(c, 0)), (DIRENTRY *)rc_hptr(hle_arg(c, 1)))); }   /* firstfile */

const RcFunc hle_table_eu[] = {
    {0x80025214u, h_80025214},
    {0x80099de0u, h_80099de0},
    {0x8009b1e0u, h_8009b1e0},
    {0x8009b200u, h_8009b200},
    {0x8009b220u, h_8009b220},
    {0x8009b28cu, h_8009b28c},
    {0x8009b358u, h_8009b358},
    {0x8009b450u, h_8009b450},
    {0x8009b5ccu, h_8009b5cc},
    {0x8009b604u, h_8009b604},
    {0x8009b64cu, h_8009b64c},
    {0x8009c2a0u, h_8009c2a0},
    {0x8009ce80u, h_8009ce80},
    {0x8009cf18u, h_8009cf18},
    {0x8009d630u, h_8009d630},
    {0x8009e218u, h_8009e218},
    {0x8009e238u, h_8009e238},
    {0x8009e2b8u, h_8009e2b8},
    {0x8009e498u, h_8009e498},
    {0x8009ee48u, h_8009ee48},
    {0x8009eec8u, h_8009eec8},
    {0x8009f1b8u, h_8009f1b8},
    {0x8009f358u, h_8009f358},
    {0x8009f3c8u, h_8009f3c8},
    {0x8009f588u, h_8009f588},
    {0x8009f5e8u, h_8009f5e8},
    {0x8009f748u, h_8009f748},
    {0x8009f7a8u, h_8009f7a8},
    {0x8009f7d8u, h_8009f7d8},
    {0x8009f888u, h_8009f888},
    {0x8009fcdcu, h_8009fcdc},
    {0x8009fd68u, h_8009fd68},
    {0x800a0568u, h_800a0568},
    {0x800a0778u, h_800a0778},
    {0x800a0808u, h_800a0808},
    {0x800a086cu, h_800a086c},
    {0x800a11f8u, h_800a11f8},
    {0x800a1228u, h_800a1228},
    {0x800a13b4u, h_800a13b4},
    {0x800a1420u, h_800a1420},
    {0x800a1440u, h_800a1440},
    {0x800a14bcu, h_800a14bc},
    {0x800a14fcu, h_800a14fc},
    {0x800a1510u, h_800a1510},
    {0x800a1524u, h_800a1524},
    {0x800a1660u, h_800a1660},
    {0x800a1794u, h_800a1794},
    {0x800a18e0u, h_800a18e0},
    {0x800a1900u, h_800a1900},
    {0x800a1920u, h_800a1920},
    {0x800a1940u, h_800a1940},
    {0x800a30f8u, h_800a30f8},
    {0x800a4168u, h_800a4168},
    {0x800a4304u, h_800a4304},
    {0x800a4438u, h_800a4438},
    {0x800a453cu, h_800a453c},
    {0x800a45c8u, h_800a45c8},
    {0x800a4698u, h_800a4698},
    {0x800a4740u, h_800a4740},
    {0x800a47c8u, h_800a47c8},
    {0x800a4928u, h_800a4928},
    {0x800a4b08u, h_800a4b08},
    {0x800a4c18u, h_800a4c18},
    {0x800a4d78u, h_800a4d78},
    {0x800a4ed8u, h_800a4ed8},
    {0x800a4fe8u, h_800a4fe8},
    {0x800a5128u, h_800a5128},
    {0x800a5238u, h_800a5238},
    {0x800a5348u, h_800a5348},
    {0x800a53a8u, h_800a53a8},
    {0x800a53d8u, h_800a53d8},
    {0x800a5518u, h_800a5518},
    {0x800a5548u, h_800a5548},
    {0x800a5578u, h_800a5578},
    {0x800a5598u, h_800a5598},
    {0x800a55b8u, h_800a55b8},
    {0x800a55c8u, h_800a55c8},
    {0x800a55e8u, h_800a55e8},
    {0x800a5608u, h_800a5608},
    {0x800a57b4u, h_800a57b4},
    {0x800a5828u, h_800a5828},
    {0x800a5858u, h_800a5858},
    {0x800a58b8u, h_800a58b8},
    {0x800a58e8u, h_800a58e8},
    {0x800a5918u, h_800a5918},
    {0x800a5958u, h_800a5958},
    {0x800a5be8u, h_800a5be8},
    {0x800a5e78u, h_800a5e78},
    {0x800a6088u, h_800a6088},
    {0x800a62e8u, h_800a62e8},
    {0x800a65ccu, h_800a65cc},
    {0x800a6664u, h_800a6664},
    {0x800a67e8u, h_800a67e8},
    {0x800a6878u, h_800a6878},
    {0x800a6910u, h_800a6910},
    {0x800a6970u, h_800a6970},
    {0x800a69d0u, h_800a69d0},
    {0x800a6b50u, h_800a6b50},
    {0x800a6c58u, h_800a6c58},
    {0x800a6cc8u, h_800a6cc8},
    {0x800a6e94u, h_800a6e94},
    {0x800a73c0u, h_800a73c0},
    {0x800a73f0u, h_800a73f0},
    {0x800a8e9cu, h_800a8e9c},
    {0x800a9218u, h_800a9218},
    {0x800a9228u, h_800a9228},
    {0x800aaa98u, h_800aaa98},
    {0x800aab58u, h_800aab58},
    {0x800aab98u, h_800aab98},
    {0x800aabd8u, h_800aabd8},
    {0x800aac08u, h_800aac08},
    {0x800aac38u, h_800aac38},
    {0x800aacd8u, h_800aacd8},
    {0x800aae30u, h_800aae30},
    {0x800aaeacu, h_800aaeac},
    {0x800aaf08u, h_800aaf08},
    {0x800aaf74u, h_800aaf74},
    {0x800ab368u, h_800ab368},
    {0x800ab398u, h_800ab398},
    {0x800ab6e8u, h_800ab6e8},
    {0x800ab7d8u, h_800ab7d8},
    {0x800ab7e8u, h_800ab7e8},
    {0x800ab7f8u, h_800ab7f8},
    {0x800ab808u, h_800ab808},
    {0x800ab868u, h_800ab868},
    {0x800ab8f8u, h_800ab8f8},
    {0x800ab940u, h_800ab940},
    {0x800abc38u, h_800abc38},
    {0x800ac0b8u, h_800ac0b8},
    {0x800ac148u, h_800ac148},
    {0x800ac178u, h_800ac178},
    {0x800ac1a8u, h_800ac1a8},
    {0x800ac1b8u, h_800ac1b8},
    {0x800ac2d8u, h_800ac2d8},
    {0x800ac328u, h_800ac328},
    {0x800ac3e8u, h_800ac3e8},
    {0x800acd28u, h_800acd28},
    {0x800ad758u, h_800ad758},
    {0x800ad768u, h_800ad768},
    {0x800ad788u, h_800ad788},
    {0x800ad798u, h_800ad798},
    {0x800ad7a8u, h_800ad7a8},
    {0x800ad7e8u, h_800ad7e8},
    {0x800ad7f8u, h_800ad7f8},
    {0x800ad808u, h_800ad808},
    {0x800ad818u, h_800ad818},
    {0x800ad828u, h_800ad828},
    {0x800ad838u, h_800ad838},
    {0x800ad848u, h_800ad848},
    {0x800ad898u, h_800ad898},
    {0x800ad934u, h_800ad934},
    {0x800ad96cu, h_800ad96c},
    {0x800ad99cu, h_800ad99c},
    {0x800ad9d0u, h_800ad9d0},
    {0x800ada08u, h_800ada08},
};
const unsigned hle_count_eu = 155;

