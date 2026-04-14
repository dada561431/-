package com.zhijiangdiana.hachimitsu.utils;

import java.util.regex.Matcher;
import java.util.regex.Pattern;

public class JsonUtils {

    /**
     * 将
     * @param str
     * @return
     */
    public static String decodeUnicode(String str) {
        StringBuffer sb = new StringBuffer();
        Pattern pattern = Pattern.compile("\\\\u([0-9a-fA-F]{4})");
        Matcher matcher = pattern.matcher(str);

        while (matcher.find()) {
            String group = matcher.group(1); // 获取Unicode码
            char ch = (char) Integer.parseInt(group, 16); // 转换为字符
            matcher.appendReplacement(sb, String.valueOf(ch));
        }
        matcher.appendTail(sb);
        return sb.toString();
    }
}