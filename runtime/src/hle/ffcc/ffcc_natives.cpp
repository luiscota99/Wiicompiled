#include "hle_stubs.h"
#include "memory.h"
#include "abi_bridge.h"
#include <cstdio>
#include <cstdlib>

// ==========================================
// Mapped Native Wrappers for FFCC
// ==========================================

// Subsystem: audio
extern "C" uint32_t AIGetDMAStartAddr_8012406c();
PPC_NATIVE_OVERRIDE(8018ef00, AIGetDMAStartAddr_8012406c, uint32_t, (), ());
extern "C" uint32_t AIGetDSPSampleRate_8012409c();
PPC_NATIVE_OVERRIDE(8018f0e4, AIGetDSPSampleRate_8012409c, uint32_t, (), ());
extern "C" void AIInit_801240b0(uint32_t callback_stack_switch);
PPC_NATIVE_OVERRIDE_VOID(8018f234, AIInit_801240b0, (uint32_t callback_stack_switch), (callback_stack_switch));
extern "C" void AIInitDMA_80123fcc(uint32_t start_addr, uint32_t length);
PPC_NATIVE_OVERRIDE_VOID(8018ee48, AIInitDMA_80123fcc, (uint32_t start_addr, uint32_t length), (start_addr, length));
extern "C" uint32_t AIRegisterDMACallback_80123f88(uint32_t callback);
PPC_NATIVE_OVERRIDE(8018ee04, AIRegisterDMACallback_80123f88, uint32_t, (uint32_t callback), (callback));
extern "C" void AIStartDMA_80124048();
PPC_NATIVE_OVERRIDE_VOID(8018eed0, AIStartDMA_80124048, (), ());
extern "C" uint32_t DSPAddTask_8015d50c(uint32_t task_ptr);
PPC_NATIVE_OVERRIDE(80197b34, DSPAddTask_8015d50c, uint32_t, (uint32_t task_ptr), (task_ptr));
extern "C" uint32_t DSPAssertTask_8015d57c(uint32_t taskPtr);
PPC_NATIVE_OVERRIDE(80197be4, DSPAssertTask_8015d57c, uint32_t, (uint32_t taskPtr), (taskPtr));
extern "C" uint32_t DSPCheckInit_8015d504();
PPC_NATIVE_OVERRIDE(80197b2c, DSPCheckInit_8015d504, uint32_t, (), ());
extern "C" uint32_t DSPCheckMailFromDSP_8015d40c();
PPC_NATIVE_OVERRIDE(80197a2c, DSPCheckMailFromDSP_8015d40c, uint32_t, (), ());
extern "C" uint32_t DSPCheckMailToDSP_8015d3fc();
PPC_NATIVE_OVERRIDE(80197a1c, DSPCheckMailToDSP_8015d3fc, uint32_t, (), ());
extern "C" void DSPInit_8015d444();
// DSPInit: the GameCube SDK returns at once when already initialised (GBAInit calls it after AXInit);
// the shared HLE re-initialises the mixer and the task list, so gate it on the DSP init flag.
extern "C" uint32_t DSPCheckInit_8015d504();
extern "C" void DSPInit_ffcc() { if (DSPCheckInit_8015d504() == 0) DSPInit_8015d444(); }
PPC_NATIVE_OVERRIDE_VOID(80197a68, DSPInit_ffcc, (), ());
extern "C" uint32_t DSPReadMailFromDSP_8015d41c();
PPC_NATIVE_OVERRIDE(80197a3c, DSPReadMailFromDSP_8015d41c, uint32_t, (), ());
extern "C" void DSPSendMailToDSP_8015d430(uint32_t mail);
PPC_NATIVE_OVERRIDE_VOID(80197a54, DSPSendMailToDSP_8015d430, (uint32_t mail), (mail));
extern "C" void __DSP_boot_task_8015dc60(uint32_t task_ptr);
PPC_NATIVE_OVERRIDE_VOID(801982c0, __DSP_boot_task_8015dc60, (uint32_t task_ptr), (task_ptr));
extern "C" void OSInitAudioSystem_801A1358();
PPC_NATIVE_OVERRIDE_VOID(8017cc18, OSInitAudioSystem_801A1358, (), ());
extern "C" void OSStopAudioSystem_801A1520();
PPC_NATIVE_OVERRIDE_VOID(8017cdd4, OSStopAudioSystem_801A1520, (), ());

// Subsystem: gx
extern "C" void GX__CopyDisp_8016fc38(uint32_t da, uint32_t c);
PPC_NATIVE_OVERRIDE_VOID(801a31e0, GX__CopyDisp_8016fc38, (uint32_t da, uint32_t c), (da, c));
extern "C" void GX__CopyTex_8016fd74(uint32_t da, uint32_t c);
PPC_NATIVE_OVERRIDE_VOID(801a333c, GX__CopyTex_8016fd74, (uint32_t da, uint32_t c), (da, c));
extern "C" void GX__SetCopyFilter_8016fa40(uint32_t aa, uint32_t spa, uint32_t vf, uint32_t vfa);
PPC_NATIVE_OVERRIDE_VOID(801a2f9c, GX__SetCopyFilter_8016fa40, (uint32_t aa, uint32_t spa, uint32_t vf, uint32_t vfa), (aa, spa, vf, vfa));
extern "C" void GX__SetDispCopyDst_8016f4b8(uint32_t w, uint32_t h);
PPC_NATIVE_OVERRIDE_VOID(801a2a14, GX__SetDispCopyDst_8016f4b8, (uint32_t w, uint32_t h), (w, h));
extern "C" void GX__SetDispCopyGamma_8016fc24(uint32_t g);
PPC_NATIVE_OVERRIDE_VOID(801a31c4, GX__SetDispCopyGamma_8016fc24, (uint32_t g), (g));
extern "C" void GX__SetDispCopySrc_8016f438(uint32_t l, uint32_t t, uint32_t w, uint32_t h);
PPC_NATIVE_OVERRIDE_VOID(801a28f4, GX__SetDispCopySrc_8016f438, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));
extern "C" void GX__SetTexCopyDst_8016f4dc(uint32_t w, uint32_t h, uint32_t f, uint32_t m);
PPC_NATIVE_OVERRIDE_VOID(801a2a50, GX__SetTexCopyDst_8016f4dc, (uint32_t w, uint32_t h, uint32_t f, uint32_t m), (w, h, f, m));
extern "C" void GX__SetTexCopySrc_8016f478(uint32_t l, uint32_t t, uint32_t w, uint32_t h);
PPC_NATIVE_OVERRIDE_VOID(801a2984, GX__SetTexCopySrc_8016f478, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));
extern "C" void GX__CallDisplayList_80172f64(uint32_t listAddr, uint32_t nbytes);
PPC_NATIVE_OVERRIDE_VOID(801a6194, GX__CallDisplayList_80172f64, (uint32_t listAddr, uint32_t nbytes), (listAddr, nbytes));
extern "C" void GX__SetIndTexCoordScale_80171968(uint32_t s, uint32_t ss, uint32_t ts);
PPC_NATIVE_OVERRIDE_VOID(801a4d40, GX__SetIndTexCoordScale_80171968, (uint32_t s, uint32_t ss, uint32_t ts), (s, ss, ts));
extern "C" void GX__SetIndTexMtx_80171814(uint32_t id, uint32_t ma, uint32_t se);
PPC_NATIVE_OVERRIDE_VOID(801a4be0, GX__SetIndTexMtx_80171814, (uint32_t id, uint32_t ma, uint32_t se), (id, ma, se));
extern "C" void GX__SetIndTexOrder_80171a6c(uint32_t s, uint32_t c, uint32_t m);
PPC_NATIVE_OVERRIDE_VOID(801a4ebc, GX__SetIndTexOrder_80171a6c, (uint32_t s, uint32_t c, uint32_t m), (s, c, m));
extern "C" void GX__SetNumIndStages_80171b38(uint32_t n);
PPC_NATIVE_OVERRIDE_VOID(801a4fd0, GX__SetNumIndStages_80171b38, (uint32_t n), (n));
extern "C" void GX__SetTevDirect_80171b58(uint32_t s);
PPC_NATIVE_OVERRIDE_VOID(801a4ff8, GX__SetTevDirect_80171b58, (uint32_t s), (s));
extern "C" void GX__SetTevIndWarp_80171ba0(uint32_t ts, uint32_t is, uint32_t so, uint32_t rm, uint32_t ms);
PPC_NATIVE_OVERRIDE_VOID(801a5040, GX__SetTevIndWarp_80171ba0, (uint32_t ts, uint32_t is, uint32_t so, uint32_t rm, uint32_t ms), (ts, is, so, rm, ms));
extern "C" void GX__SetTevIndirect_801717ac(uint32_t ts, uint32_t is, uint32_t f, uint32_t bs, uint32_t ms, uint32_t ws, uint32_t wt, uint32_t ap, uint32_t il, uint32_t as);
PPC_NATIVE_OVERRIDE_VOID(801a4b44, GX__SetTevIndirect_801717ac, (uint32_t ts, uint32_t is, uint32_t f, uint32_t bs, uint32_t ms, uint32_t ws, uint32_t wt, uint32_t ap, uint32_t il, uint32_t as), (ts, is, f, bs, ms, ws, wt, ap, il, as));
extern "C" void GX__Flush_8016e654();
PPC_NATIVE_OVERRIDE_VOID(801a1d14, GX__Flush_8016e654, (), ());
extern "C" void GX__GetCPUFifo_8016cf10(uint32_t fa);
// PPC_NATIVE_OVERRIDE_VOID(801a0b00, GX__GetCPUFifo_8016cf10, (uint32_t fa), (fa));  // replaced by ffcc_gx.cpp (GameCube signature or globals)
extern "C" void GX__SetCPUFifo_8016c94c(uint32_t fa);
// PPC_NATIVE_OVERRIDE_VOID(801a04e4, GX__SetCPUFifo_8016c94c, (uint32_t fa), (fa));  // replaced by ffcc_gx.cpp (GameCube signature or globals)
extern "C" void GX__SetGPFifo_8016cb2c(uint32_t fa);
// PPC_NATIVE_OVERRIDE_VOID(801a05f4, GX__SetGPFifo_8016cb2c, (uint32_t fa), (fa));  // replaced by ffcc_gx.cpp (GameCube signature or globals)
extern "C" void GX__LoadLightObjImm_80170320(uint32_t la, uint32_t lid);
PPC_NATIVE_OVERRIDE_VOID(801a3804, GX__LoadLightObjImm_80170320, (uint32_t la, uint32_t lid), (la, lid));
extern "C" void GX__SetChanAmbColor_8017039c(uint32_t c, uint32_t cp);
PPC_NATIVE_OVERRIDE_VOID(801a3880, GX__SetChanAmbColor_8017039c, (uint32_t c, uint32_t cp), (c, cp));
extern "C" void GX__SetChanCtrl_80170570(uint32_t ch, uint32_t en, uint32_t as, uint32_t ms, uint32_t lm, uint32_t df, uint32_t af);
PPC_NATIVE_OVERRIDE_VOID(801a3aac, GX__SetChanCtrl_80170570, (uint32_t ch, uint32_t en, uint32_t as, uint32_t ms, uint32_t lm, uint32_t df, uint32_t af), (ch, en, as, ms, lm, df, af));
extern "C" void GX__SetChanMatColor_80170474(uint32_t c, uint32_t cp);
PPC_NATIVE_OVERRIDE_VOID(801a3974, GX__SetChanMatColor_80170474, (uint32_t c, uint32_t cp), (c, cp));
extern "C" void GX__SetNumChans_8017054c(uint32_t n);
PPC_NATIVE_OVERRIDE_VOID(801a3a68, GX__SetNumChans_8017054c, (uint32_t n), (n));
extern "C" void GX__SetAlphaCompare_80172088(uint32_t c0, uint32_t r0, uint32_t op, uint32_t c1, uint32_t r1);
PPC_NATIVE_OVERRIDE_VOID(801a56dc, GX__SetAlphaCompare_80172088, (uint32_t c0, uint32_t r0, uint32_t op, uint32_t c1, uint32_t r1), (c0, r0, op, c1, r1));
extern "C" void GX__SetAlphaUpdate_801727f8(uint32_t en);
PPC_NATIVE_OVERRIDE_VOID(801a5d58, GX__SetAlphaUpdate_801727f8, (uint32_t en), (en));
extern "C" void GX__SetBlendMode_8017277c(uint32_t t, uint32_t s, uint32_t d, uint32_t op);
PPC_NATIVE_OVERRIDE_VOID(801a5cd8, GX__SetBlendMode_8017277c, (uint32_t t, uint32_t s, uint32_t d, uint32_t op), (t, s, d, op));
extern "C" void GX__SetClipMode_8017351c(uint32_t m);
PPC_NATIVE_OVERRIDE_VOID(801a6908, GX__SetClipMode_8017351c, (uint32_t m), (m));
extern "C" void GX__SetCoPlanar_8016f3e0(uint32_t en);
PPC_NATIVE_OVERRIDE_VOID(801a2774, GX__SetCoPlanar_8016f3e0, (uint32_t en), (en));
extern "C" void GX__SetColorUpdate_801727cc(uint32_t en);
PPC_NATIVE_OVERRIDE_VOID(801a5d2c, GX__SetColorUpdate_801727cc, (uint32_t en), (en));
extern "C" void GX__SetCullMode_8016f3b8(uint32_t m);
PPC_NATIVE_OVERRIDE_VOID(801a2728, GX__SetCullMode_8016f3b8, (uint32_t m), (m));
extern "C" void GX__SetDither_80172930(uint32_t d);
PPC_NATIVE_OVERRIDE_VOID(801a5ed8, GX__SetDither_80172930, (uint32_t d), (d));
extern "C" void GX__SetDstAlpha_8017295c(uint32_t en, uint32_t a);
PPC_NATIVE_OVERRIDE_VOID(801a5f04, GX__SetDstAlpha_8017295c, (uint32_t en, uint32_t a), (en, a));
extern "C" void GX__SetFog_801722cc(uint32_t t, float sz, float ez, float nz, float fz, uint32_t cp);
PPC_NATIVE_OVERRIDE_VOID(801a59bc, GX__SetFog_801722cc, (uint32_t t, float sz, float ez, float nz, float fz, uint32_t cp), (t, sz, ez, nz, fz, cp));
extern "C" void GX__SetPixelFmt_80172888(uint32_t pf, uint32_t zf);
PPC_NATIVE_OVERRIDE_VOID(801a5df0, GX__SetPixelFmt_80172888, (uint32_t pf, uint32_t zf), (pf, zf));
extern "C" void GX__SetZCompLoc_80172858(uint32_t bt);
PPC_NATIVE_OVERRIDE_VOID(801a5db8, GX__SetZCompLoc_80172858, (uint32_t bt), (bt));
extern "C" void GX__SetZMode_80172824(uint32_t ce, uint32_t f, uint32_t ue);
PPC_NATIVE_OVERRIDE_VOID(801a5d84, GX__SetZMode_80172824, (uint32_t ce, uint32_t f, uint32_t ue), (ce, f, ue));
extern "C" void GX__SetZTexture_801720c0(uint32_t op, uint32_t f, uint32_t b);
PPC_NATIVE_OVERRIDE_VOID(801a5730, GX__SetZTexture_801720c0, (uint32_t op, uint32_t f, uint32_t b), (op, f, b));
extern "C" void GX__ClearBoundingBox_8016fecc();
PPC_NATIVE_OVERRIDE_VOID(801a34b8, GX__ClearBoundingBox_8016fecc, (), ());
extern "C" void GX__FinishInterruptHandler_8016ed94();
// PPC_NATIVE_OVERRIDE_VOID(801a2320, GX__FinishInterruptHandler_8016ed94, (), ());  // replaced by ffcc_gx.cpp (GameCube signature or globals)
extern "C" void __GX__FlushTextureState_80171c28();
PPC_NATIVE_OVERRIDE_VOID(801a51b4, __GX__FlushTextureState_80171c28, (), ());
extern "C" void GX__PixModeSync_8016eb70();
PPC_NATIVE_OVERRIDE_VOID(801a2084, GX__PixModeSync_8016eb70, (), ());
extern "C" void GX__SetCopyClamp_8016f618(uint32_t c);
PPC_NATIVE_OVERRIDE_VOID(801a2bcc, GX__SetCopyClamp_8016f618, (uint32_t c), (c));
// __GXSetDirtyState (801a2424) is registered in ffcc_gx.cpp
extern "C" void GX__SetDispCopyFrame2Field_8016f5f8(uint32_t f);
PPC_NATIVE_OVERRIDE_VOID(801a2ba4, GX__SetDispCopyFrame2Field_8016f5f8, (uint32_t f), (f));
extern "C" void GX__SetDrawSync_8016e9fc(uint32_t token);
PPC_NATIVE_OVERRIDE_VOID(801a1edc, GX__SetDrawSync_8016e9fc, (uint32_t token), (token));
extern "C" void __GX__SetSUTexRegs_801712f0();
PPC_NATIVE_OVERRIDE_VOID(801a4788, __GX__SetSUTexRegs_801712f0, (), ());
extern "C" void __GX__SetTmemConfig_80171458(uint32_t mode);
PPC_NATIVE_OVERRIDE_VOID(801a4904, __GX__SetTmemConfig_80171458, (uint32_t mode), (mode));
extern "C" void GX__SetNumTevStages_801722a8(uint32_t n);
PPC_NATIVE_OVERRIDE_VOID(801a598c, GX__SetNumTevStages_801722a8, (uint32_t n), (n));
extern "C" void GX__SetTevAlphaIn_80171d20(uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
PPC_NATIVE_OVERRIDE_VOID(801a52a8, GX__SetTevAlphaIn_80171d20, (uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d), (s, a, b, c, d));
extern "C" void GX__SetTevAlphaOp_80171db8(uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_);
PPC_NATIVE_OVERRIDE_VOID(801a5354, GX__SetTevAlphaOp_80171db8, (uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_), (s, op, b, sc, cl, or_));
extern "C" void GX__SetTevColor_80171e10(uint32_t id, uint32_t cp);
PPC_NATIVE_OVERRIDE_VOID(801a53bc, GX__SetTevColor_80171e10, (uint32_t id, uint32_t cp), (id, cp));
extern "C" void GX__SetTevColorIn_80171ce0(uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d);
PPC_NATIVE_OVERRIDE_VOID(801a5264, GX__SetTevColorIn_80171ce0, (uint32_t s, uint32_t a, uint32_t b, uint32_t c, uint32_t d), (s, a, b, c, d));
extern "C" void GX__SetTevColorOp_80171d60(uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_);
PPC_NATIVE_OVERRIDE_VOID(801a52ec, GX__SetTevColorOp_80171d60, (uint32_t s, uint32_t op, uint32_t b, uint32_t sc, uint32_t cl, uint32_t or_), (s, op, b, sc, cl, or_));
extern "C" void GX__SetTevColorS10_80171e70(uint32_t id, uint32_t cp);
PPC_NATIVE_OVERRIDE_VOID(801a5430, GX__SetTevColorS10_80171e70, (uint32_t id, uint32_t cp), (id, cp));
extern "C" void GX__SetTevKAlphaSel_80171f80(uint32_t s, uint32_t sel);
PPC_NATIVE_OVERRIDE_VOID(801a5584, GX__SetTevKAlphaSel_80171f80, (uint32_t s, uint32_t sel), (s, sel));
extern "C" void GX__SetTevKColor_80171ed4(uint32_t id, uint32_t cp);
PPC_NATIVE_OVERRIDE_VOID(801a54a4, GX__SetTevKColor_80171ed4, (uint32_t id, uint32_t cp), (id, cp));
extern "C" void GX__SetTevKColorSel_80171f30(uint32_t s, uint32_t sel);
PPC_NATIVE_OVERRIDE_VOID(801a5518, GX__SetTevKColorSel_80171f30, (uint32_t s, uint32_t sel), (s, sel));
extern "C" void GX__SetTevOp_80171c4c(uint32_t s, uint32_t m);
PPC_NATIVE_OVERRIDE_VOID(801a51d8, GX__SetTevOp_80171c4c, (uint32_t s, uint32_t m), (s, m));
extern "C" void GX__SetTevOrder_8017214c(uint32_t s, uint32_t c, uint32_t m, uint32_t col);
PPC_NATIVE_OVERRIDE_VOID(801a57b4, GX__SetTevOrder_8017214c, (uint32_t s, uint32_t c, uint32_t m, uint32_t col), (s, c, m, col));
extern "C" void GX__SetTevSwapMode_80171fd0(uint32_t s, uint32_t rs, uint32_t ts);
PPC_NATIVE_OVERRIDE_VOID(801a55f0, GX__SetTevSwapMode_80171fd0, (uint32_t s, uint32_t rs, uint32_t ts), (s, rs, ts));
extern "C" void GX__SetTevSwapModeTable_8017200c(uint32_t id, uint32_t r, uint32_t g, uint32_t b, uint32_t a);
PPC_NATIVE_OVERRIDE_VOID(801a5644, GX__SetTevSwapModeTable_8017200c, (uint32_t id, uint32_t r, uint32_t g, uint32_t b, uint32_t a), (id, r, g, b, a));
extern "C" void GX__InitTexObj_801707f8(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m);
PPC_NATIVE_OVERRIDE_VOID(801a3d9c, GX__InitTexObj_801707f8, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m), (oa, da, w, h, f, ws, wt, m));
extern "C" void GX__InitTexObjCI_80170a04(uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m, uint32_t tl);
PPC_NATIVE_OVERRIDE_VOID(801a4010, GX__InitTexObjCI_80170a04, (uint32_t oa, uint32_t da, uint32_t w, uint32_t h, uint32_t f, uint32_t ws, uint32_t wt, uint32_t m, uint32_t tl), (oa, da, w, h, f, ws, wt, m, tl));
extern "C" void GX__InitTexObjLOD_80170a4c(uint32_t oa, uint32_t mif, uint32_t maf, float mil, float mal, float lb, uint32_t bc, uint32_t el, uint32_t ma);
PPC_NATIVE_OVERRIDE_VOID(801a4058, GX__InitTexObjLOD_80170a4c, (uint32_t oa, uint32_t mif, uint32_t maf, float mil, float mal, float lb, uint32_t bc, uint32_t el, uint32_t ma), (oa, mif, maf, mil, mal, lb, bc, el, ma));
extern "C" void GX__InitTexObjTlut_80170b64(uint32_t oa, uint32_t tl);
PPC_NATIVE_OVERRIDE_VOID(801a41ec, GX__InitTexObjTlut_80170b64, (uint32_t oa, uint32_t tl), (oa, tl));
extern "C" void GX__InitTlutObj_80170f80(uint32_t oa, uint32_t da, uint32_t f, uint32_t e);
PPC_NATIVE_OVERRIDE_VOID(801a4414, GX__InitTlutObj_80170f80, (uint32_t oa, uint32_t da, uint32_t f, uint32_t e), (oa, da, f, e));
extern "C" void GX__InvalidateTexAll_80171110();
PPC_NATIVE_OVERRIDE_VOID(801a4660, GX__InvalidateTexAll_80171110, (), ());
extern "C" void GX__LoadTexObj_80170f2c(uint32_t oa, uint32_t tid);
PPC_NATIVE_OVERRIDE_VOID(801a43c0, GX__LoadTexObj_80170f2c, (uint32_t oa, uint32_t tid), (oa, tid));
extern "C" void GX__LoadTexObjPreLoaded_80170dc8(uint32_t oa, uint32_t tid);
// PPC_NATIVE_OVERRIDE_VOID(801a4228, GX__LoadTexObjPreLoaded_80170dc8, (uint32_t oa, uint32_t tid), (oa, tid));  // replaced by ffcc_gx.cpp (GameCube signature or globals)
extern "C" void GX__LoadTlut_80170fa8(uint32_t oa, uint32_t tl);
PPC_NATIVE_OVERRIDE_VOID(801a445c, GX__LoadTlut_80170fa8, (uint32_t oa, uint32_t tl), (oa, tl));
extern "C" void GX__GetProjectionv_801730cc(uint32_t pa);
PPC_NATIVE_OVERRIDE_VOID(801a64ec, GX__GetProjectionv_801730cc, (uint32_t pa), (pa));
extern "C" void GX__LoadNrmMtxImm_80173188(uint32_t ma, uint32_t id);
PPC_NATIVE_OVERRIDE_VOID(801a659c, GX__LoadNrmMtxImm_80173188, (uint32_t ma, uint32_t id), (ma, id));
extern "C" void GX__LoadPosMtxImm_8017310c(uint32_t ma, uint32_t id);
PPC_NATIVE_OVERRIDE_VOID(801a654c, GX__LoadPosMtxImm_8017310c, (uint32_t ma, uint32_t id), (ma, id));
extern "C" void GX__LoadTexMtxImm_80173234(uint32_t ma, uint32_t id, uint32_t t);
PPC_NATIVE_OVERRIDE_VOID(801a6624, GX__LoadTexMtxImm_80173234, (uint32_t ma, uint32_t id, uint32_t t), (ma, id, t));
extern "C" void GX__SetCurrentMtx_80173214(uint32_t id);
PPC_NATIVE_OVERRIDE_VOID(801a65ec, GX__SetCurrentMtx_80173214, (uint32_t id), (id));
extern "C" void GX__SetProjection_8017301c(uint32_t ma, uint32_t pt);
PPC_NATIVE_OVERRIDE_VOID(801a6378, GX__SetProjection_8017301c, (uint32_t ma, uint32_t pt), (ma, pt));
extern "C" void GX__SetProjectionv_80173080(uint32_t pa);
PPC_NATIVE_OVERRIDE_VOID(801a642c, GX__SetProjectionv_80173080, (uint32_t pa), (pa));
extern "C" void GX__SetScissor_80173430(uint32_t l, uint32_t t, uint32_t w, uint32_t h);
PPC_NATIVE_OVERRIDE_VOID(801a6838, GX__SetScissor_80173430, (uint32_t l, uint32_t t, uint32_t w, uint32_t h), (l, t, w, h));
extern "C" void GX__SetScissorBoxOffset_801734e0(int32_t xo, int32_t yo);
PPC_NATIVE_OVERRIDE_VOID(801a68c8, GX__SetScissorBoxOffset_801734e0, (int32_t xo, int32_t yo), (xo, yo));
extern "C" void GX__SetViewport_801733b4(float l, float t, float w, float h, float nz, float fz);
PPC_NATIVE_OVERRIDE_VOID(801a67dc, GX__SetViewport_801733b4, (float l, float t, float w, float h, float nz, float fz), (l, t, w, h, nz, fz));
extern "C" void GX__SetViewportJitter_80173378(float l, float t, float w, float h, float nz, float fz, uint32_t f);
PPC_NATIVE_OVERRIDE_VOID(801a66d8, GX__SetViewportJitter_80173378, (float l, float t, float w, float h, float nz, float fz, uint32_t f), (l, t, w, h, nz, fz, f));
extern "C" void GX__Begin_8016f0f0(uint32_t t, uint32_t vf, uint32_t nv);
PPC_NATIVE_OVERRIDE_VOID(801a24c4, GX__Begin_8016f0f0, (uint32_t t, uint32_t vf, uint32_t nv), (t, vf, nv));
extern "C" void GX__ClearVtxDesc_8016dc34();
PPC_NATIVE_OVERRIDE_VOID(801a1130, GX__ClearVtxDesc_8016dc34, (), ());
extern "C" void GX__EnableTexOffsets_8016f37c(uint32_t coord, uint32_t lineEnable, uint32_t pointEnable);
PPC_NATIVE_OVERRIDE_VOID(801a26cc, GX__EnableTexOffsets_8016f37c, (uint32_t coord, uint32_t lineEnable, uint32_t pointEnable), (coord, lineEnable, pointEnable));
extern "C" void GX__SetArray_8016e32c(uint32_t a, uint32_t ba, uint32_t str);
PPC_NATIVE_OVERRIDE_VOID(801a18d4, GX__SetArray_8016e32c, (uint32_t a, uint32_t ba, uint32_t str), (a, ba, str));
extern "C" void GX__SetLineWidth_8016f314(uint32_t width, uint32_t texOffsets);
PPC_NATIVE_OVERRIDE_VOID(801a263c, GX__SetLineWidth_8016f314, (uint32_t width, uint32_t texOffsets), (width, texOffsets));
extern "C" void GX__SetNumTexGens_8016e5a4(uint32_t n);
PPC_NATIVE_OVERRIDE_VOID(801a1c40, GX__SetNumTexGens_8016e5a4, (uint32_t n), (n));
extern "C" void GX__SetPointSize_8016f348(uint32_t pointSize, uint32_t texOffsets);
PPC_NATIVE_OVERRIDE_VOID(801a2684, GX__SetPointSize_8016f348, (uint32_t pointSize, uint32_t texOffsets), (pointSize, texOffsets));
extern "C" void GX__SetTexCoordGen2_8016e37c(uint32_t dc, uint32_t f, uint32_t sp, uint32_t m, uint32_t n, uint32_t pm);
PPC_NATIVE_OVERRIDE_VOID(801a1970, GX__SetTexCoordGen2_8016e37c, (uint32_t dc, uint32_t f, uint32_t sp, uint32_t m, uint32_t n, uint32_t pm), (dc, f, sp, m, n, pm));
extern "C" void GX__SetVtxAttrFmt_8016dc68(uint32_t vf, uint32_t a, uint32_t c, uint32_t t, uint32_t fr);
PPC_NATIVE_OVERRIDE_VOID(801a1168, GX__SetVtxAttrFmt_8016dc68, (uint32_t vf, uint32_t a, uint32_t c, uint32_t t, uint32_t fr), (vf, a, c, t, fr));
extern "C" void GX__SetVtxAttrFmtv_8016de08(uint32_t vf, uint32_t la);
PPC_NATIVE_OVERRIDE_VOID(801a14c0, GX__SetVtxAttrFmtv_8016de08, (uint32_t vf, uint32_t la), (vf, la));
extern "C" void GX__SetVtxDesc_8016d3a4(uint32_t a, uint32_t t);
PPC_NATIVE_OVERRIDE_VOID(801a0c68, GX__SetVtxDesc_8016d3a4, (uint32_t a, uint32_t t), (a, t));

// Subsystem: input
extern "C" uint32_t PAD__Init_HLE();
PPC_NATIVE_OVERRIDE(8018de3c, PAD__Init_HLE, uint32_t, (), ());
extern "C" uint32_t PAD__Read_HLE(uint32_t statusPtr);
PPC_NATIVE_OVERRIDE(8018e054, PAD__Read_HLE, uint32_t, (uint32_t statusPtr), (statusPtr));
extern "C" uint32_t PAD__Reset_HLE(uint32_t mask);
PPC_NATIVE_OVERRIDE(8018dd3c, PAD__Reset_HLE, uint32_t, (uint32_t mask), (mask));

// Subsystem: os
extern "C" void DCFlushRange_801a162c(uint32_t addr, uint32_t length);
PPC_NATIVE_OVERRIDE_VOID(8017ceec, DCFlushRange_801a162c, (uint32_t addr, uint32_t length), (addr, length));
extern "C" void DCFlushRangeNoSync_801a168c(uint32_t addr, uint32_t length);
PPC_NATIVE_OVERRIDE_VOID(8017cf4c, DCFlushRangeNoSync_801a168c, (uint32_t addr, uint32_t length), (addr, length));
extern "C" void DCInvalidateRange_801a1600(uint32_t addr, uint32_t length);
PPC_NATIVE_OVERRIDE_VOID(8017cec0, DCInvalidateRange_801a1600, (uint32_t addr, uint32_t length), (addr, length));
extern "C" void DCStoreRange_801a165c(uint32_t addr, uint32_t length);
PPC_NATIVE_OVERRIDE_VOID(8017cf1c, DCStoreRange_801a165c, (uint32_t addr, uint32_t length), (addr, length));
extern "C" void OS__ClearContext_801a2098(uint32_t contextAddr);
PPC_NATIVE_OVERRIDE_VOID(8017d914, OS__ClearContext_801a2098, (uint32_t contextAddr), (contextAddr));
extern "C" uint32_t OS__GetCurrentThread_801a98b0_hle();
PPC_NATIVE_OVERRIDE(80180b60, OS__GetCurrentThread_801a98b0_hle, uint32_t, (), ());
extern "C" void OS__SetCurrentContext_801a1e70(uint32_t contextAddr);
PPC_NATIVE_OVERRIDE_VOID(8017d74c, OS__SetCurrentContext_801a1e70, (uint32_t contextAddr), (contextAddr));
extern "C" uint32_t OSGetResetCode_801a8a50();
PPC_NATIVE_OVERRIDE(8017f83c, OSGetResetCode_801a8a50, uint32_t, (), ());
extern "C" uint32_t OS____InitMemoryProtection_801a7dfc(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8);
PPC_NATIVE_OVERRIDE(8017ef94, OS____InitMemoryProtection_801a7dfc, uint32_t, (uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8), (r3, r4, r5, r6, r7, r8));
extern "C" void OS____PSInit_801a04a0();
PPC_NATIVE_OVERRIDE_VOID(8017c0ec, OS____PSInit_801a04a0, (), ());
extern "C" void PPCDisableSpeculation_8012e654();
PPC_NATIVE_OVERRIDE_VOID(8017b578, PPCDisableSpeculation_8012e654, (), ());
extern "C" void PPCMfhid0_8012e574();
PPC_NATIVE_OVERRIDE_VOID(8017b4a4, PPCMfhid0_8012e574, (), ());
extern "C" void PPCMtdec_8012e594();
PPC_NATIVE_OVERRIDE_VOID(8017b4c4, PPCMtdec_8012e594, (), ());
extern "C" void PPCMthid0_8012e57c();
PPC_NATIVE_OVERRIDE_VOID(8017b4ac, PPCMthid0_8012e57c, (), ());
extern "C" void PPCMtmmcr0_8012e5b8();
PPC_NATIVE_OVERRIDE_VOID(8017b4e8, PPCMtmmcr0_8012e5b8, (), ());
extern "C" void PPCMtmmcr1_8012e5c0();
PPC_NATIVE_OVERRIDE_VOID(8017b4f0, PPCMtmmcr1_8012e5c0, (), ());
extern "C" void PPCMtpmc1_8012e5c8();
PPC_NATIVE_OVERRIDE_VOID(8017b4f8, PPCMtpmc1_8012e5c8, (), ());
extern "C" void PPCMtpmc2_8012e5d0();
PPC_NATIVE_OVERRIDE_VOID(8017b500, PPCMtpmc2_8012e5d0, (), ());
extern "C" void PPCMtpmc3_8012e5d8();
PPC_NATIVE_OVERRIDE_VOID(8017b508, PPCMtpmc3_8012e5d8, (), ());
extern "C" void PPCMtpmc4_8012e5e0();
PPC_NATIVE_OVERRIDE_VOID(8017b510, PPCMtpmc4_8012e5e0, (), ());
extern "C" void PPCMtwpar_8012e64c();
PPC_NATIVE_OVERRIDE_VOID(8017b570, PPCMtwpar_8012e64c, (), ());
extern "C" void PPCSync_8012e59c();
PPC_NATIVE_OVERRIDE_VOID(8017b4cc, PPCSync_8012e59c, (), ());
extern "C" void SIInit_801b2de0();
PPC_NATIVE_OVERRIDE_VOID(80184954, SIInit_801b2de0, (), ());
extern "C" void __init_hardware_80006348();
PPC_NATIVE_OVERRIDE_VOID(80003400, __init_hardware_80006348, (), ());
extern "C" uint32_t EXIDeselect_80168b00(uint32_t channel);
PPC_NATIVE_OVERRIDE(80182e38, EXIDeselect_80168b00, uint32_t, (uint32_t channel), (channel));
extern "C" uint32_t EXIDma_80168288(uint32_t channel, uint32_t buffer, uint32_t length, uint32_t type, uint32_t callback);
PPC_NATIVE_OVERRIDE(801824a0, EXIDma_80168288, uint32_t, (uint32_t channel, uint32_t buffer, uint32_t length, uint32_t type, uint32_t callback), (channel, buffer, length, type, callback));
extern "C" uint32_t EXIImm_80167f68(uint32_t channel, uint32_t buffer, uint32_t length, uint32_t type, uint32_t callback);
PPC_NATIVE_OVERRIDE(801821a4, EXIImm_80167f68, uint32_t, (uint32_t channel, uint32_t buffer, uint32_t length, uint32_t type, uint32_t callback), (channel, buffer, length, type, callback));
extern "C" uint32_t EXIInit_80168fa0();
PPC_NATIVE_OVERRIDE(801832f8, EXIInit_80168fa0, uint32_t, (), ());
extern "C" uint32_t EXISelect_801689d0(uint32_t channel, uint32_t device, uint32_t frequency);
PPC_NATIVE_OVERRIDE(80182d0c, EXISelect_801689d0, uint32_t, (uint32_t channel, uint32_t device, uint32_t frequency), (channel, device, frequency));
extern "C" uint32_t EXISync_80168380(uint32_t channel);
PPC_NATIVE_OVERRIDE(8018258c, EXISync_80168380, uint32_t, (uint32_t channel), (channel));
extern "C" uint32_t EXIUnlock_80169260(uint32_t channel);
PPC_NATIVE_OVERRIDE(801835c0, EXIUnlock_80169260, uint32_t, (uint32_t channel), (channel));
extern "C" int32_t OS__DisableInterrupts_801a65ac();
PPC_NATIVE_OVERRIDE(8017e340, OS__DisableInterrupts_801a65ac, int32_t, (), ());
extern "C" int32_t OS__EnableInterrupts_801a65c0();
PPC_NATIVE_OVERRIDE(8017e354, OS__EnableInterrupts_801a65c0, int32_t, (), ());
extern "C" uint32_t OS__ExceptionInit_801a00e0(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r20);
PPC_NATIVE_OVERRIDE(8017bd20, OS__ExceptionInit_801a00e0, uint32_t, (uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8, uint32_t r20), (r3, r4, r5, r6, r7, r8, r20));
extern "C" uint32_t OS____InterruptInit_801a661c(uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8);
PPC_NATIVE_OVERRIDE(8017e3bc, OS____InterruptInit_801a661c, uint32_t, (uint32_t r3, uint32_t r4, uint32_t r5, uint32_t r6, uint32_t r7, uint32_t r8), (r3, r4, r5, r6, r7, r8));
extern "C" uint32_t OS____MaskInterrupts_801a693c(uint32_t mask, uint32_t unmask);
PPC_NATIVE_OVERRIDE(8017e708, OS____MaskInterrupts_801a693c, uint32_t, (uint32_t mask, uint32_t unmask), (mask, unmask));
extern "C" int32_t OS__RestoreInterrupts_801a65d4(int32_t level);
PPC_NATIVE_OVERRIDE(8017e368, OS__RestoreInterrupts_801a65d4, int32_t, (int32_t level), (level));
extern "C" uint32_t __OSSetInterruptHandler_801a65f8_hle(uint32_t interrupt, uint32_t handler);
PPC_NATIVE_OVERRIDE(8017e38c, __OSSetInterruptHandler_801a65f8_hle, uint32_t, (uint32_t interrupt, uint32_t handler), (interrupt, handler));
extern "C" uint32_t __OSUnmaskInterrupts_801a69bc_hle(uint32_t mask);
PPC_NATIVE_OVERRIDE(8017e790, __OSUnmaskInterrupts_801a69bc_hle, uint32_t, (uint32_t mask), (mask));
extern "C" void SetExiInterruptMask_80167e78(uint32_t channel, uint32_t exi_struct_ptr);
PPC_NATIVE_OVERRIDE_VOID(801820b0, SetExiInterruptMask_80167e78, (uint32_t channel, uint32_t exi_struct_ptr), (channel, exi_struct_ptr));
extern "C" uint32_t SetInterruptMask_801a66e0(uint32_t mask, uint32_t enable);
PPC_NATIVE_OVERRIDE(8017e430, SetInterruptMask_801a66e0, uint32_t, (uint32_t mask, uint32_t enable), (mask, enable));
extern "C" uint32_t OSResetSystem();
PPC_NATIVE_OVERRIDE(8017f5b4, OSResetSystem, uint32_t, (), ());
extern "C" void PPCHalt_8012E5A4();
PPC_NATIVE_OVERRIDE_VOID(8017b4d4, PPCHalt_8012E5A4, (), ());

// Subsystem: storage
extern "C" int32_t DVDLowInquiry_80165A30(uint32_t cmdBlockPtr, uint32_t callback);
PPC_NATIVE_OVERRIDE(80187f48, DVDLowInquiry_80165A30, int32_t, (uint32_t cmdBlockPtr, uint32_t callback), (cmdBlockPtr, callback));
extern "C" int32_t DVDLowRead_80166330(uint32_t buffer, uint32_t length, uint32_t offset, uint32_t callback);
PPC_NATIVE_OVERRIDE(80187a34, DVDLowRead_80166330, int32_t, (uint32_t buffer, uint32_t length, uint32_t offset, uint32_t callback), (buffer, length, offset, callback));
extern "C" int32_t DVDLowReadDiskID_80164AAC(uint32_t diskIdPtr, uint32_t callback);
PPC_NATIVE_OVERRIDE(80187d8c, DVDLowReadDiskID_80164AAC, int32_t, (uint32_t diskIdPtr, uint32_t callback), (diskIdPtr, callback));
extern "C" int32_t DVD__ReadAbsAsyncPrio_HLE_801628cc(uint32_t cmdBlockPtr,                                                       uint32_t bufferPtr,                                                       int32_t length,                                                       int32_t offset,                                                       uint32_t callbackPtr,                                                       int32_t prio);
PPC_NATIVE_OVERRIDE(8018a65c, DVD__ReadAbsAsyncPrio_HLE_801628cc, int32_t, (uint32_t cmdBlockPtr,                                                       uint32_t bufferPtr,                                                       int32_t length,                                                       int32_t offset,                                                       uint32_t callbackPtr,                                                       int32_t prio), (cmdBlockPtr, bufferPtr, length, offset, callbackPtr, prio));
extern "C" int32_t DVD__ReadAsyncPrio_HLE_8015e74c(uint32_t fileInfoPtr,                                                    uint32_t bufferPtr,                                                    int32_t length,                                                    int32_t offset,                                                    uint32_t callbackPtr,                                                    int32_t prio);
PPC_NATIVE_OVERRIDE(80188980, DVD__ReadAsyncPrio_HLE_8015e74c, int32_t, (uint32_t fileInfoPtr,                                                    uint32_t bufferPtr,                                                    int32_t length,                                                    int32_t offset,                                                    uint32_t callbackPtr,                                                    int32_t prio), (fileInfoPtr, bufferPtr, length, offset, callbackPtr, prio));

// ==========================================
// FFCC-specific Hooks (OS DB / Panic)
// ==========================================

// OSPanic(file, line, fmt, ...): print the location, the raw format and the first integer
namespace {
const char* FfccGuestString(uint32_t addr) {
    if (addr < 0x80000000u) return nullptr;
    const uint8_t* p = Memory::GetPointer(addr, 1);
    return p ? reinterpret_cast<const char*>(p) : nullptr;
}
}  // namespace

// arguments before stopping, so the game's own assertion text reaches the log.
extern "C" void OSPanic_ffcc(CpuContext* ctx) {
    const char* file = FfccGuestString(ctx->gpr[3]);
    const char* fmt = FfccGuestString(ctx->gpr[5]);
    std::fprintf(stderr, "[ffcc] OSPanic %s:%u: %s  (args r6=0x%08X r7=0x%08X r8=0x%08X)" "\n", file ? file : "?",
                 ctx->gpr[4], fmt ? fmt : "?", ctx->gpr[6], ctx->gpr[7], ctx->gpr[8]);
    std::fflush(stderr);
    std::abort();
}
PPC_NATIVE_OVERRIDE_VOID(800223dc, OSPanic_ffcc, (CpuContext* ctx), (ctx));

extern "C" void DBInit_8017b5a8() { }
PPC_NATIVE_OVERRIDE_VOID(8017b5a8, DBInit_8017b5a8, (), ());

extern "C" uint32_t __DBIsExceptionMarked_8017b628(uint32_t a) { return 0; }
PPC_NATIVE_OVERRIDE(8017b628, __DBIsExceptionMarked_8017b628, uint32_t, (uint32_t a), (a));

extern "C" void DBPrintf_8017b644(uint32_t a) { }
PPC_NATIVE_OVERRIDE_VOID(8017b644, DBPrintf_8017b644, (uint32_t a), (a));

extern "C" void __OSDBJump_8017bfc4() { }
PPC_NATIVE_OVERRIDE_VOID(8017bfc4, __OSDBJump_8017bfc4, (), ());

// OSReport(fmt, ...): same register convention as the Wii OSReport; reuse its formatting HLE.
extern "C" void OS__Report_801a25d0(CpuContext* ctx);
PPC_NATIVE_OVERRIDE_VOID(8017dd68, OS__Report_801a25d0, (CpuContext* ctx), (ctx));

extern "C" void __OSDBIntegrator_8017bfa0() { }
PPC_NATIVE_OVERRIDE_VOID(8017bfa0, __OSDBIntegrator_8017bfa0, (), ());

extern "C" void __DBExceptionDestinationAux_8017b5d0() { }
PPC_NATIVE_OVERRIDE_VOID(8017b5d0, __DBExceptionDestinationAux_8017b5d0, (), ());

extern "C" void __DBExceptionDestination_8017b618() { }
PPC_NATIVE_OVERRIDE_VOID(8017b618, __DBExceptionDestination_8017b618, (), ());

// ==========================================
// FFCC-specific: GameCube DVD layer (Phase 1 spike)
// ==========================================
extern "C" void DVDInit_8015EA1C();
PPC_NATIVE_OVERRIDE_VOID(80188a74, DVDInit_8015EA1C, (), ());
extern "C" int32_t DVDCheckDevice_801643FC();
PPC_NATIVE_OVERRIDE(8018af58, DVDCheckDevice_801643FC, int32_t, (), ());   // DVDCheckDisk
extern "C" int32_t DVDGetDriveStatus_ffcc() { return 0; }              // DVD_STATE_END
PPC_NATIVE_OVERRIDE(8018aaf8, DVDGetDriveStatus_ffcc, int32_t, (), ());
extern "C" void DVDReset_ffcc() { }
PPC_NATIVE_OVERRIDE_VOID(8018aa68, DVDReset_ffcc, (), ());
extern "C" int32_t DVDCompareDiskID_ffcc(uint32_t a, uint32_t b) { return 1; }
PPC_NATIVE_OVERRIDE(8018b4fc, DVDCompareDiskID_ffcc, int32_t, (uint32_t a, uint32_t b), (a, b));

// ==========================================
// FFCC-specific: GameCube-only OS entry points
// ==========================================
// OSGetResetButtonState reads the PI interrupt cause register (0xCC003000, RSWST bit) which the PC
// port has no device for; the console reset button is never pressed.
extern "C" uint32_t OSGetResetButtonState_ffcc() { return 0; }
PPC_NATIVE_OVERRIDE(8017f960, OSGetResetButtonState_ffcc, uint32_t, (), ());
// OSGetFontEncode samples a VI status bit (0xCC00206E) to choose the font encoding; the PAL build
// always uses OS_FONT_ENCODE_ANSI (0).
extern "C" uint32_t OSGetFontEncode_ffcc() { return 0; }
PPC_NATIVE_OVERRIDE(8017e2e8, OSGetFontEncode_ffcc, uint32_t, (), ());
