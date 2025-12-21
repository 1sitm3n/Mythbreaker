#include <fstream>
#include <cmath>
#include <vector>
#include <cstdint>

void writeWAV(const std::string& filename, const std::vector<int16_t>& samples, int sampleRate) {
    std::ofstream f(filename, std::ios::binary);
    int dataSize = samples.size() * 2;
    int fileSize = 36 + dataSize;
    
    f.write("RIFF", 4);
    f.write((char*)&fileSize, 4);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    int fmtSize = 16;
    f.write((char*)&fmtSize, 4);
    int16_t audioFormat = 1;
    f.write((char*)&audioFormat, 2);
    int16_t numChannels = 1;
    f.write((char*)&numChannels, 2);
    f.write((char*)&sampleRate, 4);
    int byteRate = sampleRate * 2;
    f.write((char*)&byteRate, 4);
    int16_t blockAlign = 2;
    f.write((char*)&blockAlign, 2);
    int16_t bitsPerSample = 16;
    f.write((char*)&bitsPerSample, 2);
    f.write("data", 4);
    f.write((char*)&dataSize, 4);
    f.write((char*)samples.data(), dataSize);
}

int main() {
    const int sampleRate = 44100;
    
    // Attack sound - short burst
    {
        std::vector<int16_t> samples(sampleRate / 10);
        for (int i = 0; i < samples.size(); i++) {
            float t = i / (float)sampleRate;
            float env = 1.0f - (i / (float)samples.size());
            float wave = sin(220.0f * 2 * 3.14159f * t) + sin(440.0f * 2 * 3.14159f * t) * 0.5f;
            samples[i] = (int16_t)(wave * env * 16000);
        }
        writeWAV("assets/sounds/attack.wav", samples, sampleRate);
    }
    
    // Pickup sound - rising tone
    {
        std::vector<int16_t> samples(sampleRate / 5);
        for (int i = 0; i < samples.size(); i++) {
            float t = i / (float)sampleRate;
            float freq = 400.0f + (i / (float)samples.size()) * 400.0f;
            float env = 1.0f - (i / (float)samples.size());
            float wave = sin(freq * 2 * 3.14159f * t);
            samples[i] = (int16_t)(wave * env * 12000);
        }
        writeWAV("assets/sounds/pickup.wav", samples, sampleRate);
    }
    
    // Hit/damage sound
    {
        std::vector<int16_t> samples(sampleRate / 8);
        for (int i = 0; i < samples.size(); i++) {
            float t = i / (float)sampleRate;
            float env = 1.0f - (i / (float)samples.size());
            float wave = sin(150.0f * 2 * 3.14159f * t) * ((rand() % 100) / 100.0f + 0.5f);
            samples[i] = (int16_t)(wave * env * 14000);
        }
        writeWAV("assets/sounds/hit.wav", samples, sampleRate);
    }
    
    // Footstep
    {
        std::vector<int16_t> samples(sampleRate / 15);
        for (int i = 0; i < samples.size(); i++) {
            float t = i / (float)sampleRate;
            float env = 1.0f - (i / (float)samples.size());
            env = env * env;
            float wave = ((rand() % 200 - 100) / 100.0f);
            samples[i] = (int16_t)(wave * env * 8000);
        }
        writeWAV("assets/sounds/footstep.wav", samples, sampleRate);
    }
    
    return 0;
}
