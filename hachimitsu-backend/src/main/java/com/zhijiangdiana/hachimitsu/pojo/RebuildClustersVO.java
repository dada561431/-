package com.zhijiangdiana.hachimitsu.pojo;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

@Data
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class RebuildClustersVO {
    private Integer total;
    private Integer analyzed;
    private Integer failed;
    private Integer clustered;
}
