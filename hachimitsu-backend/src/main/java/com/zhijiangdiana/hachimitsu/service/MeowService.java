package com.zhijiangdiana.hachimitsu.service;

import com.zhijiangdiana.hachimitsu.pojo.AddMeowDto;
import com.zhijiangdiana.hachimitsu.pojo.AttachMeowImageDto;

/**
 * @Description
 * @Author 嘉然今天吃向晚
 * @Date 2025/12/15-01:16:38
 */
public interface MeowService {

    void saveMeowLog(AddMeowDto dto, String ip);

    boolean attachMeowImage(AttachMeowImageDto dto);

    boolean attachRawYuyvImage(String equipmentId,
                               Long timestamp,
                               byte[] yuyvImage,
                               int width,
                               int height,
                               Integer sourceStride);

    boolean attachPcmAudio(String equipmentId,
                           Long timestamp,
                           byte[] pcmAudio,
                           int sampleRate,
                           int bitsPerSample,
                           int channelCount,
                           Integer sampleCount);
}
