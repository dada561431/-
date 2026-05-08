package com.zhijiangdiana.hachimitsu.service;

public interface AudioStorageService {
    String storePcmAudioAsWav(byte[] pcmAudio,
                              int sampleRate,
                              int bitsPerSample,
                              int channelCount,
                              Integer sampleCount,
                              String equipmentId,
                              Long timestamp);
}
