package com.zhijiangdiana.hachimitsu.controller;

import com.mongodb.client.MongoClient;
import com.zhijiangdiana.hachimitsu.pojo.*;
import org.bson.Document;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.data.domain.Sort;
import org.springframework.data.mongodb.core.MongoTemplate;
import org.springframework.data.mongodb.core.aggregation.Aggregation;
import org.springframework.data.mongodb.core.aggregation.AggregationResults;
import org.springframework.data.mongodb.core.query.Criteria;
import org.springframework.data.mongodb.core.query.Query;
import org.springframework.web.bind.annotation.GetMapping;
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
import java.util.Collections;
import java.util.Date;
import java.util.List;

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

    private final Integer DEFAULT_WINDOW_LENGTH = 8;

    private final Integer MAX_WINDOW_LENGTH = 24;

    private final ZoneId zoneId = ZoneId.of("Asia/Shanghai");
    @Autowired
    private MongoClient mongo;

    @GetMapping("/charts/line")
    public ResponseResult getLineChart(Integer windowLength) {
        // 鍙傛暟鏍￠獙
        if (windowLength == null || windowLength > MAX_WINDOW_LENGTH) {
            windowLength = DEFAULT_WINDOW_LENGTH;
        }

        // 鍒濆鍖栨椂闂?
        ZonedDateTime start = LocalDateTime.now().withMinute(0).withSecond(0).withNano(0).minusHours(1).atZone(zoneId);
        ZonedDateTime end = LocalDateTime.now().withMinute(0).withSecond(0).withNano(0).atZone(zoneId);

        // 渚濇鏌ユ壘鍑哄墠windowLength澶╃殑缁熻鍊?
        List<String> timeLine = new ArrayList<>();
        List<Integer> countLine = new ArrayList<>();
        for (int i = 0; i < windowLength; i++) {
            Date startTime = Date.from(start.toInstant());
            Date endTime = Date.from(end.toInstant());

            timeLine.add(end.format(formatter));
            countLine.add((int) mongoTemplate.count(Query.query(Criteria.where("createTime").lte(endTime).gt(startTime)), Meow.class));

            start = start.minusHours(1);
            end = end.minusHours(1);
        }

        // 灏嗘椂闂磋酱浠庡皬鍒板ぇ鎺?
        Collections.reverse(timeLine);
        Collections.reverse(countLine);

        LineChartVO lineChartVO = new LineChartVO();
        lineChartVO.setTimeLabels(timeLine);
        lineChartVO.setCounts(countLine);

        return ResponseResult.okResult(lineChartVO);
    }

    private final Integer MAX_PIE_ITEM_COUNT = 8;
    private final Integer DEFAULT_PIE_ITEM_COUNT = 4;

    @GetMapping("/charts/pie")
    public ResponseResult getPieChart(Integer maxItemCount, Integer windowLength) {
        // 鍙傛暟鏍￠獙
        if (maxItemCount == null || maxItemCount > MAX_PIE_ITEM_COUNT) {
            maxItemCount = DEFAULT_PIE_ITEM_COUNT;
        }
        // 鍙傛暟鏍￠獙
        if (windowLength == null || windowLength > MAX_WINDOW_LENGTH) {
            windowLength = DEFAULT_WINDOW_LENGTH;
        }

        Instant end = Instant.now();
        Instant start = end.minus(windowLength, ChronoUnit.HOURS);

        Aggregation aggregation = Aggregation.newAggregation(
                Aggregation.match(
                        Criteria.where("createTime").gte(start).lt(end)
                ),
                Aggregation.group("address").count().as("value"),
                Aggregation.sort(Sort.Direction.DESC, "value")
        );

        AggregationResults<Document> results =
                mongoTemplate.aggregate(aggregation, Meow.class, Document.class);

        List<Document> docs = results.getMappedResults();

        // 缁撴灉澶勭悊锛堝悎骞垛€滃叾浠栤€濓級
        List<PieChartCountVO> voList = new ArrayList<>();
        long otherSum = 0;

        for (int i = 0; i < docs.size(); i++) {
            Document doc = docs.get(i);
            String address = doc.getString("_id");
            long count = doc.getInteger("value");

            if (i < maxItemCount - 1) {
                voList.add(new PieChartCountVO(address, count));
            } else {
                otherSum += count;
            }
        }

        if (otherSum > 0) {
            voList.add(new PieChartCountVO("鍏朵粬", otherSum));
        }


        return ResponseResult.okResult(voList);
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
    private final Double CAT_THRESHOLD = 0.8;

    @GetMapping("/logs")
    public ResponseResult getLogs(Integer cntLogs) {
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
            MeowLogsVO vo = MeowLogsVO.builder()
                    .id(log.getId())
                    .equipmentId(log.getEquipmentId())
                    .lat(log.getLatitude())
                    .lng(log.getLongitude())
                    .confidence(log.getConfidence())
                    .imageUrl(log.getImageUrl())
                    .audioUrl(log.getAudioUrl())
                    .audioDurationMs(log.getAudioDurationMs())
                    .time(log.getCreateTime()
                            .toInstant()
                            .atZone(zoneId)
                            .format(yyyyMMdd_HHmmss))
                    .isCat(log.getConfidence() >= CAT_THRESHOLD)
                    .build();
            res.add(vo);
        }

        return ResponseResult.okResult(res);
    }
}




