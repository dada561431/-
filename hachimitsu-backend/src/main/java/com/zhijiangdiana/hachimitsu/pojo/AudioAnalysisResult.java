package com.zhijiangdiana.hachimitsu.pojo;

import lombok.Data;

import java.util.List;

@Data
public class AudioAnalysisResult {
    private String status;
    private List<Double> embedding;
    private Double energy;
    private Double zeroCrossingRate;
    private String clusterId;
    private Double clusterDistance;
    private Integer clusterSampleCount;
    private Boolean forcedAssignment;
    private Double clusterThreshold;
    private Integer expectedCatCount;
    private String emotionStatus;
    private String emotionCode;
    private String emotionLabel;
    private Double emotionScore;
    private List<AudioEmotionScore> emotionScores;
    private String emotionMessage;
    private String message;
}
