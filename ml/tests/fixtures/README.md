# Test fixtures

`SYNTHETIC_matching_fixture.csv` is **synthetic**: hand-written for unit tests. It is not taken
from or derived from the real dataset. Its `synthetic_expected_match` column is a test label
invented to check that obvious pairs rank first. It says nothing about real-world accuracy, and
results on it must not be reported as model accuracy.

The real dataset (`data/raw/…30k.csv`) contains no matching labels.
