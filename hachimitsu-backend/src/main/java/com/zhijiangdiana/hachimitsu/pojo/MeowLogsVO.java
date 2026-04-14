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
    private String time;
    private Boolean isCat;
}
