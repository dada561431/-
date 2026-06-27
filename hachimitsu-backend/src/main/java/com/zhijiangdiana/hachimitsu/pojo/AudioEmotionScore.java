package com.zhijiangdiana.hachimitsu.pojo;

import lombok.Data;

@Data
public class AudioEmotionScore {
    private String code;
    private String label;
    private String prompt;
    private Double score;
}
