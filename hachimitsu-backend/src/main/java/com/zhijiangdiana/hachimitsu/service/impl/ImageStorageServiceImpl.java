package com.zhijiangdiana.hachimitsu.service.impl;

import com.zhijiangdiana.hachimitsu.service.ImageStorageService;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.Base64;
import java.util.Locale;
import java.util.UUID;

@Slf4j
@Service
public class ImageStorageServiceImpl implements ImageStorageService {

    private final Path uploadRoot;

    public ImageStorageServiceImpl(@Value("${app.upload-dir:${user.dir}/uploads}") String uploadDir) {
        this.uploadRoot = Paths.get(uploadDir).toAbsolutePath().normalize();
    }

    @Override
    public String storeBase64Image(String base64Image, String contentType, String equipmentId, Long timestamp) {
        if (base64Image == null || base64Image.isBlank()) {
            return null;
        }

        String normalizedBase64 = stripDataUrlPrefix(base64Image);
        normalizedBase64 = normalizedBase64.replaceAll("\\s+", "");
        normalizedBase64 = padBase64(normalizedBase64);
        byte[] imageBytes;
        try {
            imageBytes = Base64.getDecoder().decode(normalizedBase64);
        } catch (IllegalArgumentException ex) {
            try {
                imageBytes = Base64.getMimeDecoder().decode(normalizedBase64);
            } catch (IllegalArgumentException nestedEx) {
                log.warn("Invalid base64 image payload for equipment {}. payloadLength={}",
                        equipmentId,
                        normalizedBase64.length(),
                        nestedEx);
                return null;
            }
        }

        String extension = resolveExtension(contentType, base64Image);
        String safeEquipmentId = sanitize(equipmentId == null ? "unknown" : equipmentId);
        long safeTimestamp = timestamp == null ? System.currentTimeMillis() : timestamp;
        Path targetDir = uploadRoot.resolve("meow");
        String fileName = safeEquipmentId + "-" + safeTimestamp + "-" + UUID.randomUUID() + "." + extension;
        Path targetFile = targetDir.resolve(fileName);

        try {
            Files.createDirectories(targetDir);
            Files.write(targetFile, imageBytes);
            return "/uploads/meow/" + fileName;
        } catch (IOException ex) {
            log.warn("Failed to store meow image for equipment {}", equipmentId, ex);
            return null;
        }
    }

    private static String stripDataUrlPrefix(String base64Image) {
        int commaIndex = base64Image.indexOf(',');
        if (base64Image.startsWith("data:") && commaIndex >= 0) {
            return base64Image.substring(commaIndex + 1);
        }
        return base64Image;
    }

    private static String padBase64(String value) {
        int remainder;

        if (value == null || value.isEmpty()) {
            return value;
        }

        remainder = value.length() % 4;
        if (remainder == 0) {
            return value;
        }

        return value + "=".repeat(4 - remainder);
    }

    private static String resolveExtension(String contentType, String base64Image) {
        String source = contentType;
        if ((source == null || source.isBlank()) && base64Image.startsWith("data:")) {
            int end = base64Image.indexOf(';');
            if (end > 5) {
                source = base64Image.substring(5, end);
            }
        }

        if (source == null) {
            return "jpg";
        }

        String normalized = source.toLowerCase(Locale.ROOT);
        if (normalized.contains("png")) {
            return "png";
        }
        if (normalized.contains("bmp")) {
            return "bmp";
        }
        if (normalized.contains("webp")) {
            return "webp";
        }
        if (normalized.contains("jpeg") || normalized.contains("jpg")) {
            return "jpg";
        }
        return "jpg";
    }

    private static String sanitize(String value) {
        return value.replaceAll("[^a-zA-Z0-9_-]", "_");
    }
}
