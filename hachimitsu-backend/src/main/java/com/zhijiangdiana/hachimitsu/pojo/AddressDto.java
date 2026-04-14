package com.zhijiangdiana.hachimitsu.pojo;

import lombok.*;

@Data
@ToString
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class AddressDto {
    private String address;
    private Double longitude;
    private Double latitude;
}