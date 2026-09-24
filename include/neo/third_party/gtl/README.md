Vendored GTL flat_hash_map headers from [greg7mdp/gtl v1.2.0](https://github.com/greg7mdp/gtl/tree/v1.2.0), commit `4c0a7a1d4a86143247b6ea7a5e4c7d6104d78590`.

Only phmap.hpp and its five local header dependencies are included. All six headers are unmodified; SHA256SUMS records their upstream bytes and LICENSE. The Apache-2.0 license and upstream copyright notices are retained.

Trie uses the single-threaded flat_hash_map. Copying include/neo or installing the library includes this dependency; no network download or additional include path is needed. Keep vendored headers out of project formatting.
