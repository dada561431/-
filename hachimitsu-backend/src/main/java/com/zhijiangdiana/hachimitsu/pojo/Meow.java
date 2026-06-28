package com.zhijiangdiana.hachimitsu.pojo;

import lombok.Data;
import lombok.ToString;
import org.springframework.data.mongodb.core.mapping.Document;

import java.util.Date;
import java.util.List;

/**
 * @Description
 * @Author 嘉然今天吃向晚
 * @Date 2025/12/6-00:02:03
 */
@Data
@ToString
@Document("meow_record")
public class Meow {
    private String id;
    private String equipmentId;
    private String address;
    private Double latitude;
    private Double longitude;
    private Double confidence;
    private String imageUrl;
    private String audioUrl;
    private Integer audioDurationMs;
    private Integer audioSampleRate;
    private Integer audioSampleCount;
    private String audioAnalysisStatus;
    private List<Double> audioEmbedding;
    private Double audioEnergy;
    private Double audioZeroCrossingRate;
    private String catClusterId;
    private Double catClusterDistance;
    private Integer catClusterSampleCount;
    private String emotionStatus;
    private String emotionCode;
    private String emotionLabel;
    private Double emotionScore;
    private List<AudioEmotionScore> emotionScores;
    private String emotionMessage;
    private Date createTime;
}
