package com.zhijiangdiana.hachimitsu.pojo;

import lombok.Data;

import java.util.List;

/**
 * @Description
 * @Author 嘉然今天吃向晚
 * @Date 2025/12/15-23:30:18
 */
@Data
public class LineChartVO {
    private List<String> timeLabels;
    private List<Integer> counts;
}
