package com.zhijiangdiana.hachimitsu.service.impl;

import com.zhijiangdiana.hachimitsu.service.AudioStorageService;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.UUID;

@Slf4j
@Service
public class AudioStorageServiceImpl implements AudioStorageService {

    private static final int PCM_FORMAT = 1;

    private final Path uploadRoot;

    public AudioStorageServiceImpl(@Value("${app.upload-dir:${user.dir}/uploads}") String uploadDir) {
        this.uploadRoot = Paths.get(uploadDir).toAbsolutePath().normalize();
    }

    @Override
    public String storePcmAudioAsWav(byte[] pcmAudio,
                                     int sampleRate,
                                     int bitsPerSample,
                                     int channelCount,
                                     Integer sampleCount,
                                     String equipmentId,
                                     Long timestamp) {
        int bytesPerSample;
        int blockAlign;
        long expectedLength;
        String safeEquipmentId;
        long safeTimestamp;
        Path targetDir;
        String fileName;
        Path targetFile;

        if (pcmAudio == null || pcmAudio.length == 0 || sampleRate <= 0 ||
                bitsPerSample <= 0 || (bitsPerSample % 8) != 0 || channelCount <= 0) {
            log.warn("Invalid PCM audio payload. equipmentId={}, sampleRate={}, bitsPerSample={}, channelCount={}, payloadLength={}",
                    equipmentId,
                    sampleRate,
                    bitsPerSample,
                    channelCount,
                    pcmAudio == null ? 0 : pcmAudio.length);
            return null;
        }

        bytesPerSample = bitsPerSample / 8;
        blockAlign = channelCount * bytesPerSample;
        if ((pcmAudio.length % blockAlign) != 0) {
            log.warn("PCM audio payload is not aligned. equipmentId={}, blockAlign={}, payloadLength={}",
                    equipmentId, blockAlign, pcmAudio.length);
            return null;
        }

        if (sampleCount != null && sampleCount > 0) {
            expectedLength = (long) sampleCount * blockAlign;
            if (expectedLength != pcmAudio.length) {
                log.warn("PCM audio payload length mismatch. equipmentId={}, sampleCount={}, payloadLength={}, expected={}",
                        equipmentId, sampleCount, pcmAudio.length, expectedLength);
                return null;
            }
        }

        safeEquipmentId = sanitize(equipmentId == null ? "unknown" : equipmentId);
        safeTimestamp = timestamp == null ? System.currentTimeMillis() : timestamp;
        targetDir = uploadRoot.resolve("meow-audio");
        fileName = safeEquipmentId + "-" + safeTimestamp + "-" + UUID.randomUUID() + ".wav";
        targetFile = targetDir.resolve(fileName);

        try {
            Files.createDirectories(targetDir);
            try (OutputStream outputStream = Files.newOutputStream(targetFile)) {
                writeWavHeader(outputStream, pcmAudio.length, sampleRate, bitsPerSample, channelCount);
                outputStream.write(pcmAudio);
            }
            return "/uploads/meow-audio/" + fileName;
        } catch (IOException ex) {
            log.warn("Failed to store meow audio for equipment {}", equipmentId, ex);
            return null;
        }
    }

    private static void writeWavHeader(OutputStream outputStream,
                                       int dataLength,
                                       int sampleRate,
                                       int bitsPerSample,
                                       int channelCount) throws IOException {
        int blockAlign = channelCount * (bitsPerSample / 8);
        int byteRate = sampleRate * blockAlign;

        writeAscii(outputStream, "RIFF");
        writeIntLE(outputStream, 36 + dataLength);
        writeAscii(outputStream, "WAVE");
        writeAscii(outputStream, "fmt ");
        writeIntLE(outputStream, 16);
        writeShortLE(outputStream, PCM_FORMAT);
        writeShortLE(outputStream, channelCount);
        writeIntLE(outputStream, sampleRate);
        writeIntLE(outputStream, byteRate);
        writeShortLE(outputStream, blockAlign);
        writeShortLE(outputStream, bitsPerSample);
        writeAscii(outputStream, "data");
        writeIntLE(outputStream, dataLength);
    }

    private static void writeAscii(OutputStream outputStream, String value) throws IOException {
        for (int i = 0; i < value.length(); i++) {
            outputStream.write(value.charAt(i));
        }
    }

    private static void writeIntLE(OutputStream outputStream, int value) throws IOException {
        outputStream.write(value & 0xFF);
        outputStream.write((value >>> 8) & 0xFF);
        outputStream.write((value >>> 16) & 0xFF);
        outputStream.write((value >>> 24) & 0xFF);
    }

    private static void writeShortLE(OutputStream outputStream, int value) throws IOException {
        outputStream.write(value & 0xFF);
        outputStream.write((value >>> 8) & 0xFF);
    }

    private static String sanitize(String value) {
        return value.replaceAll("[^a-zA-Z0-9_-]", "_");
    }
}
