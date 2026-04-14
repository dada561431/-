package com.zhijiangdiana.hachimitsu.filter;

import jakarta.servlet.ServletOutputStream;
import jakarta.servlet.WriteListener;
import jakarta.servlet.http.HttpServletResponse;
import jakarta.servlet.http.HttpServletResponseWrapper;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;

public class BufferedResponseWrapper extends HttpServletResponseWrapper {

        private ByteArrayOutputStream buffer = new ByteArrayOutputStream();

        private ServletOutputStream out = new ServletOutputStream() {
            @Override
            public void write(int b) throws IOException {
                buffer.write(b);
            }

            @Override
            public boolean isReady() {
                // 这里返回 true 表示随时可写
                return true;
            }

            @Override
            public void setWriteListener(WriteListener writeListener) {
                // 简单实现，忽略异步通知
            }
        };


        private PrintWriter writer = new PrintWriter(new OutputStreamWriter(buffer));

        public BufferedResponseWrapper(HttpServletResponse response) {
            super(response);
        }

        @Override
        public ServletOutputStream getOutputStream() {
            return out;
        }

        @Override
        public PrintWriter getWriter() {
            return writer;
        }

        public byte[] getBody() {
            writer.flush();
            return buffer.toByteArray();
        }
    }