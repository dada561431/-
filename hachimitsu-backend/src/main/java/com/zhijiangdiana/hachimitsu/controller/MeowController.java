package com.zhijiangdiana.hachimitsu.controller;

import com.zhijiangdiana.hachimitsu.pojo.AddMeowDto;
import com.zhijiangdiana.hachimitsu.pojo.AttachMeowImageDto;
import com.zhijiangdiana.hachimitsu.pojo.ResponseResult;
import com.zhijiangdiana.hachimitsu.service.MeowService;
import jakarta.servlet.http.HttpServletRequest;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.http.MediaType;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestParam;
import org.springframework.web.bind.annotation.RestController;

@Slf4j
@RestController
@RequestMapping("/meow")
public class MeowController {

    @Autowired
    private MeowService meowService;

    @PostMapping("/add")
    public ResponseResult add(@RequestBody AddMeowDto dto, HttpServletRequest request) {
        meowService.saveMeowLog(dto, request.getRemoteAddr());
        return ResponseResult.okResult();
    }

    @PostMapping("/attach-image")
    public ResponseResult attachImage(@RequestBody AttachMeowImageDto dto) {
        if (meowService.attachMeowImage(dto)) {
            return ResponseResult.okResult();
        }
        return ResponseResult.errorResult(500, "image attach failed");
    }

    @PostMapping(value = "/attach-yuyv", consumes = MediaType.APPLICATION_OCTET_STREAM_VALUE)
    public ResponseResult attachYuyvImage(@RequestParam String equipmentId,
                                          @RequestParam Long timestamp,
                                          @RequestParam Integer width,
                                          @RequestParam Integer height,
                                          @RequestParam(required = false) Integer sourceStride,
                                          @RequestParam(defaultValue = "YUYV") String pixelFormat,
                                          @RequestBody byte[] imageBytes) {
        if (!"YUYV".equalsIgnoreCase(pixelFormat) && !"YUY2".equalsIgnoreCase(pixelFormat)) {
            return ResponseResult.errorResult(400, "unsupported pixel format");
        }

        if (meowService.attachRawYuyvImage(equipmentId, timestamp, imageBytes, width, height, sourceStride)) {
            return ResponseResult.okResult();
        }
        return ResponseResult.errorResult(500, "raw image attach failed");
    }

    @PostMapping(value = "/attach-audio", consumes = MediaType.APPLICATION_OCTET_STREAM_VALUE)
    public ResponseResult attachAudio(@RequestParam String equipmentId,
                                      @RequestParam Long timestamp,
                                      @RequestParam Integer sampleRate,
                                      @RequestParam(defaultValue = "16") Integer bitsPerSample,
                                      @RequestParam(defaultValue = "1") Integer channelCount,
                                      @RequestParam(required = false) Integer sampleCount,
                                      @RequestParam(defaultValue = "PCM_S16LE") String format,
                                      @RequestBody byte[] audioBytes) {
        if (!"PCM_S16LE".equalsIgnoreCase(format)) {
            return ResponseResult.errorResult(400, "unsupported audio format");
        }

        if (meowService.attachPcmAudio(equipmentId,
                timestamp,
                audioBytes,
                sampleRate,
                bitsPerSample,
                channelCount,
                sampleCount)) {
            return ResponseResult.okResult();
        }
        return ResponseResult.errorResult(500, "audio attach failed");
    }
}
