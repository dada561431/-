package com.zhijiangdiana.hachimitsu.pojo;

import lombok.Data;

@Data
public class AttachMeowImageDto {
    private String equipmentId;
    private Long timestamp;
    private String imageBase64;
    private String imageContentType;
}
