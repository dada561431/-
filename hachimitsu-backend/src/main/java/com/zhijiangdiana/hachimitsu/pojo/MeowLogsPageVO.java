package com.zhijiangdiana.hachimitsu.pojo;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Data;
import lombok.NoArgsConstructor;

import java.util.List;

@Data
@AllArgsConstructor
@NoArgsConstructor
@Builder
public class MeowLogsPageVO {
    private List<MeowLogsVO> records;
    private Long total;
    private Integer page;
    private Integer pageSize;
    private Integer totalPages;
    private Boolean hasPrevious;
    private Boolean hasNext;
}
