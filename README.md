# Hachimitsu

Hachimitsu 是一个面向猫叫检测、事件采集、音频声纹聚类和数据可视化的端云协同项目。系统由 PSOC Edge 端侧检测工程和 Spring Boot 后端服务组成：端侧负责实时检测猫叫并上传音频、图像和事件数据，后端负责数据接收、存储、分析、聚类和看板展示。

## 项目特性

- 基于 PSOC Edge MCU 的端侧猫叫检测。
- 设备端通过 HTTPS 上传猫叫事件、PCM 音频和摄像头图像。
- Spring Boot 后端提供事件管理、文件存储、统计查询和 Web 看板。
- Python 音频分析服务对上传音频进行声纹聚类。
- 支持可选 CLAP 模型进行猫叫情绪倾向分析。
- 内置数据看板展示趋势、地点分布、情绪统计、声纹簇和日志。

## 项目结构

```text
.
+-- hachimitsu-backend/              # 后端服务、Web 看板、音频分析服务
+-- PSOC_Edge_Hachimitsu_Detector/   # PSOC Edge 端侧猫叫检测与数据上传工程
```

## 系统架构

```text
PSOC Edge MCU
  |
  | HTTPS: event / audio / image
  v
Spring Boot Backend
  |
  | local HTTP: WAV path
  v
Python Audio Analysis Service
  |
  v
MongoDB / Redis / Web Dashboard
```

## 后端服务

后端位于 `hachimitsu-backend`，使用 Java 17、Spring Boot、MongoDB 和 Redis。

主要模块：

- `HachimitsuApplication.java`：Spring Boot 应用入口。
- `MeowController.java`：接收设备端猫叫事件、图片和 PCM 音频。
- `StatisticController.java`：提供趋势、日志、情绪和声纹聚类统计接口。
- `RTCController.java`：为设备端提供时间同步接口。
- `AudioStorageServiceImpl.java`：将上传的 PCM 音频保存为 WAV 文件。
- `AudioAnalysisServiceImpl.java`：调用 Python 音频分析服务。
- `src/main/resources/static/index.html`：内置 Web 数据看板。

### 环境要求

- JDK 17
- MongoDB，默认 `127.0.0.1:27017`
- Redis，默认 `127.0.0.1:6379`
- Python 3.9+，用于音频声纹聚类服务

### 配置

默认配置位于：

```text
hachimitsu-backend/src/main/resources/application.yml
```

关键配置：

- 后端端口：`8080`
- MongoDB 数据库：`hachimitsu`
- Redis 地址：`127.0.0.1:6379`
- 上传目录：`D:/mtw/hachimitsu-backend/uploads`
- 音频分析服务：`http://127.0.0.1:5055/analyze`

部署到新环境前，请根据实际路径和服务地址调整 `application.yml`。

### 启动音频分析服务

```powershell
cd hachimitsu-backend\audio-analysis
python .\audio_cluster_service.py
```

服务默认监听：

```text
http://127.0.0.1:5055
```

接口：

- `POST /analyze`：分析 WAV 文件并返回声纹簇、声学特征和情绪结果。
- `POST /reset`：重置聚类状态，用于重新构建历史声纹簇。

如需启用 CLAP 情绪识别，需要安装额外依赖：

```powershell
pip install torch transformers
```

### 启动后端

```powershell
cd hachimitsu-backend
.\mvnw.cmd spring-boot:run
```

启动后访问：

```text
https://127.0.0.1:8080/
```

## 端侧工程

端侧工程位于 `PSOC_Edge_Hachimitsu_Detector`，基于 Infineon PSOC Edge MCU 示例改造，包含 CM33 secure、CM33 non-secure 和 CM55 三工程结构。

主要模块：

- `proj_cm55/audio.c`：麦克风音频采集、窗口处理、模型推理和高置信上传触发。
- `proj_cm55/model/`：猫叫检测模型文件和 TFLite 模型。
- `proj_cm33_ns/service.c`：上传猫叫事件、PCM 音频和摄像头图像。
- `proj_cm33_ns/secure_http_client.*`：HTTPS 客户端封装。
- `shared/`：CM33 与 CM55 间共享内存消息结构。

端侧工程需要 ModusToolbox 3.6+ 和 PSOC Edge E84 开发板环境。更详细的端侧构建和烧录说明见：

- `PSOC_Edge_Hachimitsu_Detector/README.md`
- `PSOC_Edge_Hachimitsu_Detector/docs/design_and_implementation.md`
- `PSOC_Edge_Hachimitsu_Detector/docs/using_the_code_example.md`

## 数据流程

1. CM55 采集麦克风音频并运行猫叫检测模型。
2. 检测到高置信猫叫后，通过共享内存将事件传递给 CM33 non-secure 工程。
3. CM33 non-secure 工程通过 HTTPS 上传事件、PCM 音频和摄像头图像。
4. 后端保存事件数据、图片和 WAV 音频文件。
5. 后端调用 Python 音频分析服务完成声纹聚类和可选情绪分析。
6. Web 看板展示趋势、日志、情绪统计和猫声纹簇结果。

## 主要接口

### 设备上传接口

- `POST /meow/add`：上传猫叫事件基础信息。
- `POST /meow/attach-image`：绑定图片。
- `POST /meow/attach-yuyv`：上传 YUYV 原始图像。
- `POST /meow/attach-audio`：上传 `PCM_S16LE` 音频并触发分析。
- `GET /rtc/get`：设备时间同步。

### 看板统计接口

- `GET /api/charts/line`：按小时、天或月统计猫叫数量。
- `GET /api/charts/pie`：统计地点分布。
- `GET /api/charts/emotions`：统计音频情绪分类。
- `GET /api/charts/voiceprints`：统计声纹簇。
- `GET /api/logs`：分页查看猫叫日志。
- `GET /api/cats/clusters`：查看猫声纹聚类结果。
- `POST /api/cats/rebuild-clusters`：按历史音频重建声纹聚类。

## 技术栈

- Java 17
- Spring Boot
- MongoDB
- Redis
- Python
- TensorFlow Lite Micro
- Infineon PSOC Edge MCU
- ModusToolbox

## 说明

当前项目包含端侧检测、后端服务、音频声纹聚类和数据看板的完整链路。实际部署时需要先启动 MongoDB、Redis 和 Python 音频分析服务，再启动 Spring Boot 后端，并确保端侧工程中的服务地址、证书和设备配置与后端环境一致。
