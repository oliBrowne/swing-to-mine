# data/

Recorded gesture sessions. Format and rules are in [../docs/architecture.md](../docs/architecture.md#recorded-data); the collection plan is in [../docs/gesture-notes.md](../docs/gesture-notes.md#data-collection-plan).

```
data/raw/<person_id>/<session_id>.csv          t_ms,ax,ay,az,gx,gy,gz (raw int16 counts)
data/raw/<person_id>/<session_id>.labels.csv   start_ms,end_ms,label
```

## Rules

- **Everyone records**, always with the sword handle on and the same physical sensor.
- People are `P01`, `P02`, … Keep the name mapping out of the repo.
- Only commit data from people who agreed to it being in the repo.
- Never edit raw files by hand; fix labels with the label tool or a new labels file.
- Labels must be one of the classes in [../training/labels.py](../training/labels.py).

## Recorded so far

| Person | Sessions | Minutes | Notes |
|---|---|---|---|
| | | | |
