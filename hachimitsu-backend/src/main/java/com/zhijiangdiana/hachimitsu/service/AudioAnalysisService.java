package com.zhijiangdiana.hachimitsu.service;

import com.zhijiangdiana.hachimitsu.pojo.AudioAnalysisResult;

public interface AudioAnalysisService {
    AudioAnalysisResult analyzeStoredAudio(String equipmentId, Long timestamp, String audioUrl);

    boolean resetClusters();
}
