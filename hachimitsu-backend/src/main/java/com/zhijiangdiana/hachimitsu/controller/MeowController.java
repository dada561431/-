package com.zhijiangdiana.hachimitsu.controller;

import com.zhijiangdiana.hachimitsu.pojo.AddMeowDto;
import com.zhijiangdiana.hachimitsu.pojo.AttachMeowImageDto;
import com.zhijiangdiana.hachimitsu.pojo.ResponseResult;
import com.zhijiangdiana.hachimitsu.service.MeowService;
import jakarta.servlet.http.HttpServletRequest;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
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
}