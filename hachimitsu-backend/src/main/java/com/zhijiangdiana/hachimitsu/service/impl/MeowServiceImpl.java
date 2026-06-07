package com.zhijiangdiana.hachimitsu.service.impl;

import com.alibaba.fastjson.JSON;
import com.zhijiangdiana.hachimitsu.pojo.AddMeowDto;
import com.zhijiangdiana.hachimitsu.pojo.AddressDto;
import com.zhijiangdiana.hachimitsu.pojo.AudioAnalysisResult;
import com.zhijiangdiana.hachimitsu.pojo.AttachMeowImageDto;
import com.zhijiangdiana.hachimitsu.pojo.Meow;
import com.zhijiangdiana.hachimitsu.service.AddressService;
import com.zhijiangdiana.hachimitsu.service.AudioAnalysisService;
import com.zhijiangdiana.hachimitsu.service.AudioStorageService;
import com.zhijiangdiana.hachimitsu.service.CacheService;
import com.zhijiangdiana.hachimitsu.service.ImageStorageService;
import com.zhijiangdiana.hachimitsu.service.MeowService;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.BeanUtils;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.data.domain.Sort;
import org.springframework.data.mongodb.core.MongoTemplate;
import org.springframework.data.mongodb.core.query.Criteria;
import org.springframework.data.mongodb.core.query.Query;
import org.springframework.messaging.simp.SimpMessagingTemplate;
import org.springframework.stereotype.Service;

import java.io.IOException;
import java.util.Date;
import java.util.concurrent.TimeUnit;

@Slf4j
@Service
public class MeowServiceImpl implements MeowService {

    private static final long IP_CACHE_EXPIRE_SECONDS = 3600L;
    private static final int IMAGE_ATTACH_RETRY_COUNT = 30;
    private static final long IMAGE_ATTACH_RETRY_DELAY_MS = 300L;
    private static final long IMAGE_ATTACH_TIME_WINDOW_MS = 10000L;
    private static final long IMAGE_ATTACH_FALLBACK_WINDOW_MS = 120000L;

    @Autowired
    private MongoTemplate mongoTemplate;

    @Autowired
    private AddressService addressService;

    @Autowired
    private CacheService cacheService;

    @Autowired
    private SimpMessagingTemplate messagingTemplate;

    @Autowired
    private ImageStorageService imageStorageService;

    @Autowired
    private AudioStorageService audioStorageService;

    @Autowired
    private AudioAnalysisService audioAnalysisService;

    @Override
    public void saveMeowLog(AddMeowDto dto, String ip) {
        long safeTimestamp;
        int imageLength;

        if (dto == null) {
            log.warn("Received null meow payload");
            return;
        }

        if (ip.startsWith("127") || ip.startsWith("192")) {
            ip = "121.40.25.50";
        }
        log.info("input ip: {}", ip);

        safeTimestamp = dto.getTimestamp() != null ? dto.getTimestamp() : System.currentTimeMillis();
        imageLength = dto.getImageBase64() == null ? 0 : dto.getImageBase64().length();
        if (dto.getTimestamp() == null) {
            log.warn("Incoming meow payload missing timestamp. equipmentId={}, confidence={}, imageLength={}",
                    dto.getEquipmentId(),
                    dto.getConfidence(),
                    imageLength);
        }

        Meow meow = new Meow();
        BeanUtils.copyProperties(dto, meow);
        meow.setCreateTime(new Date(safeTimestamp));
        meow.setImageUrl(imageStorageService.storeBase64Image(
                dto.getImageBase64(),
                dto.getImageContentType(),
                dto.getEquipmentId(),
                safeTimestamp
        ));

        mongoTemplate.save(meow);
        messagingTemplate.convertAndSend("/topic/logs", meow);

        AddressDto address = loadAddress(ip);
        if (address != null) {
            meow.setLongitude(address.getLongitude());
            meow.setLatitude(address.getLatitude());
            meow.setAddress(address.getAddress());
            mongoTemplate.save(meow);
            messagingTemplate.convertAndSend("/topic/logs", meow);
        }
    }

    @Override
    public boolean attachMeowImage(AttachMeowImageDto dto) {
        String imageUrl;

        if ((dto == null) || (dto.getTimestamp() == null) || (dto.getEquipmentId() == null)
                || (dto.getImageBase64() == null) || dto.getImageBase64().isBlank()) {
            log.warn("Invalid delayed image attach payload. equipmentId={}, timestamp={}, imageLength={}",
                    dto == null ? null : dto.getEquipmentId(),
                    dto == null ? null : dto.getTimestamp(),
                    (dto == null || dto.getImageBase64() == null) ? 0 : dto.getImageBase64().length());
            return false;
        }

        log.info("Processing delayed image attach. equipmentId={}, timestamp={}, imageLength={}, contentType={}",
                dto.getEquipmentId(),
                dto.getTimestamp(),
                dto.getImageBase64().length(),
                dto.getImageContentType());

        imageUrl = imageStorageService.storeBase64Image(
                dto.getImageBase64(),
                dto.getImageContentType(),
                dto.getEquipmentId(),
                dto.getTimestamp()
        );
        if ((imageUrl == null) || imageUrl.isBlank()) {
            log.warn("Failed to store delayed meow image. equipmentId={}, timestamp={}",
                    dto.getEquipmentId(), dto.getTimestamp());
            return false;
        }

        return attachStoredImageToMeow(dto.getEquipmentId(), dto.getTimestamp(), imageUrl);
    }

    @Override
    public boolean attachRawYuyvImage(String equipmentId,
                                      Long timestamp,
                                      byte[] yuyvImage,
                                      int width,
                                      int height,
                                      Integer sourceStride) {
        String imageUrl;

        if ((equipmentId == null) || equipmentId.isBlank() || (timestamp == null) ||
                (yuyvImage == null) || (yuyvImage.length == 0) || (width <= 0) || (height <= 0)) {
            log.warn("Invalid raw YUYV attach payload. equipmentId={}, timestamp={}, width={}, height={}, payloadLength={}",
                    equipmentId,
                    timestamp,
                    width,
                    height,
                    yuyvImage == null ? 0 : yuyvImage.length);
            return false;
        }

        log.info("Processing raw YUYV image attach. equipmentId={}, timestamp={}, width={}, height={}, sourceStride={}, payloadLength={}",
                equipmentId,
                timestamp,
                width,
                height,
                sourceStride,
                yuyvImage.length);

        imageUrl = imageStorageService.storeYuyvImage(yuyvImage, width, height, sourceStride, equipmentId, timestamp);
        if ((imageUrl == null) || imageUrl.isBlank()) {
            log.warn("Failed to store raw YUYV meow image. equipmentId={}, timestamp={}",
                    equipmentId, timestamp);
            return false;
        }

        return attachStoredImageToMeow(equipmentId, timestamp, imageUrl);
    }

    @Override
    public boolean attachPcmAudio(String equipmentId,
                                  Long timestamp,
                                  byte[] pcmAudio,
                                  int sampleRate,
                                  int bitsPerSample,
                                  int channelCount,
                                  Integer sampleCount) {
        String audioUrl;
        Integer resolvedSampleCount;
        Integer durationMs;
        int bytesPerSample;
        int blockAlign;

        if ((equipmentId == null) || equipmentId.isBlank() || (timestamp == null) ||
                (pcmAudio == null) || (pcmAudio.length == 0) || sampleRate <= 0 ||
                bitsPerSample <= 0 || (bitsPerSample % 8) != 0 || channelCount <= 0) {
            log.warn("Invalid PCM audio attach payload. equipmentId={}, timestamp={}, sampleRate={}, bitsPerSample={}, channelCount={}, payloadLength={}",
                    equipmentId,
                    timestamp,
                    sampleRate,
                    bitsPerSample,
                    channelCount,
                    pcmAudio == null ? 0 : pcmAudio.length);
            return false;
        }

        bytesPerSample = bitsPerSample / 8;
        blockAlign = bytesPerSample * channelCount;
        if ((pcmAudio.length % blockAlign) != 0) {
            log.warn("Unaligned PCM audio attach payload. equipmentId={}, timestamp={}, blockAlign={}, payloadLength={}",
                    equipmentId, timestamp, blockAlign, pcmAudio.length);
            return false;
        }

        resolvedSampleCount = (sampleCount != null && sampleCount > 0)
                ? sampleCount
                : pcmAudio.length / blockAlign;
        durationMs = (int) (((long) resolvedSampleCount * 1000L) / sampleRate);

        log.info("Processing PCM audio attach. equipmentId={}, timestamp={}, sampleRate={}, bitsPerSample={}, channelCount={}, sampleCount={}, durationMs={}, payloadLength={}",
                equipmentId,
                timestamp,
                sampleRate,
                bitsPerSample,
                channelCount,
                resolvedSampleCount,
                durationMs,
                pcmAudio.length);

        audioUrl = audioStorageService.storePcmAudioAsWav(pcmAudio,
                sampleRate,
                bitsPerSample,
                channelCount,
                resolvedSampleCount,
                equipmentId,
                timestamp);
        if ((audioUrl == null) || audioUrl.isBlank()) {
            log.warn("Failed to store meow audio. equipmentId={}, timestamp={}", equipmentId, timestamp);
            return false;
        }

        return attachStoredAudioToMeow(equipmentId,
                timestamp,
                audioUrl,
                durationMs,
                sampleRate,
                resolvedSampleCount);
    }

    private boolean attachStoredImageToMeow(String equipmentId, Long timestamp, String imageUrl) {
        Meow meow;

        meow = findMeowForImageAttach(equipmentId, timestamp);
        if (meow == null) {
            meow = findLatestMeowForEquipment(equipmentId, timestamp);
            if (meow != null) {
                log.warn("Falling back to latest meow record for delayed image attach. equipmentId={}, timestamp={}, meowId={}",
                        equipmentId, timestamp, meow.getId());
            }
        }
        if (meow == null) {
            meow = new Meow();
            meow.setEquipmentId(equipmentId);
            meow.setCreateTime(new Date(timestamp));
            log.warn("Creating placeholder meow record for delayed image attach. equipmentId={}, timestamp={}",
                    equipmentId, timestamp);
        }

        meow.setImageUrl(imageUrl);
        mongoTemplate.save(meow);
        messagingTemplate.convertAndSend("/topic/logs", meow);
        return true;
    }

    private boolean attachStoredAudioToMeow(String equipmentId,
                                            Long timestamp,
                                            String audioUrl,
                                            Integer audioDurationMs,
                                            Integer audioSampleRate,
                                            Integer audioSampleCount) {
        Meow meow;
        AudioAnalysisResult analysisResult;

        meow = findMeowForImageAttach(equipmentId, timestamp);
        if (meow == null) {
            meow = findLatestMeowForEquipment(equipmentId, timestamp);
            if (meow != null) {
                log.warn("Falling back to latest meow record for delayed audio attach. equipmentId={}, timestamp={}, meowId={}",
                        equipmentId, timestamp, meow.getId());
            }
        }
        if (meow == null) {
            meow = new Meow();
            meow.setEquipmentId(equipmentId);
            meow.setCreateTime(new Date(timestamp));
            log.warn("Creating placeholder meow record for delayed audio attach. equipmentId={}, timestamp={}",
                    equipmentId, timestamp);
        }

        meow.setAudioUrl(audioUrl);
        meow.setAudioDurationMs(audioDurationMs);
        meow.setAudioSampleRate(audioSampleRate);
        meow.setAudioSampleCount(audioSampleCount);

        analysisResult = audioAnalysisService.analyzeStoredAudio(equipmentId, timestamp, audioUrl);
        if (analysisResult != null) {
            meow.setAudioAnalysisStatus(analysisResult.getStatus());
            meow.setAudioEmbedding(analysisResult.getEmbedding());
            meow.setAudioEnergy(analysisResult.getEnergy());
            meow.setAudioZeroCrossingRate(analysisResult.getZeroCrossingRate());
            meow.setCatClusterId(analysisResult.getClusterId());
            meow.setCatClusterDistance(analysisResult.getClusterDistance());
            meow.setCatClusterSampleCount(analysisResult.getClusterSampleCount());
        }

        mongoTemplate.save(meow);
        messagingTemplate.convertAndSend("/topic/logs", meow);
        return true;
    }

    private Meow findMeowForImageAttach(String equipmentId, Long timestamp) {
        Date center = new Date(timestamp);
        Date start = new Date(timestamp - IMAGE_ATTACH_TIME_WINDOW_MS);
        Date end = new Date(timestamp + IMAGE_ATTACH_TIME_WINDOW_MS);
        Query exactQuery = Query.query(Criteria.where("equipmentId").is(equipmentId)
                .and("createTime").is(center));
        Query fuzzyQuery = Query.query(Criteria.where("equipmentId").is(equipmentId)
                .and("createTime").gte(start).lte(end));
        Meow meow = null;

        for (int attempt = 0; attempt < IMAGE_ATTACH_RETRY_COUNT; attempt++) {
            meow = mongoTemplate.findOne(exactQuery, Meow.class);
            if (meow == null) {
                meow = mongoTemplate.findOne(fuzzyQuery, Meow.class);
            }
            if (meow != null) {
                return meow;
            }

            try {
                Thread.sleep(IMAGE_ATTACH_RETRY_DELAY_MS);
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                return null;
            }
        }

        return null;
    }

    private Meow findLatestMeowForEquipment(String equipmentId, Long timestamp) {
        Date start = new Date(timestamp - IMAGE_ATTACH_FALLBACK_WINDOW_MS);
        Date end = new Date(timestamp + IMAGE_ATTACH_FALLBACK_WINDOW_MS);
        Query latestQuery = Query.query(Criteria.where("equipmentId").is(equipmentId)
                        .and("createTime").gte(start).lte(end))
                .with(Sort.by(Sort.Direction.DESC, "createTime"))
                .limit(1);
        return mongoTemplate.findOne(latestQuery, Meow.class);
    }

    private AddressDto loadAddress(String ip) {
        try {
            String ipJson = cacheService.get(ip);
            if (ipJson != null) {
                return JSON.parseObject(ipJson, AddressDto.class);
            }

            AddressDto address = addressService.getAddressByIP(ip);
            if (address != null) {
                cacheService.setEx(ip, JSON.toJSONString(address), IP_CACHE_EXPIRE_SECONDS, TimeUnit.SECONDS);
                log.info("load ip: {} into redis", ip);
            }
            return address;
        } catch (IOException ex) {
            log.warn("Failed to resolve address for ip {}", ip, ex);
            return null;
        } catch (Exception ex) {
            log.warn("Failed to load address cache for ip {}", ip, ex);
            return null;
        }
    }
}
