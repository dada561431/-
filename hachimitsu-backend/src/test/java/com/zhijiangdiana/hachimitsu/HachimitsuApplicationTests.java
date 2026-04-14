package com.zhijiangdiana.hachimitsu;

import com.zhijiangdiana.hachimitsu.pojo.AddressDto;
import com.zhijiangdiana.hachimitsu.service.AddressService;
import org.junit.jupiter.api.Test;
import org.springframework.boot.test.context.SpringBootTest;

import java.io.IOException;

@SpringBootTest(webEnvironment = SpringBootTest.WebEnvironment.RANDOM_PORT)
class HachimitsuApplicationTests {

    @Test
    void addressServiceTest() throws IOException {
        long start = System.currentTimeMillis();
        AddressService addressService = new AddressService();
        AddressDto addressByIP = addressService.getAddressByIP(null);
        long end = System.currentTimeMillis();
        System.out.println(addressByIP);
        System.out.println(end - start);
    }

}
