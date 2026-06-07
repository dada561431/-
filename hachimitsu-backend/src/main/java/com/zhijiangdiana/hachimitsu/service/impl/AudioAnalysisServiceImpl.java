package com.zhijiangdiana.hachimitsu.service.impl;

import com.alibaba.fastjson.JSON;
import com.alibaba.fastjson.JSONObject;
import com.zhijiangdiana.hachimitsu.pojo.AudioAnalysisResult;
import com.zhijiangdiana.hachimitsu.service.AudioAnalysisService;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.net.URI;
import java.net.http.HttpClient;
import java.net.http.HttpRequest;
import java.net.http.HttpResponse;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.time.Duration;

@Slf4j
@Service
public class AudioAnalysisServiceImpl implements AudioAnalysisService {

    private final boolean enabled;
    private final String analysisUrl;
    private final Path uploadRoot;
    private final HttpClient httpClient;
    private final Duration timeout;

    public AudioAnalysisServiceImpl(@Value("${app.audio-analysis.enabled:true}") boolean enabled,
                                    @Value("${app.audio-analysis.url:http://127.0.0.1:5055/analyze}") String analysisUrl,
                                    @Value("${app.audio-analysis.timeout-ms:3000}") long timeoutMs,
                                    @Value("${app.upload-dir:${user.dir}/uploads}") String uploadDir) {
        this.enabled = enabled;
        this.analysisUrl = analysisUrl;
        this.uploadRoot = Paths.get(uploadDir).toAbsolutePath().normalize();
        this.timeout = Duration.ofMillis(Math.max(500L, timeoutMs));
        this.httpClient = HttpClient.newBuilder()
                .connectTimeout(this.timeout)
                .build();
    }

    @Override
    public AudioAnalysisResult analyzeStoredAudio(String equipmentId, Long timestamp, String audioUrl) {
        JSONObject requestJson;
        HttpRequest request;
        HttpResponse<String> response;
        Path audioPath;

        if (!enabled) {
            return null;
        }
        if (audioUrl == null || audioUrl.isBlank()) {
            log.warn("Skipping audio analysis because audioUrl is empty. equipmentId={}, timestamp={}",
                    equipmentId, timestamp);
            return null;
        }

        audioPath = resolveAudioPath(audioUrl);
        if (audioPath == null) {
            log.warn("Skipping audio analysis because audioUrl cannot be resolved. audioUrl={}", audioUrl);
            return null;
        }

        requestJson = new JSONObject();
        requestJson.put("equipmentId", equipmentId);
        requestJson.put("timestamp", timestamp);
        requestJson.put("audioPath", audioPath.toString());

        request = HttpRequest.newBuilder()
                .uri(URI.create(analysisUrl))
                .timeout(timeout)
                .header("Content-Type", "application/json")
                .POST(HttpRequest.BodyPublishers.ofString(requestJson.toJSONString()))
                .build();

        try {
            response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                log.warn("Audio analysis service returned status {} body={}",
                        response.statusCode(), response.body());
                return failedResult("http_" + response.statusCode());
            }
            return JSON.parseObject(response.body(), AudioAnalysisResult.class);
        } catch (IOException ex) {
            log.warn("Audio analysis service is unavailable. url={}, equipmentId={}, timestamp={}",
                    analysisUrl, equipmentId, timestamp, ex);
            return failedResult("service_unavailable");
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            log.warn("Audio analysis request interrupted. equipmentId={}, timestamp={}", equipmentId, timestamp);
            return failedResult("interrupted");
        } catch (Exception ex) {
            log.warn("Failed to parse audio analysis response. equipmentId={}, timestamp={}",
                    equipmentId, timestamp, ex);
            return failedResult("invalid_response");
        }
    }

    @Override
    public boolean resetClusters() {
        HttpRequest request;
        HttpResponse<String> response;
        String resetUrl;

        if (!enabled) {
            return false;
        }

        resetUrl = analysisUrl.endsWith("/analyze")
                ? analysisUrl.substring(0, analysisUrl.length() - "/analyze".length()) + "/reset"
                : analysisUrl + "/reset";
        request = HttpRequest.newBuilder()
                .uri(URI.create(resetUrl))
                .timeout(timeout)
                .POST(HttpRequest.BodyPublishers.noBody())
                .build();

        try {
            response = httpClient.send(request, HttpResponse.BodyHandlers.ofString());
            if (response.statusCode() < 200 || response.statusCode() >= 300) {
                log.warn("Audio analysis reset returned status {} body={}", response.statusCode(), response.body());
                return false;
            }
            AudioAnalysisResult result = JSON.parseObject(response.body(), AudioAnalysisResult.class);
            return result != null && "ok".equalsIgnoreCase(result.getStatus());
        } catch (IOException ex) {
            log.warn("Audio analysis reset service is unavailable. url={}", resetUrl, ex);
            return false;
        } catch (InterruptedException ex) {
            Thread.currentThread().interrupt();
            log.warn("Audio analysis reset request interrupted.");
            return false;
        } catch (Exception ex) {
            log.warn("Failed to parse audio analysis reset response.", ex);
            return false;
        }
    }

    private Path resolveAudioPath(String audioUrl) {
        String relativePath;

        relativePath = audioUrl;
        if (relativePath.startsWith("/uploads/")) {
            relativePath = relativePath.substring("/uploads/".length());
        } else if (relativePath.startsWith("uploads/")) {
            relativePath = relativePath.substring("uploads/".length());
        } else {
            return null;
        }

        return uploadRoot.resolve(relativePath).normalize();
    }

    private static AudioAnalysisResult failedResult(String status) {
        AudioAnalysisResult result = new AudioAnalysisResult();
        result.setStatus(status);
        return result;
    }
}
