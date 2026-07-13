# Third-party notices

The first-party dsa-solver source is licensed under Apache License 2.0. Third-party source and
submodules retain their own licenses.

## Apache TVM

`TvmHillClimbSolver` is a behavioral reimplementation of the graph-guided ordering policy in Apache
TVM Unified Static Memory Planning. The primary reference is
[`hill_climb.cc` at TVM v0.14.0](https://github.com/apache/tvm/blob/v0.14.0/src/tir/usmp/algo/hill_climb.cc).
Apache TVM is licensed under Apache License 2.0. The applicable attribution is also retained in
[`NOTICE`](NOTICE).

## Google MiniMalloc

`third_party/minimalloc` pins the official Google MiniMalloc repository. MiniMalloc is licensed under
Apache License 2.0; its complete license remains at `third_party/minimalloc/LICENSE`. The optional
`dsa-suite` exact baseline compiles MiniMalloc's unmodified core sources from that submodule.

MiniMalloc uses Abseil, pinned at `third_party/minimalloc/external/abseil-cpp`. Abseil is licensed under
Apache License 2.0; its complete license remains in that submodule. Installed distributions include
copies as `LICENSE.minimalloc` and `LICENSE.abseil-cpp`.

## JSON for Modern C++

`third_party/json` pins nlohmann/json. It is licensed under the MIT License; its complete license
remains at `third_party/json/LICENSE.MIT` and is installed as `LICENSE.nlohmann-json`.
