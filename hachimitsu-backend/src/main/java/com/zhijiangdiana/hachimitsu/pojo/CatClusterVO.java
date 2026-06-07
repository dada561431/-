package com.zhijiangdiana.hachimitsu.pojo;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.Date;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class CatClusterVO {
    private String clusterId;
    private Integer sampleCount;
    private Boolean stable;
    private Date firstSeen;
    private Date lastSeen;
    private Double averageDistance;
}
