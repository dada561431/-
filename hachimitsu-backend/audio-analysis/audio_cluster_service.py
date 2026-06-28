#!/usr/bin/env python3
"""
Small dependency-free audio clustering service for Hachimitsu.

Run:
    python audio_cluster_service.py

Java sends a WAV path to POST /analyze. This service extracts a compact
acoustic embedding and assigns the sample to the nearest online cluster.
"""

from __future__ import annotations

import cmath
import json
import math
import os
import socketserver
import threading
import wave
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any, Optional


HOST = os.environ.get("HACHIMITSU_AUDIO_HOST", "127.0.0.1")
PORT = int(os.environ.get("HACHIMITSU_AUDIO_PORT", "5055"))
CLUSTER_FILE = Path(os.environ.get(
    "HACHIMITSU_AUDIO_CLUSTER_FILE",
    str(Path(__file__).with_name("audio_clusters.json")),
))
CLUSTER_THRESHOLD = float(os.environ.get("HACHIMITSU_AUDIO_CLUSTER_THRESHOLD", "0.065"))
EXPECTED_CAT_COUNT = int(os.environ.get("HACHIMITSU_AUDIO_EXPECTED_CAT_COUNT", "3"))
FORCED_ASSIGN_UPDATE_THRESHOLD = float(os.environ.get(
    "HACHIMITSU_AUDIO_FORCED_ASSIGN_UPDATE_THRESHOLD",
    "0.18",
))
MAX_SAMPLES = int(os.environ.get("HACHIMITSU_AUDIO_MAX_SAMPLES", "48000"))
EMOTION_ENABLED = os.environ.get("HACHIMITSU_AUDIO_EMOTION_ENABLED", "true").lower() not in {
    "0",
    "false",
    "no",
    "off",
}
EMOTION_MODEL_NAME = os.environ.get("HACHIMITSU_AUDIO_EMOTION_MODEL", "laion/clap-htsat-unfused")
EMOTION_SAMPLE_RATE = int(os.environ.get("HACHIMITSU_AUDIO_EMOTION_SAMPLE_RATE", "48000"))
EMOTION_MAX_SECONDS = float(os.environ.get("HACHIMITSU_AUDIO_EMOTION_MAX_SECONDS", "3"))

EMOTION_LABELS = [
    {
        "code": "hungry",
        "label": "\u9965\u997f\u8ba8\u98df",
        "prompt": "a cat meowing repeatedly in a hungry way asking for food",
    },
    {
        "code": "relaxed",
        "label": "\u653e\u677e\u5b89\u9759",
        "prompt": "a relaxed cat purring softly while resting",
    },
    {
        "code": "irritated",
        "label": "\u70e6\u8e81\u4e0d\u6ee1",
        "prompt": "a cat making short sharp meows showing irritation",
    },
    {
        "code": "defensive",
        "label": "\u9632\u5fa1\u8b66\u6212",
        "prompt": "a cat hissing aggressively with defensive posture",
    },
    {
        "code": "frightened",
        "label": "\u6050\u60e7\u7d27\u5f20",
        "prompt": "a frightened cat making high-pitched anxious sounds",
    },
]

_lock = threading.Lock()
_emotion_lock = threading.Lock()
_emotion_model: Any | None = None
_emotion_processor: Any | None = None
_emotion_device: str | None = None
_emotion_import_error: str | None = None
_emotion_model_error: str | None = None


def read_wav_mono(path: Path) -> tuple[list[float], int]:
    with wave.open(str(path), "rb") as wav:
        channels = wav.getnchannels()
        sample_width = wav.getsampwidth()
        sample_rate = wav.getframerate()
        frames = min(wav.getnframes(), MAX_SAMPLES)
        data = wav.readframes(frames)

    if sample_width != 2:
        raise ValueError(f"only 16-bit PCM WAV is supported, got sample_width={sample_width}")

    samples: list[float] = []
    frame_width = channels * sample_width
    for offset in range(0, len(data), frame_width):
        values = []
        for channel in range(channels):
            start = offset + channel * sample_width
            raw = int.from_bytes(data[start:start + sample_width], "little", signed=True)
            values.append(raw / 32768.0)
        samples.append(sum(values) / len(values))

    return samples, sample_rate


def resample_linear(samples: list[float], source_rate: int, target_rate: int) -> list[float]:
    if source_rate == target_rate or not samples:
        return samples
    if source_rate <= 0 or target_rate <= 0:
        return samples

    output_length = max(1, int(round(len(samples) * target_rate / source_rate)))
    if output_length == 1:
        return [samples[0]]

    scale = (len(samples) - 1) / (output_length - 1)
    output: list[float] = []
    for index in range(output_length):
        position = index * scale
        left = int(math.floor(position))
        right = min(len(samples) - 1, left + 1)
        fraction = position - left
        output.append(samples[left] * (1.0 - fraction) + samples[right] * fraction)
    return output


def load_emotion_model() -> tuple[Any, Any, str]:
    global _emotion_import_error, _emotion_model, _emotion_model_error, _emotion_processor, _emotion_device

    with _emotion_lock:
        if _emotion_model is not None and _emotion_processor is not None and _emotion_device is not None:
            return _emotion_model, _emotion_processor, _emotion_device
        if _emotion_import_error:
            raise RuntimeError(_emotion_import_error)
        if _emotion_model_error:
            raise RuntimeError(_emotion_model_error)

        try:
            import torch  # type: ignore
            from transformers import ClapModel, ClapProcessor  # type: ignore
        except Exception as exc:
            _emotion_import_error = (
                "emotion dependencies are unavailable; install torch and transformers"
            )
            raise RuntimeError(_emotion_import_error) from exc

        try:
            _emotion_device = "cuda" if torch.cuda.is_available() else "cpu"
            _emotion_model = ClapModel.from_pretrained(EMOTION_MODEL_NAME)
            _emotion_processor = ClapProcessor.from_pretrained(EMOTION_MODEL_NAME)
            _emotion_model.to(_emotion_device)
            _emotion_model.eval()
            return _emotion_model, _emotion_processor, _emotion_device
        except Exception as exc:
            _emotion_model = None
            _emotion_processor = None
            _emotion_device = None
            _emotion_model_error = f"failed to load emotion model {EMOTION_MODEL_NAME}: {exc}"
            raise RuntimeError(_emotion_model_error) from exc


def emotion_unavailable(status: str, message: str) -> dict[str, Any]:
    return {
        "emotionStatus": status,
        "emotionMessage": message,
        "emotionScores": [],
    }


def predict_emotion(samples: list[float], sample_rate: int) -> dict[str, Any]:
    if not EMOTION_ENABLED:
        return emotion_unavailable("disabled", "emotion analysis is disabled")
    if not samples:
        return emotion_unavailable("no_audio", "audio file has no samples")

    try:
        model, processor, device = load_emotion_model()
        import torch  # type: ignore

        active_samples = trim_to_active_voice(samples, sample_rate)
        audio = resample_linear(active_samples, sample_rate, EMOTION_SAMPLE_RATE)
        max_samples = max(1, int(EMOTION_SAMPLE_RATE * EMOTION_MAX_SECONDS))
        if len(audio) > max_samples:
            audio = audio[:max_samples]

        inputs = processor(
            text=[item["prompt"] for item in EMOTION_LABELS],
            audio=audio,
            return_tensors="pt",
            padding=True,
            sampling_rate=EMOTION_SAMPLE_RATE,
        )
        inputs = {
            key: value.to(device) if hasattr(value, "to") else value
            for key, value in inputs.items()
        }

        with torch.no_grad():
            outputs = model(**inputs)
            probs = outputs.logits_per_audio.softmax(dim=-1).cpu().numpy()[0]

        scores = [
            {
                "code": label["code"],
                "label": label["label"],
                "prompt": label["prompt"],
                "score": float(probs[index]),
            }
            for index, label in enumerate(EMOTION_LABELS)
        ]
        scores.sort(key=lambda item: item["score"], reverse=True)
        top = scores[0]
        return {
            "emotionStatus": "ok",
            "emotionCode": top["code"],
            "emotionLabel": top["label"],
            "emotionScore": top["score"],
            "emotionScores": scores,
        }
    except RuntimeError as exc:
        return emotion_unavailable("unavailable", str(exc))
    except Exception as exc:
        return emotion_unavailable("error", str(exc))


def frame_audio(samples: list[float], frame_size: int, hop_size: int) -> list[list[float]]:
    if len(samples) < frame_size:
        padded = samples + [0.0] * (frame_size - len(samples))
        return [padded]

    frames = []
    for start in range(0, len(samples) - frame_size + 1, hop_size):
        frames.append(samples[start:start + frame_size])
    return frames


def hann(size: int) -> list[float]:
    return [0.5 - 0.5 * math.cos((2.0 * math.pi * i) / (size - 1)) for i in range(size)]


def fft(values: list[complex]) -> list[complex]:
    size = len(values)
    if size <= 1:
        return values
    even = fft(values[0::2])
    odd = fft(values[1::2])
    terms = [cmath.exp(-2j * math.pi * k / size) * odd[k] for k in range(size // 2)]
    return [even[k] + terms[k] for k in range(size // 2)] + [
        even[k] - terms[k] for k in range(size // 2)
    ]


def zero_crossing_rate(frame: list[float]) -> float:
    crossings = 0
    previous = frame[0]
    for sample in frame[1:]:
        if (previous >= 0.0 > sample) or (previous < 0.0 <= sample):
            crossings += 1
        previous = sample
    return crossings / max(1, len(frame) - 1)


def rms(frame: list[float]) -> float:
    return math.sqrt(sum(sample * sample for sample in frame) / max(1, len(frame)))


def mean_std(values: list[float]) -> tuple[float, float]:
    if not values:
        return 0.0, 0.0
    mean = sum(values) / len(values)
    variance = sum((value - mean) ** 2 for value in values) / len(values)
    return mean, math.sqrt(variance)


def median(values: list[float]) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2.0


def trim_to_active_voice(samples: list[float], sample_rate: int) -> list[float]:
    """Keep the energetic part of the 832 ms window so timing does not dominate identity."""
    if len(samples) < max(512, sample_rate // 10):
        return samples

    frame_size = max(160, int(sample_rate * 0.02))
    hop_size = max(80, frame_size // 2)
    frame_rms: list[float] = []
    positions: list[int] = []

    for start in range(0, max(1, len(samples) - frame_size + 1), hop_size):
        frame = samples[start:start + frame_size]
        if len(frame) < frame_size:
            break
        frame_rms.append(rms(frame))
        positions.append(start)

    if not frame_rms:
        return samples

    peak_energy = max(frame_rms)
    noise_floor = median(frame_rms)
    threshold = max(noise_floor * 2.5, peak_energy * 0.28, 0.00015)
    active = [positions[index] for index, value in enumerate(frame_rms) if value >= threshold]
    if not active:
        return samples

    padding = int(sample_rate * 0.08)
    start = max(0, active[0] - padding)
    end = min(len(samples), active[-1] + frame_size + padding)
    if end - start < frame_size:
        return samples
    return samples[start:end]


def hz_to_mel(frequency: float) -> float:
    return 2595.0 * math.log10(1.0 + frequency / 700.0)


def mel_to_hz(mel: float) -> float:
    return 700.0 * (10.0 ** (mel / 2595.0) - 1.0)


def dct(values: list[float], count: int) -> list[float]:
    size = len(values)
    if size == 0:
        return [0.0] * count
    return [
        math.sqrt(2.0 / size) * sum(
            values[index] * math.cos(math.pi * coeff * (2 * index + 1) / (2 * size))
            for index in range(size)
        )
        for coeff in range(count)
    ]


def build_mel_filterbank(sample_rate: int,
                         fft_size: int,
                         band_count: int,
                         min_hz: float = 80.0,
                         max_hz: Optional[float] = None) -> list[list[float]]:
    max_hz = max_hz or (sample_rate / 2.0)
    fft_bins = fft_size // 2
    min_mel = hz_to_mel(min_hz)
    max_mel = hz_to_mel(max_hz)
    mel_points = [
        min_mel + (max_mel - min_mel) * index / (band_count + 1)
        for index in range(band_count + 2)
    ]
    bin_points = [
        max(0, min(fft_bins - 1, int((fft_size + 1) * mel_to_hz(point) / sample_rate)))
        for point in mel_points
    ]
    filters: list[list[float]] = []

    for band in range(1, band_count + 1):
        left = bin_points[band - 1]
        center = max(bin_points[band], left + 1)
        right = max(bin_points[band + 1], center + 1)
        weights = [0.0] * fft_bins

        for index in range(left, min(center, fft_bins)):
            weights[index] = (index - left) / max(1, center - left)
        for index in range(center, min(right, fft_bins)):
            weights[index] = (right - index) / max(1, right - center)

        filters.append(weights)

    return filters


def l2_normalize(values: list[float]) -> list[float]:
    norm = math.sqrt(sum(value * value for value in values))
    if norm <= 1e-12:
        return values
    return [value / norm for value in values]


def extract_embedding(samples: list[float], sample_rate: int) -> dict[str, Any]:
    samples = trim_to_active_voice(samples, sample_rate)
    frame_size = 1024
    hop_size = 512
    mel_band_count = 32
    mfcc_count = 14
    window = hann(frame_size)
    frames = frame_audio(samples, frame_size, hop_size)
    mel_filters = build_mel_filterbank(sample_rate, frame_size, mel_band_count)

    rms_values: list[float] = []
    zcr_values: list[float] = []
    centroid_values: list[float] = []
    bandwidth_values: list[float] = []
    rolloff_values: list[float] = []
    mfcc_values: list[list[float]] = [[] for _ in range(mfcc_count)]

    for frame in frames:
        rms_values.append(rms(frame))
        zcr_values.append(zero_crossing_rate(frame))

    for frame in frames:
        windowed = [complex(frame[i] * window[i], 0.0) for i in range(frame_size)]
        spectrum = fft(windowed)[:frame_size // 2]
        magnitudes = [abs(value) for value in spectrum]
        powers = [value * value for value in magnitudes]
        if not magnitudes:
            continue
        nyquist = sample_rate / 2.0
        total_power = sum(powers) or 1e-12
        frequencies = [(index * nyquist) / len(magnitudes) for index in range(len(magnitudes))]
        centroid = sum(frequencies[index] * powers[index] for index in range(len(powers))) / total_power
        bandwidth = math.sqrt(
            sum(((frequencies[index] - centroid) ** 2) * powers[index] for index in range(len(powers))) / total_power
        )
        cumulative = 0.0
        rolloff = nyquist
        for index, power in enumerate(powers):
            cumulative += power
            if cumulative >= total_power * 0.85:
                rolloff = frequencies[index]
                break

        mel_energies = [
            sum(powers[index] * weights[index] for index in range(len(weights)))
            for weights in mel_filters
        ]
        coeffs = dct([math.log(max(energy, 1e-12)) for energy in mel_energies], mfcc_count)

        centroid_values.append(centroid / nyquist)
        bandwidth_values.append(bandwidth / nyquist)
        rolloff_values.append(rolloff / nyquist)
        for index, coeff in enumerate(coeffs):
            mfcc_values[index].append(coeff)

    rms_mean, rms_deviation = mean_std(rms_values)
    zcr_mean, zcr_deviation = mean_std(zcr_values)
    peak = max((abs(sample) for sample in samples), default=0.0)
    centroid_mean, centroid_deviation = mean_std(centroid_values)
    bandwidth_mean, bandwidth_deviation = mean_std(bandwidth_values)
    rolloff_mean, rolloff_deviation = mean_std(rolloff_values)
    mfcc_features: list[float] = []

    # Coefficients 1..13 carry timbre better than coefficient 0, which mostly tracks loudness.
    for coeff_series in mfcc_values[1:]:
        mean, deviation = mean_std(coeff_series)
        mfcc_features.extend([
            math.tanh(mean / 18.0),
            math.tanh(deviation / 8.0),
        ])

    raw_features = [
        rms_mean * 10.0,
        rms_deviation * 10.0,
        zcr_mean * 5.0,
        zcr_deviation * 5.0,
        peak,
        centroid_mean,
        centroid_deviation,
        bandwidth_mean,
        bandwidth_deviation,
        rolloff_mean,
        rolloff_deviation,
    ] + mfcc_features

    return {
        "embedding": l2_normalize(raw_features),
        "energy": rms_mean,
        "zeroCrossingRate": zcr_mean,
    }


def split_into_chunks(samples: list[float], count: int) -> list[list[float]]:
    if count <= 1 or not samples:
        return [samples]
    chunk_size = max(1, len(samples) // count)
    chunks = []
    for index in range(count):
        start = index * chunk_size
        end = len(samples) if index == count - 1 else min(len(samples), (index + 1) * chunk_size)
        chunks.append(samples[start:end] or [0.0])
    return chunks


def cosine_distance(left: list[float], right: list[float]) -> float:
    length = min(len(left), len(right))
    if length == 0:
        return 1.0
    similarity = sum(left[i] * right[i] for i in range(length))
    similarity = max(-1.0, min(1.0, similarity))
    return 1.0 - similarity


def load_clusters() -> dict[str, Any]:
    if not CLUSTER_FILE.exists():
        return {"nextId": 1, "clusters": []}
    with CLUSTER_FILE.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def save_clusters(data: dict[str, Any]) -> None:
    CLUSTER_FILE.parent.mkdir(parents=True, exist_ok=True)
    with CLUSTER_FILE.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, ensure_ascii=True, indent=2)


def assign_cluster(embedding: list[float], equipment_id: str | None) -> dict[str, Any]:
    with _lock:
        data = load_clusters()
        clusters = data.setdefault("clusters", [])

        best_cluster = None
        best_distance = 1.0
        for cluster in clusters:
            if equipment_id and cluster.get("equipmentId") not in (None, equipment_id):
                continue
            distance = cosine_distance(embedding, cluster["centroid"])
            if distance < best_distance:
                best_distance = distance
                best_cluster = cluster

        can_create_cluster = EXPECTED_CAT_COUNT <= 0 or len(clusters) < EXPECTED_CAT_COUNT
        forced_assignment = False

        if best_cluster is None or (best_distance > CLUSTER_THRESHOLD and can_create_cluster):
            cluster_id = f"cat-{data.get('nextId', 1):03d}"
            data["nextId"] = data.get("nextId", 1) + 1
            best_cluster = {
                "id": cluster_id,
                "equipmentId": equipment_id,
                "centroid": embedding,
                "sampleCount": 1,
            }
            clusters.append(best_cluster)
            best_distance = 0.0
        else:
            forced_assignment = best_distance > CLUSTER_THRESHOLD
            should_update = (
                not forced_assignment
                or best_distance <= FORCED_ASSIGN_UPDATE_THRESHOLD
            )
            if should_update:
                count = int(best_cluster.get("sampleCount", 1))
                centroid = best_cluster["centroid"]
                updated = [
                    ((centroid[i] * count) + embedding[i]) / (count + 1)
                    for i in range(min(len(centroid), len(embedding)))
                ]
                best_cluster["centroid"] = l2_normalize(updated)
                best_cluster["sampleCount"] = count + 1

        save_clusters(data)

        return {
            "clusterId": best_cluster["id"],
            "clusterDistance": best_distance,
            "clusterSampleCount": best_cluster["sampleCount"],
            "forcedAssignment": forced_assignment,
        }


def analyze(payload: dict[str, Any]) -> dict[str, Any]:
    audio_path = Path(str(payload.get("audioPath", ""))).expanduser()
    equipment_id = payload.get("equipmentId")

    if not audio_path.exists():
        raise FileNotFoundError(f"audio file not found: {audio_path}")

    samples, sample_rate = read_wav_mono(audio_path)
    if not samples:
        raise ValueError("audio file has no samples")

    features = extract_embedding(samples, sample_rate)
    cluster = assign_cluster(features["embedding"], equipment_id)
    emotion = predict_emotion(samples, sample_rate)

    return {
        "status": "ok",
        "embedding": features["embedding"],
        "energy": features["energy"],
        "zeroCrossingRate": features["zeroCrossingRate"],
        "clusterThreshold": CLUSTER_THRESHOLD,
        "expectedCatCount": EXPECTED_CAT_COUNT,
        **cluster,
        **emotion,
    }


class Handler(BaseHTTPRequestHandler):
    def do_POST(self) -> None:
        if self.path == "/reset":
            try:
                reset_clusters()
                self.send_json(200, {"status": "ok"})
            except Exception as exc:
                self.send_json(200, {"status": "error", "message": str(exc)})
            return

        if self.path != "/analyze":
            self.send_json(404, {"status": "not_found"})
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
            payload = json.loads(self.rfile.read(length).decode("utf-8"))
            self.send_json(200, analyze(payload))
        except Exception as exc:  # Keep Java upload flow alive by returning JSON errors.
            self.send_json(200, {"status": "error", "message": str(exc)})

    def log_message(self, format: str, *args: Any) -> None:
        return

    def send_json(self, status: int, payload: dict[str, Any]) -> None:
        body = json.dumps(payload, ensure_ascii=True).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


class SafeThreadingHTTPServer(ThreadingHTTPServer):
    def server_bind(self) -> None:
        socketserver.TCPServer.server_bind(self)
        host, port = self.server_address[:2]
        self.server_name = str(host)
        self.server_port = int(port)


def reset_clusters() -> None:
    with _lock:
        save_clusters({"nextId": 1, "clusters": []})


def main() -> None:
    server = SafeThreadingHTTPServer((HOST, PORT), Handler)
    print(f"Hachimitsu audio clustering service listening on http://{HOST}:{PORT}")
    print(f"Cluster file: {CLUSTER_FILE}")
    print(f"Cluster threshold: {CLUSTER_THRESHOLD}")
    print(f"Expected cat count: {EXPECTED_CAT_COUNT if EXPECTED_CAT_COUNT > 0 else 'unlimited'}")
    print(f"Emotion analysis: {'enabled' if EMOTION_ENABLED else 'disabled'}")
    if EMOTION_ENABLED:
        print(f"Emotion model: {EMOTION_MODEL_NAME}")
    server.serve_forever()


if __name__ == "__main__":
    main()
