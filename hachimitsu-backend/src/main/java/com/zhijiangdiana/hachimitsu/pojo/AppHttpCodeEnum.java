package com.zhijiangdiana.hachimitsu.pojo;

/**
 * @Description
 * @Author 嘉然今天吃向晚
 * @Date 2025/12/6-00:18:23
 */
public enum AppHttpCodeEnum {

    SUCCESS(200,"操作成功"),
    SERVER_ERROR(503,"服务器内部错误");

    int code;
    String errorMessage;

    AppHttpCodeEnum(int code, String errorMessage){
        this.code = code;
        this.errorMessage = errorMessage;
    }

    public int getCode() {
        return code;
    }

    public String getErrorMessage() {
        return errorMessage;
    }
}
