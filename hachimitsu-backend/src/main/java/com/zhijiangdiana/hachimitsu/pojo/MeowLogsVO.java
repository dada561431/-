package com.zhijiangdiana.hachimitsu.pojo;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

/**
 * @Description
 * @Author 嘉然今天吃向晚
 * @Date 2025/12/17-16:26:46
 */
@Data
@AllArgsConstructor
@NoArgsConstructor
@Builder
public class MeowLogsVO {
    private String id;
    private String equipmentId;
    private Double lat;
    private Double lng;
    private Double confidence;
    private String imageUrl;
    private String audioUrl;
    private Integer audioDurationMs;
    private String audioAnalysisStatus;
    private String catClusterId;
    private Double catClusterDistance;
    private Integer catClusterSampleCount;
    private String emotionStatus;
    private String emotionCode;
    private String emotionLabel;
    private Double emotionScore;
    private java.util.List<AudioEmotionScore> emotionScores;
    private String emotionMessage;
    private String time;
    private Boolean isCat;
}
