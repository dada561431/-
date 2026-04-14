package com.zhijiangdiana.hachimitsu.controller;

import com.zhijiangdiana.hachimitsu.pojo.ResponseResult;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.bind.annotation.*;

/**
 * @Description 
 * @Author 嘉然今天吃向晚
 * @Date 2025/12/13-18:27:30
 */
@RestController
@RequestMapping("/rtc")
@Slf4j
public class RTCController {

    @GetMapping("/get")
    public ResponseResult getRTCDate() {
        return ResponseResult.okResult(System.currentTimeMillis());
    }

}
