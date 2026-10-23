# data/raw

The source dataset belongs here as a read-only copy:

```
data/raw/lost_found_dataset_realistic_metadata_30k.csv
SHA-256 117f92940977ef8855d03416b9ca5abd49023b74daa714da4437497614567730  (3,464,978 bytes)
```

It is **not committed to Git** because its licence is unverified (see `docs/DATASET.md`). To
restore it, put the downloaded file in the project root, then:

```sh
cp -p lost_found_dataset_realistic_metadata_30k.csv data/raw/
chmod a-w data/raw/lost_found_dataset_realistic_metadata_30k.csv
shasum -a 256 data/raw/lost_found_dataset_realistic_metadata_30k.csv   # must match the hash above
```

Never edit or overwrite this file; `findit_ingest` only reads it.
