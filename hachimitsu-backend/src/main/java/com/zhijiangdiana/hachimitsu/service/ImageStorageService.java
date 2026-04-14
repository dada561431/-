package com.zhijiangdiana.hachimitsu.service;

public interface ImageStorageService {
    String storeBase64Image(String base64Image, String contentType, String equipmentId, Long timestamp);
}
