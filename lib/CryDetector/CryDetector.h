#pragma once
class CryDetector {
public:
    CryDetector(
        float on_th = 0.70f,
        float off_th = 0.18f,
        float ema_alpha = 0.15f,
        float stable_on_s = 1.0f,
        float stable_off_s = 2.3f,
        float min_on_s = 2.0f,
        float min_off_s = 1.5f,
        float block_dur_s = 0.25f,
        float inference_interval = 0.5f
    );
    bool update(float cry_prob);
    bool isCrying();
    float score() const { return ema_score; }
private:
    float on_th, off_th, ema_alpha;
    float stable_on_s, stable_off_s;
    float min_on_s, min_off_s;
    float block_dur_s;
    float dt;
    float ema_score;
    float on_counter;
    float off_counter;
    float block_timer;
    bool crying;
};
