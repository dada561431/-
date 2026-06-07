# Hachimitsu Audio Analysis Service

This is the Python side of the Java + Python audio clustering flow.

Start it before running the Spring Boot backend:

```powershell
cd D:\mtw\hachimitsu-backend\audio-analysis
python .\audio_cluster_service.py
```

The service listens on `127.0.0.1:5055` and exposes:

```text
POST /analyze
POST /reset
```

Java sends the local WAV path after `/meow/attach-audio` stores the uploaded PCM as a WAV file. The Python service extracts a compact acoustic embedding and assigns the sound to an online cluster such as `cat-001`.

`POST /reset` clears the in-memory/on-disk cluster state so the Java backend can rebuild clusters from historical audio in chronological order.

Environment variables:

```text
HACHIMITSU_AUDIO_HOST=127.0.0.1
HACHIMITSU_AUDIO_PORT=5055
HACHIMITSU_AUDIO_CLUSTER_FILE=D:\mtw\hachimitsu-backend\audio-analysis\audio_clusters.json
HACHIMITSU_AUDIO_CLUSTER_THRESHOLD=0.065
HACHIMITSU_AUDIO_EXPECTED_CAT_COUNT=3
HACHIMITSU_AUDIO_FORCED_ASSIGN_UPDATE_THRESHOLD=0.18
```

The default configuration is tuned for the current household setup with three cats. `HACHIMITSU_AUDIO_EXPECTED_CAT_COUNT=3` prevents a few noisy or shifted windows from creating many extra pseudo-cats. Set it to `0` if the system should discover an unlimited number of clusters.

This dependency-free version trims each WAV to its active voice region before extracting features, so silence and the position of the meow inside the 832 ms upload window do not dominate clustering. Later, the feature extractor can be upgraded to `librosa`, `scikit-learn`, or a pretrained audio embedding model without changing the board upload protocol.
