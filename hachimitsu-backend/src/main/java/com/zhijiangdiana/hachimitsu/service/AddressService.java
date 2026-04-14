package com.zhijiangdiana.hachimitsu.service;

import com.alibaba.fastjson.JSON;
import com.alibaba.fastjson.JSONObject;
import com.zhijiangdiana.hachimitsu.pojo.AddressDto;
import com.zhijiangdiana.hachimitsu.utils.JsonUtils;
import lombok.Getter;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;
import org.apache.tomcat.util.http.fileupload.IOUtils;
import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.stereotype.Component;
import org.springframework.web.util.UriUtils;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.net.URL;
import java.net.URLConnection;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.Map;

@Getter
@Setter
@Slf4j
@Component
@ConfigurationProperties(prefix = "baidu")
public class AddressService {

    private String ak;
    private String url;

    /**
     * 默认ak
     * 选择了ak，使用IP白名单校验：
     * 根据您选择的AK已为您生成调用代码
     * 检测到您当前的ak设置了IP白名单校验
     * 您的IP白名单中的IP非公网IP，请设置为公网IP，否则将请求失败
     * 请在IP地址为的计算发起请求，否则将请求失败
     */
    public AddressDto getAddressByIP(String ip) throws IOException {
        Map<String, String> params = new LinkedHashMap<>();
        params.put("ip", ip);
        params.put("coor", "bd09ll");
        params.put("ak", this.ak);

        StringBuffer queryString = new StringBuffer();
        queryString.append(this.url).append("?");
        for (Map.Entry<?, ?> pair : params.entrySet()) {
            queryString.append(pair.getKey() + "=");
            //    第一种方式使用的 jdk 自带的转码方式  第二种方式使用的 spring 的转码方法 两种均可
            //    queryString.append(URLEncoder.encode((String) pair.getValue(), "UTF-8").replace("+", "%20") + "&");
            queryString.append(UriUtils.encode((String) pair.getValue(), "UTF-8") + "&");
        }

        if (queryString.length() > 0) {
            queryString.deleteCharAt(queryString.length() - 1);
        }

        URL url = new URL(queryString.toString());
//        System.out.println(queryString);
        URLConnection httpConnection = url.openConnection();
        httpConnection.setRequestProperty("Content-type", "text/html");
        httpConnection.setRequestProperty("Accept-Charset", "UTF-8");
        httpConnection.setRequestProperty("contentType", "UTF-8");

        httpConnection.connect();
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        IOUtils.copy(httpConnection.getInputStream(), baos);
        String resp = baos.toString(StandardCharsets.UTF_8);
        resp = JsonUtils.decodeUnicode(resp);

//        System.out.println("AK: " + resp);
        // 组装dto
        AddressDto addressDto = new AddressDto();
        try {
            JSONObject jsonObject = JSON.parseObject(resp);
            if (jsonObject == null) {
                log.warn("Failed to parse IP lookup response. ip={}, response={}", ip, resp);
                return null;
            }

            JSONObject content = jsonObject.getJSONObject("content");
            if (content == null) {
                log.warn("IP lookup response missing content. ip={}, response={}", ip, resp);
                return null;
            }

            addressDto.setAddress(content.getString("address"));

            JSONObject point = content.getJSONObject("point");
            if (point == null) {
                log.warn("IP lookup response missing point. ip={}, response={}", ip, resp);
                return null;
            }

            addressDto.setLongitude(point.getDouble("x"));
            addressDto.setLatitude(point.getDouble("y"));
        } catch (Exception e) {
            log.warn("Failed to resolve address from IP lookup response. ip={}, response={}", ip, resp, e);
            return null;
        }

        return addressDto;
    }
}
