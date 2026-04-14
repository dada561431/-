package com.zhijiangdiana.hachimitsu.pojo;

import lombok.Data;

/**
 * @Description
 * @Author 嘉然今天吃向晚
 * @Date 2025/12/5-23:59:21
 */
@Data
public class AddMeowDto {
    private String equipmentId;
    private Double latitude;
    private Double longitude;
    private Double confidence;
    private Long timestamp;
    private String imageBase64;
    private String imageContentType;
}
