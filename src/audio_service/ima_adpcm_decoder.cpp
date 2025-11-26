#include "ima_adpcm_decoder.h"
#include <cstring>

// Step table IMA
static const int stepTable[89] = {
    7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31,
    34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88, 97, 107, 118, 130,
    143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449,
    494, 544, 598, 658, 724, 796, 876, 963, 1060, 1166, 1282, 1411,
    1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660, 4026,
    4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487,
    12635, 13899, 15289, 16818, 18500, 20350, 22385, 24623, 27086, 29794,
    32767
};

static const int8_t indexTable[16] = {
  -1, -1, -1, -1, 2, 4, 6, 8,
  -1, -1, -1, -1, 2, 4, 6, 8
};

bool decodeIMA4BitBlock(const uint8_t* adpcm, size_t adpcm_len_bytes,
                        int16_t* pcm_out, size_t& pcm_samples, IMAState& st) {
    pcm_samples = 0;
    if (adpcm_len_bytes < 4) return false;

    // Header mỗi block: predictor(16-bit LE), index(8-bit), reserved(8-bit)
    st.predictor = (int16_t)(adpcm[0] | (adpcm[1] << 8));
    st.index = (int8_t)adpcm[2];
    if (st.index < 0) st.index = 0;
    if (st.index > 88) st.index = 88;

    int16_t* out = pcm_out;
    *out++ = st.predictor;
    pcm_samples++;

    const uint8_t* p = adpcm + 4;
    size_t nibbles = (adpcm_len_bytes - 4) * 2;
    for (size_t i = 0; i < nibbles; ++i) {
        uint8_t byte = p[i >> 1];
        uint8_t nibble = (i & 1) ? (byte >> 4) : (byte & 0x0F);
        int step = stepTable[st.index];
        int diff = step >> 3;
        if (nibble & 1) diff += step >> 2;
        if (nibble & 2) diff += step >> 1;
        if (nibble & 4) diff += step;
        if (nibble & 8) diff = -diff;
        int predictor = st.predictor + diff;
        if (predictor > 32767) predictor = 32767;
        if (predictor < -32768) predictor = -32768;
        st.predictor = (int16_t)predictor;
        st.index += indexTable[nibble];
        if (st.index < 0) st.index = 0;
        if (st.index > 88) st.index = 88;
        *out++ = st.predictor;
        pcm_samples++;
    }
    return true;
}

WAVInfo parseWavIMA(File& f) {
    WAVInfo info;
    f.seek(0);
    uint8_t hdr[44];
    if (f.read(hdr, 44) != 44) return info;
    if (memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) return info;
    uint16_t audioFormat = hdr[20] | (hdr[21] << 8);
    uint16_t numCh = hdr[22] | (hdr[23] << 8);
    uint32_t sr = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    uint16_t blockAlign = hdr[32] | (hdr[33] << 8);
    uint32_t dataOffset = 44;
    uint32_t dataSize = f.size() > 44 ? f.size() - 44 : 0;
    if (audioFormat != 0x0011 || numCh != 1) return info;
    info.sampleRate = sr;
    info.numChannels = numCh;
    info.blockAlign = blockAlign;
    info.dataOffset = dataOffset;
    info.dataSize = dataSize;
    info.ok = true;
    return info;
}
