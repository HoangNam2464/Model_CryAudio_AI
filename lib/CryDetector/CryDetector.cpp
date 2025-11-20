#include "CryDetector.h"
CryDetector::CryDetector(
    float on_th, float off_th, float ema_alpha,
    float stable_on_s, float stable_off_s,
    float min_on_s, float min_off_s,
    float block_dur_s, float inference_interval
)
: on_th(on_th), off_th(off_th), ema_alpha(ema_alpha),
  stable_on_s(stable_on_s), stable_off_s(stable_off_s),
  min_on_s(min_on_s), min_off_s(min_off_s),
  block_dur_s(block_dur_s), dt(inference_interval),
  ema_score(0.0f), on_counter(0.0f), off_counter(0.0f),
  block_timer(0.0f), crying(false) {}

bool CryDetector::update(float cry_prob){
    ema_score = ema_alpha*cry_prob + (1.0f-ema_alpha)*ema_score;

    if (block_timer > 0.0f){ block_timer -= dt; if (block_timer<0) block_timer=0; return crying; }

    if (crying){
        on_counter  = (ema_score > on_th)  ? on_counter + dt  : 0.0f;
        off_counter = (ema_score < off_th) ? off_counter + dt : 0.0f;
        if (ema_score < off_th && off_counter >= stable_off_s && on_counter >= min_on_s){
            crying = false; block_timer = block_dur_s; on_counter = off_counter = 0.0f;
        }
    }else{
        off_counter = (ema_score < off_th) ? off_counter + dt : 0.0f;
        on_counter  = (ema_score > on_th)  ? on_counter + dt  : 0.0f;
        if (ema_score > on_th && on_counter >= stable_on_s && off_counter >= min_off_s){
            crying = true; block_timer = block_dur_s; on_counter = off_counter = 0.0f;
        }
    }
    return crying;
}
bool CryDetector::isCrying(){ return crying; }

void CryDetector::configure(float new_on_th, float new_off_th,
                            float new_stable_on_s, float new_stable_off_s,
                            float new_min_on_s, float new_min_off_s){
    on_th = new_on_th;
    off_th = new_off_th;
    stable_on_s = new_stable_on_s;
    stable_off_s = new_stable_off_s;
    min_on_s = new_min_on_s;
    min_off_s = new_min_off_s;
}
