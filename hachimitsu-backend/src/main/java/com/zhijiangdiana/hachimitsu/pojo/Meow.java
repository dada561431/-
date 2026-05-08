package com.zhijiangdiana.hachimitsu.pojo;

import lombok.Data;
import lombok.ToString;
import org.springframework.data.mongodb.core.mapping.Document;

import java.util.Date;

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
    private Date createTime;
}
