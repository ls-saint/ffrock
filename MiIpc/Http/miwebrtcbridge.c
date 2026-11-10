#include "miwebrtcbridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <time.h>

#include "mbedtls/bignum.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/rsa.h"
#if defined(MI_WEBRTC_ENABLE_MBEDTLS_TEST_CERTS) && MI_WEBRTC_ENABLE_MBEDTLS_TEST_CERTS
#include "mbedtls/certs.h"
#endif
#include "mbedtls/version.h"
#include "mbedtls/x509.h"
#include "mbedtls/x509_crt.h"
#include "miiceagent.h"
#include "miipc_audio_capture_stats.h"
#include "micall.h"
#include "mibase.h"
#include "midtls_transport.h"
#include "mimutex.h"
#include "misdp.h"
#include "mithread.h"
#if defined(_WIN32)
#include "../Soc/Win32/winwarp.h"
#endif

#define MI_WEBRTC_TRACK_VIDEO 1
#define MI_WEBRTC_TRACK_AUDIO 2
#define MI_WEBRTC_INVALID_TRACK_ID (-1)
#ifndef MI_WEBRTC_ANSWER_MAX
#define MI_WEBRTC_ANSWER_MAX 16384
#endif
#ifndef MI_WEBRTC_JSON_MAX
#define MI_WEBRTC_JSON_MAX 32768
#endif
#define MI_WEBRTC_PROCESS_SLEEP_MS 10
#define MI_WEBRTC_GATHER_WAIT_MS 80
#ifndef MI_WEBRTC_CERT_BUF_SIZE
#define MI_WEBRTC_CERT_BUF_SIZE 4096
#endif
#ifndef MI_WEBRTC_KEY_BUF_SIZE
#define MI_WEBRTC_KEY_BUF_SIZE 4096
#endif
#ifndef MI_WEBRTC_MAX_SESSIONS
#define MI_WEBRTC_MAX_SESSIONS 4
#endif
#define MI_WEBRTC_MAX_TRACKS 2
#define MI_WEBRTC_SESSION_TIMEOUT_MS 30000u
#define MI_WEBRTC_CONNECT_TIMEOUT_MS 30000u
#define MI_WEBRTC_IDR_RETRY_MS 300u
#ifndef MI_WEBRTC_FAST_START_BITRATE_BPS
#define MI_WEBRTC_FAST_START_BITRATE_BPS 6000000u
#endif
#ifndef MI_WEBRTC_FAST_START_BURST_MS
#define MI_WEBRTC_FAST_START_BURST_MS 120u
#endif
#ifndef MI_WEBRTC_PACED_QUEUE_LIMIT_MS
#define MI_WEBRTC_PACED_QUEUE_LIMIT_MS 1500u
#endif
#ifndef MI_WEBRTC_PACED_QUEUE_CAPACITY
#define MI_WEBRTC_PACED_QUEUE_CAPACITY 512u
#endif
#define MI_WEBRTC_PACED_QUEUE_CAPACITY_MIN 512u
#define MI_WEBRTC_RTP_PACKET_MAX 1250u
#define MI_WEBRTC_RTP_PACKET_TARGET 1200u
#define MI_WEBRTC_PACED_PACKET_SIZE MI_WEBRTC_RTP_PACKET_MAX
#define MI_WEBRTC_VIDEO_NACK_HISTORY_MIN 1000u
#define MI_WEBRTC_VIDEO_NACK_HISTORY 1000u
#define MI_WEBRTC_VIDEO_NACK_MAX_MISSING 128u
#define MI_WEBRTC_VIDEO_NACK_MAX_MISSING_MIN MI_WEBRTC_VIDEO_NACK_MAX_MISSING
#define MI_WEBRTC_AUDIO_NACK_HISTORY_MIN 64u
#define MI_WEBRTC_AUDIO_NACK_HISTORY 64u
#define MI_WEBRTC_AUDIO_NACK_HISTORY_MAX 260u
#define MI_WEBRTC_AUDIO_NACK_MAX_MISSING 32u
#define MI_WEBRTC_AUDIO_NACK_MAX_MISSING_MIN MI_WEBRTC_AUDIO_NACK_MAX_MISSING
#define MI_WEBRTC_NACK_PACKET_SIZE MI_WEBRTC_RTP_PACKET_MAX
#define MI_WEBRTC_NACK_HISTORY_MAX 1024u
#define MI_WEBRTC_NACK_MISSING_MAX 1024u
#define MI_WEBRTC_NACK_PACKET_SIZE_MAX MI_WEBRTC_RTP_PACKET_MAX
#define MI_WEBRTC_NET_ESTIMATE_LOG_INTERVAL_MS 1000u
#define MI_WEBRTC_NET_ESTIMATE_LOG_DELTA_BPS 100000u
#define MI_WEBRTC_TWCC_LOG_INTERVAL_MS 1000u
#ifndef MI_WEBRTC_ENCODER_MIN_BITRATE_BPS
#define MI_WEBRTC_ENCODER_MIN_BITRATE_BPS 100000u
#endif
#ifndef MI_WEBRTC_ENCODER_MAX_BITRATE_BPS
#define MI_WEBRTC_ENCODER_MAX_BITRATE_BPS 2500000u
#endif
#define MI_WEBRTC_ENCODER_RECONFIG_INTERVAL_MS 1000u
#define MI_WEBRTC_ENCODER_RECONFIG_DELTA_BPS 100000u
#define MI_WEBRTC_SESSION_JOIN_TIMEOUT_MS 3000u
#ifndef MI_WEBRTC_ENABLE_SESSION_LOOP_DEBUG
#define MI_WEBRTC_ENABLE_SESSION_LOOP_DEBUG 0
#endif
#define MI_WEBRTC_SESSION_DEBUG_LOOPS 8u
#define MI_WEBRTC_DIALOGUE_ID_MAX 0x7fffffffu
#define MI_WEBRTC_TURN_STATUS_INIT 0
#define MI_WEBRTC_TURN_STATUS_ERROR 1
#define MI_WEBRTC_TURN_STATUS_TIMEOUT 2
#define MI_WEBRTC_TURN_STATUS_CONNECTED 3
#ifndef MI_WEBRTC_SESSION_THREAD_STACK_SIZE
#define MI_WEBRTC_SESSION_THREAD_STACK_SIZE (256u * 1024u)
#endif
#ifndef MI_WEBRTC_ENABLE_SIM_LOSS
#define MI_WEBRTC_ENABLE_SIM_LOSS 0
#endif
#define MI_WEBRTC_SIM_LOSS_RATE_Q16 3277u
#ifndef MI_WEBRTC_USE_STATIC_DTLS_CERT
#define MI_WEBRTC_USE_STATIC_DTLS_CERT 0
#endif
#if defined(TX_5110)
#define MI_WEBRTC_DEFAULT_SOURCE_CHN 1
#else
#define MI_WEBRTC_DEFAULT_SOURCE_CHN 0
#endif

typedef struct mi_webrtc_track_s {
    int valid;
    int track_id;
    int kind;
    int payload_type;
    char encoding[MI_CALL_ENCODING_MAX];
    int clock_rate;
    int channels;
    int media_index;
    char fmtp[MI_SDP_TEXT_MAX];
    int enable_nack;
    int enable_pli;
    int enable_twcc;
    int twcc_ext_id;
    uint32_t ssrc;
    char sdp_label[64];
} mi_webrtc_track_t;

typedef struct mi_webrtc_session_s {
    struct mi_webrtc_session_s* next;
    uint32_t session_id;
    uint32_t created_ms;
    uint32_t last_active_ms;
    MiCall* call;
    struct mi_Mutex* call_lock;
    mi_thread_t thread;
    volatile int running;
    int thread_started;
    int ref_count;
    int removed;
    int ice_state;
    int rtc_state;
    int last_error;
    int source_chn;
    int video_track_id;
    int audio_track_id;
    int ice_checks_started;
    int remote_sdp_applied;
    int turn_server_configured;
    int close_requested;
    int video_keyframe_ready;
    int idr_requested;
    uint32_t last_idr_request_ms;
    uint32_t idr_request_count;
    uint32_t pli_request_count;
    uint32_t fir_request_count;
    uint32_t h264_recv_frames;
    uint32_t h264_wait_srtp_frames;
    uint32_t h264_wait_keyframe_frames;
    uint32_t h264_in_frames;
    uint32_t h264_in_bytes;
    uint32_t h264_send_ok;
    uint32_t h264_send_fail;
    int last_h264_send_ret;
    int64_t last_h264_pts;
    uint32_t last_h264_input_log_ms;
    uint32_t g711_recv_frames;
    uint32_t g711_in_frames;
    uint32_t g711_in_bytes;
    uint32_t g711_send_ok;
    uint32_t g711_send_fail;
    int last_g711_send_ret;
    uint32_t last_g711_input_log_ms;
    uint32_t last_g711_pts;
    uint32_t rtp_out_packets;
    uint32_t rtp_out_bytes;
    uint32_t rtcp_in_packets;
    uint32_t rtcp_in_bytes;
    uint32_t rtcp_out_packets;
    uint32_t rtcp_out_bytes;
    uint32_t selected_pair_count;
    uint32_t local_candidate_count;
    uint32_t remote_candidate_count;
    uint32_t target_bitrate_bps;
    uint32_t last_net_estimate_print_ms;
    uint32_t last_net_estimate_print_bps;
    uint32_t last_twcc_print_ms;
    uint32_t last_loss_print_dropped;
    uint32_t loop_debug_count;
    mi_webrtc_track_t tracks[MI_WEBRTC_MAX_TRACKS];
    int track_count;
} mi_webrtc_session_t;

typedef struct mi_webrtc_send_target_s {
    mi_webrtc_session_t* session;
    int track_id;
} mi_webrtc_send_target_t;

typedef struct mi_webrtc_cert_ctx_s {
    mbedtls_x509_crt parsed_cert;
    mbedtls_pk_context parsed_key;
    mbedtls_entropy_context entropy;
    mbedtls_ctr_drbg_context ctr_drbg;
    mbedtls_pk_context key;
    mbedtls_x509write_cert crt;
    mbedtls_mpi serial;
} mi_webrtc_cert_ctx_t;

static struct mi_Mutex* g_webrtc_lock;
static mi_webrtc_session_t* g_sessions;
static uint32_t g_next_session_id = 1;
static char g_cert_pem[MI_WEBRTC_CERT_BUF_SIZE];
static char g_key_pem[MI_WEBRTC_KEY_BUF_SIZE];
static int g_cert_ready;
static void (*g_request_idr_callback)(int chn, void* user_data);
static void* g_request_idr_user_data;
static void (*g_encoder_bitrate_callback)(int chn, uint32_t bitrate_bps, void* user_data);
static void* g_encoder_bitrate_user_data;
static uint32_t g_encoder_target_bitrate_bps;
static uint32_t g_last_encoder_set_bitrate_bps;
static uint32_t g_last_encoder_set_ms;
static mi_webrtc_resource_cfg_t g_resource_cfg = {
    MI_WEBRTC_VIDEO_NACK_HISTORY,
    MI_WEBRTC_VIDEO_NACK_MAX_MISSING,
    MI_WEBRTC_AUDIO_NACK_HISTORY,
    MI_WEBRTC_AUDIO_NACK_MAX_MISSING,
    MI_WEBRTC_NACK_PACKET_SIZE,
    MI_WEBRTC_PACED_QUEUE_CAPACITY,
    MI_WEBRTC_PACED_PACKET_SIZE
};

#if MI_WEBRTC_USE_STATIC_DTLS_CERT
static const char g_static_dtls_cert_pem[] =
"-----BEGIN CERTIFICATE-----\n"
"MIIBbTCCAROgAwIBAgIUPQzTw5LCt7fdZRDLF4YxN2yV8WgwCgYIKoZIzj0EAwIw\n"
"HjEcMBoGA1UEAwwTTWlJcGMgVFhXODJ4IFdlYlJUQzAeFw0yNjA2MDkxMjA2MTha\n"
"Fw0zNjA2MDYxMjA2MThaMB4xHDAaBgNVBAMME01pSXBjIFRYVzgyeCBXZWJSVEMw\n"
"WTATBgcqhkjOPQIBBggqhkjOPQMBBwNCAAR/FJeot1fzqaiSDUbfb1MWk2p/FdLq\n"
"aZ6g9tKfA46uOE5nQR83+RH3UcqwtHOuE+cvF4f0SJrxymP6WFZhkomooy8wLTAJ\n"
"BgNVHRMEAjAAMAsGA1UdDwQEAwIHgDATBgNVHSUEDDAKBggrBgEFBQcDATAKBggq\n"
"hkjOPQQDAgNIADBFAiA83npeTVhR8baKOlWGkGcAeuQlziBLBllDNKEqWUviRAIh\n"
"AN95bwWfqXLvOQltR9+sVdov3Q+Q0ip7hm7Y6LxqaZyp\n"
"-----END CERTIFICATE-----\n";

static const char g_static_dtls_key_pem[] =
"-----BEGIN EC PRIVATE KEY-----\n"
"MHcCAQEEIGWirlHiCCnpkCIZ/u2t2FIsXDvv4+5y4RL7znJhunokoAoGCCqGSM49\n"
"AwEHoUQDQgAEfxSXqLdX86mokg1G329TFpNqfxXS6mmeoPbSnwOOrjhOZ0EfN/kR\n"
"91HKsLRzrhPnLxeH9Eia8cpj+lhWYZKJqA==\n"
"-----END EC PRIVATE KEY-----\n";
#endif

static const char g_webrtc_page[] =
"<!doctype html>\n"
"<html lang=\"zh-CN\">\n"
"<head>\n"
"<meta charset=\"utf-8\">\n"
"<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\n"
"<title>MiIpc WebRTC</title>\n"
"<style>\n"
"html,body{margin:0;height:100%;font-family:Segoe UI,Arial,sans-serif;background:#101820;color:#eef4f6;}\n"
"main{min-height:100%;display:grid;grid-template-columns:minmax(0,1fr) 360px;gap:16px;padding:16px;box-sizing:border-box;}\n"
"video{width:100%;height:calc(100vh - 32px);background:#050708;object-fit:contain;border:1px solid #2d3a3f;}\n"
".panel{border-left:1px solid #2d3a3f;padding-left:16px;display:flex;flex-direction:column;gap:12px;}\n"
"button{height:36px;border:0;background:#32c48d;color:#062016;font-weight:600;cursor:pointer;}\n"
"button:disabled{opacity:.45;cursor:default;}\n"
"pre{white-space:pre-wrap;word-break:break-word;background:#071013;border:1px solid #2d3a3f;padding:10px;min-height:220px;overflow:auto;}\n"
"label{display:flex;gap:8px;align-items:center;color:#c9d4d8;font-size:13px;}\n"
"@media(max-width:800px){main{grid-template-columns:1fr;}video{height:56vh}.panel{border-left:0;padding-left:0}}\n"
"</style>\n"
"</head>\n"
"<body>\n"
"<main>\n"
"<video id=\"remote\" autoplay playsinline controls muted></video>\n"
"<section class=\"panel\">\n"
"<button id=\"start\">Start WebRTC</button>\n"
"<label><input id=\"wantAudio\" type=\"checkbox\" checked> audio</label>\n"
"<pre id=\"log\"></pre>\n"
"</section>\n"
"</main>\n"
"<script>\n"
"const logEl=document.getElementById('log');\n"
"const video=document.getElementById('remote');\n"
"const button=document.getElementById('start');\n"
"const params=new URLSearchParams(location.search);\n"
"const sourceChnText=params.get('chn')||params.get('channel')||params.get('route')||'';\n"
"const sourceCodecText=(params.get('codec')||'').toLowerCase();\n"
"const wantAudioDefault=!['0','false','off','no'].includes((params.get('audio')||'1').toLowerCase());\n"
"document.getElementById('wantAudio').checked=wantAudioDefault;\n"
"let pc=null;\n"
"let remoteStream=null;\n"
"let candidateState={pending:[],enabled:false,flushing:false};\n"
"let sessionId=0;\n"
"let reconnectTimer=0;\n"
"let reconnectAttempts=0;\n"
"let disconnectTimer=0;\n"
"let starting=false;\n"
"let manualStop=false;\n"
"function log(m){logEl.textContent+=new Date().toLocaleTimeString()+' '+m+'\\n';logEl.scrollTop=logEl.scrollHeight;}\n"
"function resetCandidateState(){candidateState={pending:[],enabled:false,flushing:false};}\n"
"async function closeRemoteSession(id){\n"
"  if(!id)return;\n"
"  try{\n"
"    const res=await fetch('/api/webrtc/close',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({sessionId:id}),keepalive:true});\n"
"    log('session close '+id+' '+res.status);\n"
"  }catch(e){log('session close error '+e.message);}\n"
"}\n"
"function closeLocalPc(reason){\n"
"  if(disconnectTimer){clearTimeout(disconnectTimer);disconnectTimer=0;}\n"
"  if(pc){\n"
"    const old=pc;\n"
"    pc=null;\n"
"    old.onconnectionstatechange=null;\n"
"    old.oniceconnectionstatechange=null;\n"
"    old.onicecandidate=null;\n"
"    old.ontrack=null;\n"
"    try{old.close();}catch(e){}\n"
"    log('pc closed '+reason);\n"
"  }\n"
"  remoteStream=null;\n"
"  video.srcObject=null;\n"
"}\n"
"function scheduleReconnect(reason){\n"
"  if(manualStop||reconnectTimer)return;\n"
"  const oldSessionId=sessionId;\n"
"  sessionId=0;\n"
"  resetCandidateState();\n"
"  closeLocalPc(reason);\n"
"  if(oldSessionId)closeRemoteSession(oldSessionId);\n"
"  const delay=Math.min(1000+reconnectAttempts*1000,5000);\n"
"  reconnectAttempts++;\n"
"  log('reconnect '+reason+' in '+delay+'ms');\n"
"  reconnectTimer=setTimeout(()=>{reconnectTimer=0;startSession(true);},delay);\n"
"}\n"
"function watchConnection(localPc){\n"
"  if(localPc!==pc)return;\n"
"  const ice=localPc.iceConnectionState;\n"
"  const state=localPc.connectionState;\n"
"  if(state==='connected'||ice==='connected'||ice==='completed'){\n"
"    reconnectAttempts=0;\n"
"    if(disconnectTimer){clearTimeout(disconnectTimer);disconnectTimer=0;}\n"
"  }\n"
"  if(state==='failed'||ice==='failed'){\n"
"    scheduleReconnect('failed');\n"
"  }else if(state==='disconnected'||ice==='disconnected'){\n"
"    if(!disconnectTimer){\n"
"      const observedPc=localPc;\n"
"      disconnectTimer=setTimeout(()=>{\n"
"        disconnectTimer=0;\n"
"        if(pc===observedPc&&(observedPc.connectionState==='disconnected'||observedPc.iceConnectionState==='disconnected'))scheduleReconnect('disconnected');\n"
"      },65000);\n"
"    }\n"
"  }\n"
"}\n"
"function sendCandidate(c){\n"
"  if(!c)return;\n"
"  if(c.candidate&&!/\\sudp\\s/i.test(c.candidate)){log('candidate skip non-udp');return;}\n"
"  candidateState.pending.push(c);\n"
"  if(candidateState.enabled)flushCandidates();\n"
"}\n"
"async function flushCandidates(){\n"
"  const state=candidateState,id=sessionId;\n"
"  state.enabled=true;if(state.flushing)return;state.flushing=true;\n"
"  try{\n"
"    while(state===candidateState&&id===sessionId&&state.pending.length){\n"
"      const c=state.pending.shift();\n"
"      try{const res=await fetch('/api/webrtc/candidate',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({sessionId:id,candidate:c.candidate||'',sdpMid:c.sdpMid,sdpMLineIndex:c.sdpMLineIndex})});log('candidate post '+res.status);}\n"
"      catch(e){log('candidate error '+e.message);}\n"
"    }\n"
"  }finally{state.flushing=false;}\n"
"}\n"
"async function pollRemoteCandidates(localPc,id){\n"
"  const seen=new Set((localPc.remoteDescription.sdp.match(/^a=candidate:.*$/gm)||[]).map(c=>c.trim().substring(2)));\n"
"  while(pc===localPc&&sessionId===id){\n"
"    try{\n"
"      const res=await fetch('/api/webrtc/candidates?sessionId='+id,{cache:'no-store'});\n"
"      if(res.status===404)return;if(!res.ok)throw new Error('candidate poll '+res.status);\n"
"      const data=await res.json();if(pc!==localPc||sessionId!==id)return;\n"
"      if(data.restartRequired){scheduleReconnect('ICE restart required');return;}\n"
"      for(const c of data.candidates||[]){if(!seen.has(c)){await localPc.addIceCandidate({candidate:c,sdpMLineIndex:0});seen.add(c);}}\n"
"      if(data.complete&&!data.continual){await localPc.addIceCandidate(null);return;}\n"
"    }catch(e){if(pc!==localPc)return;log('candidate poll error '+e.message);}\n"
"    await new Promise(resolve=>setTimeout(resolve,localPc.connectionState==='connected'?1000:250));\n"
"  }\n"
"}\n"
"async function startSession(autoReconnect){\n"
"  if(starting)return;\n"
"  starting=true;\n"
"  button.disabled=true;\n"
"  resetCandidateState();\n"
"  const oldSessionId=sessionId;\n"
"  sessionId=0;\n"
"  closeLocalPc(autoReconnect?'reconnect':'restart');\n"
"  if(oldSessionId)await closeRemoteSession(oldSessionId);\n"
"  try{\n"
"    const localPc=new RTCPeerConnection({iceServers:[],iceTransportPolicy:'all'});\n"
"    pc=localPc;\n"
"    remoteStream=new MediaStream();\n"
"    video.srcObject=remoteStream;\n"
"    video.muted=true;\n"
"    video.volume=1;\n"
"    window.miPc=pc;\n"
"    localPc.onconnectionstatechange=()=>{log('pc '+localPc.connectionState);watchConnection(localPc);};\n"
"    localPc.oniceconnectionstatechange=()=>{log('ice '+localPc.iceConnectionState);watchConnection(localPc);};\n"
"    localPc.onicecandidate=e=>{if(localPc===pc)sendCandidate(e.candidate||{candidate:''});};\n"
"    localPc.ontrack=e=>{\n"
"      if(!remoteStream){remoteStream=new MediaStream();video.srcObject=remoteStream;}\n"
"      if(!remoteStream.getTracks().some(t=>t.id===e.track.id))remoteStream.addTrack(e.track);\n"
"      video.play().catch(err=>log('play '+err.message));\n"
"      log('track '+e.track.kind+' tracks='+remoteStream.getTracks().map(t=>t.kind+':'+t.readyState).join(','));\n"
"    };\n"
"    const videoTransceiver=localPc.addTransceiver('video',{direction:'recvonly'});\n"
"    if(sourceCodecText==='h265'||sourceCodecText==='hevc'||sourceCodecText==='h264'){\n"
"      const caps=window.RTCRtpReceiver&&RTCRtpReceiver.getCapabilities?RTCRtpReceiver.getCapabilities('video'):null;\n"
"      const codecs=(caps&&caps.codecs)||[];\n"
"      const isWanted=c=>sourceCodecText==='h264'?/\\/H264$/i.test(c.mimeType):(/\\/H265$/i.test(c.mimeType)||/\\/HEVC$/i.test(c.mimeType));\n"
"      const preferred=codecs.filter(isWanted);\n"
"      if(preferred.length&&videoTransceiver.setCodecPreferences){videoTransceiver.setCodecPreferences(preferred.concat(codecs.filter(c=>!isWanted(c))));log('prefer codec '+sourceCodecText);}\n"
"      else log('codec '+sourceCodecText+' unavailable');\n"
"    }\n"
"    const wantAudio=document.getElementById('wantAudio').checked;\n"
"    if(wantAudio){localPc.addTransceiver('audio',{direction:'recvonly'});log('audio requested');}\n"
"    const offer=await localPc.createOffer();\n"
"    await localPc.setLocalDescription(offer);\n"
"    const offerBody={type:'offer',sdp:localPc.localDescription.sdp};\n"
"    if(sourceCodecText!==''){offerBody.codec=sourceCodecText;}\n"
"    if(sourceChnText!==''){\n"
"      const sourceChn=parseInt(sourceChnText,10);\n"
"      if(Number.isFinite(sourceChn)){offerBody.channel=sourceChn;offerBody.route=sourceChn;log('source chn '+sourceChn);}\n"
"    }\n"
"    const res=await fetch('/api/webrtc/offer',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(offerBody)});\n"
"    const text=await res.text();\n"
"    if(!res.ok)throw new Error(text);\n"
"    const answer=JSON.parse(text);\n"
"    sessionId=answer.sessionId||0;\n"
"    log('session '+sessionId);\n"
"    await localPc.setRemoteDescription({type:answer.type,sdp:answer.sdp});\n"
"    pollRemoteCandidates(localPc,sessionId);\n"
"    await flushCandidates();\n"
"    log('answer ok');\n"
"  }catch(e){\n"
"    log('error '+e.message);\n"
"    closeLocalPc('start error');\n"
"    if(sessionId){const id=sessionId;sessionId=0;closeRemoteSession(id);}\n"
"    if(autoReconnect&&!manualStop){\n"
"      scheduleReconnect('start error');\n"
"    }else{\n"
"      button.disabled=false;\n"
"    }\n"
"  }finally{\n"
"    starting=false;\n"
"  }\n"
"}\n"
"button.onclick=()=>{\n"
"  manualStop=false;\n"
"  reconnectAttempts=0;\n"
"  startSession(false);\n"
"};\n"
"window.addEventListener('beforeunload',()=>{manualStop=true;if(reconnectTimer)clearTimeout(reconnectTimer);const id=sessionId;sessionId=0;if(id)fetch('/api/webrtc/close',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({sessionId:id}),keepalive:true});});\n"
"if(new URLSearchParams(location.search).has('auto'))setTimeout(()=>button.click(),0);\n"
"</script>\n"
"</body>\n"
"</html>\n";

static void webrtc_lock(void)
{
    if (g_webrtc_lock == NULL) {
        g_webrtc_lock = mi_CreateMutex(1);
    }
    if (g_webrtc_lock != NULL) {
        g_webrtc_lock->Lock(g_webrtc_lock);
    }
}

static void webrtc_unlock(void)
{
    if (g_webrtc_lock != NULL) {
        g_webrtc_lock->UnLock(g_webrtc_lock);
    }
}

static uint16_t webrtc_clamp_u16(uint16_t value, uint16_t fallback_value, uint16_t min_value, uint16_t max_value)
{
    uint16_t out = value == 0 ? fallback_value : value;

    if (out < min_value) {
        out = min_value;
    }
    if (out > max_value) {
        out = max_value;
    }
    return out;
}

static void webrtc_normalize_resource_config(mi_webrtc_resource_cfg_t* cfg)
{
    if (cfg == NULL) {
        return;
    }

    cfg->video_nack_history = webrtc_clamp_u16(cfg->video_nack_history,
        MI_WEBRTC_VIDEO_NACK_HISTORY, MI_WEBRTC_VIDEO_NACK_HISTORY_MIN, MI_WEBRTC_NACK_HISTORY_MAX);
    cfg->video_nack_max_missing = webrtc_clamp_u16(cfg->video_nack_max_missing,
        MI_WEBRTC_VIDEO_NACK_MAX_MISSING, MI_WEBRTC_VIDEO_NACK_MAX_MISSING_MIN, MI_WEBRTC_NACK_MISSING_MAX);
    cfg->audio_nack_history = webrtc_clamp_u16(cfg->audio_nack_history,
        MI_WEBRTC_AUDIO_NACK_HISTORY, MI_WEBRTC_AUDIO_NACK_HISTORY_MIN, MI_WEBRTC_AUDIO_NACK_HISTORY_MAX);
    cfg->audio_nack_max_missing = webrtc_clamp_u16(cfg->audio_nack_max_missing,
        MI_WEBRTC_AUDIO_NACK_MAX_MISSING, MI_WEBRTC_AUDIO_NACK_MAX_MISSING_MIN, MI_WEBRTC_NACK_MISSING_MAX);
    cfg->nack_max_packet_size = webrtc_clamp_u16(cfg->nack_max_packet_size,
        MI_WEBRTC_NACK_PACKET_SIZE, MI_WEBRTC_NACK_PACKET_SIZE, MI_WEBRTC_NACK_PACKET_SIZE_MAX);
    cfg->paced_queue_capacity = webrtc_clamp_u16(cfg->paced_queue_capacity,
        MI_WEBRTC_PACED_QUEUE_CAPACITY, MI_WEBRTC_PACED_QUEUE_CAPACITY_MIN, 1024u);
    cfg->paced_max_packet_size = webrtc_clamp_u16(cfg->paced_max_packet_size,
        MI_WEBRTC_PACED_PACKET_SIZE, MI_WEBRTC_PACED_PACKET_SIZE, MI_WEBRTC_NACK_PACKET_SIZE_MAX);
}

static void webrtc_resource_config_snapshot(mi_webrtc_resource_cfg_t* cfg)
{
    if (cfg == NULL) {
        return;
    }
    webrtc_lock();
    *cfg = g_resource_cfg;
    webrtc_unlock();
    webrtc_normalize_resource_config(cfg);
}

static int webrtc_append(char* out, int out_size, int* offset, const char* fmt, ...)
{
    va_list ap;
    int n;

    if (out == NULL || offset == NULL || *offset < 0 || *offset >= out_size) {
        return -1;
    }

    va_start(ap, fmt);
    n = vsnprintf(out + *offset, (size_t)(out_size - *offset), fmt, ap);
    va_end(ap);
    if (n < 0 || n >= out_size - *offset) {
        return -1;
    }
    *offset += n;
    return 0;
}

static int webrtc_copy_text(char* dst, int dst_size, const char* src)
{
    size_t len;

    if (dst == NULL || dst_size <= 0) {
        return -1;
    }
    dst[0] = '\0';
    if (src == NULL) {
        return 0;
    }

    len = strlen(src);
    if (len >= (size_t)dst_size) {
        len = (size_t)dst_size - 1u;
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
    return 0;
}

static int webrtc_ascii_tolower(int c)
{
    return (c >= 'A' && c <= 'Z') ? (c + ('a' - 'A')) : c;
}

static int webrtc_stricmp(const char* a, const char* b)
{
    int ca;
    int cb;

    if (a == NULL || b == NULL) {
        return a == b ? 0 : (a == NULL ? -1 : 1);
    }

    while (*a != '\0' && *b != '\0') {
        ca = webrtc_ascii_tolower((unsigned char)*a);
        cb = webrtc_ascii_tolower((unsigned char)*b);
        if (ca != cb) {
            return ca - cb;
        }
        ++a;
        ++b;
    }
    return webrtc_ascii_tolower((unsigned char)*a) - webrtc_ascii_tolower((unsigned char)*b);
}

static int webrtc_strnicmp_local(const char* a, const char* b, int n)
{
    int i;

    if (n <= 0) {
        return 0;
    }
    if (a == NULL || b == NULL) {
        return a == b ? 0 : (a == NULL ? -1 : 1);
    }
    for (i = 0; i < n; ++i) {
        int ca = webrtc_ascii_tolower((unsigned char)a[i]);
        int cb = webrtc_ascii_tolower((unsigned char)b[i]);
        if (ca != cb || ca == '\0' || cb == '\0') {
            return ca - cb;
        }
    }
    return 0;
}

static int webrtc_contains_ci(const char* text, const char* needle)
{
    int needle_len;
    const char* p;

    if (text == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    needle_len = (int)strlen(needle);
    for (p = text; *p != '\0'; ++p) {
        if (webrtc_strnicmp_local(p, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

static int webrtc_ends_with_ci(const char* text, const char* suffix)
{
    int text_len;
    int suffix_len;

    if (text == NULL || suffix == NULL) {
        return 0;
    }
    text_len = (int)strlen(text);
    suffix_len = (int)strlen(suffix);
    if (suffix_len <= 0 || text_len < suffix_len) {
        return 0;
    }
    return webrtc_stricmp(text + text_len - suffix_len, suffix) == 0;
}

static int webrtc_parse_u32_token(const char* token, uint32_t* out)
{
    char* end;
    unsigned long value;

    if (token == NULL || out == NULL || token[0] == '\0') {
        return -1;
    }
    value = strtoul(token, &end, 10);
    if (end == token || *end != '\0' || value > 0xffffffffUL) {
        return -1;
    }
    *out = (uint32_t)value;
    return 0;
}

static int webrtc_parse_u16_token(const char* token, uint16_t* out)
{
    uint32_t value;

    if (out == NULL || webrtc_parse_u32_token(token, &value) != 0 || value > 65535u) {
        return -1;
    }
    *out = (uint16_t)value;
    return 0;
}

static int webrtc_candidate_type_from_text(const char* text)
{
    if (webrtc_stricmp(text, "host") == 0) {
        return ICE_CANDIDATE_TYPE_HOST;
    }
    if (webrtc_stricmp(text, "srflx") == 0) {
        return ICE_CANDIDATE_TYPE_SERVER_REFLEXIVE;
    }
    if (webrtc_stricmp(text, "prflx") == 0) {
        return ICE_CANDIDATE_TYPE_PEER_REFLEXIVE;
    }
    if (webrtc_stricmp(text, "relay") == 0) {
        return ICE_CANDIDATE_TYPE_RELAY;
    }
    return ICE_CANDIDATE_TYPE_HOST;
}

static void webrtc_json_escape_append(char* out, int out_size, int* offset, const char* text)
{
    const unsigned char* p = (const unsigned char*)text;

    while (p != NULL && *p != '\0' && *offset < out_size - 2) {
        unsigned char c = *p++;
        if (c == '"' || c == '\\') {
            webrtc_append(out, out_size, offset, "\\%c", c);
        } else if (c == '\n') {
            webrtc_append(out, out_size, offset, "\\n");
        } else if (c == '\r') {
            webrtc_append(out, out_size, offset, "\\r");
        } else if (c == '\t') {
            webrtc_append(out, out_size, offset, "\\t");
        } else if (c < 0x20) {
            webrtc_append(out, out_size, offset, "\\u%04x", (unsigned int)c);
        } else {
            out[(*offset)++] = (char)c;
            out[*offset] = '\0';
        }
    }
}

static char* webrtc_strdup_text(const char* text)
{
    size_t len;
    char* copy;

    if (text == NULL) {
        return NULL;
    }

    len = strlen(text);
    if (len > (size_t)0x7ffffffeu) {
        return NULL;
    }
    copy = (char*)mi_mem_malloc_large((int)len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, len + 1u);
    return copy;
}

static MiSdpSession* webrtc_alloc_sdp_session(void)
{
    return (MiSdpSession*)mi_mem_calloc_large(1, (int)sizeof(MiSdpSession));
}

static void webrtc_free_sdp_session(MiSdpSession* session)
{
    mi_mem_free_large(session);
}

static uint32_t webrtc_now_ms(void)
{
    return mi_getTick32();
}

static uint32_t webrtc_clamp_bitrate(uint32_t bitrate_bps)
{
    if (bitrate_bps < MI_WEBRTC_ENCODER_MIN_BITRATE_BPS) {
        return MI_WEBRTC_ENCODER_MIN_BITRATE_BPS;
    }
    if (bitrate_bps > MI_WEBRTC_ENCODER_MAX_BITRATE_BPS) {
        return MI_WEBRTC_ENCODER_MAX_BITRATE_BPS;
    }
    return bitrate_bps;
}

static void webrtc_request_idr(int chn)
{
    void (*callback)(int chn, void* user_data);
    void* user_data;

    webrtc_lock();
    callback = g_request_idr_callback;
    user_data = g_request_idr_user_data;
    webrtc_unlock();

    if (callback != NULL) {
        callback(chn, user_data);
    }
}

static void webrtc_notify_encoder_bitrate(int chn, uint32_t target_bitrate_bps)
{
    void (*callback)(int chn, uint32_t bitrate_bps, void* user_data);
    void* user_data;

    webrtc_lock();
    callback = g_encoder_bitrate_callback;
    user_data = g_encoder_bitrate_user_data;
    webrtc_unlock();

    if (callback != NULL) {
        callback(chn, target_bitrate_bps, user_data);
        printf("MiIpc WebRTC encoder bitrate target: chn=%d %u kbps\n",
            chn,
            (unsigned int)(target_bitrate_bps / 1000u));
        fflush(stdout);
    }
}

static uint32_t webrtc_update_encoder_target_locked(uint32_t now_ms, int* source_chn)
{
    mi_webrtc_session_t* session;
    uint32_t target_bitrate_bps;
    uint32_t delta_bps;
    int target_chn = MI_WEBRTC_DEFAULT_SOURCE_CHN;

    if (g_encoder_bitrate_callback == NULL) {
        return 0;
    }

    target_bitrate_bps = 0;
    for (session = g_sessions; session != NULL; session = session->next) {
        if (session->target_bitrate_bps == 0) {
            continue;
        }
        if (target_bitrate_bps == 0 || session->target_bitrate_bps < target_bitrate_bps) {
            target_bitrate_bps = session->target_bitrate_bps;
            target_chn = session->source_chn;
        }
    }
    if (target_bitrate_bps == 0) {
        target_bitrate_bps = MI_WEBRTC_FAST_START_BITRATE_BPS;
    }
    target_bitrate_bps = webrtc_clamp_bitrate(target_bitrate_bps);
    g_encoder_target_bitrate_bps = target_bitrate_bps;
    delta_bps = target_bitrate_bps > g_last_encoder_set_bitrate_bps ?
        target_bitrate_bps - g_last_encoder_set_bitrate_bps :
        g_last_encoder_set_bitrate_bps - target_bitrate_bps;

    if (g_last_encoder_set_bitrate_bps != 0) {
        if ((uint32_t)(now_ms - g_last_encoder_set_ms) < MI_WEBRTC_ENCODER_RECONFIG_INTERVAL_MS) {
            return 0;
        }
        if (delta_bps < MI_WEBRTC_ENCODER_RECONFIG_DELTA_BPS) {
            return 0;
        }
    }

    g_last_encoder_set_bitrate_bps = target_bitrate_bps;
    g_last_encoder_set_ms = now_ms;
    if (source_chn != NULL) {
        *source_chn = target_chn;
    }
    return target_bitrate_bps;
}

static int webrtc_mark_session_idr_locked(mi_webrtc_session_t* session, uint32_t now)
{
    if (session == NULL || session->video_track_id <= 0) {
        return 0;
    }
    if (session->rtc_state != MI_RTC_TRANSPORT_STATE_CONNECTED) {
        return 0;
    }
    if (now == 0) {
        now = webrtc_now_ms();
    }
    if (session->last_idr_request_ms != 0 &&
        (uint32_t)(now - session->last_idr_request_ms) < MI_WEBRTC_IDR_RETRY_MS) {
        return 0;
    }
    session->idr_requested = 1;
    session->last_idr_request_ms = now;
    ++session->idr_request_count;
    return 1;
}

static uint32_t webrtc_new_session_id(void)
{
    uint32_t id;
    mi_webrtc_session_t* session;
    int attempt;

    for (attempt = 0; attempt < 1024; ++attempt) {
        if (g_next_session_id == 0 || g_next_session_id > MI_WEBRTC_DIALOGUE_ID_MAX) {
            g_next_session_id = 1;
        }
        id = g_next_session_id++;
        if (g_next_session_id == 0 || g_next_session_id > MI_WEBRTC_DIALOGUE_ID_MAX) {
            g_next_session_id = 1;
        }
        for (session = g_sessions; session != NULL; session = session->next) {
            if (session->session_id == id) {
                break;
            }
        }
        if (session == NULL) {
            return id;
        }
    }

    if (g_next_session_id == 0 || g_next_session_id > MI_WEBRTC_DIALOGUE_ID_MAX) {
        g_next_session_id = 1;
    }
    return g_next_session_id++;
}

static int webrtc_session_is_dead(const mi_webrtc_session_t* session, uint32_t now)
{
    if (session == NULL) {
        return 1;
    }
    if (session->rtc_state == MI_RTC_TRANSPORT_STATE_CLOSED ||
        session->rtc_state == MI_RTC_TRANSPORT_STATE_FAILED) {
        return 1;
    }
    if (session->close_requested) {
        return 1;
    }
    if (session->rtc_state != MI_RTC_TRANSPORT_STATE_CONNECTED &&
        session->created_ms != 0 &&
        (uint32_t)(now - session->created_ms) > MI_WEBRTC_CONNECT_TIMEOUT_MS) {
        return 1;
    }
    if (session->last_active_ms != 0 &&
        (uint32_t)(now - session->last_active_ms) > MI_WEBRTC_SESSION_TIMEOUT_MS) {
        return 1;
    }
    return 0;
}

static void webrtc_touch_session(mi_webrtc_session_t* session)
{
    if (session != NULL) {
        session->last_active_ms = webrtc_now_ms();
    }
}

static int webrtc_session_count_locked(void)
{
    mi_webrtc_session_t* session;
    int count = 0;

    for (session = g_sessions; session != NULL; session = session->next) {
        ++count;
    }
    return count;
}

static mi_webrtc_session_t* webrtc_find_session_locked(uint32_t session_id)
{
    mi_webrtc_session_t* session;

    if (session_id == 0) {
        return g_sessions;
    }
    for (session = g_sessions; session != NULL; session = session->next) {
        if (session->session_id == session_id) {
            return session;
        }
    }
    return NULL;
}

static void webrtc_session_destroy(mi_webrtc_session_t* session);

static int webrtc_session_add_ref_locked(mi_webrtc_session_t* session)
{
    if (session == NULL || session->removed || session->close_requested || session->ref_count <= 0) {
        return 0;
    }
    ++session->ref_count;
    return 1;
}

static int webrtc_session_drop_ref_locked(mi_webrtc_session_t* session)
{
    if (session == NULL) {
        return 0;
    }
    if (session->ref_count > 0) {
        --session->ref_count;
    }
    return session->removed && session->ref_count == 0;
}

static void webrtc_session_release(mi_webrtc_session_t* session)
{
    int destroy_now;

    if (session == NULL) {
        return;
    }

    webrtc_lock();
    destroy_now = webrtc_session_drop_ref_locked(session);
    webrtc_unlock();

    if (destroy_now) {
        webrtc_session_destroy(session);
    }
}

static void webrtc_session_call_lock(mi_webrtc_session_t* session)
{
    if (session != NULL && session->call_lock != NULL) {
        session->call_lock->Lock(session->call_lock);
    }
}

static void webrtc_session_call_unlock(mi_webrtc_session_t* session)
{
    if (session != NULL && session->call_lock != NULL) {
        session->call_lock->UnLock(session->call_lock);
    }
}

static void webrtc_prune_sessions_locked(uint32_t now)
{
    mi_webrtc_session_t** link;
    mi_webrtc_session_t* session;

    link = &g_sessions;
    while (*link != NULL) {
        session = *link;
        if (webrtc_session_is_dead(session, now)) {
            int destroy_now;
            int connect_timeout = session->rtc_state != MI_RTC_TRANSPORT_STATE_CONNECTED &&
                session->created_ms != 0 &&
                (uint32_t)(now - session->created_ms) > MI_WEBRTC_CONNECT_TIMEOUT_MS;

            if (connect_timeout) {
                printf("MiIpc WebRTC session %u connect timeout cleanup ice=%d rtc=%d age=%u ms\n",
                    (unsigned int)session->session_id,
                    session->ice_state,
                    session->rtc_state,
                    (unsigned int)(now - session->created_ms));
                fflush(stdout);
            }

            *link = session->next;
            session->next = NULL;
            session->removed = 1;
            session->close_requested = 1;
            session->running = 0;
            destroy_now = webrtc_session_drop_ref_locked(session);
            webrtc_unlock();
            if (destroy_now) {
                webrtc_session_destroy(session);
            }
            webrtc_lock();
            link = &g_sessions;
            continue;
        }
        link = &session->next;
    }
}

static void webrtc_add_session_locked(mi_webrtc_session_t* session)
{
    if (session == NULL) {
        return;
    }
    session->ref_count = 1;
    session->removed = 0;
    session->next = g_sessions;
    g_sessions = session;
}

static int webrtc_remove_session_locked(mi_webrtc_session_t* target)
{
    mi_webrtc_session_t** link;

    if (target == NULL || target->removed) {
        return 0;
    }

    link = &g_sessions;
    while (*link != NULL) {
        if (*link == target) {
            *link = target->next;
            target->next = NULL;
            target->removed = 1;
            target->close_requested = 1;
            target->running = 0;
            return webrtc_session_drop_ref_locked(target);
        }
        link = &(*link)->next;
    }

    target->removed = 1;
    target->close_requested = 1;
    target->running = 0;
    return target->ref_count <= 0;
}

static void webrtc_remove_session(mi_webrtc_session_t* session)
{
    int destroy_now;

    if (session == NULL) {
        return;
    }

    webrtc_lock();
    destroy_now = webrtc_remove_session_locked(session);
    webrtc_unlock();
    if (destroy_now) {
        webrtc_session_destroy(session);
    }
}

static int webrtc_set_answer(mi_webrtc_answer_t* answer, int status, const char* content_type, const char* body)
{
    if (answer == NULL || body == NULL) {
        return -1;
    }

    memset(answer, 0, sizeof(*answer));
    answer->body = webrtc_strdup_text(body);
    if (answer->body == NULL) {
        return -1;
    }
    answer->status = status;
    answer->body_len = (int)strlen(answer->body);
    webrtc_copy_text(answer->content_type, (int)sizeof(answer->content_type),
        content_type != NULL ? content_type : "text/plain");
    return 0;
}

static int webrtc_set_error(mi_webrtc_answer_t* answer, int status, const char* message)
{
    char json[512];
    int off = 0;

    json[0] = '\0';
    webrtc_append(json, (int)sizeof(json), &off, "{\"error\":\"");
    webrtc_json_escape_append(json, (int)sizeof(json), &off, message != NULL ? message : "error");
    webrtc_append(json, (int)sizeof(json), &off, "\"}\n");
    return webrtc_set_answer(answer, status, "application/json", json);
}

static int webrtc_find_payload(const MiSdpMedia* media, int payload_type)
{
    int i;

    if (media == NULL) {
        return -1;
    }
    for (i = 0; i < media->payload_count; ++i) {
        if (media->payloads[i].payload_type == payload_type) {
            return i;
        }
    }
    return -1;
}

static int webrtc_payload_is_aux(const MiSdpPayload* payload)
{
    if (payload == NULL || payload->encoding[0] == '\0') {
        return 0;
    }
    return webrtc_stricmp(payload->encoding, "rtx") == 0 ||
           webrtc_stricmp(payload->encoding, "red") == 0 ||
           webrtc_stricmp(payload->encoding, "ulpfec") == 0 ||
           webrtc_stricmp(payload->encoding, "flexfec-03") == 0 ||
           webrtc_stricmp(payload->encoding, "telephone-event") == 0 ||
           webrtc_stricmp(payload->encoding, "CN") == 0;
}

static int webrtc_h264_payload_is_baseline(const MiSdpPayload* payload)
{
    if (payload == NULL || webrtc_stricmp(payload->encoding, "H264") != 0) {
        return 0;
    }
    if (payload->fmtp[0] == '\0') {
        return 1;
    }
    return webrtc_contains_ci(payload->fmtp, "profile-level-id=42e0") ||
           webrtc_contains_ci(payload->fmtp, "profile-level-id=42c0") ||
           webrtc_contains_ci(payload->fmtp, "profile-level-id=4200");
}

static int webrtc_payload_is_h265(const MiSdpPayload* payload)
{
    if (payload == NULL) {
        return 0;
    }
    return webrtc_stricmp(payload->encoding, "H265") == 0 ||
           webrtc_stricmp(payload->encoding, "HEVC") == 0;
}

static int webrtc_track_encoding_is_h265(const char* encoding)
{
    return encoding != NULL &&
           (webrtc_stricmp(encoding, "H265") == 0 ||
            webrtc_stricmp(encoding, "HEVC") == 0);
}

static int webrtc_payload_is_supported_video(const MiSdpPayload* payload)
{
    if (payload == NULL) {
        return 0;
    }
    return webrtc_stricmp(payload->encoding, "H264") == 0 ||
           webrtc_payload_is_h265(payload);
}

static int webrtc_payload_is_pcma(const MiSdpPayload* payload)
{
    return payload != NULL && webrtc_stricmp(payload->encoding, "PCMA") == 0;
}

static int webrtc_select_payload(const MiSdpMedia* media)
{
    int i;

    if (media == NULL) {
        return -1;
    }

    if (media->kind == MI_SDP_MEDIA_VIDEO) {
        for (i = 0; i < media->payload_type_count; ++i) {
            int index = webrtc_find_payload(media, media->payload_types[i]);
            if (index >= 0 && webrtc_h264_payload_is_baseline(&media->payloads[index])) {
                return index;
            }
        }
        for (i = 0; i < media->payload_count; ++i) {
            if (webrtc_h264_payload_is_baseline(&media->payloads[i])) {
                return i;
            }
        }
        for (i = 0; i < media->payload_type_count; ++i) {
            int index = webrtc_find_payload(media, media->payload_types[i]);
            if (index >= 0 && webrtc_payload_is_supported_video(&media->payloads[index])) {
                return index;
            }
        }
        for (i = 0; i < media->payload_count; ++i) {
            if (webrtc_payload_is_supported_video(&media->payloads[i])) {
                return i;
            }
        }
        return -1;
    }

    for (i = 0; i < media->payload_type_count; ++i) {
        int index = webrtc_find_payload(media, media->payload_types[i]);
        if (index >= 0 && webrtc_payload_is_pcma(&media->payloads[index])) {
            return index;
        }
    }
    for (i = 0; i < media->payload_count; ++i) {
        if (webrtc_payload_is_pcma(&media->payloads[i])) {
            return i;
        }
    }

    for (i = 0; i < media->payload_type_count; ++i) {
        int index = webrtc_find_payload(media, media->payload_types[i]);
        if (index >= 0 && !webrtc_payload_is_aux(&media->payloads[index])) {
            return index;
        }
    }
    for (i = 0; i < media->payload_count; ++i) {
        if (!webrtc_payload_is_aux(&media->payloads[i])) {
            return i;
        }
    }
    return media->payload_count > 0 ? 0 : -1;
}

static int webrtc_media_payload_supported(const MiSdpPayload* payload)
{
    if (payload == NULL || payload->encoding[0] == '\0') {
        return 0;
    }
    return webrtc_stricmp(payload->encoding, "H264") == 0 ||
           webrtc_payload_is_h265(payload) ||
           webrtc_stricmp(payload->encoding, "opus") == 0 ||
           webrtc_stricmp(payload->encoding, "PCMU") == 0 ||
           webrtc_stricmp(payload->encoding, "PCMA") == 0 ||
           webrtc_stricmp(payload->encoding, "G722") == 0;
}

static int webrtc_track_accepts_frame(const mi_webrtc_track_t* track, const struct Stream_Info* frame)
{
    if (track == NULL || frame == NULL || !track->valid) {
        return 0;
    }
    if (track->kind == MI_CALL_TRACK_KIND_VIDEO) {
        if (frame->enType == CI_H264) {
            return webrtc_stricmp(track->encoding, "H264") == 0;
        }
        if (frame->enType == CI_H265) {
            return webrtc_track_encoding_is_h265(track->encoding);
        }
        return 0;
    }
    if (track->kind == MI_CALL_TRACK_KIND_AUDIO) {
        if (frame->enType == CI_OPUS) {
            return webrtc_stricmp(track->encoding, "opus") == 0;
        }
        if (frame->enType == CI_G711) {
            return webrtc_stricmp(track->encoding, "PCMA") == 0;
        }
        if (frame->enType == CI_PCM) {
            return webrtc_stricmp(track->encoding, "G722") == 0;
        }
    }
    return 0;
}

static int webrtc_find_twcc_ext_id(const MiSdpMedia* media)
{
    int i;

    if (media == NULL) {
        return 0;
    }
    for (i = 0; i < media->extmap_count; ++i) {
        if (strstr(media->extmaps[i].uri, "transport-wide-cc") != NULL) {
            return media->extmaps[i].id;
        }
    }
    return 0;
}

static uint32_t webrtc_make_ssrc(int track_id)
{
    uint32_t seed = (uint32_t)time(NULL);
    seed ^= (uint32_t)(uintptr_t)&seed;
    seed ^= (uint32_t)(track_id * 0x45d9f3bu);
    if (seed == 0) {
        seed = (uint32_t)(0x10000u + (uint32_t)track_id);
    }
    return seed;
}

static int webrtc_track_from_media(const MiSdpMedia* media, int track_id, mi_webrtc_track_t* out)
{
    const MiSdpPayload* payload;
    int payload_index;

    if (media == NULL || out == NULL) {
        return -1;
    }
    if (media->kind != MI_SDP_MEDIA_AUDIO && media->kind != MI_SDP_MEDIA_VIDEO) {
        return -1;
    }

    payload_index = webrtc_select_payload(media);
    if (payload_index < 0) {
        return -1;
    }
    payload = &media->payloads[payload_index];
    if (!webrtc_media_payload_supported(payload)) {
        return -1;
    }

    memset(out, 0, sizeof(*out));
    out->valid = 1;
    out->track_id = track_id;
    out->kind = media->kind == MI_SDP_MEDIA_AUDIO ? MI_CALL_TRACK_KIND_AUDIO : MI_CALL_TRACK_KIND_VIDEO;
    out->payload_type = payload->payload_type;
    webrtc_copy_text(out->encoding, (int)sizeof(out->encoding), payload->encoding);
    out->clock_rate = payload->clock_rate > 0 ? payload->clock_rate :
        (media->kind == MI_SDP_MEDIA_AUDIO ? 48000 : 90000);
    out->channels = payload->channels;
    webrtc_copy_text(out->fmtp, (int)sizeof(out->fmtp), payload->fmtp);
    out->enable_nack = payload->mirtcp_fb_nack || payload->mirtcp_fb_nack_pli ||
        media->mirtcp_fb_nack || media->mirtcp_fb_nack_pli;
    out->enable_pli = payload->mirtcp_fb_nack_pli || media->mirtcp_fb_nack_pli;
    out->enable_twcc = payload->mirtcp_fb_transport_cc || media->mirtcp_fb_transport_cc;
    out->twcc_ext_id = webrtc_find_twcc_ext_id(media);
    if (out->twcc_ext_id > 0) {
        out->enable_twcc = 1;
    }
    out->ssrc = webrtc_make_ssrc(track_id);
    snprintf(out->sdp_label, sizeof(out->sdp_label), "MiIpcTrack%d", track_id);
    return 0;
}

static int webrtc_build_tracks(const MiSdpSession* remote,
    MiCallTrackCfg* call_tracks,
    int max_tracks,
    mi_webrtc_track_t* answer_tracks,
    const mi_webrtc_resource_cfg_t* resource_cfg)
{
    int i;
    int count = 0;
    int next_track_id = 1;

    if (remote == NULL || call_tracks == NULL || answer_tracks == NULL || max_tracks <= 0) {
        return -1;
    }

    memset(call_tracks, 0, (size_t)max_tracks * sizeof(call_tracks[0]));
    memset(answer_tracks, 0, (size_t)max_tracks * sizeof(answer_tracks[0]));

    for (i = 0; i < remote->media_count && count < max_tracks; ++i) {
        mi_webrtc_track_t track;
        MiCallTrackCfg* call_track;

        int track_id = remote->media[i].kind == MI_SDP_MEDIA_VIDEO ? MI_WEBRTC_TRACK_VIDEO : MI_WEBRTC_TRACK_AUDIO;
        if (track_id < next_track_id) {
            track_id = next_track_id;
        }
        if (webrtc_track_from_media(&remote->media[i], track_id, &track) != 0) {
            continue;
        }
        track.media_index = i;
        next_track_id = track_id + 1;

        answer_tracks[count] = track;

        call_track = &call_tracks[count];
        call_track->track_id = track.track_id;
        call_track->kind = track.kind;
        call_track->remote_ssrc = remote->media[i].ssrc_count > 0 ? remote->media[i].ssrcs[0] : 0;
        call_track->media.payload_type = track.payload_type;
        call_track->media.encoding = answer_tracks[count].encoding;
        call_track->media.frequency = track.clock_rate;
        call_track->media.ssrc = track.ssrc;
        call_track->media.enable_nack = track.enable_nack;
        call_track->media.enable_twcc = track.enable_twcc;
        call_track->media.twcc_ext_id = (uint8_t)track.twcc_ext_id;
        call_track->media.twcc_initial_bitrate_bps = 600000;
        call_track->media.twcc_min_bitrate_bps = 100000;
        call_track->media.twcc_max_bitrate_bps = MI_WEBRTC_ENCODER_MAX_BITRATE_BPS;
        call_track->media.twcc_feedback_interval_ms = 100;
        if (track.kind == MI_CALL_TRACK_KIND_AUDIO) {
            call_track->media.nack_history = resource_cfg != NULL ?
                resource_cfg->audio_nack_history :
                MI_WEBRTC_AUDIO_NACK_HISTORY;
            call_track->media.nack_max_missing = resource_cfg != NULL ?
                resource_cfg->audio_nack_max_missing :
                MI_WEBRTC_AUDIO_NACK_MAX_MISSING;
        } else {
            call_track->media.nack_history = resource_cfg != NULL ?
                resource_cfg->video_nack_history :
                MI_WEBRTC_VIDEO_NACK_HISTORY;
            call_track->media.nack_max_missing = resource_cfg != NULL ?
                resource_cfg->video_nack_max_missing :
                MI_WEBRTC_VIDEO_NACK_MAX_MISSING;
        }
        call_track->media.nack_max_packet_size = resource_cfg != NULL ?
            resource_cfg->nack_max_packet_size :
            MI_WEBRTC_NACK_PACKET_SIZE;
        call_track->media.nack_feedback_interval_ms = 20;
        call_track->media.nack_retry_interval_ms = 60;
        call_track->media.mirtp_packet_size = MI_WEBRTC_RTP_PACKET_TARGET;

        ++count;
    }

    return count;
}

static void webrtc_log_tracks(const char* prefix, const mi_webrtc_track_t* tracks, int track_count)
{
    int i;

    if (tracks == NULL || track_count <= 0) {
        return;
    }
    for (i = 0; i < track_count; ++i) {
        const mi_webrtc_track_t* track = &tracks[i];
        const char* kind = "unknown";
        if (!track->valid) {
            continue;
        }
        if (track->kind == MI_CALL_TRACK_KIND_AUDIO) {
            kind = "audio";
        } else if (track->kind == MI_CALL_TRACK_KIND_VIDEO) {
            kind = "video";
        }
        printf("MiIpc WebRTC %s track=%d mid=%d kind=%s codec=%s pt=%d clock=%d channels=%d ssrc=%u\n",
            prefix != NULL ? prefix : "sdp",
            track->track_id,
            track->media_index,
            kind,
            track->encoding,
            track->payload_type,
            track->clock_rate,
            track->channels,
            (unsigned int)track->ssrc);
    }
    fflush(stdout);
}

static int webrtc_parse_private_key(mbedtls_pk_context* key,
    const unsigned char* pem,
    size_t pem_len,
    const unsigned char* password,
    size_t password_len,
    mbedtls_ctr_drbg_context* ctr_drbg)
{
#if defined(MBEDTLS_VERSION_NUMBER) && MBEDTLS_VERSION_NUMBER >= 0x03000000
    return mbedtls_pk_parse_key(key, pem, pem_len, password, password_len,
        mbedtls_ctr_drbg_random, ctr_drbg);
#else
    (void)ctr_drbg;
    return mbedtls_pk_parse_key(key, pem, pem_len, password, password_len);
#endif
}

static int webrtc_init_certificate(void)
{
#if MI_WEBRTC_USE_STATIC_DTLS_CERT
    if (g_cert_ready) {
        return 0;
    }
    if (strlen(g_static_dtls_cert_pem) + 1u > sizeof(g_cert_pem) ||
        strlen(g_static_dtls_key_pem) + 1u > sizeof(g_key_pem)) {
        return -1;
    }
    webrtc_copy_text(g_cert_pem, (int)sizeof(g_cert_pem), g_static_dtls_cert_pem);
    webrtc_copy_text(g_key_pem, (int)sizeof(g_key_pem), g_static_dtls_key_pem);
    g_cert_ready = 1;
    return 0;
#else
    mi_webrtc_cert_ctx_t* ctx = NULL;
    const char personal[] = "MiIpcWebRtcCert";
    int ret;

    if (g_cert_ready) {
        return 0;
    }

    ctx = (mi_webrtc_cert_ctx_t*)mi_mem_calloc_large(1, (int)sizeof(*ctx));
    if (ctx == NULL) {
        return -1;
    }

    memset(g_cert_pem, 0, sizeof(g_cert_pem));
    memset(g_key_pem, 0, sizeof(g_key_pem));

    mbedtls_entropy_init(&ctx->entropy);
    mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
    mbedtls_pk_init(&ctx->key);
    mbedtls_x509write_crt_init(&ctx->crt);
    mbedtls_mpi_init(&ctx->serial);
    mbedtls_x509_crt_init(&ctx->parsed_cert);
    mbedtls_pk_init(&ctx->parsed_key);

    ret = mbedtls_ctr_drbg_seed(&ctx->ctr_drbg,
        mbedtls_entropy_func,
        &ctx->entropy,
        (const unsigned char*)personal,
        sizeof(personal) - 1u);
    if (ret != 0) {
        goto done;
    }

    ret = mbedtls_pk_setup(&ctx->key, mbedtls_pk_info_from_type(MBEDTLS_PK_RSA));
    if (ret != 0) {
        goto done;
    }
    ret = mbedtls_rsa_gen_key(mbedtls_pk_rsa(ctx->key), mbedtls_ctr_drbg_random, &ctx->ctr_drbg, 2048, 65537);
    if (ret != 0) {
        goto done;
    }

    memset(g_key_pem, 0, sizeof(g_key_pem));
    ret = mbedtls_pk_write_key_pem(&ctx->key, (unsigned char*)g_key_pem, sizeof(g_key_pem));
    if (ret != 0) {
        goto done;
    }

    ret = mbedtls_mpi_lset(&ctx->serial, 1);
    if (ret != 0) {
        goto done;
    }

    mbedtls_x509write_crt_set_version(&ctx->crt, MBEDTLS_X509_CRT_VERSION_3);
    ret = mbedtls_x509write_crt_set_serial(&ctx->crt, &ctx->serial);
    if (ret != 0) {
        goto done;
    }
    mbedtls_x509write_crt_set_md_alg(&ctx->crt, MBEDTLS_MD_SHA256);
    mbedtls_x509write_crt_set_subject_key(&ctx->crt, &ctx->key);
    mbedtls_x509write_crt_set_issuer_key(&ctx->crt, &ctx->key);
    ret = mbedtls_x509write_crt_set_subject_name(&ctx->crt, "CN=MiIpc WebRTC");
    if (ret != 0) {
        goto done;
    }
    ret = mbedtls_x509write_crt_set_issuer_name(&ctx->crt, "CN=MiIpc WebRTC");
    if (ret != 0) {
        goto done;
    }
    ret = mbedtls_x509write_crt_set_validity(&ctx->crt, "20260101000000", "20351231235959");
    if (ret != 0) {
        goto done;
    }
    (void)mbedtls_x509write_crt_set_basic_constraints(&ctx->crt, 0, -1);
    (void)mbedtls_x509write_crt_set_key_usage(&ctx->crt, MBEDTLS_X509_KU_DIGITAL_SIGNATURE);
    (void)mbedtls_x509write_crt_set_ns_cert_type(&ctx->crt, MBEDTLS_X509_NS_CERT_TYPE_SSL_SERVER);

    memset(g_cert_pem, 0, sizeof(g_cert_pem));
    ret = mbedtls_x509write_crt_pem(&ctx->crt,
        (unsigned char*)g_cert_pem,
        sizeof(g_cert_pem),
        mbedtls_ctr_drbg_random,
        &ctx->ctr_drbg);
    if (ret == 0) {
        ret = mbedtls_x509_crt_parse(&ctx->parsed_cert,
            (const unsigned char*)g_cert_pem,
            strlen(g_cert_pem) + 1u);
    }
    if (ret == 0) {
        ret = webrtc_parse_private_key(&ctx->parsed_key,
            (const unsigned char*)g_key_pem,
            strlen(g_key_pem) + 1u,
            NULL,
            0,
            &ctx->ctr_drbg);
    }
    if (ret == 0) {
        g_cert_ready = 1;
    }
#if defined(MI_WEBRTC_ENABLE_MBEDTLS_TEST_CERTS) && MI_WEBRTC_ENABLE_MBEDTLS_TEST_CERTS
    else {
        int fallback_ret;

        mbedtls_x509_crt_free(&ctx->parsed_cert);
        mbedtls_pk_free(&ctx->parsed_key);
        mbedtls_x509_crt_init(&ctx->parsed_cert);
        mbedtls_pk_init(&ctx->parsed_key);

        fallback_ret = mbedtls_x509_crt_parse(&ctx->parsed_cert,
            (const unsigned char*)mbedtls_test_srv_crt,
            strlen(mbedtls_test_srv_crt) + 1u);
        if (fallback_ret == 0) {
            fallback_ret = webrtc_parse_private_key(&ctx->parsed_key,
                (const unsigned char*)mbedtls_test_srv_key,
                strlen(mbedtls_test_srv_key) + 1u,
                (const unsigned char*)mbedtls_test_srv_pwd,
                strlen(mbedtls_test_srv_pwd),
                &ctx->ctr_drbg);
        }
        if (fallback_ret == 0) {
            webrtc_copy_text(g_cert_pem, (int)sizeof(g_cert_pem), mbedtls_test_srv_crt);
            webrtc_copy_text(g_key_pem, (int)sizeof(g_key_pem), mbedtls_test_srv_key);
            g_cert_ready = 1;
            ret = 0;
        }
    }
#endif

done:
    mbedtls_pk_free(&ctx->parsed_key);
    mbedtls_x509_crt_free(&ctx->parsed_cert);
    mbedtls_mpi_free(&ctx->serial);
    mbedtls_x509write_crt_free(&ctx->crt);
    mbedtls_pk_free(&ctx->key);
    mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
    mbedtls_entropy_free(&ctx->entropy);
    mi_mem_free_large(ctx);
    return ret;
#endif
}

static void webrtc_on_state(void* user_data, int state)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)user_data;
    int request_idr = 0;
    int request_chn = MI_WEBRTC_DEFAULT_SOURCE_CHN;

    webrtc_lock();
    if (session != NULL && !session->removed) {
        session->rtc_state = state;
        webrtc_touch_session(session);
        if (state == MI_RTC_TRANSPORT_STATE_CONNECTED) {
            session->video_keyframe_ready = 0;
            request_idr = webrtc_mark_session_idr_locked(session, 0);
            request_chn = session->source_chn;
        }
        printf("MiIpc WebRTC transport state: %d\n", state);
        fflush(stdout);
    }
    webrtc_unlock();
    if (request_idr) {
        webrtc_request_idr(request_chn);
    }
}

static void webrtc_on_ice_state(void* user_data, int state)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)user_data;

    webrtc_lock();
    if (session != NULL && !session->removed) {
        session->ice_state = state;
        webrtc_touch_session(session);
        printf("MiIpc WebRTC ICE state: %d\n", state);
        fflush(stdout);
    }
    webrtc_unlock();
}

static void webrtc_on_error(void* user_data, int error_code)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)user_data;

    webrtc_lock();
    if (session != NULL && !session->removed) {
        session->last_error = error_code;
        webrtc_touch_session(session);
    }
    printf("MiIpc WebRTC error: %d\n", error_code);
    fflush(stdout);
    webrtc_unlock();
}

static int webrtc_ice_checks_active(int ice_state)
{
    return ice_state == MI_ICE_STATE_CHECKING ||
           ice_state == MI_ICE_STATE_CONNECTED ||
           ice_state == MI_ICE_STATE_COMPLETED ||
           ice_state == MI_ICE_STATE_DISCONNECTED;
}

static int webrtc_session_turn_status(const mi_webrtc_session_t* session, uint32_t now_ms, int has_relay)
{
    if (session == NULL) {
        return MI_WEBRTC_TURN_STATUS_TIMEOUT;
    }
    if (has_relay) {
        return MI_WEBRTC_TURN_STATUS_CONNECTED;
    }
    if (!session->turn_server_configured) {
        return MI_WEBRTC_TURN_STATUS_INIT;
    }
    if (session->rtc_state == MI_RTC_TRANSPORT_STATE_FAILED ||
        session->rtc_state == MI_RTC_TRANSPORT_STATE_CLOSED ||
        session->ice_state == MI_ICE_STATE_FAILED ||
        session->ice_state == MI_ICE_STATE_CLOSED) {
        return MI_WEBRTC_TURN_STATUS_ERROR;
    }
    if (session->created_ms != 0 &&
        (uint32_t)(now_ms - session->created_ms) > MI_WEBRTC_CONNECT_TIMEOUT_MS) {
        return MI_WEBRTC_TURN_STATUS_TIMEOUT;
    }
    return MI_WEBRTC_TURN_STATUS_INIT;
}

static void webrtc_on_local_candidate(void* user_data, const ice_candi_t* candidate)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)user_data;

    webrtc_lock();
    if (session != NULL && !session->removed) {
        ++session->local_candidate_count;
    }
    if (candidate != NULL) {
        printf("MiIpc WebRTC local candidate %s:%u type=%d\n",
            candidate->address,
            (unsigned int)candidate->port,
            candidate->canditype);
        fflush(stdout);
    }
    webrtc_unlock();
}

static void webrtc_on_selected_pair(void* user_data, const ice_candi_t* local, const ice_candi_t* remote)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)user_data;

    webrtc_lock();
    if (session != NULL && !session->removed) {
        ++session->selected_pair_count;
        webrtc_touch_session(session);
    }
    if (local != NULL && remote != NULL) {
        printf("MiIpc WebRTC selected pair %s:%u -> %s:%u\n",
            local->address,
            (unsigned int)local->port,
            remote->address,
            (unsigned int)remote->port);
        fflush(stdout);
    }
    webrtc_unlock();
}

static void webrtc_on_target_bitrate(void* user_data, uint32_t bitrate_bps)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)user_data;
    MiTwccStats twcc_stats;
    uint32_t now_ms;
    uint32_t elapsed_ms;
    uint32_t delta_bps;
    uint32_t notify_bitrate_bps = 0;
    int notify_chn = MI_WEBRTC_DEFAULT_SOURCE_CHN;

    webrtc_lock();
    if (session != NULL && !session->removed) {
        session->target_bitrate_bps = bitrate_bps;
        webrtc_touch_session(session);
        now_ms = webrtc_now_ms();
        notify_bitrate_bps = webrtc_update_encoder_target_locked(now_ms, &notify_chn);
        elapsed_ms = now_ms - session->last_net_estimate_print_ms;
        delta_bps = bitrate_bps > session->last_net_estimate_print_bps ?
            bitrate_bps - session->last_net_estimate_print_bps :
            session->last_net_estimate_print_bps - bitrate_bps;
        if (session->last_net_estimate_print_ms == 0 ||
            elapsed_ms >= MI_WEBRTC_NET_ESTIMATE_LOG_INTERVAL_MS ||
            delta_bps >= MI_WEBRTC_NET_ESTIMATE_LOG_DELTA_BPS) {
            memset(&twcc_stats, 0, sizeof(twcc_stats));
            if (session->call != NULL && session->call->GetTwccStats != NULL) {
                session->call->GetTwccStats(session->call, &twcc_stats);
            }
            session->last_net_estimate_print_ms = now_ms;
            session->last_net_estimate_print_bps = bitrate_bps;
            printf("MiIpc WebRTC network estimate: target=%u kbps ack=%u kbps smooth_ack=%u kbps loss=%u/256 smooth_loss=%u/256 delay=%d ms smooth_delay=%d ms feedback=%u\n",
                (unsigned int)(bitrate_bps / 1000u),
                (unsigned int)(twcc_stats.acknowledged_bitrate_bps / 1000u),
                (unsigned int)(twcc_stats.smoothed_acknowledged_bitrate_bps / 1000u),
                (unsigned int)twcc_stats.loss_fraction_q8,
                (unsigned int)twcc_stats.smoothed_loss_fraction_q8,
                (int)twcc_stats.delay_trend_ms,
                (int)twcc_stats.smoothed_delay_trend_ms,
                (unsigned int)twcc_stats.remote_feedback_count);
            fflush(stdout);
        }
    }
    webrtc_unlock();
    if (notify_bitrate_bps != 0) {
        webrtc_notify_encoder_bitrate(notify_chn, notify_bitrate_bps);
    }
}

static void webrtc_on_keyframe_request(void* user_data, int track_id, int reason, uint32_t media_ssrc)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)user_data;
    int request_idr = 0;
    int request_chn = MI_WEBRTC_DEFAULT_SOURCE_CHN;

    webrtc_lock();
    if (session == NULL || session->removed || track_id != session->video_track_id) {
        webrtc_unlock();
        return;
    }
    if (reason == MI_CALL_KEYFRAME_REQUEST_PLI) {
        ++session->pli_request_count;
    } else if (reason == MI_CALL_KEYFRAME_REQUEST_FIR) {
        ++session->fir_request_count;
    }
    session->video_keyframe_ready = 0;
    request_idr = webrtc_mark_session_idr_locked(session, 0);
    request_chn = session->source_chn;
    webrtc_touch_session(session);
    printf("MiIpc WebRTC keyframe request track=%d reason=%d media_ssrc=%u\n",
        track_id,
        reason,
        (unsigned int)media_ssrc);
    fflush(stdout);
    webrtc_unlock();
    if (request_idr) {
        webrtc_request_idr(request_chn);
    }
}

static void webrtc_on_track_mirtp_packet(void* user_data, int track_id, const uint8_t* data, uint16_t len, int direction)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)user_data;

    (void)data;
    webrtc_lock();
    if (session == NULL || session->removed || direction != MI_CALL_PACKET_OUTGOING || len == 0) {
        webrtc_unlock();
        return;
    }
    if (track_id == session->video_track_id) {
        ++session->rtp_out_packets;
        session->rtp_out_bytes += len;
    }
    webrtc_unlock();
}

static void webrtc_on_track_mirtcp_packet(void* user_data, int track_id, const uint8_t* data, uint16_t len, int direction)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)user_data;

    (void)track_id;
    (void)data;
    webrtc_lock();
    if (session == NULL || session->removed || len == 0) {
        webrtc_unlock();
        return;
    }
    if (direction == MI_CALL_PACKET_OUTGOING) {
        ++session->rtcp_out_packets;
        session->rtcp_out_bytes += len;
    } else {
        ++session->rtcp_in_packets;
        session->rtcp_in_bytes += len;
        webrtc_touch_session(session);
    }
    webrtc_unlock();
}

static void webrtc_session_destroy(mi_webrtc_session_t* session)
{
    mi_thread_err_t join_ret;

    if (session == NULL) {
        return;
    }

    printf("MiIpc WebRTC session %u destroy begin thread=%d call=%p\n",
        (unsigned int)session->session_id,
        session->thread_started,
        (void*)session->call);
    fflush(stdout);

    webrtc_lock();
    session->removed = 1;
    session->close_requested = 1;
    session->running = 0;
    webrtc_unlock();

    session->running = 0;
    if (session->thread_started) {
        join_ret = mi_thread_join(&session->thread, NULL, MI_WEBRTC_SESSION_JOIN_TIMEOUT_MS);
        if (join_ret != MI_THREAD_OK) {
            printf("MiIpc WebRTC session %u thread join failed: %d\n",
                (unsigned int)session->session_id,
                (int)join_ret);
            return;
        }
        session->thread_started = 0;
    }
    webrtc_session_call_lock(session);
    if (session->call != NULL) {
        if (session->call->Destroy != NULL) {
            session->call->Destroy(session->call);
        } else {
            mi_call_destroy(session->call);
        }
        session->call = NULL;
    }
    webrtc_session_call_unlock(session);
    if (session->call_lock != NULL) {
        mi_DelMutex(session->call_lock);
        session->call_lock = NULL;
    }
    printf("MiIpc WebRTC session %u destroy done\n",
        (unsigned int)session->session_id);
    fflush(stdout);
    free(session);
}

static void webrtc_print_twcc_stats(mi_webrtc_session_t* session, uint32_t now_ms)
{
    MiTwccStats twcc_stats;
    MiLossStats loss_stats;
    MiPacedStats paced_stats;

    if (session == NULL || session->call == NULL || session->call->GetTwccStats == NULL) {
        return;
    }
    if (session->last_twcc_print_ms != 0 &&
        now_ms - session->last_twcc_print_ms < MI_WEBRTC_TWCC_LOG_INTERVAL_MS) {
        return;
    }

    memset(&twcc_stats, 0, sizeof(twcc_stats));
    memset(&loss_stats, 0, sizeof(loss_stats));
    memset(&paced_stats, 0, sizeof(paced_stats));
    if (session->call->GetTwccStats(session->call, &twcc_stats) != 0) {
        return;
    }
    if (session->call->GetLossStats != NULL) {
        session->call->GetLossStats(session->call, &loss_stats);
    }
    if (session->call->GetPacedStats != NULL) {
        session->call->GetPacedStats(session->call, &paced_stats);
    }
    session->last_twcc_print_ms = now_ms;
    if (loss_stats.dropped_packet_count != session->last_loss_print_dropped) {
        session->last_loss_print_dropped = loss_stats.dropped_packet_count;
    }
    printf("MiIpc WebRTC TWCC: enabled=%d ext=%u tracks=%u sent=%u recv=%u fb_local=%u fb_remote=%u req_fb=%u pending=%u target=%u kbps ack=%u kbps smooth_ack=%u kbps loss=%u/256 smooth_loss=%u/256 delay=%d/%d ms paced_queue=%u delay=%u/%u ms last_fb=%u/%u/%u updates=%u overuse=%u underuse=%u dup=%u ooo=%u sim_loss=%u/%u\n",
        twcc_stats.enabled,
        (unsigned int)twcc_stats.ext_id,
        (unsigned int)twcc_stats.registered_track_count,
        (unsigned int)twcc_stats.sent_packet_count,
        (unsigned int)twcc_stats.received_packet_count,
        (unsigned int)twcc_stats.feedback_count,
        (unsigned int)twcc_stats.remote_feedback_count,
        (unsigned int)twcc_stats.requested_feedback_count,
        (unsigned int)twcc_stats.pending_feedback_packets,
        (unsigned int)(twcc_stats.target_bitrate_bps / 1000u),
        (unsigned int)(twcc_stats.acknowledged_bitrate_bps / 1000u),
        (unsigned int)(twcc_stats.smoothed_acknowledged_bitrate_bps / 1000u),
        (unsigned int)twcc_stats.loss_fraction_q8,
        (unsigned int)twcc_stats.smoothed_loss_fraction_q8,
        (int)twcc_stats.delay_trend_ms,
        (int)twcc_stats.smoothed_delay_trend_ms,
        (unsigned int)paced_stats.queued_packets,
        (unsigned int)paced_stats.current_queue_delay_ms,
        (unsigned int)paced_stats.max_queue_delay_ms,
        (unsigned int)twcc_stats.last_remote_feedback_packets,
        (unsigned int)twcc_stats.last_remote_feedback_received,
        (unsigned int)twcc_stats.last_remote_feedback_lost,
        (unsigned int)twcc_stats.target_update_count,
        (unsigned int)twcc_stats.overuse_count,
        (unsigned int)twcc_stats.underuse_count,
        (unsigned int)twcc_stats.duplicate_packet_count,
        (unsigned int)twcc_stats.out_of_order_packet_count,
        (unsigned int)loss_stats.dropped_packet_count,
        (unsigned int)loss_stats.packet_count);
    fflush(stdout);
}

static void webrtc_print_session_loop_stage(const mi_webrtc_session_t* session,
    const char* stage,
    int value)
{
#if MI_WEBRTC_ENABLE_SESSION_LOOP_DEBUG
    if (session == NULL || stage == NULL ||
        session->loop_debug_count >= MI_WEBRTC_SESSION_DEBUG_LOOPS) {
        return;
    }
    printf("MiIpc WebRTC session loop sid=%u loop=%u stage=%s value=%d running=%d call=%p ice=%d rtc=%d\n",
        (unsigned int)session->session_id,
        (unsigned int)session->loop_debug_count,
        stage,
        value,
        session->running,
        (void*)session->call,
        session->ice_state,
        session->rtc_state);
    fflush(stdout);
#else
    (void)session;
    (void)stage;
    (void)value;
#endif
}

static void* webrtc_session_thread(void* arg)
{
    mi_webrtc_session_t* session = (mi_webrtc_session_t*)arg;
    uint32_t now_ms;

    while (session != NULL && session->running) {
        int process_ret = 0;
        int dead = 0;
        int request_idr = 0;
        int request_chn = MI_WEBRTC_DEFAULT_SOURCE_CHN;

        webrtc_print_session_loop_stage(session, "enter", 0);
        webrtc_session_call_lock(session);
        webrtc_print_session_loop_stage(session, "call-locked", 0);
        if (session->call != NULL && session->call->Process != NULL) {
            webrtc_print_session_loop_stage(session, "process-before", 0);
            process_ret = session->call->Process(session->call);
            webrtc_print_session_loop_stage(session, "process-after", process_ret);
        }
        now_ms = webrtc_now_ms();
        webrtc_print_session_loop_stage(session, "twcc-before", 0);
        webrtc_print_twcc_stats(session, now_ms);
        webrtc_session_call_unlock(session);

        webrtc_lock();
        if (session->rtc_state == MI_RTC_TRANSPORT_STATE_CONNECTED &&
            session->video_track_id > 0 &&
            !session->video_keyframe_ready) {
            request_idr = webrtc_mark_session_idr_locked(session, now_ms);
            request_chn = session->source_chn;
        }
        webrtc_print_session_loop_stage(session, "twcc-after", 0);
        dead = session->rtc_state == MI_RTC_TRANSPORT_STATE_CLOSED ||
            session->rtc_state == MI_RTC_TRANSPORT_STATE_FAILED ||
            webrtc_session_is_dead(session, now_ms);
        webrtc_print_session_loop_stage(session, "dead-check", dead);
        if (dead) {
            session->running = 0;
        }
        webrtc_unlock();
        if (request_idr) {
            webrtc_request_idr(request_chn);
        }
        webrtc_print_session_loop_stage(session, "unlocked", 0);
#if MI_WEBRTC_ENABLE_SESSION_LOOP_DEBUG
        if (session->loop_debug_count < MI_WEBRTC_SESSION_DEBUG_LOOPS) {
            ++session->loop_debug_count;
        }
#endif
        mi_thread_sleep(MI_WEBRTC_PROCESS_SLEEP_MS);
        webrtc_print_session_loop_stage(session, "sleep-after", 0);
    }
    if (session != NULL) {
        printf("MiIpc WebRTC session loop exit sid=%u running=%d ice=%d rtc=%d error=%d\n",
            (unsigned int)session->session_id,
            session->running,
            session->ice_state,
            session->rtc_state,
            session->last_error);
        fflush(stdout);
    }
    return NULL;
}

static int webrtc_start_session_thread(mi_webrtc_session_t* session)
{
    thread_config_t cfg;

    if (session == NULL) {
        return -1;
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.name = "MiIpcWebRtc";
    cfg.detached = 0;
    cfg.stack_size = MI_WEBRTC_SESSION_THREAD_STACK_SIZE;
    session->running = 1;
    if (mi_thread_create(&session->thread, webrtc_session_thread, session, &cfg) != MI_THREAD_OK) {
        session->running = 0;
        return -1;
    }
    session->thread_started = 1;
    return 0;
}

static const mi_webrtc_track_t* webrtc_track_for_id(const mi_webrtc_session_t* session, int track_id)
{
    int i;

    if (session == NULL || track_id <= 0) {
        return NULL;
    }
    for (i = 0; i < session->track_count; ++i) {
        if (session->tracks[i].valid && session->tracks[i].track_id == track_id) {
            return &session->tracks[i];
        }
    }
    return NULL;
}

static int webrtc_remote_candidate_count(const MiSdpSession* session)
{
    int count;
    int i;

    if (session == NULL) {
        return 0;
    }

    count = session->candidate_count;
    for (i = 0; i < session->media_count; ++i) {
        count += session->media[i].candidate_count;
    }
    return count;
}

static void webrtc_build_sdp_tracks(const mi_webrtc_session_t* session,
    MiSdpLocalTrack* out,
    int max_tracks,
    int* out_count)
{
    int i;
    int count = 0;

    if (out_count != NULL) {
        *out_count = 0;
    }
    if (session == NULL || out == NULL || max_tracks <= 0) {
        return;
    }

    memset(out, 0, (size_t)max_tracks * sizeof(out[0]));
    for (i = 0; i < session->track_count && count < max_tracks; ++i) {
        const mi_webrtc_track_t* src = &session->tracks[i];
        MiSdpLocalTrack* dst;
        if (!src->valid) {
            continue;
        }
        dst = &out[count++];
        dst->valid = src->valid;
        dst->track_id = src->track_id;
        dst->kind = src->kind;
        dst->media_index = src->media_index;
        dst->payload_type = src->payload_type;
        dst->encoding = src->encoding;
        dst->clock_rate = src->clock_rate;
        dst->channels = src->channels;
        dst->fmtp = src->fmtp;
        dst->enable_nack = src->enable_nack;
        dst->enable_pli = src->enable_pli;
        dst->enable_twcc = src->enable_twcc;
        dst->twcc_ext_id = src->twcc_ext_id;
        dst->ssrc = src->ssrc;
        dst->cname = "MiIpc";
        dst->stream_id = "MiIpcStream";
        dst->track_label = src->sdp_label;
    }
    if (out_count != NULL) {
        *out_count = count;
    }
}

static const char* webrtc_candidate_type_to_text(int type)
{
    switch (type) {
    case ICE_CANDIDATE_TYPE_SERVER_REFLEXIVE:
        return "srflx";
    case ICE_CANDIDATE_TYPE_PEER_REFLEXIVE:
        return "prflx";
    case ICE_CANDIDATE_TYPE_RELAY:
        return "relay";
    case ICE_CANDIDATE_TYPE_HOST:
    default:
        return "host";
    }
}

static int webrtc_candidate_to_text(char* out, int out_size, int* offset, const ice_candi_t* candidate)
{
    if (candidate == NULL || candidate->address[0] == '\0' || candidate->port == 0) {
        return -1;
    }
    if (webrtc_append(out,
        out_size,
        offset,
        "candidate:%s 1 udp %u %s %u typ %s",
        candidate->foundation[0] != '\0' ? candidate->foundation : "h1",
        (unsigned int)(candidate->priority != 0 ? candidate->priority : 2122260223u),
        candidate->address,
        (unsigned int)candidate->port,
        webrtc_candidate_type_to_text(candidate->canditype)) != 0) {
        return -1;
    }
    if (candidate->rel_addr[0] != '\0' && candidate->rel_port != 0) {
        if (webrtc_append(out,
            out_size,
            offset,
            " raddr %s rport %u",
            candidate->rel_addr,
            (unsigned int)candidate->rel_port) != 0) {
            return -1;
        }
    }
    if (candidate->username[0] != '\0') {
        if (webrtc_append(out, out_size, offset, " ufrag %s", candidate->username) != 0) {
            return -1;
        }
    }
    return webrtc_append(out, out_size, offset, "\r\n");
}

static int webrtc_append_sdp_candidate(char* out, int out_size, int* offset, const ice_candi_t* candidate)
{
    if (webrtc_append(out, out_size, offset, "a=") != 0) {
        return -1;
    }
    return webrtc_candidate_to_text(out, out_size, offset, candidate);
}

static int webrtc_copy_first_candidate_text(MiRtcTransport* transport, char* out, int out_size)
{
    ice_candi_t candidate;
    int offset = 0;

    if (transport == NULL || out == NULL || out_size <= 0 ||
        transport->GetLocalCandidate == NULL ||
        transport->GetLocalCandidate(transport, 0, &candidate) != 0) {
        return -1;
    }
    out[0] = '\0';
    if (webrtc_candidate_to_text(out, out_size, &offset, &candidate) != 0) {
        out[0] = '\0';
        return -1;
    }
    while (offset > 0 && (out[offset - 1] == '\r' || out[offset - 1] == '\n')) {
        out[--offset] = '\0';
    }
    return 0;
}

static int webrtc_normalize_ice_url(const char* url, int use_turn, char* out, int out_size)
{
    int off = 0;

    if (out == NULL || out_size <= 0) {
        return -1;
    }
    out[0] = '\0';
    if (url == NULL || url[0] == '\0') {
        return 0;
    }
    if (webrtc_strnicmp_local(url, "stun:", 5) == 0 ||
        webrtc_strnicmp_local(url, "turn:", 5) == 0) {
        webrtc_copy_text(out, out_size, url);
    } else if (webrtc_append(out, out_size, &off, "%s:%s", use_turn ? "turn" : "stun", url) != 0) {
        return -1;
    }
    if (use_turn && !webrtc_contains_ci(out, "transport=")) {
        off = (int)strlen(out);
        if (webrtc_append(out,
            out_size,
            &off,
            "%ctransport=udp",
            strchr(out, '?') != NULL ? '&' : '?') != 0) {
            return -1;
        }
    }
    return 0;
}

static int webrtc_offer_uses_turn(const mi_webrtc_offer_prepare_cfg_t* cfg)
{
    return cfg != NULL &&
        cfg->ice_url != NULL && cfg->ice_url[0] != '\0' &&
        cfg->ice_username != NULL && cfg->ice_username[0] != '\0' &&
        cfg->ice_password != NULL && cfg->ice_password[0] != '\0';
}

static void webrtc_apply_offer_ice_servers(MiRtcTransportCfg* transport_cfg,
    const mi_webrtc_offer_prepare_cfg_t* cfg)
{
    mi_ice_server_cfg_t servers;
    char url[256];
    int use_turn;

    if (transport_cfg == NULL || cfg == NULL || cfg->ice_url == NULL || cfg->ice_url[0] == '\0') {
        return;
    }

    memset(&servers, 0, sizeof(servers));
    use_turn = webrtc_offer_uses_turn(cfg);
    if (webrtc_normalize_ice_url(cfg->ice_url, use_turn, url, sizeof(url)) != 0 || url[0] == '\0') {
        return;
    }
    if (use_turn) {
        servers.turn_url = url;
        servers.turn_username = cfg->ice_username;
        servers.turn_password = cfg->ice_password;
    } else {
        servers.stun_urls[0] = url;
    }
    (void)mi_ice_cfg_set_servers(&transport_cfg->ice_cfg, &servers);
}

static int webrtc_codec_is_h265(const char* codec)
{
    if (codec == NULL || codec[0] == '\0') {
        return 0;
    }
    return webrtc_contains_ci(codec, "265") || webrtc_contains_ci(codec, "hevc");
}

static uint32_t webrtc_make_offer_ssrc(uint32_t session_id, int track_id)
{
    uint32_t ssrc = webrtc_make_ssrc(track_id) ^ (session_id * 2654435761u);
    return ssrc != 0 ? ssrc : (0x10000u + (uint32_t)track_id);
}

static void webrtc_fill_offer_media_cfg(MiCallMediaCfg* media,
    const mi_webrtc_track_t* track,
    const mi_webrtc_resource_cfg_t* resource_cfg)
{
    if (media == NULL || track == NULL) {
        return;
    }

    media->payload_type = track->payload_type;
    media->encoding = track->encoding;
    media->frequency = track->clock_rate;
    media->ssrc = track->ssrc;
    media->enable_nack = track->enable_nack;
    media->enable_twcc = track->enable_twcc;
    media->twcc_ext_id = (uint8_t)track->twcc_ext_id;
    media->twcc_initial_bitrate_bps = 600000;
    media->twcc_min_bitrate_bps = 100000;
    media->twcc_max_bitrate_bps = MI_WEBRTC_ENCODER_MAX_BITRATE_BPS;
    media->twcc_feedback_interval_ms = 100;
    if (track->kind == MI_CALL_TRACK_KIND_AUDIO) {
        media->nack_history = resource_cfg != NULL ?
            resource_cfg->audio_nack_history :
            MI_WEBRTC_AUDIO_NACK_HISTORY;
        media->nack_max_missing = resource_cfg != NULL ?
            resource_cfg->audio_nack_max_missing :
            MI_WEBRTC_AUDIO_NACK_MAX_MISSING;
    } else {
        media->nack_history = resource_cfg != NULL ?
            resource_cfg->video_nack_history :
            MI_WEBRTC_VIDEO_NACK_HISTORY;
        media->nack_max_missing = resource_cfg != NULL ?
            resource_cfg->video_nack_max_missing :
            MI_WEBRTC_VIDEO_NACK_MAX_MISSING;
    }
    media->nack_max_packet_size = resource_cfg != NULL ?
        resource_cfg->nack_max_packet_size :
        MI_WEBRTC_NACK_PACKET_SIZE;
    media->nack_feedback_interval_ms = 20;
    media->nack_retry_interval_ms = 60;
    media->mirtp_packet_size = MI_WEBRTC_RTP_PACKET_TARGET;
}

static int webrtc_build_offer_tracks(const mi_webrtc_offer_prepare_cfg_t* cfg,
    uint32_t session_id,
    MiCallTrackCfg* call_tracks,
    int max_tracks,
    mi_webrtc_track_t* offer_tracks,
    const mi_webrtc_resource_cfg_t* resource_cfg)
{
    int count = 0;
    int audio_mode;
    int want_audio;
    int want_video;

    if (call_tracks == NULL || offer_tracks == NULL || max_tracks <= 0) {
        return -1;
    }

    memset(call_tracks, 0, (size_t)max_tracks * sizeof(call_tracks[0]));
    memset(offer_tracks, 0, (size_t)max_tracks * sizeof(offer_tracks[0]));

    audio_mode = cfg != NULL ? cfg->audio_enable : 0;
    if (audio_mode < 0 || audio_mode > 2) {
        return -1;
    }
    want_audio = audio_mode > 0;
    want_video = (cfg == NULL || cfg->video_enable != 0 || !want_audio);

    if (want_video && count < max_tracks) {
        mi_webrtc_track_t* track = &offer_tracks[count];
        MiCallTrackCfg* call_track = &call_tracks[count];
        int h265 = cfg != NULL && webrtc_codec_is_h265(cfg->codec);

        memset(track, 0, sizeof(*track));
        track->valid = 1;
        track->track_id = MI_WEBRTC_TRACK_VIDEO;
        track->kind = MI_CALL_TRACK_KIND_VIDEO;
        track->payload_type = h265 ? 97 : 96;
        webrtc_copy_text(track->encoding, (int)sizeof(track->encoding), h265 ? "H265" : "H264");
        track->clock_rate = 90000;
        track->media_index = count;
        if (!h265) {
            webrtc_copy_text(track->fmtp,
                (int)sizeof(track->fmtp),
                "level-asymmetry-allowed=1;packetization-mode=1;profile-level-id=42e01f");
        }
        track->enable_nack = 1;
        track->enable_pli = 1;
        track->enable_twcc = 1;
        track->twcc_ext_id = 3;
        track->ssrc = webrtc_make_offer_ssrc(session_id, track->track_id);
        snprintf(track->sdp_label, sizeof(track->sdp_label), "MiIpcVideo%u", (unsigned int)session_id);

        call_track->track_id = track->track_id;
        call_track->kind = track->kind;
        call_track->remote_ssrc = 0;
        webrtc_fill_offer_media_cfg(&call_track->media, track, resource_cfg);
        ++count;
    }

    if (want_audio && count < max_tracks) {
        mi_webrtc_track_t* track = &offer_tracks[count];
        MiCallTrackCfg* call_track = &call_tracks[count];
        int opus = audio_mode == 2;

        memset(track, 0, sizeof(*track));
        track->valid = 1;
        track->track_id = MI_WEBRTC_TRACK_AUDIO;
        track->kind = MI_CALL_TRACK_KIND_AUDIO;
        track->payload_type = opus ? 111 : 8;
        webrtc_copy_text(track->encoding, (int)sizeof(track->encoding), opus ? "opus" : "PCMA");
        track->clock_rate = opus ? 48000 : 8000;
        track->channels = opus ? 2 : 1;
        track->media_index = count;
        if (opus) {
            webrtc_copy_text(track->fmtp, (int)sizeof(track->fmtp), "minptime=10;useinbandfec=1");
        }
        track->enable_nack = 1;
        track->enable_twcc = 1;
        track->twcc_ext_id = 3;
        track->ssrc = webrtc_make_offer_ssrc(session_id, track->track_id);
        snprintf(track->sdp_label, sizeof(track->sdp_label), "MiIpcAudio%u", (unsigned int)session_id);

        call_track->track_id = track->track_id;
        call_track->kind = track->kind;
        call_track->remote_ssrc = 0;
        webrtc_fill_offer_media_cfg(&call_track->media, track, resource_cfg);
        ++count;
    }

    return count;
}

static int webrtc_append_offer_media(char* out,
    int out_size,
    int* offset,
    const mi_webrtc_track_t* track,
    const char* ice_ufrag,
    const char* ice_pwd,
    const char* fingerprint,
    const ice_candi_t* candidates,
    int candidate_count,
    int gathering_complete)
{
    const char* kind;
    int i;

    if (track == NULL || !track->valid || ice_ufrag == NULL || ice_pwd == NULL || fingerprint == NULL) {
        return -1;
    }

    kind = track->kind == MI_CALL_TRACK_KIND_AUDIO ? "audio" : "video";
    if (webrtc_append(out,
        out_size,
        offset,
        "m=%s 9 UDP/TLS/RTP/SAVPF %d\r\n"
        "c=IN IP4 0.0.0.0\r\n"
        "a=rtcp:9 IN IP4 0.0.0.0\r\n"
        "a=mid:%d\r\n"
        "a=sendonly\r\n"
        "a=rtcp-mux\r\n"
        "a=rtcp-rsize\r\n"
        "a=ice-ufrag:%s\r\n"
        "a=ice-pwd:%s\r\n"
        "a=fingerprint:sha-256 %s\r\n"
        "a=setup:actpass\r\n",
        kind,
        track->payload_type,
        track->media_index,
        ice_ufrag,
        ice_pwd,
        fingerprint) != 0) {
        return -1;
    }

    if (webrtc_append(out,
        out_size,
        offset,
        "a=rtpmap:%d %s/%d",
        track->payload_type,
        track->encoding,
        track->clock_rate) != 0) {
        return -1;
    }
    if (track->kind == MI_CALL_TRACK_KIND_AUDIO && track->channels > 1) {
        if (webrtc_append(out, out_size, offset, "/%d", track->channels) != 0) {
            return -1;
        }
    }
    if (webrtc_append(out, out_size, offset, "\r\n") != 0) {
        return -1;
    }
    if (track->fmtp[0] != '\0' &&
        webrtc_append(out, out_size, offset, "a=fmtp:%d %s\r\n", track->payload_type, track->fmtp) != 0) {
        return -1;
    }
    if (track->enable_nack &&
        webrtc_append(out, out_size, offset, "a=rtcp-fb:%d nack\r\n", track->payload_type) != 0) {
        return -1;
    }
    if (track->enable_pli &&
        webrtc_append(out, out_size, offset, "a=rtcp-fb:%d nack pli\r\n", track->payload_type) != 0) {
        return -1;
    }
    if (track->enable_twcc &&
        webrtc_append(out, out_size, offset, "a=rtcp-fb:%d transport-cc\r\n", track->payload_type) != 0) {
        return -1;
    }
    if (track->twcc_ext_id > 0 &&
        webrtc_append(out,
            out_size,
            offset,
            "a=extmap:%d http://www.ietf.org/id/draft-holmer-rmcat-transport-wide-cc-extensions-01\r\n",
            track->twcc_ext_id) != 0) {
        return -1;
    }
    if (webrtc_append(out,
        out_size,
        offset,
        "a=ssrc:%u cname:MiIpc\r\n"
        "a=ssrc:%u msid:MiIpcStream %s\r\n",
        (unsigned int)track->ssrc,
        (unsigned int)track->ssrc,
        track->sdp_label) != 0) {
        return -1;
    }
    for (i = 0; i < candidate_count; ++i) {
        if (webrtc_append_sdp_candidate(out, out_size, offset, &candidates[i]) != 0) {
            return -1;
        }
    }
    return gathering_complete ? webrtc_append(out, out_size, offset, "a=end-of-candidates\r\n") : 0;
}

static int webrtc_build_sdp_offer(MiCall* call,
    const mi_webrtc_session_t* session,
    char* out,
    int out_size)
{
    MiRtcTransport* transport;
    ice_candi_t candidates[MI_SDP_CANDIDATE_MAX];
    char fingerprint[MI_DTLS_FINGERPRINT_TEXT_MAX];
    const char* ice_ufrag;
    const char* ice_pwd;
    int candidate_count;
    int published_count = 0;
    int off = 0;
    int i;

    if (call == NULL || session == NULL || out == NULL || out_size <= 0 ||
        session->track_count <= 0) {
        return -1;
    }

    out[0] = '\0';
    transport = call->GetTransport != NULL ? call->GetTransport(call) : NULL;
    if (transport == NULL || transport->GetLocalFingerprint == NULL ||
        transport->GetLocalFingerprint(transport, fingerprint, sizeof(fingerprint)) != 0 ||
        transport->GetLocalCandidateCount == NULL ||
        transport->GetLocalCandidate == NULL) {
        return -1;
    }

    memset(candidates, 0, sizeof(candidates));
    candidate_count = transport->GetLocalCandidateCount(transport);
    if (candidate_count <= 0) {
        return -1;
    }
    if (candidate_count > MI_SDP_CANDIDATE_MAX) {
        candidate_count = MI_SDP_CANDIDATE_MAX;
    }
    for (i = 0; i < candidate_count; ++i) {
        ice_candi_t candidate;
        if (transport->GetLocalCandidate(transport, i, &candidate) != 0) {
            return -1;
        }
        if (candidate.canditype != ICE_CANDIDATE_TYPE_PEER_REFLEXIVE) candidates[published_count++] = candidate;
    }
    candidate_count = published_count;

    ice_ufrag = candidates[0].username;
    ice_pwd = candidates[0].password;
    if (ice_ufrag == NULL || ice_ufrag[0] == '\0' ||
        ice_pwd == NULL || strlen(ice_pwd) < 22u) {
        return -1;
    }

    if (webrtc_append(out,
        out_size,
        &off,
        "v=0\r\n"
        "o=- %u 2 IN IP4 0.0.0.0\r\n"
        "s=MiIpc WebRTC\r\n"
        "t=0 0\r\n"
        "a=group:BUNDLE",
        (unsigned int)session->session_id) != 0) {
        return -1;
    }
    for (i = 0; i < session->track_count; ++i) {
        if (session->tracks[i].valid &&
            webrtc_append(out, out_size, &off, " %d", session->tracks[i].media_index) != 0) {
            return -1;
        }
    }
    if (webrtc_append(out,
        out_size,
        &off,
        "\r\n"
        "a=msid-semantic: WMS MiIpcStream\r\n"
        "a=ice-options:trickle renomination\r\n") != 0) {
        return -1;
    }

    for (i = 0; i < session->track_count; ++i) {
        if (session->tracks[i].valid &&
            webrtc_append_offer_media(out,
                out_size,
                &off,
                &session->tracks[i],
                ice_ufrag,
                ice_pwd,
                fingerprint,
                candidates,
                candidate_count,
                0) != 0) {
            return -1;
        }
    }

    return 0;
}

static char* webrtc_next_token(char** cursor)
{
    char* p;
    char* start;

    if (cursor == NULL || *cursor == NULL) {
        return NULL;
    }

    p = *cursor;
    while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
        ++p;
    }
    if (*p == '\0') {
        *cursor = p;
        return NULL;
    }

    start = p;
    while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
        ++p;
    }
    if (*p != '\0') {
        *p++ = '\0';
    }
    *cursor = p;
    return start;
}

static int webrtc_candidate_text_is_udp(const char* text, int len)
{
    char line[1024];
    char* cursor;
    char* token;
    char* transport;

    if (text == NULL || len <= 0 || len >= (int)sizeof(line)) {
        return -1;
    }
    while (len > 0 && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')) {
        ++text;
        --len;
    }
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' ||
           text[len - 1] == '\r' || text[len - 1] == '\n')) {
        --len;
    }
    if (len <= 0 || len >= (int)sizeof(line)) {
        return -1;
    }

    memcpy(line, text, (size_t)len);
    line[len] = '\0';
    cursor = line;
    token = webrtc_next_token(&cursor);
    if (token == NULL) {
        return -1;
    }
    if (strncmp(token, "a=candidate:", 12) == 0) {
        token += 12;
    } else if (strncmp(token, "candidate:", 10) == 0) {
        token += 10;
    } else {
        return -1;
    }
    if (*token == '\0') {
        token = webrtc_next_token(&cursor);
    }
    if (token == NULL || webrtc_next_token(&cursor) == NULL) {
        return -1;
    }
    transport = webrtc_next_token(&cursor);
    if (transport == NULL) {
        return -1;
    }
    return webrtc_stricmp(transport, "udp") == 0 ? 1 : 0;
}

static int webrtc_candidate_from_text(const char* text, int len, ice_candi_t* out)
{
    char line[1024];
    char* cursor;
    char* token;
    char* component;
    char* transport;
    char* priority;
    char* address;
    char* port;
    char* typ;
    char* cand_type;

    if (text == NULL || out == NULL || len <= 0) {
        return -1;
    }

    while (len > 0 && (*text == ' ' || *text == '\t' || *text == '\r' || *text == '\n')) {
        ++text;
        --len;
    }
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' ||
           text[len - 1] == '\r' || text[len - 1] == '\n')) {
        --len;
    }
    if (len <= 0 || len >= (int)sizeof(line) - 4) {
        return -1;
    }

    memcpy(line, text, (size_t)len);
    line[len] = '\0';
    cursor = line;

    token = webrtc_next_token(&cursor);
    if (token == NULL) {
        return -1;
    }
    if (strncmp(token, "a=candidate:", 12) == 0) {
        token += 12;
    } else if (strncmp(token, "candidate:", 10) == 0) {
        token += 10;
    } else {
        return -1;
    }
    if (*token == '\0') {
        token = webrtc_next_token(&cursor);
    }
    component = webrtc_next_token(&cursor);
    transport = webrtc_next_token(&cursor);
    priority = webrtc_next_token(&cursor);
    address = webrtc_next_token(&cursor);
    port = webrtc_next_token(&cursor);
    typ = webrtc_next_token(&cursor);
    cand_type = webrtc_next_token(&cursor);
    (void)component;
    if (token == NULL || transport == NULL || priority == NULL ||
        address == NULL || port == NULL || typ == NULL || cand_type == NULL ||
        webrtc_stricmp(typ, "typ") != 0) {
        return -1;
    }
    if (webrtc_stricmp(transport, "udp") != 0) {
        return -2;
    }

    memset(out, 0, sizeof(*out));
    webrtc_copy_text(out->foundation, (int)sizeof(out->foundation), token);
    out->transtype = ICE_TRANSPORT_UDP;
    out->canditype = webrtc_candidate_type_from_text(cand_type);
    if (webrtc_parse_u32_token(priority, &out->priority) != 0 ||
        webrtc_parse_u16_token(port, &out->port) != 0) {
        return -1;
    }
    webrtc_copy_text(out->address, (int)sizeof(out->address), address);
    webrtc_copy_text(out->base_address, (int)sizeof(out->base_address), address);
    out->base_port = out->port;

    while ((token = webrtc_next_token(&cursor)) != NULL) {
        char* arg = webrtc_next_token(&cursor);
        if (arg == NULL) {
            break;
        }
        if (webrtc_stricmp(token, "raddr") == 0) {
            webrtc_copy_text(out->rel_addr, (int)sizeof(out->rel_addr), arg);
        } else if (webrtc_stricmp(token, "rport") == 0) {
            (void)webrtc_parse_u16_token(arg, &out->rel_port);
        } else if (webrtc_stricmp(token, "ufrag") == 0) {
            webrtc_copy_text(out->username, (int)sizeof(out->username), arg);
        } else if (webrtc_stricmp(token, "pwd") == 0) {
            webrtc_copy_text(out->password, (int)sizeof(out->password), arg);
        }
    }
    return 0;
}

static int webrtc_answer_json(uint32_t session_id, const char* sdp, mi_webrtc_answer_t* answer)
{
    char* json;
    int off = 0;
    int ret;

    if (sdp == NULL || answer == NULL) {
        return -1;
    }

    json = (char*)mi_mem_malloc_large(MI_WEBRTC_JSON_MAX);
    if (json == NULL) {
        return -1;
    }
    json[0] = '\0';

    if (webrtc_append(json,
        MI_WEBRTC_JSON_MAX,
        &off,
        "{\"type\":\"answer\",\"sessionId\":%u,\"sdp\":\"",
        (unsigned int)session_id) != 0) {
        mi_mem_free_large(json);
        return -1;
    }
    webrtc_json_escape_append(json, MI_WEBRTC_JSON_MAX, &off, sdp);
    if (webrtc_append(json, MI_WEBRTC_JSON_MAX, &off, "\"}\n") != 0) {
        mi_mem_free_large(json);
        return -1;
    }

    ret = webrtc_set_answer(answer, 200, "application/json", json);
    mi_mem_free_large(json);
    return ret;
}

const char* mi_webrtc_page_html(void)
{
    return g_webrtc_page;
}

void mi_webrtc_set_request_idr_callback(void (*callback)(int chn, void* user_data), void* user_data)
{
    webrtc_lock();
    g_request_idr_callback = callback;
    g_request_idr_user_data = user_data;
    webrtc_unlock();
}

void mi_webrtc_set_encoder_bitrate_callback(void (*callback)(int chn, uint32_t bitrate_bps, void* user_data), void* user_data)
{
    webrtc_lock();
    g_encoder_bitrate_callback = callback;
    g_encoder_bitrate_user_data = user_data;
    g_last_encoder_set_bitrate_bps = 0;
    g_last_encoder_set_ms = 0;
    webrtc_unlock();
}

int mi_webrtc_set_resource_config(const mi_webrtc_resource_cfg_t* cfg)
{
    mi_webrtc_resource_cfg_t normalized;

    if (cfg == NULL) {
        return -1;
    }

    normalized = *cfg;
    webrtc_normalize_resource_config(&normalized);

    webrtc_lock();
    g_resource_cfg = normalized;
    webrtc_unlock();
    return 0;
}

void mi_webrtc_get_resource_config(mi_webrtc_resource_cfg_t* cfg)
{
    webrtc_resource_config_snapshot(cfg);
}

int mi_webrtc_prepare_sdp_offer(const mi_webrtc_offer_prepare_cfg_t* prepare_cfg,
    mi_webrtc_offer_prepare_result_t* result)
{
    MiCallTrackCfg call_tracks[MI_WEBRTC_MAX_TRACKS];
    MiCallCfg call_cfg;
    mi_webrtc_resource_cfg_t resource_cfg;
    mi_webrtc_session_t* session = NULL;
    MiRtcTransport* transport;
    int track_count;
    int elapsed;
    int ret = -1;
    int source_chn;
    uint32_t now_ms;

    if (result == NULL) {
        return -1;
    }
    memset(result, 0, sizeof(*result));

    if (prepare_cfg != NULL && prepare_cfg->close_all) {
        mi_webrtc_shutdown();
    }

    webrtc_resource_config_snapshot(&resource_cfg);
    if (webrtc_init_certificate() != 0) {
        return -2;
    }

    session = (mi_webrtc_session_t*)calloc(1, sizeof(*session));
    if (session == NULL) {
        return -3;
    }
    session->call_lock = mi_CreateMutex(1);
    if (session->call_lock == NULL) {
        free(session);
        return -4;
    }

    source_chn = prepare_cfg != NULL && prepare_cfg->source_chn >= 0 ?
        prepare_cfg->source_chn :
        MI_WEBRTC_DEFAULT_SOURCE_CHN;

    webrtc_lock();
    now_ms = webrtc_now_ms();
    webrtc_prune_sessions_locked(now_ms);
    if (webrtc_session_count_locked() >= MI_WEBRTC_MAX_SESSIONS) {
        webrtc_unlock();
        webrtc_session_destroy(session);
        return -5;
    }
    session->session_id = webrtc_new_session_id();
    session->created_ms = now_ms;
    session->last_active_ms = now_ms;
    session->source_chn = source_chn;
    session->turn_server_configured = webrtc_offer_uses_turn(prepare_cfg);
    webrtc_unlock();

    track_count = webrtc_build_offer_tracks(prepare_cfg,
        session->session_id,
        call_tracks,
        MI_WEBRTC_MAX_TRACKS,
        session->tracks,
        &resource_cfg);
    if (track_count <= 0) {
        webrtc_session_destroy(session);
        return -6;
    }
    session->track_count = track_count;
    webrtc_log_tracks("local-offer", session->tracks, track_count);
    for (elapsed = 0; elapsed < track_count; ++elapsed) {
        if (session->tracks[elapsed].kind == MI_CALL_TRACK_KIND_VIDEO && session->video_track_id == 0) {
            session->video_track_id = session->tracks[elapsed].track_id;
        } else if (session->tracks[elapsed].kind == MI_CALL_TRACK_KIND_AUDIO && session->audio_track_id == 0) {
            session->audio_track_id = session->tracks[elapsed].track_id;
        }
    }

    memset(&call_cfg, 0, sizeof(call_cfg));
    call_cfg.transport_cfg.dtls_role = MI_DTLS_ROLE_SERVER;
    call_cfg.transport_cfg.dtls_verify_mode = MI_DTLS_VERIFY_OPTIONAL;
    call_cfg.transport_cfg.dtls_mtu = 1200;
    call_cfg.transport_cfg.cert_pem = g_cert_pem;
    call_cfg.transport_cfg.key_pem = g_key_pem;
    call_cfg.transport_cfg.ice_cfg.controlling = 1;
    call_cfg.transport_cfg.ice_cfg.port_min = 50000;
    call_cfg.transport_cfg.ice_cfg.port_max = 50100;
    webrtc_apply_offer_ice_servers(&call_cfg.transport_cfg, prepare_cfg);
    call_cfg.callback.user_data = session;
    call_cfg.callback.on_state = webrtc_on_state;
    call_cfg.callback.on_ice_state = webrtc_on_ice_state;
    call_cfg.callback.on_error = webrtc_on_error;
    call_cfg.callback.on_local_candidate = webrtc_on_local_candidate;
    call_cfg.callback.on_selected_pair = webrtc_on_selected_pair;
    call_cfg.callback.on_target_bitrate = webrtc_on_target_bitrate;
    call_cfg.callback.on_keyframe_request = webrtc_on_keyframe_request;
    call_cfg.callback.on_track_mirtp_packet = webrtc_on_track_mirtp_packet;
    call_cfg.callback.on_track_mirtcp_packet = webrtc_on_track_mirtcp_packet;
    call_cfg.tracks = call_tracks;
    call_cfg.track_count = track_count;
    call_cfg.enable_paced = 1;
    call_cfg.paced_queue_capacity = resource_cfg.paced_queue_capacity;
    call_cfg.paced_max_packet_size = resource_cfg.paced_max_packet_size;
    call_cfg.paced_bitrate_bps = 0;
    call_cfg.paced_burst_ms = MI_WEBRTC_FAST_START_BURST_MS;
    call_cfg.paced_max_packets_per_process = 32;
    call_cfg.paced_max_bytes_per_process = 48000;
    call_cfg.paced_queue_time_limit_ms = MI_WEBRTC_PACED_QUEUE_LIMIT_MS;
    call_cfg.loss_cfg.enabled = MI_WEBRTC_ENABLE_SIM_LOSS ? 1 : 0;
    call_cfg.loss_cfg.direction = MI_CALL_PACKET_OUTGOING;
    call_cfg.loss_cfg.packet_kind = MI_LOSS_PACKET_RTP;
    call_cfg.loss_cfg.random_loss_rate_q16 = MI_WEBRTC_ENABLE_SIM_LOSS ? MI_WEBRTC_SIM_LOSS_RATE_Q16 : 0;
    call_cfg.loss_cfg.seed = session->session_id ^ webrtc_now_ms();

    {
        MiCall call_holder;
        memset(&call_holder, 0, sizeof(call_holder));
        ret = InitCall(&call_holder, &call_cfg);
        if (ret == 0) {
            session->call = (MiCall*)call_holder.impl;
        }
    }
    if (session->call == NULL) {
        printf("MiIpc WebRTC offer prepare call create failed ret=%d dialogue=%u\n",
            ret,
            (unsigned int)session->session_id);
        fflush(stdout);
        webrtc_session_destroy(session);
        return -7;
    }

    if (session->call->StartGathering == NULL || session->call->StartGathering(session->call) != 0) {
        webrtc_session_destroy(session);
        return -8;
    }
    for (elapsed = 0; elapsed < MI_WEBRTC_GATHER_WAIT_MS; elapsed += 20) {
        if (session->call->Process != NULL) {
            session->call->Process(session->call);
        }
        mi_thread_sleep(20);
    }

    if (webrtc_build_sdp_offer(session->call,
        session,
        result->sdp_offer,
        (int)sizeof(result->sdp_offer)) != 0) {
        webrtc_session_destroy(session);
        return -9;
    }

    transport = session->call->GetTransport != NULL ? session->call->GetTransport(session->call) : NULL;
    if (webrtc_copy_first_candidate_text(transport, result->candidate, (int)sizeof(result->candidate)) != 0) {
        result->candidate[0] = '\0';
    }

    webrtc_lock();
    session->last_active_ms = webrtc_now_ms();
    webrtc_add_session_locked(session);
    webrtc_unlock();

    if (webrtc_start_session_thread(session) != 0) {
        webrtc_remove_session(session);
        memset(result, 0, sizeof(*result));
        return -10;
    }

    result->dialogue_id = session->session_id;
    printf("MiIpc WebRTC SDP offer prepared DialogueId=%u source_chn=%d tracks=%d sdp_len=%u cand_len=%u\n",
        (unsigned int)result->dialogue_id,
        source_chn,
        track_count,
        (unsigned int)strlen(result->sdp_offer),
        (unsigned int)strlen(result->candidate));
    fflush(stdout);
    return 0;
}

int mi_webrtc_handle_offer_for_channel(const char* offer, int offer_len, mi_webrtc_answer_t* answer, int source_chn)
{
    MiSdpSession* remote = NULL;
    MiCallTrackCfg call_tracks[MI_WEBRTC_MAX_TRACKS];
    mi_webrtc_track_t answer_tracks[MI_WEBRTC_MAX_TRACKS];
    MiCallCfg cfg;
    mi_webrtc_resource_cfg_t resource_cfg;
    mi_webrtc_session_t* session = NULL;
    char* sdp_answer = NULL;
    int track_count;
    int elapsed;
    int ret;
    int request_idr = 0;
    int request_chn = MI_WEBRTC_DEFAULT_SOURCE_CHN;
    uint32_t now_ms;

    if (offer == NULL || offer_len <= 0 || answer == NULL) {
        return webrtc_set_error(answer, 400, "invalid offer");
    }
    memset(answer, 0, sizeof(*answer));
    if (source_chn < 0) {
        source_chn = MI_WEBRTC_DEFAULT_SOURCE_CHN;
    }

    printf("MiIpc WebRTC offer stage: begin sdp_len=%d source_chn=%d\n", offer_len, source_chn);
    fflush(stdout);
    webrtc_resource_config_snapshot(&resource_cfg);

    remote = webrtc_alloc_sdp_session();
    if (remote == NULL) {
        return webrtc_set_error(answer, 500, "sdp alloc failed");
    }

    if (mi_sdp_parse(offer, offer_len, remote) != 0) {
        webrtc_free_sdp_session(remote);
        return webrtc_set_error(answer, 400, "sdp parse failed");
    }
    printf("MiIpc WebRTC offer stage: sdp parsed media=%d session_candidates=%d\n",
           remote->media_count,
           remote->candidate_count);
    fflush(stdout);

    track_count = webrtc_build_tracks(remote, call_tracks, MI_WEBRTC_MAX_TRACKS, answer_tracks, &resource_cfg);
    if (track_count <= 0) {
        webrtc_free_sdp_session(remote);
        return webrtc_set_error(answer, 415, "no supported media found");
    }
    printf("MiIpc WebRTC offer stage: tracks built count=%d\n", track_count);
    webrtc_log_tracks("remote-offer", answer_tracks, track_count);
    fflush(stdout);

    if (webrtc_init_certificate() != 0) {
        webrtc_free_sdp_session(remote);
        return webrtc_set_error(answer, 500, "dtls certificate init failed");
    }
    printf("MiIpc WebRTC offer stage: certificate ready\n");
    fflush(stdout);

    session = (mi_webrtc_session_t*)calloc(1, sizeof(*session));
    if (session == NULL) {
        webrtc_free_sdp_session(remote);
        return webrtc_set_error(answer, 500, "session alloc failed");
    }
    session->call_lock = mi_CreateMutex(1);
    if (session->call_lock == NULL) {
        free(session);
        webrtc_free_sdp_session(remote);
        return webrtc_set_error(answer, 500, "session lock create failed");
    }
    webrtc_lock();
    now_ms = webrtc_now_ms();
    webrtc_prune_sessions_locked(now_ms);
    if (webrtc_session_count_locked() >= MI_WEBRTC_MAX_SESSIONS) {
        webrtc_unlock();
        webrtc_session_destroy(session);
        webrtc_free_sdp_session(remote);
        return webrtc_set_error(answer, 503, "too many webrtc sessions");
    }
    session->session_id = webrtc_new_session_id();
    session->created_ms = now_ms;
    session->last_active_ms = now_ms;
    session->source_chn = source_chn;
    webrtc_unlock();
    printf("MiIpc WebRTC offer stage: session allocated id=%u source_chn=%d\n",
           (unsigned int)session->session_id,
           session->source_chn);
    fflush(stdout);

    memcpy(session->tracks, answer_tracks, (size_t)track_count * sizeof(answer_tracks[0]));
    session->track_count = track_count;
    for (elapsed = 0; elapsed < track_count; ++elapsed) {
        if (session->tracks[elapsed].kind == MI_CALL_TRACK_KIND_VIDEO && session->video_track_id == 0) {
            session->video_track_id = session->tracks[elapsed].track_id;
        } else if (session->tracks[elapsed].kind == MI_CALL_TRACK_KIND_AUDIO && session->audio_track_id == 0) {
            session->audio_track_id = session->tracks[elapsed].track_id;
        }
    }

    memset(&cfg, 0, sizeof(cfg));
    memset(&cfg.transport_cfg, 0, sizeof(cfg.transport_cfg));
    cfg.transport_cfg.dtls_role = MI_DTLS_ROLE_SERVER;
    cfg.transport_cfg.dtls_verify_mode = MI_DTLS_VERIFY_OPTIONAL;
    cfg.transport_cfg.dtls_mtu = 1200;
    cfg.transport_cfg.cert_pem = g_cert_pem;
    cfg.transport_cfg.key_pem = g_key_pem;
    cfg.transport_cfg.ice_cfg.controlling = 0;
    cfg.transport_cfg.ice_cfg.port_min = 50000;
    cfg.transport_cfg.ice_cfg.port_max = 50100;
    cfg.callback.user_data = session;
    cfg.callback.on_state = webrtc_on_state;
    cfg.callback.on_ice_state = webrtc_on_ice_state;
    cfg.callback.on_error = webrtc_on_error;
    cfg.callback.on_local_candidate = webrtc_on_local_candidate;
    cfg.callback.on_selected_pair = webrtc_on_selected_pair;
    cfg.callback.on_target_bitrate = webrtc_on_target_bitrate;
    cfg.callback.on_keyframe_request = webrtc_on_keyframe_request;
    cfg.callback.on_track_mirtp_packet = webrtc_on_track_mirtp_packet;
    cfg.callback.on_track_mirtcp_packet = webrtc_on_track_mirtcp_packet;
    cfg.tracks = call_tracks;
    cfg.track_count = track_count;
    cfg.enable_paced = 1;
    cfg.paced_queue_capacity = resource_cfg.paced_queue_capacity;
    cfg.paced_max_packet_size = resource_cfg.paced_max_packet_size;
    cfg.paced_bitrate_bps = 0;
    cfg.paced_burst_ms = MI_WEBRTC_FAST_START_BURST_MS;
    cfg.paced_max_packets_per_process = 32;
    cfg.paced_max_bytes_per_process = 48000;
    cfg.paced_queue_time_limit_ms = MI_WEBRTC_PACED_QUEUE_LIMIT_MS;
    cfg.loss_cfg.enabled = MI_WEBRTC_ENABLE_SIM_LOSS ? 1 : 0;
    cfg.loss_cfg.direction = MI_CALL_PACKET_OUTGOING;
    cfg.loss_cfg.packet_kind = MI_LOSS_PACKET_RTP;
    cfg.loss_cfg.random_loss_rate_q16 = MI_WEBRTC_ENABLE_SIM_LOSS ? MI_WEBRTC_SIM_LOSS_RATE_Q16 : 0;
    cfg.loss_cfg.seed = session->session_id ^ webrtc_now_ms();

    {
        MiCall call_holder;
        memset(&call_holder, 0, sizeof(call_holder));
        ret = InitCall(&call_holder, &cfg);
        if (ret == 0) {
            session->call = (MiCall*)call_holder.impl;
        }
    }
    if (session->call == NULL) {
        char message[96];
        snprintf(message, sizeof(message), "call create failed: %d", ret);
        webrtc_session_destroy(session);
        webrtc_free_sdp_session(remote);
        return webrtc_set_error(answer, 500, message);
    }
    printf("MiIpc WebRTC offer stage: call ready\n");
    fflush(stdout);

    ret = mi_sdp_apply_remote_to_call(session->call, remote);
    if (ret != 0) {
        webrtc_free_sdp_session(remote);
        webrtc_session_destroy(session);
        return webrtc_set_error(answer, 400, "apply remote sdp failed");
    }
    printf("MiIpc WebRTC offer stage: remote sdp applied\n");
    fflush(stdout);

    if (session->call->StartGathering == NULL || session->call->StartGathering(session->call) != 0) {
        webrtc_free_sdp_session(remote);
        webrtc_session_destroy(session);
        return webrtc_set_error(answer, 500, "ice gathering failed");
    }
    printf("MiIpc WebRTC offer stage: ice gathering started\n");
    fflush(stdout);

    for (elapsed = 0; elapsed < MI_WEBRTC_GATHER_WAIT_MS; elapsed += 20) {
        if (session->call->Process != NULL) {
            session->call->Process(session->call);
        }
        mi_thread_sleep(20);
    }
    printf("MiIpc WebRTC offer stage: ice gather wait done\n");
    fflush(stdout);

    sdp_answer = (char*)mi_mem_malloc_large(MI_WEBRTC_ANSWER_MAX);
    if (sdp_answer == NULL) {
        webrtc_free_sdp_session(remote);
        webrtc_session_destroy(session);
        return webrtc_set_error(answer, 500, "answer alloc failed");
    }
    sdp_answer[0] = '\0';
    {
        MiSdpLocalTrack sdp_tracks[MI_WEBRTC_MAX_TRACKS];
        int sdp_track_count = 0;
        webrtc_build_sdp_tracks(session, sdp_tracks, MI_WEBRTC_MAX_TRACKS, &sdp_track_count);
        if (mi_sdp_build_answer_from_call(session->call,
            remote,
            sdp_tracks,
            sdp_track_count,
            sdp_answer,
            MI_WEBRTC_ANSWER_MAX) != 0) {
            mi_mem_free_large(sdp_answer);
            webrtc_free_sdp_session(remote);
            webrtc_session_destroy(session);
            return webrtc_set_error(answer, 500, "answer sdp build failed");
        }
    }
    printf("MiIpc WebRTC offer stage: answer sdp built bytes=%u\n",
           (unsigned int)strlen(sdp_answer));
    fflush(stdout);

    if (webrtc_remote_candidate_count(remote) > 0 && session->call->StartChecks != NULL &&
        session->call->StartChecks(session->call) == 0) {
        session->ice_checks_started = 1;
    }
    printf("MiIpc WebRTC offer stage: ice checks_started=%d remote_candidates=%d\n",
           session->ice_checks_started,
           webrtc_remote_candidate_count(remote));
    fflush(stdout);
    webrtc_lock();
    session->last_active_ms = webrtc_now_ms();
    webrtc_add_session_locked(session);
    request_idr = webrtc_mark_session_idr_locked(session, session->last_active_ms);
    request_chn = session->source_chn;
    webrtc_unlock();
    if (request_idr) {
        webrtc_request_idr(request_chn);
    }
    printf("MiIpc WebRTC offer stage: session added\n");
    fflush(stdout);

    if (webrtc_start_session_thread(session) != 0) {
        mi_mem_free_large(sdp_answer);
        webrtc_free_sdp_session(remote);
        webrtc_remove_session(session);
        return webrtc_set_error(answer, 500, "session thread start failed");
    }
    printf("MiIpc WebRTC offer stage: session thread started\n");
    fflush(stdout);

    ret = webrtc_answer_json(session->session_id, sdp_answer, answer);
    mi_mem_free_large(sdp_answer);
    webrtc_free_sdp_session(remote);
    if (ret != 0) {
        webrtc_remove_session(session);
        return webrtc_set_error(answer, 500, "answer json build failed");
    }
    printf("MiIpc WebRTC offer stage: answer json ready session=%u bytes=%d\n",
           (unsigned int)session->session_id,
           answer->body_len);
    fflush(stdout);
    return 0;
}

int mi_webrtc_handle_offer(const char* offer, int offer_len, mi_webrtc_answer_t* answer)
{
    return mi_webrtc_handle_offer_for_channel(offer, offer_len, answer, MI_WEBRTC_DEFAULT_SOURCE_CHN);
}

int mi_webrtc_apply_sdp_answer(uint32_t session_id, const char* answer, int answer_len)
{
    MiSdpSession* remote = NULL;
    mi_webrtc_session_t* session;
    int remote_candidates;
    int ret;

    if (session_id == 0 || answer == NULL || answer_len <= 0) {
        return -1;
    }

    remote = webrtc_alloc_sdp_session();
    if (remote == NULL) {
        return -2;
    }
    if (mi_sdp_parse(answer, answer_len, remote) != 0) {
        webrtc_free_sdp_session(remote);
        return -3;
    }
    remote_candidates = webrtc_remote_candidate_count(remote);

    webrtc_lock();
    session = webrtc_find_session_locked(session_id);
    if (session != NULL && session->call != NULL && webrtc_session_add_ref_locked(session)) {
        webrtc_touch_session(session);
    } else {
        session = NULL;
    }
    webrtc_unlock();

    if (session == NULL) {
        webrtc_free_sdp_session(remote);
        return -4;
    }

    webrtc_session_call_lock(session);
    ret = mi_sdp_apply_remote_to_call(session->call, remote);
    if (ret == 0 && session->call->StartChecks != NULL) {
        int need_checks = 0;

        webrtc_lock();
        if (!session->removed) {
            session->remote_sdp_applied = 1;
            if (webrtc_ice_checks_active(session->ice_state)) {
                session->ice_checks_started = 1;
            }
            need_checks = !session->ice_checks_started &&
                (remote_candidates > 0 || session->remote_candidate_count > 0);
        }
        webrtc_unlock();

        if (need_checks && session->call->StartChecks(session->call) == 0) {
            webrtc_lock();
            if (!session->removed) {
                session->ice_checks_started = 1;
            }
            webrtc_unlock();
        }
    }
    webrtc_session_call_unlock(session);

    printf("MiIpc WebRTC apply answer DialogueId=%u ret=%d remote_candidates=%d checks=%d\n",
        (unsigned int)session_id,
        ret,
        remote_candidates,
        session->ice_checks_started);
    fflush(stdout);
    webrtc_free_sdp_session(remote);
    webrtc_session_release(session);
    return ret;
}

int mi_webrtc_get_local_candidates(uint32_t session_id,
    char candidates[][MI_WEBRTC_CANDIDATE_TEXT_MAX],
    int max_count,
    int* out_count,
    int* ice_status)
{
    mi_webrtc_session_t* session;
    MiRtcTransport* transport;
    int candidate_count;
    int copied = 0;
    int has_relay = 0;
    int i;
    uint32_t now_ms;

    if (out_count != NULL) {
        *out_count = 0;
    }
    if (ice_status != NULL) {
        *ice_status = MI_WEBRTC_TURN_STATUS_INIT;
    }
    if (session_id == 0 || candidates == NULL || max_count <= 0 || out_count == NULL || ice_status == NULL) {
        return -1;
    }
    if (max_count > MI_WEBRTC_CANDIDATE_LIST_MAX) {
        max_count = MI_WEBRTC_CANDIDATE_LIST_MAX;
    }

    now_ms = webrtc_now_ms();
    webrtc_lock();
    session = webrtc_find_session_locked(session_id);
    if (session != NULL && session->call != NULL && webrtc_session_add_ref_locked(session)) {
        *ice_status = webrtc_session_turn_status(session, now_ms, 0);
        webrtc_touch_session(session);
    } else {
        *ice_status = MI_WEBRTC_TURN_STATUS_TIMEOUT;
        session = NULL;
    }
    webrtc_unlock();

    if (session == NULL) {
        return -2;
    }

    webrtc_session_call_lock(session);
    transport = session->call != NULL && session->call->GetTransport != NULL ?
        session->call->GetTransport(session->call) :
        NULL;
    if (transport == NULL ||
        transport->GetLocalCandidateCount == NULL ||
        transport->GetLocalCandidate == NULL) {
        webrtc_session_call_unlock(session);
        webrtc_session_release(session);
        return -3;
    }

    candidate_count = transport->GetLocalCandidateCount(transport);
    for (i = 0; i < candidate_count; ++i) {
        ice_candi_t candidate;
        int offset = 0;

        memset(&candidate, 0, sizeof(candidate));
        if (transport->GetLocalCandidate(transport, i, &candidate) != 0) {
            continue;
        }
        if (candidate.canditype == ICE_CANDIDATE_TYPE_PEER_REFLEXIVE) continue;
        if (candidate.canditype == ICE_CANDIDATE_TYPE_RELAY) {
            has_relay = 1;
        }
        if (copied >= max_count) {
            continue;
        }
        candidates[copied][0] = '\0';
        if (webrtc_candidate_to_text(candidates[copied],
            MI_WEBRTC_CANDIDATE_TEXT_MAX,
            &offset,
            &candidate) != 0) {
            candidates[copied][0] = '\0';
            continue;
        }
        while (offset > 0 &&
            (candidates[copied][offset - 1] == '\r' || candidates[copied][offset - 1] == '\n')) {
            candidates[copied][--offset] = '\0';
        }
        ++copied;
    }
    webrtc_session_call_unlock(session);

    webrtc_lock();
    *ice_status = webrtc_session_turn_status(session, now_ms, has_relay);
    webrtc_unlock();
    *out_count = copied;
    printf("MiIpc WebRTC get candidates DialogueId=%u count=%d turn_status=%d\n",
        (unsigned int)session_id,
        copied,
        *ice_status);
    fflush(stdout);
    webrtc_session_release(session);
    return copied > 0 ? 0 : -4;
}

int mi_webrtc_candidates_json(uint32_t session_id, char* out, int out_size)
{
    mi_webrtc_session_t* session;
    MiRtcTransport* transport;
    mi_ice_agent_t* ice;
    mi_ice_candidate_snapshot_t snapshot;
    int count, i, copied = 0, offset = 0, ret = -1, complete;
    if (session_id == 0 || out == NULL || out_size <= 0) return -1;
    out[0] = '\0';
    webrtc_lock();
    session = webrtc_find_session_locked(session_id);
    if (session == NULL || session->call == NULL || !webrtc_session_add_ref_locked(session)) session = NULL;
    else webrtc_touch_session(session);
    webrtc_unlock();
    if (session == NULL) return -2;
    webrtc_session_call_lock(session);
    transport = session->call->GetTransport != NULL ? session->call->GetTransport(session->call) : NULL;
    if (transport == NULL || transport->GetLocalCandidate == NULL ||
        transport->GetLocalCandidateCount == NULL || transport->GetIceAgent == NULL) goto done;
    ice = transport->GetIceAgent(transport);
    if (mi_ice_agent_get_candidate_snapshot(ice, &snapshot) != 0) goto done;
    complete = snapshot.gathering_state == MI_ICE_GATHERING_COMPLETE;
    count = snapshot.count;
    if (webrtc_append(out, out_size, &offset,
        "{\"complete\":%s,\"continual\":%s,\"restartRequired\":%s,\"revision\":%u,\"candidates\":[",
        complete ? "true" : "false", snapshot.continual ? "true" : "false",
        snapshot.restart_required ? "true" : "false", snapshot.revision) != 0) goto done;
    for (i = 0; i < count; ++i) {
        ice_candi_t candidate;
        char text[MI_WEBRTC_CANDIDATE_TEXT_MAX];
        int length = 0;
        candidate = snapshot.candidates[i];
        if (candidate.canditype == ICE_CANDIDATE_TYPE_PEER_REFLEXIVE) continue;
        if (webrtc_candidate_to_text(text, sizeof(text), &length, &candidate) != 0) goto done;
        while (length > 0 && (text[length - 1] == '\r' || text[length - 1] == '\n')) text[--length] = '\0';
        if (length * 6 + 8 > out_size - offset) goto done;
        if (webrtc_append(out, out_size, &offset, "%s\"", copied ? "," : "") != 0) goto done;
        webrtc_json_escape_append(out, out_size, &offset, text);
        if (webrtc_append(out, out_size, &offset, "\"") != 0) goto done;
        ++copied;
    }
    if (webrtc_append(out, out_size, &offset, "]}\n") == 0) ret = offset;
done:
    webrtc_session_call_unlock(session);
    webrtc_session_release(session);
    return ret;
}

int mi_webrtc_add_candidate(uint32_t session_id, const char* candidate, int candidate_len)
{
    mi_webrtc_session_t* session;
    ice_candi_t parsed;
    int ret = -1;
    int udp_candidate;
    int mdns_candidate = 0;

    if (candidate != NULL && candidate_len == 0) {
        MiRtcTransport* transport;
        webrtc_lock();
        session = webrtc_find_session_locked(session_id);
        if (session == NULL || session->call == NULL || !webrtc_session_add_ref_locked(session)) session = NULL;
        else webrtc_touch_session(session);
        webrtc_unlock();
        if (session == NULL) return -1;
        webrtc_session_call_lock(session);
        transport = session->call->GetTransport != NULL ? session->call->GetTransport(session->call) : NULL;
        ret = transport != NULL && transport->GetIceAgent != NULL ?
            mi_ice_agent_set_remote_gathering_complete(transport->GetIceAgent(transport)) : -1;
        webrtc_session_call_unlock(session);
        webrtc_session_release(session);
        return ret;
    }
    if (candidate == NULL || candidate_len <= 0) {
        printf("MiIpc WebRTC add candidate invalid session=%u len=%d\n",
            (unsigned int)session_id,
            candidate_len);
        fflush(stdout);
        return -1;
    }

    memset(&parsed, 0, sizeof(parsed));
    printf("MiIpc WebRTC add candidate begin session=%u len=%d\n",
        (unsigned int)session_id,
        candidate_len);
    fflush(stdout);
    udp_candidate = webrtc_candidate_text_is_udp(candidate, candidate_len);
    printf("MiIpc WebRTC add candidate transport session=%u udp=%d\n",
        (unsigned int)session_id,
        udp_candidate);
    fflush(stdout);
    if (udp_candidate == 0) {
        printf("MiIpc WebRTC add candidate skip non-udp session=%u len=%d\n",
            (unsigned int)session_id,
            candidate_len);
        fflush(stdout);
        return 0;
    }
    if (udp_candidate < 0) {
        printf("MiIpc WebRTC add candidate bad transport session=%u len=%d\n",
            (unsigned int)session_id,
            candidate_len);
        fflush(stdout);
        return -2;
    }
    if (webrtc_candidate_from_text(candidate, candidate_len, &parsed) != 0) {
        printf("MiIpc WebRTC add candidate parse failed session=%u len=%d\n",
            (unsigned int)session_id,
            candidate_len);
        fflush(stdout);
        return -2;
    }
    printf("MiIpc WebRTC add candidate parsed session=%u %s:%u type=%d\n",
        (unsigned int)session_id,
        parsed.address,
        (unsigned int)parsed.port,
        parsed.canditype);
    fflush(stdout);
    if (parsed.transtype != ICE_TRANSPORT_UDP) {
        printf("MiIpc WebRTC add candidate skip transport=%d session=%u\n",
            parsed.transtype,
            (unsigned int)session_id);
        fflush(stdout);
        return 0;
    }
    mdns_candidate = webrtc_ends_with_ci(parsed.address, ".local") ||
        webrtc_ends_with_ci(parsed.address, ".local.");

    webrtc_lock();
    session = webrtc_find_session_locked(session_id);
    if (session != NULL && session->call != NULL &&
        session->call->AddRemoteCandidate != NULL &&
        webrtc_session_add_ref_locked(session)) {
        webrtc_touch_session(session);
    } else {
        session = NULL;
    }
    webrtc_unlock();

    if (session == NULL) {
        printf("MiIpc WebRTC add candidate session not found session=%u\n",
            (unsigned int)session_id);
        fflush(stdout);
        return -1;
    }

    webrtc_session_call_lock(session);
    printf("MiIpc WebRTC add candidate call-add session=%u\n",
        (unsigned int)session_id);
    fflush(stdout);
    ret = session->call->AddRemoteCandidate(session->call, &parsed);
    printf("MiIpc WebRTC add candidate call-add-done session=%u ret=%d\n",
        (unsigned int)session_id,
        ret);
    fflush(stdout);
    if (ret == 0 && session->call->StartChecks != NULL) {
        int need_checks = 0;
        int checks_ret = -1;
        int ice_state_snapshot = MI_ICE_STATE_NEW;
        int checks_started_snapshot = 0;
        int remote_sdp_applied_snapshot = 0;

        webrtc_lock();
        if (!session->removed) {
            ++session->remote_candidate_count;
            remote_sdp_applied_snapshot = session->remote_sdp_applied;
            ice_state_snapshot = session->ice_state;
            if (webrtc_ice_checks_active(ice_state_snapshot)) {
                session->ice_checks_started = 1;
            }
            checks_started_snapshot = session->ice_checks_started;
            need_checks = session->remote_sdp_applied && !session->ice_checks_started;
        }
        webrtc_unlock();
        if (need_checks) {
            printf("MiIpc WebRTC add candidate start-checks session=%u\n",
                (unsigned int)session_id);
            fflush(stdout);
            checks_ret = session->call->StartChecks(session->call);
            printf("MiIpc WebRTC add candidate start-checks-done session=%u ret=%d\n",
                (unsigned int)session_id,
                checks_ret);
            fflush(stdout);
            if (checks_ret == 0) {
                webrtc_lock();
                if (!session->removed) {
                    session->ice_checks_started = 1;
                }
                webrtc_unlock();
            }
        } else {
            printf("MiIpc WebRTC add candidate skip start-checks session=%u ice=%d checks=%d remote_sdp=%d\n",
                (unsigned int)session_id,
                ice_state_snapshot,
                checks_started_snapshot,
                remote_sdp_applied_snapshot);
            fflush(stdout);
        }
    }
    webrtc_session_call_unlock(session);

    webrtc_lock();
    printf("MiIpc WebRTC add candidate session=%u %s:%u ret=%d checks=%d\n",
        (unsigned int)session_id,
        parsed.address,
        (unsigned int)parsed.port,
        ret,
        session->ice_checks_started);
    fflush(stdout);
    webrtc_unlock();
    if (ret != 0 && mdns_candidate) {
        printf("MiIpc WebRTC add candidate ignore mdns ret=%d session=%u address=%s\n",
            ret,
            (unsigned int)session_id,
            parsed.address);
        fflush(stdout);
        ret = -2;
    }
    webrtc_session_release(session);
    return ret;
}

int mi_webrtc_close_session(uint32_t session_id)
{
    mi_webrtc_session_t* session = NULL;

    if (session_id == 0) {
        return -1;
    }

    webrtc_lock();
    for (session = g_sessions; session != NULL; session = session->next) {
        if (session->session_id == session_id) {
            session->close_requested = 1;
            session->running = 0;
            session->last_active_ms = 1;
            printf("MiIpc WebRTC session %u close requested\n",
                (unsigned int)session->session_id);
            fflush(stdout);
            break;
        }
    }
    webrtc_unlock();
    return 0;
}

void mi_webrtc_answer_free(mi_webrtc_answer_t* answer)
{
    if (answer == NULL) {
        return;
    }
    mi_mem_free_large(answer->body);
    memset(answer, 0, sizeof(*answer));
}

static void webrtc_send_frame_to_session(mi_webrtc_session_t* session,
    int track_id,
    const struct Stream_Info* frame)
{
    MiRtcTransport* transport = NULL;
    uint32_t timestamp;
    uint32_t now_ms;
    int is_video_frame;
    int is_g711_frame;
    const char* codec_name;
    int srtp_ready = 1;
    int do_send = 1;
    int ret = -1;

    if (session == NULL || frame == NULL || frame->buffer == NULL || frame->buflen <= 0 || track_id <= 0) {
        return;
    }

    is_video_frame = frame->enType == CI_H264 || frame->enType == CI_H265;
    is_g711_frame = frame->enType == CI_G711;
    codec_name = frame->enType == CI_H265 ? "H265" : "H264";
    timestamp = is_g711_frame ?
        (uint32_t)((frame->pts * 8ULL) & 0xffffffffu) :
        (uint32_t)(frame->pts & 0xffffffffu);

    webrtc_session_call_lock(session);
    if (session->call == NULL ||
        (session->call->SendTrackEncoded == NULL &&
            session->call->SendTrackVideo == NULL)) {
        webrtc_session_call_unlock(session);
        return;
    }

    if (is_video_frame) {
        int request_idr = 0;
        int request_chn = MI_WEBRTC_DEFAULT_SOURCE_CHN;

        if (session->call->GetTransport != NULL) {
            transport = session->call->GetTransport(session->call);
        }
        if (transport != NULL && transport->IsSrtpReady != NULL) {
            srtp_ready = transport->IsSrtpReady(transport);
        } else {
            srtp_ready = 0;
        }

        now_ms = webrtc_now_ms();
        webrtc_lock();
        if (session->removed || session->close_requested) {
            do_send = 0;
        } else {
            if (session->last_h264_input_log_ms == 0 ||
                (uint32_t)(now_ms - session->last_h264_input_log_ms) >= 1000u) {
                session->last_h264_input_log_ms = now_ms;
                printf("MiIpc WebRTC video input session=%u codec=%s bytes=%d key=%d srtp=%d ready=%d recv=%u wait_srtp=%u wait_key=%u send=%u ok=%u fail=%u last_ret=%d pts=%u\n",
                    (unsigned int)session->session_id,
                    codec_name,
                    frame->buflen,
                    frame->Ikey,
                    srtp_ready,
                    session->video_keyframe_ready,
                    (unsigned int)session->h264_recv_frames,
                    (unsigned int)session->h264_wait_srtp_frames,
                    (unsigned int)session->h264_wait_keyframe_frames,
                    (unsigned int)session->h264_in_frames,
                    (unsigned int)session->h264_send_ok,
                    (unsigned int)session->h264_send_fail,
                    session->last_h264_send_ret,
                    (unsigned int)(frame->pts & 0xffffffffu));
                fflush(stdout);
            }
            if (!srtp_ready) {
                session->video_keyframe_ready = 0;
                ++session->h264_wait_srtp_frames;
                do_send = 0;
            } else if (!session->video_keyframe_ready) {
                if (!frame->Ikey) {
                    ++session->h264_wait_keyframe_frames;
                    request_idr = webrtc_mark_session_idr_locked(session, 0);
                    request_chn = session->source_chn;
                    do_send = 0;
                } else {
                    session->video_keyframe_ready = 1;
                }
            }
            if (do_send) {
                ++session->h264_in_frames;
                session->h264_in_bytes += (uint32_t)frame->buflen;
                session->last_h264_pts = frame->pts;
            }
        }
        webrtc_unlock();
        if (request_idr) {
            webrtc_request_idr(request_chn);
        }
    } else if (is_g711_frame) {
        now_ms = webrtc_now_ms();
        webrtc_lock();
        if (session->removed || session->close_requested) {
            do_send = 0;
        } else {
            if (session->last_g711_input_log_ms == 0 ||
                (uint32_t)(now_ms - session->last_g711_input_log_ms) >= 1000u) {
                session->last_g711_input_log_ms = now_ms;
                printf("MiIpc WebRTC G711 input session=%u bytes=%d recv=%u send=%u ok=%u fail=%u last_ret=%d pts_ms=%u rtp_ts=%u\n",
                    (unsigned int)session->session_id,
                    frame->buflen,
                    (unsigned int)session->g711_recv_frames,
                    (unsigned int)session->g711_in_frames,
                    (unsigned int)session->g711_send_ok,
                    (unsigned int)session->g711_send_fail,
                    session->last_g711_send_ret,
                    (unsigned int)(frame->pts & 0xffffffffu),
                    (unsigned int)timestamp);
                fflush(stdout);
            }
            if (do_send) {
                ++session->g711_in_frames;
                session->g711_in_bytes += (uint32_t)frame->buflen;
                session->last_g711_pts = (uint32_t)(frame->pts & 0xffffffffu);
            }
        }
        webrtc_unlock();
    } else {
        webrtc_lock();
        if (session->removed || session->close_requested) {
            do_send = 0;
        }
        webrtc_unlock();
    }

    if (do_send) {
        if (is_video_frame && session->call->SendTrackVideo != NULL) {
            ret = session->call->SendTrackVideo(session->call,
                track_id,
                frame->buffer,
                frame->buflen,
                timestamp,
                frame->Ikey != 0);
        } else if (session->call->SendTrackEncoded != NULL) {
            ret = session->call->SendTrackEncoded(session->call,
                track_id,
                frame->buffer,
                frame->buflen,
                timestamp);
        }
    }
    webrtc_session_call_unlock(session);

    if (is_video_frame && do_send) {
        webrtc_lock();
        session->last_h264_send_ret = ret;
        if (ret == 0) {
            ++session->h264_send_ok;
        } else {
            ++session->h264_send_fail;
        }
        webrtc_unlock();
    }
    if (is_g711_frame && do_send) {
        webrtc_lock();
        session->last_g711_send_ret = ret;
        if (ret == 0) {
            ++session->g711_send_ok;
        } else {
            ++session->g711_send_fail;
        }
        webrtc_unlock();
    }
}

void mi_webrtc_process(void)
{
    webrtc_lock();
    webrtc_prune_sessions_locked(webrtc_now_ms());
    webrtc_unlock();
}

void mi_webrtc_input_av_stream(int chn, struct Stream_Info* info, int count)
{
    mi_webrtc_session_t* session;
    mi_webrtc_send_target_t targets[MI_WEBRTC_MAX_SESSIONS];
    int i;

    if (info == NULL || count <= 0) {
        return;
    }
    memset(targets, 0, sizeof(targets));

    for (i = 0; i < count; ++i) {
        struct Stream_Info* frame = &info[i];
        int target_count = 0;
        int target_index;

        if (frame->buffer == NULL || frame->buflen <= 0) {
            continue;
        }

        webrtc_lock();
        webrtc_prune_sessions_locked(webrtc_now_ms());
        for (session = g_sessions; session != NULL; session = session->next) {
            const mi_webrtc_track_t* track = NULL;
            int track_id = MI_WEBRTC_INVALID_TRACK_ID;

            if (session->call == NULL) {
                continue;
            }
            if (frame->enType == CI_H264 || frame->enType == CI_H265) {
                if (session->source_chn != chn) {
                    continue;
                }
                track_id = session->video_track_id;
            } else if (frame->enType == CI_OPUS || frame->enType == CI_AAC ||
                       frame->enType == CI_G711 || frame->enType == CI_PCM) {
                track_id = session->audio_track_id;
            }
            if (track_id <= 0) {
                continue;
            }
            track = webrtc_track_for_id(session, track_id);
            if (!webrtc_track_accepts_frame(track, frame)) {
                continue;
            }
            if (frame->enType == CI_H264 || frame->enType == CI_H265) {
                ++session->h264_recv_frames;
            } else if (frame->enType == CI_G711) {
                ++session->g711_recv_frames;
            }
            if (target_count < MI_WEBRTC_MAX_SESSIONS &&
                webrtc_session_add_ref_locked(session)) {
                targets[target_count].session = session;
                targets[target_count].track_id = track_id;
                ++target_count;
            }
        }
        webrtc_unlock();

        for (target_index = 0; target_index < target_count; ++target_index) {
            webrtc_send_frame_to_session(targets[target_index].session,
                targets[target_index].track_id,
                frame);
            webrtc_session_release(targets[target_index].session);
        }
    }
}

int mi_webrtc_stats_json(uint32_t session_id, char* out, int out_size)
{
    mi_webrtc_session_t snapshot;
    int has_session = 0;
    int session_count = 0;
    int srtp_ready = 0;
    MiPacedStats paced_stats;
    MiTwccStats twcc_stats;
    MiLossStats loss_stats;
    mi_ice_debug_stats_t ice_stats;
    mi_webrtc_resource_cfg_t resource_cfg;
    miipc_audio_capture_stats_t pc_audio_stats;
    int pc_audio_stats_ready = 0;
    MiRtcTransport* transport = NULL;
    mi_webrtc_session_t* session_ref = NULL;

    if (out == NULL || out_size <= 0) {
        return -1;
    }

    memset(&snapshot, 0, sizeof(snapshot));
    memset(&paced_stats, 0, sizeof(paced_stats));
    memset(&twcc_stats, 0, sizeof(twcc_stats));
    memset(&loss_stats, 0, sizeof(loss_stats));
    memset(&ice_stats, 0, sizeof(ice_stats));
    memset(&pc_audio_stats, 0, sizeof(pc_audio_stats));
    webrtc_resource_config_snapshot(&resource_cfg);
#if defined(_WIN32)
    if (Win32AudioGetCaptureStats(&pc_audio_stats) == 0) {
        pc_audio_stats_ready = 1;
    }
#endif

    webrtc_lock();
    webrtc_prune_sessions_locked(webrtc_now_ms());
    session_count = webrtc_session_count_locked();
    {
        mi_webrtc_session_t* session = webrtc_find_session_locked(session_id);
        if (session_id == 0) {
            session = g_sessions;
        }
        if (session != NULL && webrtc_session_add_ref_locked(session)) {
            session_ref = session;
            snapshot = *session;
            has_session = 1;
        }
    }
    webrtc_unlock();

    if (session_ref != NULL) {
        webrtc_session_call_lock(session_ref);
        if (session_ref->call != NULL) {
            if (session_ref->call->GetTransport != NULL) {
                transport = session_ref->call->GetTransport(session_ref->call);
                if (transport != NULL && transport->IsSrtpReady != NULL) {
                    srtp_ready = transport->IsSrtpReady(transport);
                }
                if (transport != NULL && transport->GetIceAgent != NULL) {
                    mi_ice_agent_t* ice = transport->GetIceAgent(transport);
                    if (ice != NULL) {
                        mi_ice_agent_get_debug_stats(ice, &ice_stats);
                    }
                }
            }
            if (session_ref->call->GetPacedStats != NULL) {
                session_ref->call->GetPacedStats(session_ref->call, &paced_stats);
            }
            if (session_ref->call->GetTwccStats != NULL) {
                session_ref->call->GetTwccStats(session_ref->call, &twcc_stats);
            }
            if (session_ref->call->GetLossStats != NULL) {
                session_ref->call->GetLossStats(session_ref->call, &loss_stats);
            }
        }
        webrtc_session_call_unlock(session_ref);

        webrtc_lock();
        session_count = webrtc_session_count_locked();
        if (!session_ref->removed) {
            snapshot = *session_ref;
            has_session = 1;
        } else {
            memset(&snapshot, 0, sizeof(snapshot));
            has_session = 0;
        }
        webrtc_unlock();
        webrtc_session_release(session_ref);
    }

    return snprintf(out,
        (size_t)out_size,
        "{"
        "\"has_session\":%d,"
        "\"max_sessions\":%u,"
        "\"session_count\":%d,"
        "\"session_id\":%u,"
        "\"source_chn\":%d,"
        "\"ice_state\":%d,"
        "\"rtc_state\":%d,"
        "\"srtp_ready\":%d,"
        "\"last_error\":%d,"
        "\"track_count\":%d,"
        "\"video_track_id\":%d,"
        "\"audio_track_id\":%d,"
        "\"local_candidates\":%u,"
        "\"selected_pairs\":%u,"
        "\"video_keyframe_ready\":%d,"
        "\"idr_requested\":%d,"
        "\"idr_request_count\":%u,"
        "\"pli_request_count\":%u,"
        "\"fir_request_count\":%u,"
        "\"h264_recv_frames\":%u,"
        "\"h264_wait_srtp_frames\":%u,"
        "\"h264_wait_keyframe_frames\":%u,"
        "\"h264_in_frames\":%u,"
        "\"h264_in_bytes\":%u,"
        "\"h264_send_ok\":%u,"
        "\"h264_send_fail\":%u,"
        "\"last_h264_send_ret\":%d,"
        "\"last_h264_pts\":%u,"
        "\"g711_recv_frames\":%u,"
        "\"g711_in_frames\":%u,"
        "\"g711_in_bytes\":%u,"
        "\"g711_send_ok\":%u,"
        "\"g711_send_fail\":%u,"
        "\"last_g711_send_ret\":%d,"
        "\"last_g711_pts\":%u,"
        "\"pc_audio_stats_ready\":%d,"
        "\"pc_audio_active\":%d,"
        "\"pc_audio_rate\":%d,"
        "\"pc_audio_channels\":%d,"
        "\"pc_audio_bits\":%d,"
        "\"pc_audio_packets\":%llu,"
        "\"pc_audio_input_level\":%d,"
        "\"pc_audio_output_level\":%d,"
        "\"pc_audio_suppression_q8\":%d,"
        "\"pc_aec_config_enabled\":%d,"
        "\"pc_aec_active\":%d,"
        "\"pc_aec_processed_packets\":%llu,"
        "\"pc_aec_bypass_packets\":%llu,"
        "\"pc_aec_process_errors\":%llu,"
        "\"pc_aec_error_resets\":%llu,"
        "\"pc_aec_last_process_ret\":%d,"
        "\"pc_aec_farend_calls\":%llu,"
        "\"pc_aec_farend_input_samples\":%llu,"
        "\"pc_aec_farend_samples\":%llu,"
        "\"pc_aec_farend_pending_samples\":%llu,"
        "\"pc_aec_farend_errors\":%llu,"
        "\"pc_aec_processed_samples\":%llu,"
        "\"pc_aec_bypass_samples\":%llu,"
        "\"pc_aec_last_error\":%d,"
        "\"pc_aec_delay_ms\":%d,"
        "\"pc_aec_echo_mode\":%d,"
        "\"pc_aec_cng_mode\":%d,"
        "\"pc_ns_enabled\":%d,"
        "\"pc_ns_policy\":%d,"
        "\"rtp_out_packets\":%u,"
        "\"rtp_out_bytes\":%u,"
        "\"rtcp_in_packets\":%u,"
        "\"rtcp_in_bytes\":%u,"
        "\"rtcp_out_packets\":%u,"
        "\"rtcp_out_bytes\":%u,"
        "\"target_bitrate_bps\":%u,"
        "\"paced_queued_packets\":%u,"
        "\"paced_sent_packets\":%u,"
        "\"paced_dropped_packets\":%u,"
        "\"paced_queued_bytes_current\":%u,"
        "\"paced_queue_full_count\":%u,"
        "\"paced_queue_delay_ms\":%u,"
        "\"paced_current_queue_delay_ms\":%u,"
        "\"paced_max_queue_delay_ms\":%u,"
        "\"paced_dropped_bytes\":%u,"
        "\"paced_bitrate_bps\":%u,"
        "\"resource_video_nack_history\":%u,"
        "\"resource_video_nack_max_missing\":%u,"
        "\"resource_audio_nack_history\":%u,"
        "\"resource_audio_nack_max_missing\":%u,"
        "\"resource_nack_max_packet_size\":%u,"
        "\"resource_paced_queue_capacity\":%u,"
        "\"resource_paced_max_packet_size\":%u,"
        "\"twcc_sent_packets\":%u,"
        "\"twcc_remote_feedback\":%u,"
        "\"twcc_target_bitrate_bps\":%u,"
        "\"twcc_ack_bitrate_bps\":%u,"
        "\"twcc_smoothed_ack_bitrate_bps\":%u,"
        "\"twcc_loss_fraction_q8\":%u,"
        "\"twcc_smoothed_loss_fraction_q8\":%u,"
        "\"twcc_delay_trend_ms\":%d,"
        "\"twcc_smoothed_delay_trend_ms\":%d,"
        "\"twcc_min_delay_trend_ms\":%d,"
        "\"twcc_max_delay_trend_ms\":%d,"
        "\"twcc_last_feedback_packets\":%u,"
        "\"twcc_last_feedback_received\":%u,"
        "\"twcc_last_feedback_lost\":%u,"
        "\"sim_loss_enabled\":%d,"
        "\"sim_loss_packets\":%u,"
        "\"sim_loss_dropped\":%u,"
        "\"sim_loss_passed\":%u,"
        "\"sim_loss_random_dropped\":%u,"
        "\"sim_loss_burst_dropped\":%u,"
        "\"sim_loss_interval_dropped\":%u,"
        "\"sim_loss_rate_q16\":%u,"
        "\"encoder_target_bitrate_bps\":%u,"
        "\"encoder_set_bitrate_bps\":%u,"
        "\"ice_remote_candidates\":%u,"
        "\"ice_stun_rx\":%u,"
        "\"ice_stun_rx_requests\":%u,"
        "\"ice_stun_rx_success\":%u,"
        "\"ice_stun_rx_error\":%u,"
        "\"ice_stun_rx_valid_requests\":%u,"
        "\"ice_stun_invalid_username\":%u,"
        "\"ice_stun_invalid_fingerprint\":%u,"
        "\"ice_stun_invalid_integrity\":%u,"
        "\"ice_stun_invalid_role\":%u,"
        "\"ice_stun_tx_requests\":%u,"
        "\"ice_stun_tx_success\":%u,"
        "\"ice_stun_tx_error\":%u,"
        "\"ice_peer_reflexive_added\":%u,"
        "\"ice_active_check_failures\":%u,"
        "\"ice_selected_ready\":%u"
        "}\n",
        has_session,
        (unsigned int)MI_WEBRTC_MAX_SESSIONS,
        session_count,
        (unsigned int)snapshot.session_id,
        snapshot.source_chn,
        snapshot.ice_state,
        snapshot.rtc_state,
        srtp_ready,
        snapshot.last_error,
        snapshot.track_count,
        snapshot.video_track_id,
        snapshot.audio_track_id,
        (unsigned int)snapshot.local_candidate_count,
        (unsigned int)snapshot.selected_pair_count,
        snapshot.video_keyframe_ready,
        snapshot.idr_requested,
        (unsigned int)snapshot.idr_request_count,
        (unsigned int)snapshot.pli_request_count,
        (unsigned int)snapshot.fir_request_count,
        (unsigned int)snapshot.h264_recv_frames,
        (unsigned int)snapshot.h264_wait_srtp_frames,
        (unsigned int)snapshot.h264_wait_keyframe_frames,
        (unsigned int)snapshot.h264_in_frames,
        (unsigned int)snapshot.h264_in_bytes,
        (unsigned int)snapshot.h264_send_ok,
        (unsigned int)snapshot.h264_send_fail,
        snapshot.last_h264_send_ret,
        (unsigned int)(snapshot.last_h264_pts & 0xffffffffu),
        (unsigned int)snapshot.g711_recv_frames,
        (unsigned int)snapshot.g711_in_frames,
        (unsigned int)snapshot.g711_in_bytes,
        (unsigned int)snapshot.g711_send_ok,
        (unsigned int)snapshot.g711_send_fail,
        snapshot.last_g711_send_ret,
        (unsigned int)snapshot.last_g711_pts,
        pc_audio_stats_ready,
        pc_audio_stats.active,
        pc_audio_stats.sample_rate_hz,
        pc_audio_stats.channels,
        pc_audio_stats.bits_per_sample,
        (unsigned long long)pc_audio_stats.audio_packets,
        pc_audio_stats.last_input_level,
        pc_audio_stats.last_output_level,
        pc_audio_stats.last_suppression_q8,
        pc_audio_stats.aec_config_enabled,
        pc_audio_stats.aec_active,
        (unsigned long long)pc_audio_stats.aec_processed_packets,
        (unsigned long long)pc_audio_stats.aec_bypass_packets,
        (unsigned long long)pc_audio_stats.aec_process_errors,
        (unsigned long long)pc_audio_stats.aec_error_resets,
        pc_audio_stats.aec_last_process_ret,
        (unsigned long long)pc_audio_stats.aec.farend_calls,
        (unsigned long long)pc_audio_stats.aec.farend_input_samples,
        (unsigned long long)pc_audio_stats.aec.farend_samples,
        (unsigned long long)pc_audio_stats.aec.farend_pending_samples,
        (unsigned long long)pc_audio_stats.aec.farend_errors,
        (unsigned long long)pc_audio_stats.aec.processed_samples,
        (unsigned long long)pc_audio_stats.aec.bypass_samples,
        pc_audio_stats.aec.last_error,
        pc_audio_stats.aec.stream_delay_ms,
        pc_audio_stats.aec.aecm_echo_mode,
        pc_audio_stats.aec.aecm_cng_mode,
        pc_audio_stats.aec.enable_ns,
        pc_audio_stats.aec.ns_policy,
        (unsigned int)snapshot.rtp_out_packets,
        (unsigned int)snapshot.rtp_out_bytes,
        (unsigned int)snapshot.rtcp_in_packets,
        (unsigned int)snapshot.rtcp_in_bytes,
        (unsigned int)snapshot.rtcp_out_packets,
        (unsigned int)snapshot.rtcp_out_bytes,
        (unsigned int)snapshot.target_bitrate_bps,
        (unsigned int)paced_stats.queued_packets,
        (unsigned int)paced_stats.sent_packet_count,
        (unsigned int)paced_stats.dropped_packet_count,
        (unsigned int)paced_stats.queued_bytes_current,
        (unsigned int)paced_stats.queue_full_count,
        (unsigned int)paced_stats.queue_delay_ms,
        (unsigned int)paced_stats.current_queue_delay_ms,
        (unsigned int)paced_stats.max_queue_delay_ms,
        (unsigned int)paced_stats.dropped_bytes,
        (unsigned int)paced_stats.bitrate_bps,
        (unsigned int)resource_cfg.video_nack_history,
        (unsigned int)resource_cfg.video_nack_max_missing,
        (unsigned int)resource_cfg.audio_nack_history,
        (unsigned int)resource_cfg.audio_nack_max_missing,
        (unsigned int)resource_cfg.nack_max_packet_size,
        (unsigned int)resource_cfg.paced_queue_capacity,
        (unsigned int)resource_cfg.paced_max_packet_size,
        (unsigned int)twcc_stats.sent_packet_count,
        (unsigned int)twcc_stats.remote_feedback_count,
        (unsigned int)twcc_stats.target_bitrate_bps,
        (unsigned int)twcc_stats.acknowledged_bitrate_bps,
        (unsigned int)twcc_stats.smoothed_acknowledged_bitrate_bps,
        (unsigned int)twcc_stats.loss_fraction_q8,
        (unsigned int)twcc_stats.smoothed_loss_fraction_q8,
        (int)twcc_stats.delay_trend_ms,
        (int)twcc_stats.smoothed_delay_trend_ms,
        (int)twcc_stats.min_delay_trend_ms,
        (int)twcc_stats.max_delay_trend_ms,
        (unsigned int)twcc_stats.last_remote_feedback_packets,
        (unsigned int)twcc_stats.last_remote_feedback_received,
        (unsigned int)twcc_stats.last_remote_feedback_lost,
        loss_stats.enabled,
        (unsigned int)loss_stats.packet_count,
        (unsigned int)loss_stats.dropped_packet_count,
        (unsigned int)loss_stats.passed_packet_count,
        (unsigned int)loss_stats.random_drop_count,
        (unsigned int)loss_stats.burst_drop_count,
        (unsigned int)loss_stats.interval_drop_count,
        (unsigned int)loss_stats.random_loss_rate_q16,
        (unsigned int)g_encoder_target_bitrate_bps,
        (unsigned int)g_last_encoder_set_bitrate_bps,
        (unsigned int)ice_stats.remote_candidate_count,
        (unsigned int)ice_stats.stun_rx_packets,
        (unsigned int)ice_stats.stun_rx_binding_requests,
        (unsigned int)ice_stats.stun_rx_binding_success,
        (unsigned int)ice_stats.stun_rx_binding_error,
        (unsigned int)ice_stats.stun_rx_valid_requests,
        (unsigned int)ice_stats.stun_rx_invalid_username,
        (unsigned int)ice_stats.stun_rx_invalid_fingerprint,
        (unsigned int)ice_stats.stun_rx_invalid_integrity,
        (unsigned int)ice_stats.stun_rx_invalid_role,
        (unsigned int)ice_stats.stun_tx_binding_requests,
        (unsigned int)ice_stats.stun_tx_binding_success,
        (unsigned int)ice_stats.stun_tx_binding_error,
        (unsigned int)ice_stats.peer_reflexive_added,
        (unsigned int)ice_stats.active_check_failures,
        (unsigned int)ice_stats.selected_pair_ready);
}

void mi_webrtc_shutdown(void)
{
    mi_webrtc_session_t* list;
    mi_webrtc_session_t* session;

    webrtc_lock();
    list = g_sessions;
    g_sessions = NULL;
    for (session = list; session != NULL; session = session->next) {
        session->removed = 1;
        session->close_requested = 1;
        session->running = 0;
    }
    g_encoder_target_bitrate_bps = 0;
    g_last_encoder_set_bitrate_bps = 0;
    g_last_encoder_set_ms = 0;
    webrtc_unlock();

    while (list != NULL) {
        session = list;
        list = list->next;
        session->next = NULL;
        webrtc_session_release(session);
    }
}
