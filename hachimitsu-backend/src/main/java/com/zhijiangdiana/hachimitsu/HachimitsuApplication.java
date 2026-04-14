package com.zhijiangdiana.hachimitsu;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.scheduling.annotation.EnableAsync;

@EnableAsync
@SpringBootApplication
public class HachimitsuApplication {

    public static void main(String[] args) {
        SpringApplication.run(HachimitsuApplication.class, args);
    }

}
