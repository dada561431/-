package com.zhijiangdiana.hachimitsu.service;

public interface ImageStorageService {
    String storeBase64Image(String base64Image, String contentType, String equipmentId, Long timestamp);

    String storeYuyvImage(byte[] yuyvImage,
                          int width,
                          int height,
                          Integer sourceStride,
                          String equipmentId,
                          Long timestamp);
}
