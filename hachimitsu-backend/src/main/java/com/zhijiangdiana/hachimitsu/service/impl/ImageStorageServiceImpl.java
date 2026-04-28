package com.zhijiangdiana.hachimitsu.service.impl;

import com.zhijiangdiana.hachimitsu.service.ImageStorageService;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import javax.imageio.ImageIO;
import java.awt.image.BufferedImage;
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

    @Override
    public String storeYuyvImage(byte[] yuyvImage,
                                 int width,
                                 int height,
                                 Integer sourceStride,
                                 String equipmentId,
                                 Long timestamp) {
        long expectedLength;
        int rowBytes;
        String safeEquipmentId;
        long safeTimestamp;
        Path targetDir;
        String fileName;
        Path targetFile;
        BufferedImage image;

        if (yuyvImage == null || width <= 0 || height <= 0 || (width % 2) != 0) {
            return null;
        }

        rowBytes = resolveYuyvSourceStride(width, sourceStride);
        expectedLength = (long) rowBytes * height;
        if (expectedLength > Integer.MAX_VALUE || yuyvImage.length != expectedLength) {
            log.warn("Invalid YUYV image payload. equipmentId={}, width={}, height={}, sourceStride={}, payloadLength={}, expected={}",
                    equipmentId, width, height, sourceStride, yuyvImage.length, expectedLength);
            return null;
        }

        image = convertYuyvToRgbImage(yuyvImage, width, height, rowBytes);
        safeEquipmentId = sanitize(equipmentId == null ? "unknown" : equipmentId);
        safeTimestamp = timestamp == null ? System.currentTimeMillis() : timestamp;
        targetDir = uploadRoot.resolve("meow");
        fileName = safeEquipmentId + "-" + safeTimestamp + "-" + UUID.randomUUID() + ".jpg";
        targetFile = targetDir.resolve(fileName);

        try {
            Files.createDirectories(targetDir);
            if (!ImageIO.write(image, "jpg", targetFile.toFile())) {
                log.warn("No JPEG writer available for YUYV image conversion");
                return null;
            }
            return "/uploads/meow/" + fileName;
        } catch (IOException ex) {
            log.warn("Failed to store YUYV meow image for equipment {}", equipmentId, ex);
            return null;
        }
    }

    private static int resolveYuyvSourceStride(int width, Integer sourceStride) {
        int pixelRowBytes = width * 2;
        if (sourceStride == null || sourceStride < pixelRowBytes || (sourceStride % 2) != 0) {
            return pixelRowBytes;
        }
        return sourceStride;
    }

    private static BufferedImage convertYuyvToRgbImage(byte[] yuyvImage, int width, int height, int sourceStride) {
        BufferedImage image = new BufferedImage(width, height, BufferedImage.TYPE_INT_RGB);

        for (int y = 0; y < height; y++) {
            int index = y * sourceStride;
            for (int x = 0; x < width; x += 2) {
                if ((index + 3) >= yuyvImage.length) {
                    break;
                }

                int y0 = yuyvImage[index] & 0xFF;
                int u = (yuyvImage[index + 1] & 0xFF) - 128;
                int y1 = yuyvImage[index + 2] & 0xFF;
                int v = (yuyvImage[index + 3] & 0xFF) - 128;

                image.setRGB(x, y, yuvToRgb(y0, u, v));
                if ((x + 1) < width) {
                    image.setRGB(x + 1, y, yuvToRgb(y1, u, v));
                }
                index += 4;
            }
        }

        return image;
    }

    private static int yuvToRgb(int yValue, int u, int v) {
        int c = yValue - 16;
        int r = (298 * c + 409 * v + 128) >> 8;
        int g = (298 * c - 100 * u - 208 * v + 128) >> 8;
        int b = (298 * c + 516 * u + 128) >> 8;

        return (clamp(r) << 16) | (clamp(g) << 8) | clamp(b);
    }

    private static int clamp(int value) {
        if (value < 0) {
            return 0;
        }
        if (value > 255) {
            return 255;
        }
        return value;
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
