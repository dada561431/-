package com.zhijiangdiana.hachimitsu.controller;

import com.mongodb.client.MongoClient;
import com.mongodb.client.result.UpdateResult;
import com.zhijiangdiana.hachimitsu.pojo.*;
import com.zhijiangdiana.hachimitsu.service.AudioAnalysisService;
import org.bson.Document;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.data.domain.Sort;
import org.springframework.data.mongodb.core.MongoTemplate;
import org.springframework.data.mongodb.core.aggregation.Aggregation;
import org.springframework.data.mongodb.core.aggregation.AggregationResults;
import org.springframework.data.mongodb.core.query.Criteria;
import org.springframework.data.mongodb.core.query.Query;
import org.springframework.data.mongodb.core.query.Update;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

import java.text.SimpleDateFormat;
import java.time.Instant;
import java.time.LocalDateTime;
import java.time.ZoneId;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * @Description
 * @Author 鍢夌劧浠婂ぉ鍚冨悜鏅?
 * @Date 2025/12/15-21:44:27
 */
@RestController
@RequestMapping("/api")
public class StatisticController {

    @Autowired
    private MongoTemplate mongoTemplate;

    DateTimeFormatter formatter = DateTimeFormatter.ofPattern("HH:mm");

    DateTimeFormatter yyyyMMdd_HHmmss = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");
    DateTimeFormatter monthDayFormatter = DateTimeFormatter.ofPattern("MM-dd");
    DateTimeFormatter yearMonthFormatter = DateTimeFormatter.ofPattern("yyyy-MM");

    private final Integer DEFAULT_TEN_MINUTE_WINDOW_LENGTH = 6;
    private final Integer MAX_TEN_MINUTE_WINDOW_LENGTH = 36;
    private final Integer DEFAULT_WINDOW_LENGTH = 8;

    private final Integer MAX_WINDOW_LENGTH = 24;
    private final Integer DEFAULT_DAY_WINDOW_LENGTH = 14;
    private final Integer MAX_DAY_WINDOW_LENGTH = 31;
    private final Integer DEFAULT_MONTH_WINDOW_LENGTH = 6;
    private final Integer MAX_MONTH_WINDOW_LENGTH = 12;

    private final ZoneId zoneId = ZoneId.of("Asia/Shanghai");
    @Autowired
    private MongoClient mongo;

    @Autowired
    private AudioAnalysisService audioAnalysisService;

    @GetMapping("/charts/line")
    public ResponseResult getLineChart(Integer windowLength, String granularity) {
        ChronoUnit unit;
        DateTimeFormatter labelFormatter;
        ZonedDateTime start;
        ZonedDateTime end;

        granularity = granularity == null ? "hour" : granularity.trim().toLowerCase();
        if ("ten-minute".equals(granularity)) {
            unit = ChronoUnit.MINUTES;
            labelFormatter = formatter;
            if (windowLength == null || windowLength < 1) {
                windowLength = DEFAULT_TEN_MINUTE_WINDOW_LENGTH;
            }
            if (windowLength > MAX_TEN_MINUTE_WINDOW_LENGTH) {
                windowLength = MAX_TEN_MINUTE_WINDOW_LENGTH;
            }
            end = floorToTenMinutes(LocalDateTime.now()).atZone(zoneId);
            start = end.minusMinutes(10);
        } else if ("day".equals(granularity)) {
            unit = ChronoUnit.DAYS;
            labelFormatter = monthDayFormatter;
            if (windowLength == null || windowLength < 1) {
                windowLength = DEFAULT_DAY_WINDOW_LENGTH;
            }
            if (windowLength > MAX_DAY_WINDOW_LENGTH) {
                windowLength = MAX_DAY_WINDOW_LENGTH;
            }
            end = LocalDateTime.now().toLocalDate().plusDays(1).atStartOfDay(zoneId);
            start = end.minusDays(1);
        } else if ("month".equals(granularity)) {
            unit = ChronoUnit.MONTHS;
            labelFormatter = yearMonthFormatter;
            if (windowLength == null || windowLength < 1) {
                windowLength = DEFAULT_MONTH_WINDOW_LENGTH;
            }
            if (windowLength > MAX_MONTH_WINDOW_LENGTH) {
                windowLength = MAX_MONTH_WINDOW_LENGTH;
            }
            end = LocalDateTime.now().withDayOfMonth(1).toLocalDate().plusMonths(1).atStartOfDay(zoneId);
            start = end.minusMonths(1);
        } else {
            unit = ChronoUnit.HOURS;
            labelFormatter = formatter;
            if (windowLength == null || windowLength < 1) {
                windowLength = DEFAULT_WINDOW_LENGTH;
            }
            if (windowLength > MAX_WINDOW_LENGTH) {
                windowLength = MAX_WINDOW_LENGTH;
            }
            end = LocalDateTime.now().withMinute(0).withSecond(0).withNano(0).atZone(zoneId);
            start = end.minusHours(1);
        }

        List<String> timeLine = new ArrayList<>();
        List<Integer> countLine = new ArrayList<>();
        for (int i = 0; i < windowLength; i++) {
            Date startTime = Date.from(start.toInstant());
            Date endTime = Date.from(end.toInstant());

            timeLine.add(start.format(labelFormatter));
            countLine.add((int) mongoTemplate.count(Query.query(Criteria.where("createTime").lte(endTime).gt(startTime)), Meow.class));

            if ("ten-minute".equals(granularity)) {
                start = start.minusMinutes(10);
                end = end.minusMinutes(10);
            } else {
                start = start.minus(1, unit);
                end = end.minus(1, unit);
            }
        }

        Collections.reverse(timeLine);
        Collections.reverse(countLine);

        LineChartVO lineChartVO = new LineChartVO();
        lineChartVO.setTimeLabels(timeLine);
        lineChartVO.setCounts(countLine);

        return ResponseResult.okResult(lineChartVO);
    }

    private LocalDateTime floorToTenMinutes(LocalDateTime time) {
        return time.withMinute((time.getMinute() / 10) * 10).withSecond(0).withNano(0);
    }

    private final Integer MAX_PIE_ITEM_COUNT = 8;
    private final Integer DEFAULT_PIE_ITEM_COUNT = 4;

    @GetMapping("/charts/pie")
    public ResponseResult getPieChart(Integer maxItemCount, Integer windowLength, String granularity) {
        Date[] range;
        List<Meow> logs;
        Map<String, Long> locationCounts = new HashMap<>();
        List<Map.Entry<String, Long>> sortedLocations;
        List<PieChartCountVO> voList = new ArrayList<>();
        long otherSum = 0;

        if (maxItemCount == null || maxItemCount > MAX_PIE_ITEM_COUNT) {
            maxItemCount = DEFAULT_PIE_ITEM_COUNT;
        }
        if (maxItemCount < 2) {
            maxItemCount = 2;
        }

        range = buildChartRange(windowLength, granularity);
        logs = mongoTemplate.find(Query.query(Criteria.where("createTime").gte(range[0]).lt(range[1])), Meow.class);
        for (Meow log : logs) {
            String location = normalizeLocationLabel(log);
            locationCounts.put(location, locationCounts.getOrDefault(location, 0L) + 1);
        }

        sortedLocations = new ArrayList<>(locationCounts.entrySet());
        sortedLocations.sort(Map.Entry.<String, Long>comparingByValue().reversed());

        for (int i = 0; i < sortedLocations.size(); i++) {
            Map.Entry<String, Long> entry = sortedLocations.get(i);
            if (i < maxItemCount - 1) {
                voList.add(new PieChartCountVO(entry.getKey(), entry.getValue()));
            } else {
                otherSum += entry.getValue();
            }
        }

        if (otherSum > 0) {
            voList.add(new PieChartCountVO("其他", otherSum));
        }


        return ResponseResult.okResult(voList);
    }

    @GetMapping("/charts/emotions")
    public ResponseResult getEmotionChart(Integer windowLength, String granularity) {
        Date[] range = buildChartRange(windowLength, granularity);
        List<Meow> logs = mongoTemplate.find(Query.query(Criteria.where("createTime").gte(range[0]).lt(range[1])), Meow.class);
        Map<String, Long> emotionCounts = new HashMap<>();
        List<Map.Entry<String, Long>> sortedEmotions;
        List<PieChartCountVO> voList = new ArrayList<>();

        for (Meow log : logs) {
            String emotion = normalizeEmotionLabel(log);
            if (emotion == null) {
                continue;
            }
            emotionCounts.put(emotion, emotionCounts.getOrDefault(emotion, 0L) + 1);
        }

        sortedEmotions = new ArrayList<>(emotionCounts.entrySet());
        sortedEmotions.sort(Map.Entry.<String, Long>comparingByValue().reversed());
        for (Map.Entry<String, Long> entry : sortedEmotions) {
            voList.add(new PieChartCountVO(entry.getKey(), entry.getValue()));
        }

        return ResponseResult.okResult(voList);
    }

    @GetMapping("/charts/voiceprints")
    public ResponseResult getVoiceprintStats(Integer windowLength, String granularity) {
        Date[] range = buildChartRange(windowLength, granularity);
        List<Meow> logs = mongoTemplate.find(Query.query(Criteria.where("createTime").gte(range[0]).lt(range[1])), Meow.class);
        Map<String, Integer> voiceprintSamples = new HashMap<>();
        List<PieChartCountVO> clusters = new ArrayList<>();
        Map<String, Object> result = new HashMap<>();
        int sampleCount = 0;
        int stableCount = 0;

        for (Meow log : logs) {
            String clusterId = log.getCatClusterId();
            if (clusterId == null || clusterId.isBlank()) {
                continue;
            }
            sampleCount++;
            voiceprintSamples.put(clusterId, voiceprintSamples.getOrDefault(clusterId, 0) + 1);
        }

        for (Integer count : voiceprintSamples.values()) {
            if (count >= 3) {
                stableCount++;
            }
        }
        voiceprintSamples.entrySet().stream()
                .sorted(Map.Entry.<String, Integer>comparingByValue().reversed())
                .forEach(entry -> clusters.add(new PieChartCountVO(entry.getKey(), entry.getValue().longValue())));

        result.put("clusterCount", voiceprintSamples.size());
        result.put("sampleCount", sampleCount);
        result.put("stableCount", stableCount);
        result.put("clusters", clusters);
        return ResponseResult.okResult(result);
    }

    private Date[] buildChartRange(Integer windowLength, String granularity) {
        ZonedDateTime end;
        ZonedDateTime start;

        granularity = granularity == null ? "hour" : granularity.trim().toLowerCase();
        if ("ten-minute".equals(granularity)) {
            if (windowLength == null || windowLength < 1) {
                windowLength = DEFAULT_TEN_MINUTE_WINDOW_LENGTH;
            }
            if (windowLength > MAX_TEN_MINUTE_WINDOW_LENGTH) {
                windowLength = MAX_TEN_MINUTE_WINDOW_LENGTH;
            }
            end = floorToTenMinutes(LocalDateTime.now()).atZone(zoneId);
            start = end.minusMinutes((long) windowLength * 10);
        } else if ("day".equals(granularity)) {
            if (windowLength == null || windowLength < 1) {
                windowLength = DEFAULT_DAY_WINDOW_LENGTH;
            }
            if (windowLength > MAX_DAY_WINDOW_LENGTH) {
                windowLength = MAX_DAY_WINDOW_LENGTH;
            }
            end = LocalDateTime.now().toLocalDate().plusDays(1).atStartOfDay(zoneId);
            start = end.minusDays(windowLength);
        } else if ("month".equals(granularity)) {
            if (windowLength == null || windowLength < 1) {
                windowLength = DEFAULT_MONTH_WINDOW_LENGTH;
            }
            if (windowLength > MAX_MONTH_WINDOW_LENGTH) {
                windowLength = MAX_MONTH_WINDOW_LENGTH;
            }
            end = LocalDateTime.now().withDayOfMonth(1).toLocalDate().plusMonths(1).atStartOfDay(zoneId);
            start = end.minusMonths(windowLength);
        } else {
            if (windowLength == null || windowLength < 1) {
                windowLength = DEFAULT_WINDOW_LENGTH;
            }
            if (windowLength > MAX_WINDOW_LENGTH) {
                windowLength = MAX_WINDOW_LENGTH;
            }
            end = LocalDateTime.now().withMinute(0).withSecond(0).withNano(0).atZone(zoneId);
            start = end.minusHours(windowLength);
        }

        return new Date[]{Date.from(start.toInstant()), Date.from(end.toInstant())};
    }

    private String normalizeLocationLabel(Meow log) {
        String address = log.getAddress();
        Double latitude = log.getLatitude();
        Double longitude = log.getLongitude();

        if (address != null && !address.isBlank()) {
            return address;
        }
        if (latitude != null && longitude != null) {
            return String.format("%.3f, %.3f", latitude, longitude);
        }
        return "未知位置";
    }

    private String normalizeEmotionLabel(Meow log) {
        String emotionLabel = log.getEmotionLabel();

        if (emotionLabel != null && !emotionLabel.isBlank()) {
            return emotionLabel;
        }
        return null;
    }

    @GetMapping("/number/2448week")
    public ResponseResult getNumber2448week() {
        Instant now = Instant.now();
        Instant start24 = Instant.now().minus(24, ChronoUnit.HOURS);
        Instant start48 = Instant.now().minus(48, ChronoUnit.HOURS);
        Instant startWeek = Instant.now().minus(7, ChronoUnit.DAYS);

        NumberVO numberVO = new NumberVO();
        numberVO.setHours24((int) mongoTemplate.count(Query.query(Criteria.where("createTime").lt(now).gte(start24)), Meow.class));
        numberVO.setHours48((int) mongoTemplate.count(Query.query(Criteria.where("createTime").lt(now).gte(start48)), Meow.class));
        numberVO.setThisWeek((int) mongoTemplate.count(Query.query(Criteria.where("createTime").lt(now).gte(startWeek)), Meow.class));

        return ResponseResult.okResult(numberVO);
    }

    private final Integer DEFAULT_CNT_LOGS = 10;
    private final Integer MAX_CNT_LOGS = 50;
    private final Integer DEFAULT_LOG_PAGE = 1;
    private final Integer DEFAULT_LOG_PAGE_SIZE = 12;
    private final Integer MAX_LOG_PAGE_SIZE = 100;
    private final Double CAT_THRESHOLD = 0.8;

    @GetMapping("/logs")
    public ResponseResult getLogs(Integer cntLogs, Integer page, Integer pageSize) {
        if (page != null || pageSize != null) {
            if (page == null || page < 1) {
                page = DEFAULT_LOG_PAGE;
            }
            if (pageSize == null || pageSize < 1) {
                pageSize = DEFAULT_LOG_PAGE_SIZE;
            }
            if (pageSize > MAX_LOG_PAGE_SIZE) {
                pageSize = MAX_LOG_PAGE_SIZE;
            }

            Query countQuery = new Query();
            long total = mongoTemplate.count(countQuery, Meow.class);
            int totalPages = total == 0 ? 0 : (int) Math.ceil((double) total / pageSize);
            long skip = (long) (page - 1) * pageSize;

            List<Meow> logs = mongoTemplate.find(new Query()
                    .with(Sort.by(Sort.Direction.DESC, "createTime"))
                    .skip(skip)
                    .limit(pageSize), Meow.class);

            List<MeowLogsVO> records = new ArrayList<>();
            for (Meow log : logs) {
                records.add(mapMeowLog(log));
            }

            return ResponseResult.okResult(MeowLogsPageVO.builder()
                    .records(records)
                    .total(total)
                    .page(page)
                    .pageSize(pageSize)
                    .totalPages(totalPages)
                    .hasPrevious(page > 1 && totalPages > 0)
                    .hasNext(totalPages > 0 && page < totalPages)
                    .build());
        }

        if (cntLogs == null || cntLogs < 0) {
            cntLogs = DEFAULT_CNT_LOGS;
        }
        if (cntLogs > MAX_CNT_LOGS) {
            cntLogs = MAX_CNT_LOGS;
        }

        List<Meow> logs = mongoTemplate.find(new Query()
                .with(Sort.by(Sort.Direction.DESC, "createTime"))
                .limit(cntLogs), Meow.class);

        List<MeowLogsVO> res = new ArrayList<>();
        for (Meow log : logs) {
            res.add(mapMeowLog(log));
        }

        return ResponseResult.okResult(res);
    }

    private MeowLogsVO mapMeowLog(Meow log) {
        return MeowLogsVO.builder()
                .id(log.getId())
                .equipmentId(log.getEquipmentId())
                .lat(log.getLatitude())
                .lng(log.getLongitude())
                .confidence(log.getConfidence())
                .imageUrl(log.getImageUrl())
                .audioUrl(log.getAudioUrl())
                .audioDurationMs(log.getAudioDurationMs())
                .audioAnalysisStatus(log.getAudioAnalysisStatus())
                .catClusterId(log.getCatClusterId())
                .catClusterDistance(log.getCatClusterDistance())
                .catClusterSampleCount(log.getCatClusterSampleCount())
                .emotionStatus(log.getEmotionStatus())
                .emotionCode(log.getEmotionCode())
                .emotionLabel(log.getEmotionLabel())
                .emotionScore(log.getEmotionScore())
                .emotionScores(log.getEmotionScores())
                .emotionMessage(log.getEmotionMessage())
                .time(log.getCreateTime() == null ? null : log.getCreateTime()
                        .toInstant()
                        .atZone(zoneId)
                        .format(yyyyMMdd_HHmmss))
                .isCat(log.getConfidence() != null && log.getConfidence() >= CAT_THRESHOLD)
                .build();
    }

    @GetMapping("/cats/clusters")
    public ResponseResult getCatClusters(@RequestParam(defaultValue = "3") Integer stableSampleCount,
                                         @RequestParam(required = false) Integer windowMinutes) {
        List<Meow> logs;
        Criteria criteria;
        Map<String, ClusterAccumulator> clusters = new HashMap<>();
        List<CatClusterVO> result = new ArrayList<>();

        if (stableSampleCount == null || stableSampleCount < 1) {
            stableSampleCount = 3;
        }

        criteria = Criteria.where("catClusterId").exists(true).ne(null);
        if (windowMinutes != null && windowMinutes > 0) {
            criteria = new Criteria().andOperator(
                    criteria,
                    Criteria.where("createTime").gte(Date.from(Instant.now().minus(windowMinutes, ChronoUnit.MINUTES)))
            );
        }

        logs = mongoTemplate.find(Query.query(criteria), Meow.class);
        for (Meow log : logs) {
            if (log.getCatClusterId() == null || log.getCatClusterId().isBlank()) {
                continue;
            }
            clusters.computeIfAbsent(log.getCatClusterId(), ClusterAccumulator::new).add(log);
        }

        for (ClusterAccumulator cluster : clusters.values()) {
            result.add(CatClusterVO.builder()
                    .clusterId(cluster.clusterId)
                    .sampleCount(cluster.sampleCount)
                    .stable(cluster.sampleCount >= stableSampleCount)
                    .firstSeen(cluster.firstSeen)
                    .lastSeen(cluster.lastSeen)
                    .averageDistance(cluster.distanceCount == 0 ? null : cluster.distanceSum / cluster.distanceCount)
                    .build());
        }

        result.sort(Comparator.comparing(CatClusterVO::getSampleCount).reversed());
        return ResponseResult.okResult(result);
    }

    @PostMapping("/cats/rebuild-clusters")
    public ResponseResult rebuildCatClusters() {
        List<Meow> logs;
        int analyzed = 0;
        int failed = 0;
        int clustered = 0;

        if (!audioAnalysisService.resetClusters()) {
            return ResponseResult.errorResult(500, "audio analysis reset failed");
        }

        logs = mongoTemplate.find(Query.query(Criteria.where("audioUrl").exists(true).nin(null, ""))
                        .with(Sort.by(Sort.Direction.ASC, "createTime")),
                Meow.class);

        for (Meow log : logs) {
            AudioAnalysisResult result;
            Long timestamp;

            if (log.getAudioUrl() == null || log.getAudioUrl().isBlank()) {
                continue;
            }

            timestamp = log.getCreateTime() == null ? System.currentTimeMillis() : log.getCreateTime().getTime();
            result = audioAnalysisService.analyzeStoredAudio(log.getEquipmentId(), timestamp, log.getAudioUrl());
            analyzed++;

            if (result == null || !"ok".equalsIgnoreCase(result.getStatus())) {
                failed++;
                log.setAudioAnalysisStatus(result == null ? "no_result" : result.getStatus());
                mongoTemplate.save(log);
                continue;
            }

            log.setAudioAnalysisStatus(result.getStatus());
            log.setAudioEmbedding(result.getEmbedding());
            log.setAudioEnergy(result.getEnergy());
            log.setAudioZeroCrossingRate(result.getZeroCrossingRate());
            log.setCatClusterId(result.getClusterId());
            log.setCatClusterDistance(result.getClusterDistance());
            log.setCatClusterSampleCount(result.getClusterSampleCount());
            log.setEmotionStatus(result.getEmotionStatus());
            log.setEmotionCode(result.getEmotionCode());
            log.setEmotionLabel(result.getEmotionLabel());
            log.setEmotionScore(result.getEmotionScore());
            log.setEmotionScores(result.getEmotionScores());
            log.setEmotionMessage(result.getEmotionMessage());
            mongoTemplate.save(log);

            if (result.getClusterId() != null && !result.getClusterId().isBlank()) {
                clustered++;
            }
        }

        return ResponseResult.okResult(RebuildClustersVO.builder()
                .total(logs.size())
                .analyzed(analyzed)
                .failed(failed)
                .clustered(clustered)
                .build());
    }

    @PostMapping("/cats/clear-clusters")
    public ResponseResult clearCatClusters() {
        UpdateResult updateResult;
        Update update;
        Map<String, Object> result = new HashMap<>();

        if (!audioAnalysisService.resetClusters()) {
            return ResponseResult.errorResult(500, "audio analysis reset failed");
        }

        update = new Update()
                .unset("audioEmbedding")
                .unset("audioEnergy")
                .unset("audioZeroCrossingRate")
                .unset("catClusterId")
                .unset("catClusterDistance")
                .unset("catClusterSampleCount");
        updateResult = mongoTemplate.updateMulti(new Query(), update, Meow.class);

        result.put("matchedCount", updateResult.getMatchedCount());
        result.put("modifiedCount", updateResult.getModifiedCount());
        return ResponseResult.okResult(result);
    }

    private static class ClusterAccumulator {
        private final String clusterId;
        private int sampleCount;
        private double distanceSum;
        private int distanceCount;
        private Date firstSeen;
        private Date lastSeen;

        private ClusterAccumulator(String clusterId) {
            this.clusterId = clusterId;
        }

        private void add(Meow meow) {
            Date createTime = meow.getCreateTime();

            sampleCount++;
            if (meow.getCatClusterDistance() != null) {
                distanceSum += meow.getCatClusterDistance();
                distanceCount++;
            }
            if (createTime != null && (firstSeen == null || createTime.before(firstSeen))) {
                firstSeen = createTime;
            }
            if (createTime != null && (lastSeen == null || createTime.after(lastSeen))) {
                lastSeen = createTime;
            }
        }
    }
}




