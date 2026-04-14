package com.zhijiangdiana.hachimitsu.filter;

import jakarta.servlet.*;
import jakarta.servlet.http.HttpServletResponse;
import org.springframework.stereotype.Component;

import java.io.IOException;

@Component
public class ContentLengthFilter implements Filter {

    @Override
    public void doFilter(ServletRequest request, ServletResponse response, FilterChain chain)
            throws IOException, ServletException {

        HttpServletResponse httpRes = (HttpServletResponse) response;
        BufferedResponseWrapper wrapper = new BufferedResponseWrapper(httpRes);

        chain.doFilter(request, wrapper);

        byte[] body = wrapper.getBody();
        httpRes.setHeader("Content-Length", String.valueOf(body.length));
        httpRes.getOutputStream().write(body);
    }
}
