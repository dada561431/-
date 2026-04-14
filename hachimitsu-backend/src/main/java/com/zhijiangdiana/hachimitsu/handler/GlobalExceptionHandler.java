package com.zhijiangdiana.hachimitsu.handler;

import com.zhijiangdiana.hachimitsu.pojo.ResponseResult;
import lombok.extern.slf4j.Slf4j;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;

import java.sql.SQLIntegrityConstraintViolationException;

/**
 * 全局异常处理器，处理项目中抛出的业务异常
 */
@RestControllerAdvice
@Slf4j
public class GlobalExceptionHandler {

    @ExceptionHandler
    public ResponseResult exceptionHandler(SQLIntegrityConstraintViolationException e) {
        String message = e.getMessage();
        e.printStackTrace();
        log.error("异常信息：{}", message);

        return ResponseResult.errorResult(500, "Internal Server Error: " + message);
    }
}
